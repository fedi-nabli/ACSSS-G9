
#include "core/kalman/kalman.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define K_PI 3.14159265358979323846f
#define K_TWO_PI (2.0f * K_PI)
#define K_MIN_WINDOW 32
#define K_MIN_DT 0.0001f
#define K_MAX_DT 0.1f
#define K_MIN_VARIANCE 1.0e-8f

static bool valid_axis(Kalman_Axis axis)
{
    return (unsigned)axis < KALMAN_ESTIMATOR_COUNT;
}

static void reset_axis(Kalman_Axis_State *s)
{
    memset(s, 0, sizeof(*s));
}

Kalman_Config kalman_default_config(void)
{
    Kalman_Config c;

    c.min_frequency_hz = 3.0f;
    c.max_frequency_hz = 12.0f;
    c.frequency_step_hz = 0.1f;

    c.process_noise = 0.01f;
    c.measurement_noise = 0.1f;
    c.max_sample_gap_s = 0.05f;

    c.window_size = 128;
    c.frequency_update_samples = 10;

    return c;
}

bool kalman_init(Kalman *k, const Kalman_Config *config)
{
    if (k == NULL)
        return false;

    k->config = config ? *config : kalman_default_config();

    if (!isfinite(k->config.min_frequency_hz) ||
        !isfinite(k->config.max_frequency_hz) ||
        !isfinite(k->config.frequency_step_hz) ||
        !isfinite(k->config.process_noise) ||
        !isfinite(k->config.measurement_noise) ||
        !isfinite(k->config.max_sample_gap_s) ||
        k->config.min_frequency_hz <= 0.0f ||
        k->config.max_frequency_hz <= k->config.min_frequency_hz ||
        k->config.frequency_step_hz <= 0.0f ||
        k->config.process_noise < 0.0f ||
        k->config.measurement_noise <= 0.0f ||
        k->config.max_sample_gap_s <= 0.0f ||
        k->config.window_size < K_MIN_WINDOW ||
        k->config.window_size > KALMAN_MAX_WINDOW ||
        k->config.frequency_update_samples == 0)
        return false;

    kalman_reset(k);
    return true;
}

void kalman_reset(Kalman *k)
{
    if (k == NULL)
        return;

    memset(k->axes, 0, sizeof(k->axes));
}

static float read_measurement(const Raw_Sample *sample, unsigned axis)
{
    if (axis < 3)
        return sample->accel[axis];

    return sample->gyro[axis - 3];
}

static void push_sample(
    Kalman_Axis_State *s,
    uint16_t capacity,
    int64_t timestamp_ns,
    float value)
{
    s->values[s->head] = value;
    s->timestamps_ns[s->head] = timestamp_ns;
    s->head = (uint16_t)((s->head + 1u) % capacity);

    if (s->count < capacity)
        ++s->count;
}

/*
 * Fit y = a*cos(w*t) + b*sin(w*t) + dc at each candidate
 * frequency. Return the frequency with the lowest least-squares
 * residual. Timestamps are centred relative to the newest sample.
 *
 * Confidence compares the best sinusoidal fit with the DC-only
 * fit. It is a spectral-fit score, not a calibrated probability.
 */
