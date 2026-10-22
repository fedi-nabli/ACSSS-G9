/*
 * kalman_replay_test.c
 *
 * Replay normalized Raw_Sample CSV through the actual core/kalman implementation.
 *
 * Expected CSV columns:
 * seq,t_ns,gyro_x,gyro_y,gyro_z,accel_x,accel_y,accel_z,mag_x,mag_y,mag_z,flags
 *
 * Build this file inside the project using the same include paths and link target
 * used by the application. Example (adapt target names/paths to your CMake setup):
 *   cc -std=c11 -Icore/include kalman_replay_test.c <your-built-kalman-library> -lm -o kalman_replay_test
 *
 * Run:
 *   ./kalman_replay_test raw_samples_from_imu.csv replay_predictions.csv
 */
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "core/kalman/kalman.h"

#define MAX_SAMPLES 50000
#define AXES KALMAN_ESTIMATOR_COUNT
#define HORIZONS 3

#define TEST_FREQUENCY_HZ 5.0
#define TEST_AMPLITUDE   1.0
#define TEST_SAMPLE_RATE 230.0
#define TEST_DURATION_S  5.0

static const float horizon_s[HORIZONS] = {0.010f, 0.020f, 0.030f};
static const char *axis_names[AXES] = {
    "accel_x", "accel_y", "accel_z", "gyro_x", "gyro_y", "gyro_z"
};

typedef struct {
    Raw_Sample sample;
    float truth[AXES];
    float detrended[AXES];
} Replay_Row;


#include <inttypes.h>
#include <stdio.h>

/*
 * Print the first valid frequency estimate for each axis, then
 * print again whenever its frequency changes by at least 0.05 Hz.
 */
static void print_frequency_changes(
    const Kalman *kalman,
    const Raw_Sample *sample)
{
    static const char *axis_names[KALMAN_ESTIMATOR_COUNT] = {
        "accel_x",
        "accel_y",
        "accel_z",
        "gyro_x",
        "gyro_y",
        "gyro_z"
    };

    static float previous_frequency[KALMAN_ESTIMATOR_COUNT];
    static bool have_previous[KALMAN_ESTIMATOR_COUNT];

    const float frequency_threshold_hz = 0.05f;
    const float prediction_horizon_s = 0.010f;

    for (unsigned i = 0; i < KALMAN_ESTIMATOR_COUNT; ++i) {
        Kalman_Axis axis = (Kalman_Axis)i;

        Kalman_Estimate estimate = kalman_get(kalman, axis);

        if (!estimate.valid) {
            /* Allow the next valid estimate to be reported. */
            have_previous[i] = false;
            continue;
        }

        bool frequency_changed =
            !have_previous[i] ||
            fabsf(estimate.frequency_hz - previous_frequency[i])
                >= frequency_threshold_hz;

        if (!frequency_changed) {
            continue;
        }

        Kalman_Estimate prediction =
            kalman_predict(kalman, axis, prediction_horizon_s);

        printf(
            "FREQ_CHANGE,"
            "seq=%" PRIu64 ","
            "t_s=%.6f,"
            "axis=%s,"
            "frequency_hz=%.3f,"
            "confidence=%.4f,"
            "current_tremor=%.6f,"
            "predicted_10ms=%.6f\n",
            sample->seq,
            (double)sample->t_ns * 1.0e-9,
            axis_names[i],
            (double)estimate.frequency_hz,
            (double)estimate.confidence,
            (double)estimate.current_tremor,
            (double)prediction.predicted_tremor
        );

        previous_frequency[i] = estimate.frequency_hz;
        have_previous[i] = true;
    }
}

static float axis_value(const Raw_Sample *s, int axis) {
    return axis < 3 ? s->accel[axis] : s->gyro[axis - 3];
}

static int parse_row(char *line, Replay_Row *row) {
    unsigned long long seq;
    long long t_ns;
    float gx, gy, gz, ax, ay, az, mx, my, mz;
    unsigned int flags;
    int n = sscanf(line,
        "%llu,%lld,%f,%f,%f,%f,%f,%f,%f,%f,%f,%u",
        &seq, &t_ns, &gx, &gy, &gz, &ax, &ay, &az, &mx, &my, &mz, &flags);
    if (n != 12) return 0;

    memset(row, 0, sizeof(*row));
    row->sample.seq = (uint64_t)seq;
    row->sample.t_ns = (int64_t)t_ns;
    row->sample.gyro[0] = gx;
    row->sample.gyro[1] = gy;
    row->sample.gyro[2] = gz;
    row->sample.accel[0] = ax;
    row->sample.accel[1] = ay;
    row->sample.accel[2] = az;
    row->sample.mag[0] = mx;
    row->sample.mag[1] = my;
    row->sample.mag[2] = mz;
    row->sample.flags = flags;
    for (int a = 0; a < AXES; ++a) row->truth[a] = axis_value(&row->sample, a);
    return 1;
}

