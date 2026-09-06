/* filefx.c — procesa un fichero WAV por la cadena completa de micfx.
 *
 * Uso: filefx -c conf entrada.wav salida.wav
 * Lee PCM16 mono/estéreo a cualquier rate, corre la cadena a ese rate
 * (chan x2 -> mix -> reverb -> delay -> master -> limiter), escribe
 * WAV estéreo S16 (mono dual) e imprime pico/RMS de entrada y salida.
 * Sin ALSA: sirve para verificar con muestras reales sin hardware.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "dsp.h"
#include "conf.h"

static uint32_t rd32(FILE *f) {
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) return 0;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

static uint16_t rd16(FILE *f) {
    uint8_t b[2];
    if (fread(b, 1, 2, f) != 2) return 0;
    return (uint16_t)b[0] | ((uint16_t)b[1] << 8);
}

static void wr32(FILE *f, uint32_t v) {
    uint8_t b[4] = { v & 255, (v >> 8) & 255, (v >> 16) & 255, (v >> 24) & 255 };
    fwrite(b, 1, 4, f);
}

static void wr16(FILE *f, uint16_t v) {
    uint8_t b[2] = { v & 255, (v >> 8) & 255 };
    fwrite(b, 1, 2, f);
}

/* Lee WAV PCM16. Devuelve 0 ok. Salida en buffers mono float [-1,1] (malloc). */
static int read_wav(const char *path, float **l, float **r, long *n, int *rate) {
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "no abre %s\n", path); return 1; }
    char id[4];
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "RIFF", 4)) { fclose(f); return 1; }
    rd32(f);
    if (fread(id, 1, 4, f) != 4 || memcmp(id, "WAVE", 4)) { fclose(f); return 1; }
    int ch = 0, sr = 0, bits = 0;
    long data_n = 0;
    long data_at = 0;
    for (;;) {
        if (fread(id, 1, 4, f) != 4) break;
        uint32_t sz = rd32(f);
        long next = ftell(f) + (long)sz;
        if (!memcmp(id, "fmt ", 4)) {
            uint16_t fmt = rd16(f);
            ch = rd16(f); sr = (int)rd32(f);
            rd32(f); rd16(f); bits = rd16(f);
            if (fmt != 1 || bits != 16 || (ch != 1 && ch != 2)) {
                fprintf(stderr, "formato no soportado (fmt=%d ch=%d bits=%d)\n", fmt, ch, bits);
                fclose(f);
                return 1;
            }
        } else if (!memcmp(id, "data", 4)) {
            data_n = sz / (2 * ch);
            data_at = ftell(f);
        }
        fseek(f, next, SEEK_SET);
        if (data_at && ch) break;
    }
    if (!data_at || !ch) { fclose(f); return 1; }
    fseek(f, data_at, SEEK_SET);
    int16_t *raw = malloc((size_t)data_n * ch * 2);
    if (!raw || fread(raw, 2, (size_t)data_n * ch, f) != (size_t)data_n * ch) {
        fclose(f);
        free(raw);
        return 1;
    }
    fclose(f);
    *l = malloc((size_t)data_n * sizeof(float));
    *r = malloc((size_t)data_n * sizeof(float));
    if (!*l || !*r) return 1;
    for (long i = 0; i < data_n; i++) {
        (*l)[i] = raw[i * ch] / 32768.0f;
        (*r)[i] = raw[i * ch + (ch - 1)] / 32768.0f;
    }
    free(raw);
    *n = data_n;
    *rate = sr;
    return 0;
}

