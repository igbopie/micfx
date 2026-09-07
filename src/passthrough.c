/* micfx — full DSP chain over ALSA full-duplex.
 *
 * Stereo capture (L=mic1, R=mic2) -> per-channel chain
 * (HPF -> comp -> EQ -> gain) -> mono mix -> reverb -> delay
 * -> master -> limiter -> dual-mono playback.
 * Reads parameters from a "key value" file (-c).
 *
 * Usage: micfx [-C cap_dev] [-P play_dev] [-M music_dev] [-c conf]
 *          [-r Hz] [-p period] [-l latency_us]
 * Music bed: async stereo source (e.g. BlueALSA) resampled into the mix
 * with music_gain. Empty -M disables it.
 * Format: stereo S16_LE. No allocations, no I/O in the loop.
 */
#include <alloca.h>
#include <alsa/asoundlib.h>
#include <gpiod.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "dsp.h"
#include "conf.h"
#include "pins.h"
#include "lcd.h"

static conf_t CF;
/* Shared with the UI thread (aligned float/int access is atomic on ARM). */
static volatile float g_pk1, g_pk2, g_pkm;
static volatile int g_mus_ok;

static int setup(snd_pcm_t **h, const char *dev, int cap, unsigned rate, unsigned latency_us, int quiet) {
    int e = snd_pcm_open(h, dev, cap ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK,
                         quiet ? SND_PCM_NONBLOCK : 0);
    if (e < 0) { if (!quiet) fprintf(stderr, "open %s: %s\n", dev, snd_strerror(e)); return e; }
    e = snd_pcm_set_params(*h, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                           2, rate, 1, latency_us);
    if (e < 0) { if (!quiet) fprintf(stderr, "set_params %s: %s\n", dev, snd_strerror(e)); snd_pcm_close(*h); *h = NULL; return e; }
    if (cap) {
        /* set_params leaves start_threshold = buffer size, which never
           starts a capture stream (arecord uses 1). Fix that here. */
        snd_pcm_sw_params_t *sp;
        snd_pcm_sw_params_alloca(&sp);
        e = snd_pcm_sw_params_current(*h, sp);
        if (e < 0) { if (!quiet) fprintf(stderr, "sw_current %s: %s\n", dev, snd_strerror(e)); return e; }
        e = snd_pcm_sw_params_set_start_threshold(*h, sp, 1);
        if (e < 0) { if (!quiet) fprintf(stderr, "sw_start %s: %s\n", dev, snd_strerror(e)); return e; }
        e = snd_pcm_sw_params(*h, sp);
        if (e < 0) { if (!quiet) fprintf(stderr, "sw_params %s: %s\n", dev, snd_strerror(e)); return e; }
    }
    return 0;
}

/* --- music bed: async source (BT) resampled to the main rate ---
 * Ring FIFO absorbs jitter; fixed-ratio interp plus rare bounded resync
 * handles clock drift. Underflow -> silence, overflow -> drop oldest.
 * All static: nothing allocated in the loop. */
#define MRING_FR 65536u
static int16_t mring[MRING_FR * 2];
static unsigned long mwr;
static double mpos;
static unsigned mus_rate;
static int16_t mtmp[1024 * 2];

static void music_reset(void) { mwr = 0; mpos = 0.0; }

static snd_pcm_t *music_open(const char *dev, unsigned *got_rate) {
    static const unsigned try_rates[] = { 48000, 44100 };
    for (int t = 0; t < 2; t++) {
        snd_pcm_t *h = NULL;
        if (setup(&h, dev, 1, try_rates[t], 400000, 1) < 0) continue;
        snd_pcm_nonblock(h, 1);
        snd_pcm_hw_params_t *mhp;
        snd_pcm_hw_params_alloca(&mhp);
        snd_pcm_hw_params_current(h, mhp);
        unsigned mr = 0;
        if (snd_pcm_hw_params_get_rate(mhp, &mr, 0) < 0 || mr == 0) { snd_pcm_close(h); continue; }
        *got_rate = mr;
        music_reset();
        return h;
    }
    return NULL;
}

/* Drain whatever the music source has; 0 = stream lost. */
static int music_drain(snd_pcm_t *mus) {
    for (;;) {
        snd_pcm_sframes_t r = snd_pcm_readi(mus, mtmp, 1024);
        if (r == -EAGAIN) return 1;
        if (r == -EPIPE) { snd_pcm_recover(mus, r, 0); continue; }
        if (r < 0) return 0;
        for (snd_pcm_sframes_t k = 0; k < r; k++) {
            if (mwr - (unsigned long)mpos >= MRING_FR) mpos += 1.0; /* drop oldest */
            unsigned oi = (unsigned)(mwr & (MRING_FR - 1)) * 2;
            mring[oi] = mtmp[2 * k];
            mring[oi + 1] = mtmp[2 * k + 1];
            mwr++;
        }
    }
}

