/* dsp.h — bloques DSP compartidos (sin ALSA, testeables offline).
 * Etapa 3: high-pass biquad (RBJ cookbook), Q=0.7071 (Butterworth 2º orden).
 */
#ifndef MICFX_DSP_H
#define MICFX_DSP_H

#include <math.h>

typedef struct {
    float b0, b1, b2, a1, a2; /* a0 normalizado a 1 */
    float x1, x2, y1, y2;     /* estado */
} hpf_t;

static inline void hpf_init(hpf_t *f, float fc_hz, float rate_hz) {
    float w0 = 6.283185307179586f * fc_hz / rate_hz;
    float c = cosf(w0), s = sinf(w0);
    float alpha = s / 2.0f / 0.70710678f;
    float b0 = (1.0f + c) / 2.0f, b1 = -(1.0f + c), b2 = (1.0f + c) / 2.0f;
    float a0 = 1.0f + alpha, a1 = -2.0f * c, a2 = 1.0f - alpha;
    f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0;
    f->a1 = a1 / a0; f->a2 = a2 / a0;
    f->x1 = f->x2 = f->y1 = f->y2 = 0.0f;
}

static inline float hpf_run(hpf_t *f, float x) {
    float y = f->b0 * x + f->b1 * f->x1 + f->b2 * f->x2
              - f->a1 * f->y1 - f->a2 * f->y2;
    f->x2 = f->x1; f->x1 = x;
    f->y2 = f->y1; f->y1 = y;
    return y;
}

/* Compresor de pico por canal: seguidor de envolvente con attack/release
 * exponenciales + curva estática (umbral/ratio en lineal). Sin branches
 * costosos ni memoria dinámica: apto para el loop realtime.
 * ratio <= 1.0f = bypass (devuelve la entrada tal cual).
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

/* EQ de 3 bandas por canal: low-shelf 250 Hz, pico 1 kHz (Q=1),
 * high-shelf 4 kHz (RBJ cookbook). Ganancias en dB; 0 dB = pasa-todo.
 */
typedef struct {
    float b0, b1, b2, a1, a2;
    float x1, x2, y1, y2;
} bq_t;

static inline void bq_norm(bq_t *f, float b0, float b1, float b2,
                           float a0, float a1, float a2) {
    f->b0 = b0 / a0; f->b1 = b1 / a0; f->b2 = b2 / a0;
    f->a1 = a1 / a0; f->a2 = a2 / a0;
    f->x1 = f->x2 = f->y1 = f->y2 = 0.0f;
}

static inline void bq_low_shelf(bq_t *f, float fc, float db, float rate) {
    float A = powf(10.0f, db / 40.0f);
    float w0 = 6.283185307179586f * fc / rate;
    float c = cosf(w0), s = sinf(w0);
    float alpha = s / 2.0f * sqrtf(2.0f); /* S=1 */
    float sqA = sqrtf(A);
    bq_norm(f, A * ((A + 1) - (A - 1) * c + 2 * sqA * alpha),
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
    bq_norm(f, A * ((A + 1) + (A - 1) * c + 2 * sqA * alpha),
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
    bq_norm(f, 1 + alpha * A, -2 * c, 1 - alpha * A,
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

static inline void eq3_init(eq3_t *e, float low_db, float mid_db,
                            float high_db, float rate) {
    bq_low_shelf(&e->low, 250.0f, low_db, rate);
    bq_peak(&e->mid, 1000.0f, mid_db, 1.0f, rate);
    bq_high_shelf(&e->high, 4000.0f, high_db, rate);
}

static inline float eq3_run(eq3_t *e, float x) {
    return bq_run(&e->high, bq_run(&e->mid, bq_run(&e->low, x)));
}

/* Canal completo (etapas 2-5): HPF -> comp -> EQ -> ganancia.
 * Todo en float; el llamador normaliza a [-1,1] antes de entrar. */
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

/* Etapa 6: mezcla a mono dual — ambas salidas = promedio, con master. */
static inline void mix_out(float l, float r, float master, float *ol, float *or_) {
    float m = (l + r) * 0.5f * master;
    *ol = m; *or_ = m;
}

/* Etapa 7: reverb compartida tipo Schroeder (mono).
 * 4 combs en paralelo + 2 allpass en serie. Buffers estáticos
 * (hasta 50 ms @ 96 kHz): sin malloc, apta para realtime.
 * amount = nivel wet (0 = solo dry). Estable con fb < 1.
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
    /* retardos estilo Freeverb, escalados por frecuencia de muestreo */
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

#endif