static bool estimate_frequency(
    const Kalman_Axis_State *s,
    const Kalman_Config *c,
    float *frequency,
    float *confidence)
{
    const unsigned n = s->count;

    if (n < K_MIN_WINDOW)
        return false;

    float mean = 0.0f;
    const unsigned oldest =
        (s->head + c->window_size - n) % c->window_size;

    for (unsigned i = 0; i < n; ++i) {
        unsigned j = (oldest + i) % c->window_size;
        mean += s->values[j];
    }
    mean /= (float)n;

    float total_energy = 0.0f;
    for (unsigned i = 0; i < n; ++i) {
        unsigned j = (oldest + i) % c->window_size;
        float y = s->values[j] - mean;
        total_energy += y * y;
    }

    if (total_energy < K_MIN_VARIANCE)
        return false;

    /*
     * Use a time origin at the newest sample. This reduces the
     * size of the trigonometric arguments.
     */
    const int64_t newest_ns =
        s->timestamps_ns[(s->head + c->window_size - 1u)
                         % c->window_size];

    float best_error = total_energy;
    float best_frequency = 0.0f;

    for (float f = c->min_frequency_hz;
         f <= c->max_frequency_hz + 0.5f * c->frequency_step_hz;
         f += c->frequency_step_hz) {

        const float w = K_TWO_PI * f;

        float cc = 0.0f, ss = 0.0f, cs = 0.0f;
        float yc = 0.0f, ys = 0.0f;

        for (unsigned i = 0; i < n; ++i) {
            unsigned j = (oldest + i) % c->window_size;

            float t = (float)(
                (double)(s->timestamps_ns[j] - newest_ns) * 1.0e-9);

            float y = s->values[j] - mean;
            float co = cosf(w * t);
            float si = sinf(w * t);

            cc += co * co;
            ss += si * si;
            cs += co * si;
            yc += y * co;
            ys += y * si;
        }

        float determinant = cc * ss - cs * cs;
        if (determinant < K_MIN_VARIANCE)
            continue;

        float a = (yc * ss - ys * cs) / determinant;
        float b = (ys * cc - yc * cs) / determinant;

        /* Residual sum of squares for the fitted sinusoid. */
        float explained = a * yc + b * ys;
        float error = total_energy - explained;

        if (error < best_error) {
            best_error = error;
            best_frequency = f;
        }
    }

    if (best_frequency <= 0.0f)
        return false;

    *frequency = best_frequency;
    *confidence = fmaxf(
        0.0f,
        fminf(1.0f, (total_energy - best_error) / total_energy));

    return true;
}

/*
 * Two-state harmonic oscillator:
 *
 *     state = [tremor displacement, tremor velocity]
 *
 * The frequency is supplied by the independent frequency tracker.
 * A scalar measurement observes displacement.
 */
static void oscillator_update(
    Kalman_Axis_State *s,
    const Kalman_Config *c,
    float measurement,
    float dt)
{
    if (!s->initialized) {
        s->x = measurement;
        s->v = 0.0f;

        s->p00 = 1.0f;
        s->p01 = 0.0f;
        s->p10 = 0.0f;
        s->p11 = 1.0f;

        s->initialized = true;
        return;
    }

    float f = s->frequency_valid
        ? s->frequency_hz
        : 0.5f * (c->min_frequency_hz + c->max_frequency_hz);

    float w = K_TWO_PI * f;
    float co = cosf(w * dt);
    float si = sinf(w * dt);

    /* State prediction. */
    float x_pred = co * s->x + (si / w) * s->v;
    float v_pred = -w * si * s->x + co * s->v;

    /* P <- F P F^T + Q. */
    float q = c->process_noise;

    float fp00 = co * s->p00 + (si / w) * s->p10;
    float fp01 = co * s->p01 + (si / w) * s->p11;
    float fp10 = -w * si * s->p00 + co * s->p10;
    float fp11 = -w * si * s->p01 + co * s->p11;

    float p00 = fp00 * co + fp01 * (si / w) + q * dt;
    float p01 = -fp00 * w * si + fp01 * co;
    float p10 = fp10 * co + fp11 * (si / w);
    float p11 = -fp10 * w * si + fp11 * co + q * dt;

    /* Measurement observes x only. */
    float r = c->measurement_noise;
    float innovation_variance = p00 + r;

    if (!isfinite(innovation_variance) ||
        innovation_variance <= K_MIN_VARIANCE) {
        reset_axis(s);
        return;
    }

    float k0 = p00 / innovation_variance;
    float k1 = p10 / innovation_variance;
    float innovation = measurement - x_pred;

    s->x = x_pred + k0 * innovation;
    s->v = v_pred + k1 * innovation;

    /*
     * Joseph-equivalent scalar covariance update for H = [1, 0].
     */
    s->p00 = (1.0f - k0) * p00;
    s->p01 = (1.0f - k0) * p01;
    s->p10 = p10 - k1 * p00;
    s->p11 = p11 - k1 * p01;

    if (s->p00 < 0.0f) s->p00 = 0.0f;
    if (s->p11 < 0.0f) s->p11 = 0.0f;
}