/* One stereo music sample at the main rate (linear interp, fixed ratio).
 * No continuous servo: wobbling the ratio is audible as phasing. Clock
 * drift between phone and USB interface is ppm-slow, so a fixed ratio
 * plus a rare bounded resync (skip/silence) stays transparent. */
static void music_tick(float *ml, float *mr, unsigned main_rate) {
    unsigned long avail = mwr - (unsigned long)mpos;
    if (avail > mus_rate / 2) mpos = (double)mwr - (double)mus_rate * 0.15;
    avail = mwr - (unsigned long)mpos;
    if (avail < 2) { mpos = (double)mwr; *ml = 0.0f; *mr = 0.0f; return; }
    unsigned long p0 = (unsigned long)mpos;
    float f = (float)(mpos - (double)p0);
    unsigned i0 = (unsigned)(p0 & (MRING_FR - 1)) * 2;
    unsigned i1 = (unsigned)((p0 + 1) & (MRING_FR - 1)) * 2;
    *ml = (mring[i0] + f * (mring[i1] - mring[i0])) / 32768.0f;
    *mr = (mring[i0 + 1] + f * (mring[i1 + 1] - mring[i0 + 1])) / 32768.0f;
    mpos += (double)mus_rate / (double)main_rate;
}

/* --- box UI: 3 buttons + 1602 LCD, background thread ---
 * NEXT cycles the selected param (wraps), UP/DOWN adjust it (hold to
 * repeat); holding NEXT 0.8 s mutes the selected param instead.
 * Params retune CF live (audio reads aligned floats: atomic on ARM).
 * Changes are saved back to the conf file, debounced. If GPIO is
 * unavailable the box simply runs headless. */
typedef struct {
    const char *name;
    float *val;
    float step, min, max, restore;
    int muted;
} knob_t;

typedef struct {
    const char *conf;
    long *xr_c, *xr_p;
} ui_ctx_t;

