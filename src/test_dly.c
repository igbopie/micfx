/* test_dly.c — offline delay verification (no ALSA).
 *
 * 1.0 impulse, 1 s @ 48 kHz, delay 100 ms, fb 0.5, mix 0.5:
 *   y[0] = 0.5 (dry), y[4800] ≈ 0.5 (echo 1), y[9600] ≈ 0.25 (echo 2).
 * With mix=0: impulse intact (1.0), zero tail.
 * Exits 0 if all holds, 1 otherwise.
 */
#include <math.h>
#include <stdio.h>
#include "dsp.h"

static delay_t dly;

int main(void) {
    delay_init(&dly, 100.0f, 0.5f, 0.5f, 48000.0f);
    float y0 = 0, y1 = 0, y2 = 0;
    for (long i = 0; i < 48000; i++) {
        float y = delay_run(&dly, i == 0 ? 1.0f : 0.0f);
        if (i == 0) y0 = y;
        if (i == 4800) y1 = y;
        if (i == 9600) y2 = y;
    }
    printf("delay: y0=%.4f (exp 0.5) echo1=%.4f (exp 0.5) echo2=%.4f (exp 0.25)\n",
           y0, y1, y2);
    int t1 = y0 > 0.49f && y0 < 0.51f
          && y1 > 0.475f && y1 < 0.525f
          && y2 > 0.225f && y2 < 0.275f;

    delay_init(&dly, 100.0f, 0.5f, 0.0f, 48000.0f);
    float pk = 0, tail = 0;
    for (long i = 0; i < 48000; i++) {
        float y = delay_run(&dly, i == 0 ? 1.0f : 0.0f);
        float a = y < 0 ? -y : y;
        if (a > pk) pk = a;
        if (i >= 8 && a > tail) tail = a;
    }
    int t2 = pk == 1.0f && tail == 0.0f;
    printf("bypass mix=0: peak=%.4f tail=%.6f %s\n", pk, tail, t2 ? "ok" : "FAIL");

    int ok = t1 && t2;
    printf("%s\n", ok ? "DLY_OK" : "DLY_FAIL");
    return ok ? 0 : 1;
}
