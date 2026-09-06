/* tone.c — test solo-playback: seno 440 Hz estéreo, cuenta xruns.
 *
 * Uso: tone [-D hw:0,0] [-t segundos] [-r Hz] [-p period_frames]
 * Sale con el nº de xruns y CPU usada. Sirve de línea base antes
 * de tener hardware de captura.
 */
#define _POSIX_C_SOURCE 199309L
#include <alloca.h>
#include <alsa/asoundlib.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

static double now_s(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

int main(int argc, char **argv) {
    const char *dev = "hw:0,0";
    double secs = 10.0;
    unsigned rate = 48000;
    unsigned period = 128;
    unsigned latency_us = 20000;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-D") && i + 1 < argc) dev = argv[++i];
        else if (!strcmp(argv[i], "-t") && i + 1 < argc) secs = atof(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) rate = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-p") && i + 1 < argc) period = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-l") && i + 1 < argc) latency_us = atoi(argv[++i]);
        else { fprintf(stderr, "uso: %s [-D dev] [-t seg] [-r Hz] [-p frames] [-l latency_us]\n", argv[0]); return 2; }
    }

    snd_pcm_t *pcm;
    int e = snd_pcm_open(&pcm, dev, SND_PCM_STREAM_PLAYBACK, 0);
    if (e < 0) { fprintf(stderr, "open %s: %s\n", dev, snd_strerror(e)); return 1; }
    e = snd_pcm_set_params(pcm, SND_PCM_FORMAT_S16_LE, SND_PCM_ACCESS_RW_INTERLEAVED,
                           2, rate, /*soft_resample*/1, latency_us);
    if (e < 0) { fprintf(stderr, "set_params: %s\n", snd_strerror(e)); return 1; }
    snd_pcm_uframes_t bp = period;
    snd_pcm_hw_params_t *hp;
    snd_pcm_hw_params_alloca(&hp);
    snd_pcm_hw_params_current(pcm, hp);
    snd_pcm_hw_params_get_period_size(hp, &bp, 0);
    printf("dev=%s rate=%u ch=2 period=%lu frames\n", dev, rate, (unsigned long)bp);

    int16_t *buf = malloc(bp * 2 * sizeof(int16_t));
    if (!buf) { perror("malloc"); return 1; }
    long total = 0, xruns = 0;
    double t0 = now_s(), cpu0 = (double)clock() / CLOCKS_PER_SEC;
    double ph = 0.0;
    const double dph = 2.0 * 3.141592653589793 * 440.0 / rate;
    long want = (long)(secs * rate);

    while (want > 0) {
        snd_pcm_uframes_t n = want > (long)bp ? bp : (snd_pcm_uframes_t)want;
        for (snd_pcm_uframes_t i = 0; i < n; i++) {
            int16_t s = (int16_t)(28000.0 * sin(ph));
            ph += dph;
            if (ph > 2.0 * 3.141592653589793) ph -= 2.0 * 3.141592653589793;
            buf[2 * i] = s; buf[2 * i + 1] = s;
        }
        snd_pcm_sframes_t w = snd_pcm_writei(pcm, buf, n);
        if (w == -EPIPE) { xruns++; snd_pcm_recover(pcm, w, 0); }
        else if (w == -ESTRPIPE) { snd_pcm_recover(pcm, w, 0); }
        else if (w < 0) { fprintf(stderr, "writei: %s\n", snd_strerror(w)); break; }
        else { want -= w; total += w; }
    }
    double t1 = now_s(), cpu1 = (double)clock() / CLOCKS_PER_SEC;
    printf("frames=%ld xruns=%ld wall=%.2fs cpu=%.2fs (%.1f%%)\n",
           total, xruns, t1 - t0, cpu1 - cpu0, 100.0 * (cpu1 - cpu0) / (t1 - t0));
    snd_pcm_drain(pcm);
    snd_pcm_close(pcm);
    free(buf);
    return xruns == 0 ? 0 : 3;
}
