/*
 * kalman_synthetic_test.c
 *
 * Deterministic synthetic-sinusoid test for the project's real Kalman module.
 * No Sense HAT and no input CSV are required.
 *
 * Usage:
 *   mts_core_kalman_synthetic_test [output.csv] [frequency_hz] [duration_s]
 *                                  [sample_rate_hz] [amplitude] [noise_sigma]
 *
 * Defaults: output=synthetic_kalman_test.csv, f=5 Hz, duration=5 s,
 * sample rate=230 Hz, amplitude=1, noise sigma=0.
 *
 * The same known sinusoid is injected into all six accel/gyro channels so all
 * six independent estimators can be inspected. Units are intentionally
 * arbitrary: this test assesses the estimator, not sensor calibration.
 */
#include "core/kalman/kalman.h"

#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define PI 3.14159265358979323846264338327950288
#define AXES KALMAN_ESTIMATOR_COUNT
#define PREDICTION_HORIZON_S 0.010
#define METRIC_WARMUP_S 1.0
#define TIMESTAMP_BASE_NS INT64_C(1000000000)

static const char *const axis_names[AXES] = {
    "accel_x", "accel_y", "accel_z", "gyro_x", "gyro_y", "gyro_z"
};

/* Deterministic xorshift PRNG, used only when noise_sigma > 0. */
static uint32_t rng_state = UINT32_C(0x5eed1234);

static double uniform_open01(void)
{
    rng_state ^= rng_state << 13;
    rng_state ^= rng_state >> 17;
    rng_state ^= rng_state << 5;
    return ((double)rng_state + 1.0) / 4294967297.0;
}

static double gaussian_noise(void)
{
    const double u1 = uniform_open01();
    const double u2 = uniform_open01();
    return sqrt(-2.0 * log(u1)) * cos(2.0 * PI * u2);
}

static int parse_positive(const char *text, double *out)
{
    char *end = NULL;
    errno = 0;
    const double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' ||
        !isfinite(value) || value <= 0.0)
        return 0;
    *out = value;
    return 1;
}

static int parse_nonnegative(const char *text, double *out)
{
    char *end = NULL;
    errno = 0;
    const double value = strtod(text, &end);
    if (errno != 0 || end == text || *end != '\0' ||
        !isfinite(value) || value < 0.0)
        return 0;
    *out = value;
    return 1;
}

static double sinusoid(double t, double amplitude, double frequency_hz)
{
    return amplitude * sin(2.0 * PI * frequency_hz * t);
}

