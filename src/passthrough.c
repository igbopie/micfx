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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsp.h"
#include "conf.h"

static conf_t CF;

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
    for (;;) {
        snd_pcm_sframes_t r = snd_pcm_readi(cap, buf, bp);
        if (r == -EPIPE) { xr_c++; snd_pcm_recover(cap, r, 0); continue; }
        if (r < 0) { fprintf(stderr, "readi: %s\n", snd_strerror(r)); return 1; }
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
            float l = chan_run(&ch1, buf[2 * i] / 32768.0f);
            float rr = chan_run(&ch2, buf[2 * i + 1] / 32768.0f);
            float dry = (l + rr) * 0.5f; /* stage 6: mix */
            float wet = reverb_wet(&rev, CF.rev_s1 * l + CF.rev_s2 * rr);
            float echo = delay_run(&dly, dry + rev.amount * wet); /* stage 8 */
            float mx = 0.0f;
            if (mus && CF.music > 0.0f) {
                float ml, mr;
                music_tick(&ml, &mr, rate);
                mx = CF.music * 0.5f * (ml + mr);
            }
            float ol = lim_run(&lim, echo * CF.master + mx); /* stage 9: limiter */
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
