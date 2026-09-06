/* test_mix.c — offline channel + mix verification (no ALSA).
 *
 * Two bypassed channels (HPF 0, comp 1:1, flat EQ), gains 1.0 and 0.5,
 * 1 kHz sine amp 0.5 on both, master 1.0:
 *   L = R = 0.5*(0.5 + 0.25) = 0.375 (±0.5%)
 * With master 0.5: L = R = 0.1875 (±0.5%).
 * Exits 0 if all holds, 1 otherwise.
 */
#include <math.h>
#include <stdio.h>
#include "dsp.h"

static void run(float *pl, float *pr, float g1, float g2, float master) {
    chan_t ch1, ch2;
    chan_init(&ch1, 0, 1, 1, 0.005f, 0.1f, 0, 0, 0, g1, 48000.0f);
    chan_init(&ch2, 0, 1, 1, 0.005f, 0.1f, 0, 0, 0, g2, 48000.0f);
    const long skip = 48000, n = 48000;
    float pkl = 0.0f, pkr = 0.0f;
    for (long i = 0; i < skip + n; i++) {
        float x = 0.5f * sinf(6.2831853f * 1000.0f * i / 48000.0f);
        float ol, orr;
        mix_out(chan_run(&ch1, x), chan_run(&ch2, x), master, &ol, &orr);
        if (i >= skip) {
            float al = ol < 0 ? -ol : ol, ar = orr < 0 ? -orr : orr;
            if (al > pkl) pkl = al;
            if (ar > pkr) pkr = ar;
        }
    }
    *pl = pkl; *pr = pkr;
}

int main(void) {
    float l1, r1, l2, r2;
    run(&l1, &r1, 1.0f, 0.5f, 1.0f);
    run(&l2, &r2, 1.0f, 0.5f, 0.5f);
    printf("mix master=1.0: L=%.4f R=%.4f (exp 0.375)\n", l1, r1);
    printf("mix master=0.5: L=%.4f R=%.4f (exp 0.1875)\n", l2, r2);
    int ok = (l1 > 0.3731f && l1 < 0.3769f) && (r1 > 0.3731f && r1 < 0.3769f)
          && (l2 > 0.1866f && l2 < 0.1884f) && (r2 > 0.1866f && r2 < 0.1884f);
    printf("%s\n", ok ? "MIX_OK" : "MIX_FAIL");
    return ok ? 0 : 1;
}
