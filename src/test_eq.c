/* test_eq.c — offline 3-band EQ verification (no ALSA).
 *
 * Cases (48 kHz, 1 s transient + 1 s measurement):
 *   1. (0,0,0) dB, 1 kHz amp 0.5 -> 0.5 (±0.5%)
 *   2. low +6 dB, 100 Hz amp 0.5 -> ~1.0 (±5%)
 *   3. low +6 dB, 10 kHz amp 0.5 -> ~0.5 (±3%)
 *   4. mid +6 dB, 1 kHz amp 0.5 -> ~1.0 (±5%)
 *   5. high +6 dB, 10 kHz amp 0.5 -> ~1.0 (±5%)
 * Exits 0 if all hold, 1 otherwise.
 */
#include <math.h>
#include <stdio.h>
#include "dsp.h"

static float peak_out(float freq, float amp, float low, float mid, float high) {
    eq3_t e;
    eq3_init(&e, low, mid, high, 48000.0f);
    const long skip = 48000, n = 48000;
    float peak = 0.0f;
    for (long i = 0; i < skip + n; i++) {
        float x = amp * sinf(6.2831853f * freq * i / 48000.0f);
        float y = eq3_run(&e, x);
        if (i >= skip) {
            float a = y < 0 ? -y : y;
            if (a > peak) peak = a;
        }
    }
    return peak;
}

static int check(const char *name, float got, float want, float tol) {
    int ok = got > want * (1 - tol) && got < want * (1 + tol);
    printf("%-28s -> %.4f (exp %.4f) %s\n", name, got, want, ok ? "ok" : "FAIL");
    return ok;
}

int main(void) {
    int ok = 1;
    ok &= check("flat 1kHz", peak_out(1000, 0.5f, 0, 0, 0), 0.5f, 0.005f);
    ok &= check("low+6 100Hz", peak_out(100, 0.5f, 6, 0, 0), 1.0f, 0.05f);
    ok &= check("low+6 10kHz", peak_out(10000, 0.5f, 6, 0, 0), 0.5f, 0.03f);
    ok &= check("mid+6 1kHz", peak_out(1000, 0.5f, 0, 6, 0), 1.0f, 0.05f);
    ok &= check("high+6 10kHz", peak_out(10000, 0.5f, 0, 0, 6), 1.0f, 0.05f);
    printf("%s\n", ok ? "EQ_OK" : "EQ_FAIL");
    return ok ? 0 : 1;
}
