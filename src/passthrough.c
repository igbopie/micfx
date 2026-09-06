/* micfx etapa 1 — passthrough estéreo con ganancias por canal + master.
 *
 * Captura estéreo (L=mic1, R=mic2) -> aplica mic1_gain/mic2_gain/master_gain
 * -> playback. Lee ganancias de un fichero "clave valor" (-c).
 *
 * Uso: micfx [-C cap_dev] [-P play_dev] [-c conf] [-r Hz] [-p period]
 * Formato: S16_LE estéreo. Sin allocations ni I/O en el loop.
 */
#include <alloca.h>
#include <alsa/asoundlib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsp.h"
#include "conf.h"

static conf_t CF;

static int setup(snd_pcm_t **h, const char *dev, int cap, unsigned rate) {
    int e = snd_pcm_open(h, dev, cap ? SND_PCM_STREAM_CAPTURE : SND_PCM_STREAM_PLAYBACK, 0);
    if (e < 0) { fprintf(stderr, "open %s: %s\n", dev, snd_strerror(e)); return e; }
    e = snd_pcm_set_params(*h, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                           2, rate, 1, 5000);
    if (e < 0) { fprintf(stderr, "set_params %s: %s\n", dev, snd_strerror(e)); return e; }
    return 0;
}

int main(int argc, char **argv) {
    const char *capd = "hw:1,0", *playd = "hw:0,0", *conf = "micfx.conf";
    unsigned rate = 48000, period = 128;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-C") && i + 1 < argc) capd = argv[++i];
        else if (!strcmp(argv[i], "-P") && i + 1 < argc) playd = argv[++i];
        else if (!strcmp(argv[i], "-c") && i + 1 < argc) conf = argv[++i];
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) period = atoi(argv[++i]);
        else { fprintf(stderr, "uso: %s [-C cap] [-P play] [-c conf] [-r Hz] [-p frames]\n", argv[0]); return 2; }
    }
    conf_defaults(&CF);
    if (!conf_load(&CF, conf)) fprintf(stderr, "conf %s no encontrado, defaults\n", conf);
    conf_print(&CF);

    snd_pcm_t *cap, *play;
    if (setup(&cap, capd, 1, rate) < 0) return 1;
    if (setup(&play, playd, 0, rate) < 0) return 1;

    snd_pcm_uframes_t bp = period;
    snd_pcm_hw_params_t *hp;
    snd_pcm_hw_params_alloca(&hp);
    snd_pcm_hw_params_current(cap, hp);
    snd_pcm_hw_params_get_period_size(hp, &bp, 0);
    printf("cap=%s play=%s rate=%u period=%lu\n", capd, playd, rate, (unsigned long)bp);

    static int16_t buf[8192 * 2]; /* estático: sin malloc en el loop */
    chan_t ch1, ch2;
    float frate = (float)rate;
    chan_init(&ch1, CF.hpf, CF.c_thr, CF.c_ratio, CF.c_atk, CF.c_rel,
              CF.eq_low, CF.eq_mid, CF.eq_high, CF.mic1, frate);
    chan_init(&ch2, CF.hpf, CF.c_thr, CF.c_ratio, CF.c_atk, CF.c_rel,
              CF.eq_low, CF.eq_mid, CF.eq_high, CF.mic2, frate);
    static reverb_t rev; /* ~115 KB estáticos: fuera del stack */
    reverb_init(&rev, CF.rev_amt, frate);
    static delay_t dly; /* ~190 KB estáticos */
    delay_init(&dly, CF.dly_ms, CF.dly_fb, CF.dly_mix, frate);
    lim_t lim;
    lim_init(&lim, CF.lim_thr);
    long xr_c = 0, xr_p = 0, total = 0;
    for (;;) {
        snd_pcm_sframes_t r = snd_pcm_readi(cap, buf, bp);
        if (r == -EPIPE) { xr_c++; snd_pcm_recover(cap, r, 0); continue; }
        if (r < 0) { fprintf(stderr, "readi: %s\n", snd_strerror(r)); return 1; }
        for (snd_pcm_sframes_t i = 0; i < r; i++) {
            float l = chan_run(&ch1, buf[2 * i] / 32768.0f);
            float rr = chan_run(&ch2, buf[2 * i + 1] / 32768.0f);
            float dry = (l + rr) * 0.5f; /* etapa 6: mezcla */
            float wet = reverb_wet(&rev, CF.rev_s1 * l + CF.rev_s2 * rr);
            float echo = delay_run(&dly, dry + rev.amount * wet); /* etapa 8 */
            float ol = lim_run(&lim, echo * CF.master); /* etapa 9: limiter */
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
        if ((total / (long)bp) % (rate * 5 / (long)bp) == 0)
            fprintf(stderr, "frames=%ld xruns cap=%ld play=%ld\n", total, xr_c, xr_p);
    }
}
