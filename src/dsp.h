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

#endif
