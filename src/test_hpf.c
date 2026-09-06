/* test_hpf.c — offline high-pass verification (no ALSA).
 *
 * Feeds 50 Hz and 1000 Hz sines through hpf_init(fc=100 Hz)
 * and measures steady-state attenuation. Criteria:
 *   50 Hz   ≈ -12 dB (±2 dB)   [2nd order: (fc/f)^2 -> (100/50)^2 = 4x = -12 dB]
 *   1000 Hz ≈ 0 dB (±0.5 dB)
 * Exits 0 if both hold, 1 otherwise.
 */
#include <math.h>
#include <stdio.h>
#include "dsp.h"

static float measure(float freq, float fc, float rate) {
    hpf_t f;
    hpf_init(&f, fc, rate);
    long skip = (long)rate;       /* 1 s of transient */
    long n = (long)rate;          /* 1 s of measurement */
    float peak = 0.0f;
    for (long i = 0; i < skip + n; i++) {
        float x = sinf(6.2831853f * freq * i / rate);
        float y = hpf_run(&f, x);
        if (i >= skip) {
            float a = y < 0 ? -y : y;
            if (a > peak) peak = a;
        }
    }
    return peak;
}

int main(void) {
    const float rate = 48000.0f, fc = 100.0f;
    float a50 = measure(50.0f, fc, rate);
    float a1k = measure(1000.0f, fc, rate);
    float db50 = 20.0f * log10f(a50 + 1e-9f);
    float db1k = 20.0f * log10f(a1k + 1e-9f);
    printf("fc=100 Hz: 50 Hz -> %.2f dB | 1000 Hz -> %.2f dB\n", db50, db1k);
    int ok = (db50 > -14.0f && db50 < -10.0f) && (db1k > -0.5f && db1k < 0.5f);
    printf("%s\n", ok ? "HPF_OK" : "HPF_FAIL");
    return ok ? 0 : 1;
}
