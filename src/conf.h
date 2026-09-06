/* conf.h — shared micfx parameters (ALSA passthrough + filefx).
 * Format: "key value" lines (floats); # comments are ignored.
 */
#ifndef MICFX_CONF_H
#define MICFX_CONF_H

#include <stdio.h>
#include <string.h>

typedef struct {
    float mic1, mic2, master, hpf;
    float c_thr, c_ratio, c_atk, c_rel;
    float eq_low, eq_mid, eq_high;
    float rev_s1, rev_s2, rev_amt;
    float dly_ms, dly_fb, dly_mix;
    float lim_thr;
} conf_t;

static inline void conf_defaults(conf_t *c) {
    c->mic1 = 1.0f; c->mic2 = 1.0f; c->master = 1.0f; c->hpf = 0.0f;
    c->c_thr = 1.0f; c->c_ratio = 1.0f; c->c_atk = 0.005f; c->c_rel = 0.100f;
    c->eq_low = 0.0f; c->eq_mid = 0.0f; c->eq_high = 0.0f;
    c->rev_s1 = 0.0f; c->rev_s2 = 0.0f; c->rev_amt = 0.0f;
    c->dly_ms = 0.0f; c->dly_fb = 0.0f; c->dly_mix = 0.0f;
    c->lim_thr = 1.0f;
}

/* Returns 1 if the file was opened, 0 if missing (uses defaults). */
static inline int conf_load(conf_t *c, const char *path) {
    FILE *f = fopen(path, "r");
    if (!f) return 0;
    char line[128], k[64]; float v;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "%63s %f", k, &v) != 2) continue;
        if (!strcmp(k, "mic1_gain")) c->mic1 = v;
        else if (!strcmp(k, "mic2_gain")) c->mic2 = v;
        else if (!strcmp(k, "master_gain")) c->master = v;
        else if (!strcmp(k, "hpf_frequency")) c->hpf = v;
        else if (!strcmp(k, "compressor_threshold")) c->c_thr = v;
        else if (!strcmp(k, "compressor_ratio")) c->c_ratio = v;
        else if (!strcmp(k, "compressor_attack")) c->c_atk = v;
        else if (!strcmp(k, "compressor_release")) c->c_rel = v;
        else if (!strcmp(k, "eq_low")) c->eq_low = v;
        else if (!strcmp(k, "eq_mid")) c->eq_mid = v;
        else if (!strcmp(k, "eq_high")) c->eq_high = v;
        else if (!strcmp(k, "reverb_send_1")) c->rev_s1 = v;
        else if (!strcmp(k, "reverb_send_2")) c->rev_s2 = v;
        else if (!strcmp(k, "reverb_amount")) c->rev_amt = v;
        else if (!strcmp(k, "delay_ms")) c->dly_ms = v;
        else if (!strcmp(k, "delay_feedback")) c->dly_fb = v;
        else if (!strcmp(k, "delay_mix")) c->dly_mix = v;
        else if (!strcmp(k, "limiter_threshold")) c->lim_thr = v;
    }
    fclose(f);
    return 1;
}

static inline void conf_print(const conf_t *c) {
    printf("gains: mic1=%.3f mic2=%.3f master=%.3f hpf=%.1f Hz comp=%.3f:%.1f atk=%.4f rel=%.3f\n",
           c->mic1, c->mic2, c->master, c->hpf,
           c->c_thr, c->c_ratio, c->c_atk, c->c_rel);
    printf("reverb: send1=%.2f send2=%.2f amount=%.2f\n",
           c->rev_s1, c->rev_s2, c->rev_amt);
    printf("delay: %.1f ms fb=%.2f mix=%.2f | limiter: thr=%.3f\n",
           c->dly_ms, c->dly_fb, c->dly_mix, c->lim_thr);
}

#endif