void kalman_update(Kalman *k, const Raw_Sample *sample)
{
    if (k == NULL || sample == NULL)
        return;

    for (unsigned i = 0; i < KALMAN_ESTIMATOR_COUNT; ++i) {
        Kalman_Axis_State *s = &k->axes[i];
        float value = read_measurement(sample, i);

        if (!isfinite(value)) {
            reset_axis(s);
            continue;
        }

        /*
         * A gap, non-monotonic timestamp, or sequence discontinuity
         * invalidates the oscillator's time propagation.
         */
        bool discontinuity = (sample->flags & SAMPLE_GAP) != 0;

        if (s->last_timestamp_ns != 0) {
            if (sample->t_ns <= s->last_timestamp_ns)
                discontinuity = true;

            if (sample->seq != s->last_seq + 1u)
                discontinuity = true;

            double elapsed =
                ((double)sample->t_ns - (double)s->last_timestamp_ns)
                * 1.0e-9;

            if (elapsed > k->config.max_sample_gap_s)
                discontinuity = true;
        }

        if (discontinuity)
            reset_axis(s);

        s->last_timestamp_ns = sample->t_ns;
        s->last_seq = sample->seq;

        /*
         * Saturated measurements must not update the estimator.
         * Preserve the time/sequence reference so the next sample
         * can still be checked for discontinuities.
         */
        if ((sample->flags & SAMPLE_SATURATED) != 0)
            continue;

        float dt = 0.0f;

        if (s->count > 0) {
            unsigned previous =
                (s->head + k->config.window_size - 1u)
                % k->config.window_size;

            dt = (float)(
                (double)(sample->t_ns - s->timestamps_ns[previous])
                * 1.0e-9);
        }

        
        push_sample(
            s, k->config.window_size, sample->t_ns, value);

        /* Centre the oscillator input to reject gravity and DC bias. */
        float mean = 0.0f;

        for (unsigned j = 0; j < s->count; ++j) {
            unsigned index =
                (s->head + k->config.window_size - s->count + j)
                % k->config.window_size;

            mean += s->values[index];
        }

        mean /= (float)s->count;
        float detrended = value - mean;

        if (dt >= K_MIN_DT && dt <= K_MAX_DT) {
            oscillator_update(
                s, &k->config, detrended, dt);
        } else if (s->count == 1) {
            oscillator_update(
                s, &k->config, detrended, 0.0f);
        } else {
            s->initialized = false;
            oscillator_update(
                s, &k->config, detrended, 0.0f);
        }


        if (++s->samples_since_frequency_update >=
            k->config.frequency_update_samples) {

            s->samples_since_frequency_update = 0;

            float frequency, confidence;

            if (estimate_frequency(
                    s, &k->config, &frequency, &confidence)) {

                s->frequency_hz = frequency;
                s->confidence = confidence;
                s->frequency_valid = true;
            }
        }
    }
}

Kalman_Estimate kalman_get(
    const Kalman *k,
    Kalman_Axis axis)
{
    Kalman_Estimate out = {0};

    if (k == NULL || !valid_axis(axis))
        return out;

    const Kalman_Axis_State *s = &k->axes[axis];

    if (!s->initialized || !s->frequency_valid)
        return out;

    float w = K_TWO_PI * s->frequency_hz;

    out.frequency_hz = s->frequency_hz;
    out.amplitude = hypotf(s->x, s->v / w);
    out.current_tremor = s->x;
    out.predicted_tremor = s->x;
    out.confidence = s->confidence;
    out.valid = true;

    return out;
}

Kalman_Estimate kalman_predict(
    const Kalman *k,
    Kalman_Axis axis,
    float horizon_seconds)
{
    Kalman_Estimate out = kalman_get(k, axis);

    if (!out.valid || !isfinite(horizon_seconds) ||
        horizon_seconds < 0.0f)
        return (Kalman_Estimate){0};

    const Kalman_Axis_State *s = &k->axes[axis];

    float w = K_TWO_PI * s->frequency_hz;
    float co = cosf(w * horizon_seconds);
    float si = sinf(w * horizon_seconds);

    out.predicted_tremor =
        co * s->x + (si / w) * s->v;

    return out;
}
