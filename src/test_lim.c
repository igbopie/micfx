/* test_lim.c — offline limiter verification (no ALSA).
 *
 * 1 kHz sine @ 48 kHz, 1 s (after 0.5 s settling):
 *   amp 1.5, thr 1.0 -> peak 1.0 (±0.1%)
 *   amp 0.5, thr 1.0 -> peak 0.5 intact (±0.1%)
 *   amp -1.5 (phase), thr 0.8 -> symmetric 0.8 peak (±0.1%)
 * Exits 0 if all holds, 1 otherwise.
 */
#include <math.h>
#include <stdio.h>
#include "dsp.h"

static float peak_out(float amp, float thr) {
    lim_t l;
    lim_init(&l, thr);
    float peak = 0.0f;
    for (long i = 0; i < 72000; i++) {
        float y = lim_run(&l, amp * sinf(6.2831853f * 1000.0f * i / 48000.0f));
        if (i >= 24000) {
            float a = y < 0 ? -y : y;
            if (a > peak) peak = a;
        }
    }
    return peak;
}

int main(void) {
    float p1 = peak_out(1.5f, 1.0f);
    float p2 = peak_out(0.5f, 1.0f);
    float p3 = peak_out(-1.5f, 0.8f);
    printf("lim: 1.5->%.4f (exp 1.0) | 0.5->%.4f (exp 0.5) | -1.5->%.4f (exp 0.8)\n",
           p1, p2, p3);
    int ok = (p1 > 0.999f && p1 < 1.001f)
          && (p2 > 0.4995f && p2 < 0.5005f)
          && (p3 > 0.7992f && p3 < 0.8008f);
    printf("%s\n", ok ? "LIM_OK" : "LIM_FAIL");
    return ok ? 0 : 1;
}
