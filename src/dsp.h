/* dsp.h — shared DSP blocks (no ALSA, offline-testable).
 * Stage 3: high-pass biquad (RBJ cookbook), Q=0.7071 (2nd-order Butterworth).
 */
#ifndef MICFX_DSP_H
#define MICFX_DSP_H

#include <math.h>

typedef struct {
    float b0, b1, b2, a1, a2; /* a0 normalized to 1 */
    float x1, x2, y1, y2;     /* state */
} hpf_t;

/* hpf_set retunes without touching state: live-knob safe. */
static inline void hpf_set(hpf_t *f, float fc_hz, float rate_hz) {
    float w0 = 6.283185307179586f * fc_hz / rate_hz;
    float c = cosf(w0), s = sinf(w0);
    float alpha = s / 2.0f / 0.70710678f;
    float b0 = (1.0f + c) / 2.0f, b1 = -(1.0f + c), b2 = (1.0f + c) / 2.0f;
    float a0 = 1.0f + alpha, a1 = -2.0f * c, a2 = 1.0f - alpha;
    f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0;
    f->a1 = a1 / a0; f->a2 = a2 / a0;
}

static inline void hpf_init(hpf_t *f, float fc_hz, float rate_hz) {
    hpf_set(f, fc_hz, rate_hz);
    f->x1 = f->x2 = f->y1 = f->y2 = 0.0f;
}

static inline float hpf_run(hpf_t *f, float x) {
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2
              - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = x;
    f->y2 = f->y1; f->y1 = y;
    return y;
}

/* Per-channel peak compressor: envelope follower with exponential
 * attack/release + static curve (linear threshold/ratio). No costly
 * branches, no dynamic memory: safe for the realtime loop.
 * ratio <= 1.0f = bypass (returns input unchanged).
 */
typedef struct {
    float thresh, ratio, atk, rel;
    float env;
} comp_t;

static inline void comp_init(comp_t *c, float thresh, float ratio,
                             float atk_s, float rel_s, float rate_hz) {
    c->thresh = thresh; c->ratio = ratio;
    c->atk = 1.0f - expf(-1.0f / (atk_s * rate_hz));
    c->rel = 1.0f - expf(-1.0f / (rel_s * rate_hz));
    c->env = 0.0f;
}

static inline float comp_run(comp_t *c, float x) {
    if (c->ratio <= 1.0f) return x;
    float a = x < 0 ? -x : x;
    float k = a > c->env ? c->atk : c->rel;
    c->env += k * (a - c->env);
    if (c->env <= c->thresh || c->env <= 0.0f) return x;
    float target = c->thresh + (c->env - c->thresh) / c->ratio;
    return x * (target / c->env);
}

/* Per-channel 3-band EQ: 250 Hz low-shelf, 1 kHz peak (Q=1),
 * 4 kHz high-shelf (RBJ cookbook). Gains in dB; 0 dB = all-pass.
 */
typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} bq_t;

/* bq_coefs retunes without touching state: live-knob safe. */
static inline void bq_coefs(bq_t *f, float b0, float b1, float b2,
                            float a0, float a1, float a2) {
    f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0;
    f->a1 = a1 / a0; f->a2 = a2 / a0;
}

static inline void bq_norm(bq_t *f, float b0, float b1, float b2,
                           float a0, float a1, float a2) {
    bq_coefs(f, b0, b1, b2, a0, a1, a2);
    f->x1 = f->x2 = f->y1 = f->y2 = 0.0f;
}

static inline void bq_low_shelf(bq_t *f, float fc, float db, float rate) {
    float A = powf(10.0f, db / 40.0f);
    float w0 = 6.283185307179586f * fc / rate;
    float c = cosf(w0), s = sinf(w0);
    float alpha = s / 2.0f * sqrtf(2.0f); /* S=1 */
    float sqA = sqrtf(A);
    bq_coefs(f, A * ((A + 1) - (A - 1) * c + 2 * sqA * alpha),
               2 * A * ((A - 1) - (A + 1) * c),
               A * ((A + 1) - (A - 1) * c - 2 * sqA * alpha),
               (A + 1) + (A - 1) * c + 2 * sqA * alpha,
               -2 * ((A - 1) + (A + 1) * c),
               (A + 1) + (A - 1) * c - 2 * sqA * alpha);
}

