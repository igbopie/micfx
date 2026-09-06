/* test_comp.c — offline compressor verification (no ALSA).
 *
 * Cases (1 kHz sine, 48 kHz, attack 5 ms / release 100 ms):
 *   1. amplitude 0.5, thresh 0.25, ratio 4 -> peak ≈ 0.25+(0.5-0.25)/4 = 0.3125 (±10%)
 *   2. amplitude 0.1 below threshold -> peak ≈ 0.1 (±2%)
 *   3. ratio 1.0 (bypass) with amplitude 0.5 -> peak ≈ 0.5 (±0.1%)
 * Exits 0 if all three hold, 1 otherwise.
 */
#include <math.h>
#include <stdio.h>
#include "dsp.h"

static float peak_out(float amp, float thresh, float ratio) {
    comp_t c;
    comp_init(&c, thresh, ratio, 0.005f, 0.100f, 48000.0f);
    const long skip = 48000, n = 48000;
    float peak = 0.0f;
    for (long i = 0; i < skip + n; i++) {
        float x = amp * sinf(6.2831853f * 1000.0f * i / 48000.0f);
        float y = comp_run(&c, x);
        if (i >= skip) {
            float a = y < 0 ? -y : y;
            if (a > peak) peak = a;
        }
    }
    return peak;
}

int main(void) {
    float p1 = peak_out(0.5f, 0.25f, 4.0f);
    float p2 = peak_out(0.1f, 0.25f, 4.0f);
    float p3 = peak_out(0.5f, 0.25f, 1.0f);
    printf("comp 4:1//0.25: in=0.5 -> %.4f (exp 0.3125) | in=0.1 -> %.4f (exp 0.1)\n", p1, p2);
    printf("bypass 1:1: in=0.5 -> %.4f (exp 0.5)\n", p3);
    int ok = (p1 > 0.2812f && p1 < 0.3438f)
          && (p2 > 0.098f && p2 < 0.102f)
          && (p3 > 0.4995f && p3 < 0.5005f);
    printf("%s\n", ok ? "COMP_OK" : "COMP_FAIL");
    return ok ? 0 : 1;
}
