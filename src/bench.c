/* bench.c — coste CPU de la cadena completa (sin ALSA).
 *
 * Procesa 30 s estéreo sintético (senos) a 48 kHz con todos los
 * efectos activos y mide tiempo de CPU vs tiempo de audio.
 * Uso: bench  (imprime % de un núcleo para bypass y full)
 */
#include <math.h>
#include <stdio.h>
#include <time.h>
#include "dsp.h"

static double run(int full) {
    chan_t ch1, ch2;
    float hpf = full ? 80.0f : 0.0f;
    float cr = full ? 3.0f : 1.0f;
    float el = full ? 3.0f : 0.0f, em = full ? -2.0f : 0.0f, eh = full ? 1.5f : 0.0f;
    chan_init(&ch1, hpf, 0.5f, cr, 0.01f, 0.2f, el, em, eh, 0.9f, 48000.0f);
    chan_init(&ch2, hpf, 0.5f, cr, 0.01f, 0.2f, el, em, eh, 0.9f, 48000.0f);
    static reverb_t rev;
    reverb_init(&rev, full ? 0.25f : 0.0f, 48000.0f);
    static delay_t dly;
    delay_init(&dly, full ? 250.0f : 0.0f, 0.35f, full ? 0.2f : 0.0f, 48000.0f);
    lim_t lim;
    lim_init(&lim, 0.95f);
    volatile float sink = 0.0f; /* evita que el compilador pode el loop */
    clock_t t0 = clock();
    const long n = 30L * 48000L;
    for (long i = 0; i < n; i++) {
        float t = i / 48000.0f;
        float in1 = 0.4f * sinf(6.2831853f * 220.0f * t)
                  + 0.2f * sinf(6.2831853f * 440.0f * t);
        float in2 = 0.4f * sinf(6.2831853f * 277.0f * t)
                  + 0.2f * sinf(6.2831853f * 554.0f * t);
        float l = chan_run(&ch1, in1);
        float rr = chan_run(&ch2, in2);
        float dry = (l + rr) * 0.5f;
        float wet = reverb_wet(&rev, 0.3f * l + 0.4f * rr);
        float echo = delay_run(&dly, dry + rev.amount * wet);
        float o = lim_run(&lim, echo * 1.0f);
        sink += o;
    }
    clock_t t1 = clock();
    if (sink == 12345.0f) printf("nunca\n");
    return 100.0 * (double)(t1 - t0) / CLOCKS_PER_SEC / 30.0;
}

int main(void) {
    printf("bypass: %.2f%% de un nucleo\n", run(0));
    printf("full:   %.2f%% de un nucleo\n", run(1));
    return 0;
}