static double mono_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static void *ui_thread(void *arg) {
    ui_ctx_t *ux = arg;
    /* All knobs show and step a normalized 0..1 value; min/max map
     * it to native units (Hz, dB, ms, ...). The conf file keeps native
     * values. Uniform 0.02 step = 50 clicks end to end. */
    knob_t knobs[] = {
        { "music",   &CF.music,   0.02f,  0.0f,   1.5f,  0.8f,  0 },
        { "mic1",    &CF.mic1,    0.02f,  0.0f,   4.0f,  1.0f,  0 },
        { "mic2",    &CF.mic2,    0.02f,  0.0f,   4.0f,  1.0f,  0 },
        { "hpf",     &CF.hpf,     0.02f,  0.0f,   500.0f, 90.0f, 0 },
        { "c_thr",   &CF.c_thr,   0.02f,  0.01f,  1.0f,  0.08f, 0 },
        { "c_ratio", &CF.c_ratio, 0.02f,  1.0f,   20.0f, 3.0f,  0 },
        { "c_atk",   &CF.c_atk,   0.02f,  0.001f, 0.1f,  0.005f, 0 },
        { "c_rel",   &CF.c_rel,   0.02f,  0.01f,  1.0f,  0.1f,  0 },
        { "eq_lo",   &CF.eq_low,  0.02f, -12.0f, 12.0f, 0.0f,  0 },
        { "eq_mid",  &CF.eq_mid,  0.02f, -12.0f, 12.0f, 0.0f,  0 },
        { "eq_hi",   &CF.eq_high, 0.02f, -12.0f, 12.0f, 0.0f,  0 },
        { "rev_s1",  &CF.rev_s1,  0.02f,  0.0f,   1.0f,  0.4f,  0 },
        { "rev_s2",  &CF.rev_s2,  0.02f,  0.0f,   1.0f,  0.0f,  0 },
        { "rev_amt", &CF.rev_amt, 0.02f,  0.0f,   1.0f,  0.25f, 0 },
        { "dly_ms",  &CF.dly_ms,  0.02f,  0.0f,   500.0f, 180.0f, 0 },
        { "dly_fb",  &CF.dly_fb,  0.02f,  0.0f,   0.95f, 0.25f, 0 },
        { "dly_mix", &CF.dly_mix, 0.02f,  0.0f,   1.0f,  0.15f, 0 },
        { "master",  &CF.master,  0.02f,  0.0f,   1.5f,  1.0f,  0 },
        { "lim_thr", &CF.lim_thr, 0.02f,  0.1f,   1.0f,  0.9f,  0 },
    };
    const int nknobs = (int)(sizeof knobs / sizeof knobs[0]);
    static const unsigned btn[3] = { PIN_BTN_NEXT, PIN_BTN_UP, PIN_BTN_DOWN };
    static const unsigned outs[6] = { PIN_LCD_RS, PIN_LCD_EN, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7 };

    struct gpiod_chip *chip = gpiod_chip_open("/dev/gpiochip0");
    if (!chip) { fprintf(stderr, "ui: no gpiochip0, headless\n"); return NULL; }
    struct gpiod_request_config *rcfg = gpiod_request_config_new();
    struct gpiod_line_config *lcfg = gpiod_line_config_new();
    struct gpiod_line_settings *o = gpiod_line_settings_new();
    struct gpiod_line_settings *i = gpiod_line_settings_new();
    struct gpiod_line_request *req = NULL;
    if (!rcfg || !lcfg || !o || !i) goto fail;
    gpiod_request_config_set_consumer(rcfg, "micfx");
    gpiod_line_settings_set_direction(o, GPIOD_LINE_DIRECTION_OUTPUT);
    gpiod_line_settings_set_direction(i, GPIOD_LINE_DIRECTION_INPUT);
    gpiod_line_settings_set_bias(i, GPIOD_LINE_BIAS_PULL_UP);
    if (gpiod_line_config_add_line_settings(lcfg, outs, 6, o) < 0) goto fail;
    if (gpiod_line_config_add_line_settings(lcfg, btn, 3, i) < 0) goto fail;
    req = gpiod_chip_request_lines(chip, rcfg, lcfg);
    if (!req) goto fail;

    lcd_t lcd;
    lcd_init(&lcd, req, PIN_LCD_RS, PIN_LCD_EN, PIN_LCD_D4, PIN_LCD_D5, PIN_LCD_D6, PIN_LCD_D7);
    lcd_defbar(&lcd);
    fprintf(stderr, "ui: lcd + 3 buttons ready\n");

    int sel = 0;
    int hold[3] = { 0, 0, 0 };
    int narm = 1; /* NEXT arms advance on release */
    double change_at = 0, last_lcd = 0;
    int dirty = 0;
    struct timespec ts2 = { 0, 2000000 }; /* 2 ms poll */
    for (;;) {
        double now = mono_s();
        int b[3];
        for (int k = 0; k < 3; k++) {
            int v = gpiod_line_request_get_value(req, btn[k]);
            b[k] = (v == 0); /* active low */
        }
        knob_t *kb = &knobs[sel];
        if (b[0]) { /* NEXT held */
            hold[0]++;
            if (hold[0] == 400 && narm) { /* 0.8 s: mute instead of advance */
                narm = 0;
                if (!kb->muted) { kb->restore = *kb->val; *kb->val = 0.0f; kb->muted = 1; }
                else { *kb->val = kb->restore > 0.001f ? kb->restore : 1.0f; kb->muted = 0; }
                change_at = now; dirty = 1;
            }
        } else {
            if (hold[0] > 0 && hold[0] < 400 && narm) sel = (sel + 1) % nknobs; /* tap: advance */
            hold[0] = 0; narm = 1;
        }
        for (int k = 1; k <= 2; k++) { /* UP/DOWN: step now, repeat while held */
            int dir = (k == 1) ? 1 : -1;
            if (b[k]) {
                hold[k]++;
                if (hold[k] == 1 || (hold[k] > 200 && (hold[k] - 200) % 40 == 0)) {
                    float n = (*kb->val - kb->min) / (kb->max - kb->min) + dir * kb->step;
                    if (n < 0.0f) n = 0.0f;
                    if (n > 1.0f) n = 1.0f;
                    kb->muted = 0;
                    *kb->val = kb->min + n * (kb->max - kb->min);
                    change_at = now; dirty = 1;
                }
            } else hold[k] = 0;
        }
        if (dirty && now - change_at > 2.0) {
            if (conf_save(&CF, ux->conf)) dirty = 0;
            else change_at = now; /* retry later */
        }
        if (now - last_lcd > 0.2) {
            last_lcd = now;
            float p1 = g_pk1; g_pk1 = 0.0f;
            float p2 = g_pk2; g_pk2 = 0.0f;
            float pm = g_pkm; g_pkm = 0.0f;
            char l1[17], l2[17];
            snprintf(l1, sizeof l1, "1:%02d 2:%02d M:%02d%c",
                     (int)(p1 * 99), (int)(p2 * 99), (int)(pm * 99),
                     g_mus_ok ? '+' : '-');
            float norm = (*knobs[sel].val - knobs[sel].min) / (knobs[sel].max - knobs[sel].min);
            if (knobs[sel].muted) {
                snprintf(l2, sizeof l2, ">%s MUTED", knobs[sel].name);
                lcd_text(&lcd, 1, l2);
            } else {
                /* Bar fills the rest of the line: full cells 0xFF,
                 * partials are custom chars 0..3 (1..4 px). */
                char bar[16];
                memset(bar, ' ', 16);
                int n = snprintf(bar, 17, ">%s ", knobs[sel].name);
                if (n < 0) n = 0;
                if (n > 15) n = 15;
                int w = 16 - n;
                int px = (int)(norm * w * 5 + 0.5f);
                for (int q = 0; q < w; q++) {
                    int p = px - q * 5;
                    bar[n + q] = (char)(p >= 5 ? 0xFF : p <= 0 ? ' ' : p - 1);
                }
                lcd_raw(&lcd, 1, bar);
            }
            lcd_text(&lcd, 0, l1);
        }
        nanosleep(&ts2, NULL);
    }
fail:
    fprintf(stderr, "ui: gpio setup failed, headless\n");
    return NULL;
}

