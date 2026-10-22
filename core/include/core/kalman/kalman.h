




#ifndef MTS_CORE_KALMAN_H
#define MTS_CORE_KALMAN_H

#include <stdbool.h>
#include <stdint.h>

#include "core/types/raw_sample.h"

#ifdef __cplusplus
extern "C" {
#endif

#define KALMAN_AXIS_COUNT 3
#define KALMAN_ESTIMATOR_COUNT 6
#define KALMAN_MAX_WINDOW 256

typedef enum {
    KALMAN_ACCEL_X = 0,
    KALMAN_ACCEL_Y,
    KALMAN_ACCEL_Z,
    KALMAN_GYRO_X,
    KALMAN_GYRO_Y,
    KALMAN_GYRO_Z
} Kalman_Axis;

typedef struct {
    float min_frequency_hz;
    float max_frequency_hz;
    float frequency_step_hz;
    float process_noise;
    float measurement_noise;
    float max_sample_gap_s;
    uint16_t window_size;
    uint16_t frequency_update_samples;
} Kalman_Config;

typedef struct {
    float frequency_hz;
    float amplitude;
    float current_tremor;
    float predicted_tremor;
    float confidence;
    bool valid;
} Kalman_Estimate;

/*
 * Fixed-storage implementation. No heap allocation.
 * Treat fields as private to the module.
 */
typedef struct {
    float values[KALMAN_MAX_WINDOW];
    int64_t timestamps_ns[KALMAN_MAX_WINDOW];
    uint16_t head;
    uint16_t count;
    uint16_t samples_since_frequency_update;

    float frequency_hz;
    float x;
    float v;
    float p00, p01, p10, p11;
    float confidence;

    int64_t last_timestamp_ns;
    uint64_t last_seq;
    bool initialized;
    bool frequency_valid;
} Kalman_Axis_State;

typedef struct {
    Kalman_Config config;
    Kalman_Axis_State axes[KALMAN_ESTIMATOR_COUNT];
} Kalman;

Kalman_Config kalman_default_config(void);

bool kalman_init(Kalman *kalman, const Kalman_Config *config);

void kalman_reset(Kalman *kalman);

/* Consumes accel and gyro. Magnetometer data is ignored. */
void kalman_update(Kalman *kalman, const Raw_Sample *sample);

/* Predicts the tremor component horizon_seconds into the future. */
Kalman_Estimate kalman_predict(
    const Kalman *kalman,
    Kalman_Axis axis,
    float horizon_seconds
);

/* Returns the latest state estimate without advancing it. */
Kalman_Estimate kalman_get(
    const Kalman *kalman,
    Kalman_Axis axis
);

#ifdef __cplusplus
}
#endif

#endif