int main(int argc, char **argv)
{
    if (argc > 7) {
        fprintf(stderr,
            "Usage: %s [output.csv] [frequency_hz] [duration_s] "
            "[sample_rate_hz] [amplitude] [noise_sigma]\n", argv[0]);
        return 2;
    }

    const char *output_path = argc >= 2 ? argv[1] : "synthetic_kalman_test.csv";
    double frequency_hz = 5.0;
    double duration_s = 5.0;
    double sample_rate_hz = 230.0;
    double amplitude = 1.0;
    double noise_sigma = 0.0;

    if ((argc >= 3 && !parse_positive(argv[2], &frequency_hz)) ||
        (argc >= 4 && !parse_positive(argv[3], &duration_s)) ||
        (argc >= 5 && !parse_positive(argv[4], &sample_rate_hz)) ||
        (argc >= 6 && !parse_positive(argv[5], &amplitude)) ||
        (argc >= 7 && !parse_nonnegative(argv[6], &noise_sigma))) {
        fprintf(stderr, "Invalid numeric argument. Parameters must be positive; noise_sigma may be zero.\n");
        return 2;
    }

    if (frequency_hz < 3.0 || frequency_hz > 12.0) {
        fprintf(stderr, "Frequency must be within the default Kalman search range 3-12 Hz.\n");
        return 2;
    }
    if (frequency_hz >= sample_rate_hz / 2.0) {
        fprintf(stderr, "Frequency must be below the Nyquist frequency (sample_rate/2).\n");
        return 2;
    }
    if (sample_rate_hz > 10000.0 || duration_s > 3600.0) {
        fprintf(stderr, "Sample rate or duration is unreasonably large.\n");
        return 2;
    }

    const uint64_t sample_count = (uint64_t)llround(duration_s * sample_rate_hz);
    if (sample_count < 2) {
        fprintf(stderr, "Duration and sample rate produce too few samples.\n");
        return 2;
    }

    Kalman kalman;
    Kalman_Config config = kalman_default_config();
    if (!kalman_init(&kalman, &config)) {
        fprintf(stderr, "kalman_init failed.\n");
        return 1;
    }

    FILE *out = fopen(output_path, "w");
    if (out == NULL) {
        fprintf(stderr, "Cannot create %s: %s\n", output_path, strerror(errno));
        return 1;
    }

    fprintf(out,
        "seq,t_s,axis,input,truth_now,truth_future_10ms,frequency_hz,confidence,valid,"
        "current_tremor,predicted_10ms,current_error,prediction_error\n");

    double sum_abs_pred[AXES] = {0};
    double sum_sq_pred[AXES] = {0};
    double sum_abs_current[AXES] = {0};
    double sum_sq_current[AXES] = {0};
    double sum_abs_frequency[AXES] = {0};
    size_t metric_count[AXES] = {0};

    for (uint64_t i = 0; i < sample_count; ++i) {
        const double t = (double)i / sample_rate_hz;
        const double truth_now = sinusoid(t, amplitude, frequency_hz);
        const double truth_future = sinusoid(
            t + PREDICTION_HORIZON_S, amplitude, frequency_hz);

        Raw_Sample sample;
        memset(&sample, 0, sizeof(sample));
        sample.seq = i;
        sample.t_ns = TIMESTAMP_BASE_NS + (int64_t)llround(t * 1.0e9);
        sample.flags = 0;

        /* Put the synthetic waveform into all six estimator inputs. */
        for (int a = 0; a < AXES; ++a) {
            const float measurement = (float)(truth_now +
                (noise_sigma > 0.0 ? noise_sigma * gaussian_noise() : 0.0));
            if (a < 3)
                sample.accel[a] = measurement;
            else
                sample.gyro[a - 3] = measurement;
        }

        kalman_update(&kalman, &sample);

        for (int a = 0; a < AXES; ++a) {
            const Kalman_Axis axis = (Kalman_Axis)a;
            const Kalman_Estimate estimate = kalman_get(&kalman, axis);
            const Kalman_Estimate prediction = kalman_predict(
                &kalman, axis, (float)PREDICTION_HORIZON_S);
            const int valid = estimate.valid && prediction.valid;

            const double input = a < 3 ? sample.accel[a] : sample.gyro[a - 3];
            const double current = valid ? estimate.current_tremor : NAN;
            const double predicted = valid ? prediction.predicted_tremor : NAN;
            const double current_error = valid ? current - truth_now : NAN;
            const double prediction_error = valid ? predicted - truth_future : NAN;

            fprintf(out,
                "%llu,%.9f,%s,%.9g,%.9g,%.9g,%.6f,%.6f,%d,%.9g,%.9g,%.9g,%.9g\n",
                (unsigned long long)i, t, axis_names[a], input, truth_now,
                truth_future, estimate.frequency_hz, estimate.confidence,
                valid, current, predicted, current_error, prediction_error);

            if (valid && t >= METRIC_WARMUP_S) {
                sum_abs_current[a] += fabs(current_error);
                sum_sq_current[a] += current_error * current_error;
                sum_abs_pred[a] += fabs(prediction_error);
                sum_sq_pred[a] += prediction_error * prediction_error;
                sum_abs_frequency[a] += fabs(estimate.frequency_hz - frequency_hz);
                ++metric_count[a];
            }
        }
    }

    if (fclose(out) != 0) {
        fprintf(stderr, "Error closing %s: %s\n", output_path, strerror(errno));
        return 1;
    }

    printf("Synthetic sinusoid test complete\n");
    printf("  Truth:       %.3f Hz, amplitude %.3f\n", frequency_hz, amplitude);
    printf("  Sampling:    %.3f Hz for %.3f s (%llu samples)\n",
        sample_rate_hz, duration_s, (unsigned long long)sample_count);
    printf("  Noise sigma: %.6g\n", noise_sigma);
    printf("  Output CSV:  %s\n", output_path);
    printf("  Metrics exclude first %.1f s and use exact continuous-time truth at +10 ms.\n",
        METRIC_WARMUP_S);
    printf("\n%-8s %8s %12s %12s %12s %12s\n",
        "axis", "n", "freq_MAE", "current_MAE", "pred10_MAE", "pred10_RMSE");

    for (int a = 0; a < AXES; ++a) {
        const size_t n = metric_count[a];
        if (n == 0) {
            printf("%-8s %8d %12s %12s %12s %12s\n",
                axis_names[a], 0, "n/a", "n/a", "n/a", "n/a");
            continue;
        }
        printf("%-8s %8zu %12.6g %12.6g %12.6g %12.6g\n",
            axis_names[a], n,
            sum_abs_frequency[a] / (double)n,
            sum_abs_current[a] / (double)n,
            sum_abs_pred[a] / (double)n,
            sqrt(sum_sq_pred[a] / (double)n));
    }

    return 0;
}