static inline void bq_high_shelf(bq_t *f, float fc, float db, float rate) {
    float A = powf(10.0f, db / 40.0f);
    float w0 = 6.283185307179586f * fc / rate;
    float c = cosf(w0), s = sinf(w0);
    float alpha = s / 2.0f * sqrtf(2.0f); /* S=1 */
    float sqA = sqrtf(A);
    bq_coefs(f, A * ((A + 1) + (A - 1) * c + 2 * sqA * alpha),
               -2 * A * ((A - 1) + (A + 1) * c),
               A * ((A + 1) + (A - 1) * c - 2 * sqA * alpha),
               (A + 1) - (A - 1) * c + 2 * sqA * alpha,
               2 * ((A - 1) - (A + 1) * c),
               (A + 1) - (A - 1) * c - 2 * sqA * alpha);
}

static inline void bq_peak(bq_t *f, float fc, float db, float Q, float rate) {
    float A = powf(10.0f, db / 40.0f);
    float w0 = 6.283185307179586f * fc / rate;
    float c = cosf(w0), s = sinf(w0);
    float alpha = s / 2.0f / Q;
    bq_coefs(f, 1 + alpha * A, -2 * c, 1 - alpha * A,
               1 + alpha / A, -2 * c, 1 - alpha / A);
}

static inline float bq_run(bq_t *f, float x) {
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2
              - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = x;
    f->y2 = f->y1; f->y1 = y;
    return y;
}

typedef struct { bq_t low, mid, high; } eq3_t;

/* eq3_set retunes without touching state: live-knob safe. */
static inline void eq3_set(eq3_t *e, float low_db, float mid_db,
                           float high_db, float rate) {
    bq_low_shelf(&e->low, 250.0f, low_db, rate);
    bq_peak(&e->mid, 1000.0f, mid_db, 1.0f, rate);
    bq_high_shelf(&e->high, 4000.0f, high_db, rate);
}

static inline void eq3_init(eq3_t *e, float low_db, float mid_db,
                            float high_db, float rate) {
    eq3_set(e, low_db, mid_db, high_db, rate);
    e->low.x1 = e->low.x2 = e->low.y1 = e->low.y2 = 0.0f;
    e->mid.x1 = e->mid.x2 = e->mid.y1 = e->mid.y2 = 0.0f;
    e->high.x1 = e->high.x2 = e->high.y1 = e->high.y2 = 0.0f;
}

static inline float eq3_run(eq3_t *e, float x) {
    return bq_run(&e->high, bq_run(&e->mid, bq_run(&e->low, x)));
}

/* Full channel (stages 2-5): HPF -> comp -> EQ -> gain.
 * All float; the caller normalizes to [-1,1] on entry. */
typedef struct {
    hpf_t hpf; int use_hpf;
    comp_t comp;
    eq3_t eq;
    float gain;
} chan_t;

static inline void chan_init(chan_t *ch, float hpf_hz,
                             float c_thr, float c_ratio,
                             float c_atk, float c_rel,
                             float eq_low, float eq_mid, float eq_high,
                             float gain, float rate) {
    ch->use_hpf = hpf_hz > 0.0f;
    if (ch->use_hpf) hpf_init(&ch->hpf, hpf_hz, rate);
    comp_init(&ch->comp, c_thr, c_ratio, c_atk, c_rel, rate);
    eq3_init(&ch->eq, eq_low, eq_mid, eq_high, rate);
    ch->gain = gain;
}

static inline float chan_run(chan_t *ch, float x) {
    if (ch->use_hpf) x = hpf_run(&ch->hpf, x);
    x = comp_run(&ch->comp, x);
    x = eq3_run(&ch->eq, x);
    return x * ch->gain;
}

/* chan_sync pushes knob-editable params into a running channel.
 * No state is cleared: live-knob safe. Call once per block. */
static inline void chan_sync(chan_t *ch, float hpf_hz,
                             float c_thr, float c_ratio,
                             float c_atk, float c_rel,
                             float eq_low, float eq_mid, float eq_high,
                             float gain, float rate) {
    if (c_atk < 1e-4f) c_atk = 1e-4f;
    if (c_rel < 1e-4f) c_rel = 1e-4f;
    ch->use_hpf = hpf_hz > 0.0f;
    if (ch->use_hpf) hpf_set(&ch->hpf, hpf_hz, rate);
    ch->comp.thresh = c_thr; ch->comp.ratio = c_ratio;
    ch->comp.atk = 1.0f - expf(-1.0f / (c_atk * rate));
    ch->comp.rel = 1.0f - expf(-1.0f / (c_rel * rate));
    eq3_set(&ch->eq, eq_low, eq_mid, eq_high, rate);
    ch->gain = gain;
}

