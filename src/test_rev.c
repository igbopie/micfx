/* test_rev.c — offline reverb verification (no ALSA).
 *
 * 1.0 impulse on channel 1 (rest zeros), 2 s @ 48 kHz, chain:
 * chan(bypass,gain=1) -> mix -> send(1,1) -> reverb -> master=1.
 *   1. amount=0: output identically 0 after the impulse.
 *   2. amount=1: tail exists (E[0.3-1s] > 0), bounded (peak < 2.0)
 *      and decaying (E[1.5-2s] < E[0-0.5s]).
 *   3. sends=0: output 0 even with amount=1.
 * Exits 0 if all holds, 1 otherwise.
 */
#include <math.h>
#include <stdio.h>
#include "dsp.h"

static reverb_t rev;
static chan_t ch1, ch2;

static void setup(float s1, float s2, float amount) {
    (void)s1; (void)s2;
    chan_init(&ch1, 0, 1, 1, 0.005f, 0.1f, 0, 0, 0, 1.0f, 48000.0f);
    chan_init(&ch2, 0, 1, 1, 0.005f, 0.1f, 0, 0, 0, 1.0f, 48000.0f);
    reverb_init(&rev, amount, 48000.0f);
}

static float run_one(float s1, float s2, float amount,
                     float *e_early, float *e_mid, float *e_late,
                     float *peak_all, float *peak_tail) {
    setup(s1, s2, amount);
    const long n = 96000;
    float ee = 0, em = 0, el = 0, pka = 0, pkt = 0;
    for (long i = 0; i < n; i++) {
        float in1 = i == 0 ? 1.0f : 0.0f;
        float l = chan_run(&ch1, in1);
        float rr = chan_run(&ch2, 0.0f);
        float m = (l + rr) * 0.5f;
        float w = reverb_wet(&rev, s1 * l + s2 * rr);
        float o = m + rev.amount * w;
        float a = o < 0 ? -o : o;
        if (a > pka) pka = a;
        if (i >= 8 && a > pkt) pkt = a; /* tail: past the dry impulse */
        if (i < 24000) ee += a;
        else if (i < 48000) em += a;
        else el += a;
    }
    *e_early = ee; *e_mid = em; *e_late = el;
    *peak_all = pka; *peak_tail = pkt;
    return pkt;
}

int main(void) {
    float ee, em, el, pka, pkt;
    run_one(1, 1, 0.0f, &ee, &em, &el, &pka, &pkt);
    int t1 = pkt == 0.0f;
    printf("amount=0: tail=%.6f %s\n", pkt, t1 ? "ok" : "FAIL");

    run_one(1, 1, 1.0f, &ee, &em, &el, &pka, &pkt);
    int t2 = em > 0.0f && pka < 2.0f && el < ee;
    printf("amount=1: tail=%.4f peak=%.4f decays=%d %s\n", em, pka, el < ee,
           t2 ? "ok" : "FAIL");

    run_one(0, 0, 1.0f, &ee, &em, &el, &pka, &pkt);
    int t3 = pka == 0.5f && pkt == 0.0f; /* dry impulse only, no tail */
    printf("sends=0: peak=%.6f tail=%.6f %s\n", pka, pkt, t3 ? "ok" : "FAIL");

    int ok = t1 && t2 && t3;
    printf("%s\n", ok ? "REV_OK" : "REV_FAIL");
    return ok ? 0 : 1;
}