static void write_wav(const char *path, const float *l, const float *r, long n, int rate) {
    FILE *f = fopen(path, "wb");
    if (!f) { fprintf(stderr, "no escribe %s\n", path); exit(1); }
    fwrite("RIFF", 1, 4, f);
    wr32(f, 36 + (uint32_t)(n * 4));
    fwrite("WAVEfmt ", 1, 8, f);
    wr32(f, 16);
    wr16(f, 1); wr16(f, 2);
    wr32(f, (uint32_t)rate); wr32(f, (uint32_t)(rate * 4));
    wr16(f, 4); wr16(f, 16);
    fwrite("data", 1, 4, f);
    wr32(f, (uint32_t)(n * 4));
    for (long i = 0; i < n; i++) {
        float a = l[i] > 1 ? 1 : (l[i] < -1 ? -1 : l[i]);
        float b = r[i] > 1 ? 1 : (r[i] < -1 ? -1 : r[i]);
        wr16(f, (uint16_t)(int16_t)(a * 32767.0f));
        wr16(f, (uint16_t)(int16_t)(b * 32767.0f));
    }
    fclose(f);
}

int main(int argc, char **argv) {
    const char *conf = "micfx.conf", *inp = NULL, *outp = NULL;
    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-c") && i + 1 < argc) conf = argv[++i];
        else if (!inp) inp = argv[i];
        else if (!outp) outp = argv[i];
        else { fprintf(stderr, "uso: %s [-c conf] entrada.wav salida.wav\n", argv[0]); return 2; }
    }
    if (!inp || !outp) { fprintf(stderr, "uso: %s [-c conf] entrada.wav salida.wav\n", argv[0]); return 2; }

    conf_t C;
    conf_defaults(&C);
    if (!conf_load(&C, conf)) fprintf(stderr, "conf %s no encontrado, defaults\n", conf);
    conf_print(&C);

    float *il, *ir, *ol, *orr;
    long n;
    int rate;
    if (read_wav(inp, &il, &ir, &n, &rate)) { fprintf(stderr, "WAV ilegible\n"); return 1; }
    printf("in: %ld frames @ %d Hz\n", n, rate);
    ol = malloc((size_t)n * sizeof(float));
    orr = malloc((size_t)n * sizeof(float));
    if (!ol || !orr) return 1;

    float fr = (float)rate;
    chan_t ch1, ch2;
    chan_init(&ch1, C.hpf, C.c_thr, C.c_ratio, C.c_atk, C.c_rel,
              C.eq_low, C.eq_mid, C.eq_high, C.mic1, fr);
    chan_init(&ch2, C.hpf, C.c_thr, C.c_ratio, C.c_atk, C.c_rel,
              C.eq_low, C.eq_mid, C.eq_high, C.mic2, fr);
    static reverb_t rev;
    reverb_init(&rev, C.rev_amt, fr);
    static delay_t dly;
    delay_init(&dly, C.dly_ms, C.dly_fb, C.dly_mix, fr);
    lim_t lim;
    lim_init(&lim, C.lim_thr);

    double se_i = 0, se_o = 0, pk_i = 0, pk_o = 0;
    for (long i = 0; i < n; i++) {
        float l = chan_run(&ch1, il[i]);
        float rr = chan_run(&ch2, ir[i]);
        float dry = (l + rr) * 0.5f;
        float wet = reverb_wet(&rev, C.rev_s1 * l + C.rev_s2 * rr);
        float echo = delay_run(&dly, dry + rev.amount * wet);
        float o = lim_run(&lim, echo * C.master);
        ol[i] = o; orr[i] = o;
        float ai = dry < 0 ? -dry : dry; /* métrica sobre el mix dry */
        float ao = o < 0 ? -o : o;
        se_i += (double)ai * ai; se_o += (double)ao * ao;
        if (ai > pk_i) pk_i = ai;
        if (ao > pk_o) pk_o = ao;
    }
    printf("in:  pico=%.4f rms=%.4f\n", pk_i, sqrt(se_i / n));
    printf("out: pico=%.4f rms=%.4f\n", pk_o, sqrt(se_o / n));
    write_wav(outp, ol, orr, n, rate);
    printf("escrito %s\n", outp);
    return 0;
}
