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

static float g_mic1 = 1.0f, g_mic2 = 1.0f, g_master = 1.0f, g_hpf = 0.0f;
static float g_c_thr = 1.0f, g_c_ratio = 1.0f, g_c_atk = 0.005f, g_c_rel = 0.100f;

static void load_conf(const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "conf %s no encontrado, defaults\n", path); return; }
    char line[128], k[64]; float v;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%63s %f", k, &v) != 2) continue; /* comentarios/blancos */
        if (!strcmp(k, "mic1_gain")) g_mic1 = v;
        else if (!strcmp(k, "mic2_gain")) g_mic2 = v;
        else if (!strcmp(k, "master_gain")) g_master = v;
        else if (!strcmp(k, "hpf_frequency")) g_hpf = v;
        else if (!strcmp(k, "compressor_threshold")) g_c_thr = v;
        else if (!strcmp(k, "compressor_ratio")) g_c_ratio = v;
        else if (!strcmp(k, "compressor_attack")) g_c_atk = v;
        else if (!strcmp(k, "compressor_release")) g_c_rel = v;
    }
    fclose(f);
    printf("gains: mic1=%.3f mic2=%.3f master=%.3f hpf=%.1f Hz comp=%.3f:%.1f atk=%.4f rel=%.3f\n",
           g_mic1, g_mic2, g_master, g_hpf,
           g_c_thr, g_c_ratio, g_c_atk, g_c_rel);
}

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
    load_conf(conf);

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
    hpf_t hpf1, hpf2;
    int use_hpf = g_hpf > 0.0f;
    if (use_hpf) { hpf_init(&hpf1, g_hpf, (float)rate); hpf_init(&hpf2, g_hpf, (float)rate); }
    comp_t comp1, comp2;
    comp_init(&comp1, g_c_thr, g_c_ratio, g_c_atk, g_c_rel, (float)rate);
    comp_init(&comp2, g_c_thr, g_c_ratio, g_c_atk, g_c_rel, (float)rate);
    long xr_c = 0, xr_p = 0, total = 0;
    for (;;) {
        snd_pcm_sframes_t r = snd_pcm_readi(cap, buf, bp);
        if (r == -EPIPE) { xr_c++; snd_pcm_recover(cap, r, 0); continue; }
        if (r < 0) { fprintf(stderr, "readi: %s\n", snd_strerror(r)); return 1; }
        for (snd_pcm_sframes_t i = 0; i < r; i++) {
            float l = (float)buf[2 * i], rr = (float)buf[2 * i + 1];
            if (use_hpf) { l = hpf_run(&hpf1, l); rr = hpf_run(&hpf2, rr); }
            l = comp_run(&comp1, l / 32768.0f) * 32768.0f; /* comp en [-1,1] */
            rr = comp_run(&comp2, rr / 32768.0f) * 32768.0f;
            l *= g_mic1 * g_master;
            rr *= g_mic2 * g_master;
            if (l > 32767) l = 32767;
            if (l < -32768) l = -32768;
            if (rr > 32767) rr = 32767;
            if (rr < -32768) rr = -32768;
            buf[2 * i] = (int16_t)l; buf[2 * i + 1] = (int16_t)rr;
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