int main(int argc, char **argv) {
    const char *capd = "hw:1,0", *playd = "hw:0,0", *conf = "micfx.conf";
    const char *musd = "bluealsa:PROFILE=a2dp";
    unsigned rate = 48000, period = 128, latency_us = 20000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-C") && i + 1 < argc) capd = argv[++i];
        else if (!strcmp(argv[i], "-P") && i + 1 < argc) playd = argv[++i];
        else if (!strcmp(argv[i], "-M") && i + 1 < argc) musd = argv[++i];
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) conf = argv[++i];
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) period = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) latency_us = atoi(argv[++i]);
        else { fprintf(stderr, "usage: %s [-C cap] [-P play] [-M music] [-c conf] [-r Hz] [-p frames] [-l latency_us]\n", argv[0]); return 2; }
    }
    conf_defaults(&CF);
    if (!conf_load(&CF, conf)) fprintf(stderr, "conf %s not found, using defaults\n", conf);
    conf_print(&CF);

    snd_pcm_t *cap, *play;
    if (setup(&cap, capd, 1, rate, latency_us, 0) < 0) return 1;
    if (setup(&play, playd, 0, rate, latency_us, 0) < 0) return 1;
    int want_music = musd[0] != '\0' && CF.music > 0.0f;
    snd_pcm_t *mus = NULL;
    long mus_next = 0; /* next retry time, in mic frames */
    if (want_music) {
        mus = music_open(musd, &mus_rate);
        printf("music=%s rate=%u %s\n", musd, mus ? mus_rate : 0, mus ? "open" : "waiting for stream");
    }

    snd_pcm_uframes_t bp = period;
    snd_pcm_hw_params_t *hp;
    snd_pcm_hw_params_alloca(&hp);
    snd_pcm_hw_params_current(cap, hp);
    snd_pcm_hw_params_get_period_size(hp, &bp, 0);
    printf("cap=%s play=%s rate=%u period=%lu\n", capd, playd, rate, (unsigned long)bp);

    static int16_t buf[8192 * 2]; /* static: no malloc in the loop */
    chan_t ch1, ch2;
    float frate = (float)rate;
    chan_init(&ch1, CF.hpf, CF.c_thr, CF.c_ratio, CF.c_atk, CF.c_rel,
              CF.eq_low, CF.eq_mid, CF.eq_high, CF.mic1, frate);
    chan_init(&ch2, CF.hpf, CF.c_thr, CF.c_ratio, CF.c_atk, CF.c_rel,
              CF.eq_low, CF.eq_mid, CF.eq_high, CF.mic2, frate);
    static reverb_t rev; /* ~115 KB static: kept off the stack */
    reverb_init(&rev, CF.rev_amt, frate);
    static delay_t dly; /* ~190 KB static */
    delay_init(&dly, CF.dly_ms, CF.dly_fb, CF.dly_mix, frate);
    lim_t lim;
    lim_init(&lim, CF.lim_thr);
    long xr_c = 0, xr_p = 0, total = 0;
    long mus_drops = 0;
    pthread_t ui_thr;
    ui_ctx_t uix = { conf, &xr_c, &xr_p };
    if (pthread_create(&ui_thr, NULL, ui_thread, &uix) == 0)
        pthread_detach(ui_thr);
    else
        fprintf(stderr, "ui: no thread, headless\n");
    for (;;) {
        snd_pcm_sframes_t r = snd_pcm_readi(cap, buf, bp);
        if (r == -EPIPE) { xr_c++; snd_pcm_recover(cap, r, 0); continue; }
        if (r < 0) { fprintf(stderr, "readi: %s\n", snd_strerror(r)); return 1; }
        /* Knob params go live here, once per block (no state is
           cleared, so this is lock-free and click-quiet). */
        chan_sync(&ch1, CF.hpf, CF.c_thr, CF.c_ratio, CF.c_atk, CF.c_rel,
                  CF.eq_low, CF.eq_mid, CF.eq_high, CF.mic1, frate);
        chan_sync(&ch2, CF.hpf, CF.c_thr, CF.c_ratio, CF.c_atk, CF.c_rel,
                  CF.eq_low, CF.eq_mid, CF.eq_high, CF.mic2, frate);
        rev.amount = CF.rev_amt;
        delay_set_time(&dly, CF.dly_ms, frate);
        dly.fb = CF.dly_fb;
        dly.mix = CF.dly_mix;
        lim.thr = CF.lim_thr;
        if (want_music) {
            if (!mus && total >= mus_next) {
                mus = music_open(musd, &mus_rate);
                if (mus) fprintf(stderr, "music: stream open at %u Hz\n", mus_rate);
                mus_next = total + (long)rate * 5; /* retry every 5 s */
            }
            if (mus && !music_drain(mus)) {
                fprintf(stderr, "music: stream lost, waiting\n");
                snd_pcm_close(mus); mus = NULL; mus_drops++;
                mus_next = total + (long)rate * 5;
            }
        }
        for (snd_pcm_sframes_t i = 0; i < r; i++) {
            float ia1 = (float)buf[2 * i] / 32768.0f;
            float ia2 = (float)buf[2 * i + 1] / 32768.0f;
            float aa1 = ia1 < 0 ? -ia1 : ia1, aa2 = ia2 < 0 ? -ia2 : ia2;
            if (aa1 > g_pk1) g_pk1 = aa1;
            if (aa2 > g_pk2) g_pk2 = aa2;
            float l = chan_run(&ch1, ia1);
            float rr = chan_run(&ch2, ia2);
            float dry = (l + rr) * 0.5f; /* stage 6: mix */
            float wet = reverb_wet(&rev, CF.rev_s1 * l + CF.rev_s2 * rr);
            float echo = delay_run(&dly, dry + rev.amount * wet); /* stage 8 */
            float mx = 0.0f;
            if (mus && CF.music > 0.0f) {
                float ml, mr;
                music_tick(&ml, &mr, rate);
                mx = CF.music * 0.5f * (ml + mr);
                float am = mx < 0 ? -mx : mx;
                if (am > g_pkm) g_pkm = am;
            }
            g_mus_ok = mus != NULL;
            float ol = lim_run(&lim, (echo + mx) * CF.master); /* stage 9: limiter */
            float orr = ol;
            if (ol > 1.0f) ol = 1.0f;
            if (ol < -1.0f) ol = -1.0f;
            if (orr > 1.0f) orr = 1.0f;
            if (orr < -1.0f) orr = -1.0f;
            buf[2 * i] = (int16_t)(ol * 32767.0f);
            buf[2 * i + 1] = (int16_t)(orr * 32767.0f);
        }
        snd_pcm_sframes_t off = 0;
        while (off < r) {
            snd_pcm_sframes_t w = snd_pcm_writei(play, buf + off * 2, r - off);
            if (w == -EPIPE) { xr_p++; snd_pcm_recover(play, w, 0); break; }
            if (w < 0) { fprintf(stderr, "writei: %s\n", snd_strerror(w)); return 1; }
            off += w;
        }
        total += r;
        if ((total / (long)bp) % (rate * 5 / (long)bp) == 0) {
            if (want_music)
                fprintf(stderr, "frames=%ld xruns cap=%ld play=%ld mus_drops=%ld\n",
                        total, xr_c, xr_p, mus_drops);
            else
                fprintf(stderr, "frames=%ld xruns cap=%ld play=%ld\n", total, xr_c, xr_p);
        }
    }
}