static size_t closest_future_index(const Replay_Row *rows, size_t count,
                                   size_t from, int64_t target_ns) {
    size_t lo = from + 1, hi = count;
    while (lo < hi) {
        size_t mid = lo + (hi - lo) / 2;
        if (rows[mid].sample.t_ns < target_ns) lo = mid + 1;
        else hi = mid;
    }
    if (lo >= count) return count;
    if (lo > from + 1) {
        int64_t after = rows[lo].sample.t_ns - target_ns;
        int64_t before = target_ns - rows[lo - 1].sample.t_ns;
        if (before >= 0 && before <= after) return lo - 1;
    }
    return lo;
}

int main(int argc, char **argv) {
    if (argc < 2 || argc > 3) {
        fprintf(stderr, "Usage: %s input.csv [predictions.csv]\n", argv[0]);
        return 2;
    }
    const char *input_path = argv[1];
    const char *output_path = argc == 3 ? argv[2] : "replay_predictions.csv";

    Replay_Row *rows = calloc(MAX_SAMPLES, sizeof(*rows));
    if (!rows) {
        fprintf(stderr, "Allocation failed\n");
        return 1;
    }

    FILE *in = fopen(input_path, "r");
    if (!in) {
        fprintf(stderr, "Cannot open %s: %s\n", input_path, strerror(errno));
        free(rows);
        return 1;
    }

    char line[1024];
    size_t count = 0;
    while (fgets(line, sizeof(line), in)) {
        if (line[0] == '#' || strstr(line, "seq,t_ns") == line) continue;
        if (strspn(line, " \t\r\n") == strlen(line)) continue;
        if (count >= MAX_SAMPLES) {
            fprintf(stderr, "Input exceeds MAX_SAMPLES=%d; increase the constant\n", MAX_SAMPLES);
            fclose(in); free(rows);
            return 1;
        }
        if (!parse_row(line, &rows[count])) {
            fprintf(stderr, "Could not parse CSV row near sample %zu: %s", count + 1, line);
            fclose(in); free(rows);
            return 1;
        }
        if (count && rows[count].sample.t_ns <= rows[count - 1].sample.t_ns) {
            fprintf(stderr, "Timestamps must be strictly increasing (row %zu)\n", count + 1);
            fclose(in); free(rows);
            return 1;
        }
        ++count;
    }
    fclose(in);

    if (count < 100) {
        fprintf(stderr, "Only %zu samples loaded; expected a substantial recording\n", count);
        free(rows);
        return 1;
    }

    Kalman k;
    Kalman_Config cfg = kalman_default_config();

    typedef struct {
        float pred[KALMAN_ESTIMATOR_COUNT][3];
        float frequency[KALMAN_ESTIMATOR_COUNT];
        float confidence[KALMAN_ESTIMATOR_COUNT];
        unsigned char valid[KALMAN_ESTIMATOR_COUNT][3];
    } Axis_Prediction;

    Axis_Prediction *ap = calloc(count, sizeof(*ap));
    if (!ap) {
        fprintf(stderr, "Allocation failed\n");
        free(rows);
        return 1;
    }

    if (!kalman_init(&k, &cfg)) {
        fprintf(stderr, "kalman_init failed\n");
        free(rows);
        free(ap);
        return 1;
    }

    for (size_t i = 0; i < count; ++i) {
    size_t start = (i + 1 > cfg.window_size)
                 ? i + 1 - cfg.window_size
                 : 0;

    size_t n = i - start + 1;

    for (int a = 0; a < AXES; ++a) {
        double sum = 0.0;

        for (size_t j = start; j <= i; ++j) {
            sum += rows[j].truth[a];
        }

        rows[i].detrended[a] =
            rows[i].truth[a] - (float)(sum / n);
    }
}
    
    for (size_t i = 0; i < count; ++i) {
        kalman_update(&k, &rows[i].sample);

        print_frequency_changes(&k, &rows[i].sample); 
        for (int a = 0; a < AXES; ++a) {
            Kalman_Estimate now = kalman_get(&k, (Kalman_Axis)a);
            ap[i].frequency[a] = now.frequency_hz;
            ap[i].confidence[a] = now.confidence;
            for (int h = 0; h < HORIZONS; ++h) {
                Kalman_Estimate p = kalman_predict(&k, (Kalman_Axis)a, horizon_s[h]);
                ap[i].pred[a][h] = p.predicted_tremor;
                ap[i].valid[a][h] = (unsigned char)(p.valid ? 1 : 0);
            }
        }
    }

    FILE *out = fopen(output_path, "w");
    if (!out) {
        fprintf(stderr, "Cannot create %s: %s\n", output_path, strerror(errno));
        free(rows); free(ap);
        return 1;
    }
    fprintf(out, "seq,t_ns,axis,horizon_ms,frequency_hz,confidence,valid,predicted_tremor,target_index,target_time_ns,target_raw_value,persistence_raw_value,linear_raw_value\n");

    for (size_t i = 0; i < count; ++i) {
        for (int a = 0; a < AXES; ++a) {
            for (int h = 0; h < HORIZONS; ++h) {
                int64_t target_ns = rows[i].sample.t_ns + (int64_t)llround((double)horizon_s[h] * 1e9);
                size_t j = closest_future_index(rows, count, i, target_ns);
                if (j >= count) continue;
                float linear = rows[i].truth[a];
                if (i > 0) {
                    double dt = (double)(rows[i].sample.t_ns - rows[i - 1].sample.t_ns) / 1e9;
                    double future_dt = (double)(rows[j].sample.t_ns - rows[i].sample.t_ns) / 1e9;
                    if (dt > 0.0) {
                        double slope = (rows[i].truth[a] - rows[i - 1].truth[a]) / dt;
                        linear = (float)(rows[i].truth[a] + slope * future_dt);
                    }
                }
                fprintf(out, "%llu,%lld,%s,%.0f,%.6f,%.6f,%u,%.9g,%zu,%lld,%.9g,%.9g,%.9g\n",
                    (unsigned long long)rows[i].sample.seq,
                    (long long)rows[i].sample.t_ns, axis_names[a],
                    horizon_s[h] * 1000.0f,
                    ap[i].frequency[a], ap[i].confidence[a],
                    (unsigned)ap[i].valid[a][h], ap[i].pred[a][h],
                    j, (long long)rows[j].sample.t_ns, rows[j].truth[a],
                    rows[i].truth[a], linear);
            }
        }
    }
    fclose(out);

    printf("Loaded %zu samples (%.3f s). Wrote predictions to %s\n",
        count, (rows[count - 1].sample.t_ns - rows[0].sample.t_ns) / 1e9,
        output_path);
    printf("Metrics use each prediction's nearest recorded sample at +10/+20/+30 ms.\n");
    printf("Note: predicted_tremor is compared with the future raw sensor value only as a diagnostic;\n");
    printf("the quantities differ if the estimator removes DC/gravity. Inspect confidence and frequency too.\n");

    /* Metrics: compare tremor prediction against future raw and a linear raw baseline. */
    for (int a = 0; a < AXES; ++a) {
        for (int h = 0; h < HORIZONS; ++h) {
            double se_k = 0, ae_k = 0, se_hold = 0, ae_hold = 0, se_lin = 0, ae_lin = 0;
            size_t n = 0;
            for (size_t i = 1; i + 1 < count; ++i) {
                int64_t target_ns = rows[i].sample.t_ns + (int64_t)llround((double)horizon_s[h] * 1e9);
                size_t j = closest_future_index(rows, count, i, target_ns);
                if (j >= count || !ap[i].valid[a][h]) continue;
                double truth = rows[j].detrended[a];
                double kp = ap[i].pred[a][h];
                double hold = rows[i].detrended[a];
                double dt =
                (double)(rows[i].sample.t_ns -
                        rows[i - 1].sample.t_ns) / 1e9;

                double fdt =
                    (double)(rows[j].sample.t_ns -
                            rows[i].sample.t_ns) / 1e9;

                double lin = hold;

                if (dt > 0.0) {
                    lin += ((rows[i].detrended[a] -
                            rows[i - 1].detrended[a]) / dt) * fdt;
                }
                double ek = kp - truth, eh = hold - truth, el = lin - truth;
                ae_k += fabs(ek); se_k += ek * ek;
                ae_hold += fabs(eh); se_hold += eh * eh;
                ae_lin += fabs(el); se_lin += el * el;
                ++n;
            }
            if (n) {
                printf("%-8s +%2d ms | n=%zu | Kalman MAE %.6g RMSE %.6g | hold MAE %.6g RMSE %.6g | linear MAE %.6g RMSE %.6g\n",
                    axis_names[a], (int)(horizon_s[h] * 1000), n,
                    ae_k/n, sqrt(se_k/n), ae_hold/n, sqrt(se_hold/n), ae_lin/n, sqrt(se_lin/n));
            } else {
                printf("%-8s +%2d ms | no valid predictions\n", axis_names[a], (int)(horizon_s[h] * 1000));
            }
        }
    }

    free(rows);
    free(ap);
    return 0;
}