/* Stage 6: dual-mono mix — both outputs = average, with master. */
static inline void mix_out(float l, float r, float master, float *ol, float *or_) {
    float m = (l + r) * 0.5f * master;
    *ol = m; *or_ = m;
}

/* Stage 7: shared Schroeder-style reverb (mono).
 * 4 parallel combs + 2 series allpasses. Static buffers
 * (up to 50 ms @ 96 kHz): no malloc, realtime-safe.
 * amount = wet level (0 = dry only). Stable with fb < 1.
 */
#define REV_MAXD 4800

typedef struct { float buf[REV_MAXD]; int len, idx; } dline_t;

static inline float comb_run(dline_t *d, float x, float fb) {
    float y = d->buf[d->idx];
    d->buf[d->idx] = x + y * fb;
    if (++d->idx >= d->len) d->idx = 0;
    return y;
}

static inline float ap_run(dline_t *d, float x, float fb) {
    float dl = d->buf[d->idx];
    float y = -fb * x + dl;
    d->buf[d->idx] = x + fb * y;
    if (++d->idx >= d->len) d->idx = 0;
    return y;
}

typedef struct {
    dline_t comb[4];
    dline_t ap[2];
    float amount;
} reverb_t;

static inline void dline_reset(dline_t *d, int len) {
    d->len = len < REV_MAXD ? len : REV_MAXD;
    d->idx = 0;
    for (int i = 0; i < d->len; i++) d->buf[i] = 0.0f;
}

static inline void reverb_init(reverb_t *r, float amount, float rate) {
    float k = rate / 48000.0f;
    /* Freeverb-style delays, scaled by sample rate */
    const int cl[4] = { 1557, 1617, 1491, 1422 };
    const int al[2] = { 556, 441 };
    for (int i = 0; i < 4; i++) dline_reset(&r->comb[i], (int)(cl[i] * k));
    for (int i = 0; i < 2; i++) dline_reset(&r->ap[i], (int)(al[i] * k));
    r->amount = amount;
}

static inline float reverb_wet(reverb_t *r, float x) {
    float y = 0.0f;
    for (int i = 0; i < 4; i++) y += comb_run(&r->comb[i], x, 0.84f);
    y *= 0.25f;
    y = ap_run(&r->ap[0], y, 0.5f);
    y = ap_run(&r->ap[1], y, 0.5f);
    return y;
}

/* Stage 8: mono delay/echo (optional). Feedback line;
 * static buffer up to 1 s @ 48 kHz. mix=0 = bypass.
 */
#define DLY_MAX 48000

typedef struct {
    float buf[DLY_MAX];
    int len, idx;
    float fb, mix;
} delay_t;

static inline void delay_init(delay_t *d, float ms, float fb, float mix, float rate) {
    int len = (int)(ms * rate / 1000.0f);
    if (len < 1) len = 1;
    if (len > DLY_MAX) len = DLY_MAX;
    d->len = len; d->idx = 0;
    d->fb = fb; d->mix = mix;
    for (int i = 0; i < d->len; i++) d->buf[i] = 0.0f;
}

/* delay_set_time changes echo time without clearing audio. */
static inline void delay_set_time(delay_t *d, float ms, float rate) {
    int len = (int)(ms * rate / 1000.0f);
    if (len < 1) len = 1;
    if (len > DLY_MAX) len = DLY_MAX;
    if (len != d->len) { d->len = len; d->idx %= len; }
}

static inline float delay_run(delay_t *d, float x) {
    float e = d->buf[d->idx];
    d->buf[d->idx] = x + e * d->fb;
    if (++d->idx >= d->len) d->idx = 0;
    return x * (1.0f - d->mix) + e * d->mix;
}

/* Stage 9: final brickwall limiter. Clips to ±threshold (linear).
 * Last stage: anti-clip safety before the output.
 */
typedef struct { float thr; } lim_t;

static inline void lim_init(lim_t *l, float thr) { l->thr = thr; }

static inline float lim_run(lim_t *l, float x) {
    if (x > l->thr) return l->thr;
    if (x < -l->thr) return -l->thr;
    return x;
}

#endif
