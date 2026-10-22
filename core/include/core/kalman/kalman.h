/* ===================================================================================================
 * core/include/core/kalman/kalman.h
 *
 * Linear Kalman filter with a scalar measurement, as a building block:
 *
 *   state:       x[k+1] = F x[k] + w,   w ~ N(0, Q)     (n states, n <= KALMAN_N_MAX)
 *   measurement: z[k]   = H x[k] + v,   v ~ N(0, r)     (one number)
 *
 * One measurement per step is all this program ever has (one axis, one
 * sample), and it makes the innovation variance S a scalar: the gain is
 * K = P H' / S, with no matrix inversion anywhere.
 *
 * Plain data, no heap, fixed maximum size: predictors embed a Kalman by
 * value in their state block (predict.h), and copying one copies the
 * filter. Matrices are row-major, n x n, passed as const double* (a
 * double F[2][2] is passed as &F[0][0]).
 *
 * Numerics: the covariance update is the Joseph form,
 *   P = (I - K H) P (I - K H)' + K r K'
 * which keeps P symmetric positive semi-definite under rounding (the short
 * form P = (I - K H) P does not, over hours of samples), and P is
 * re-symmetrized after every step.
 *
 * Use, per sample: kalman_predict, then kalman_update with the measurement.
 * kalman_project forecasts the measurement steps ahead without touching
 * the filter.
 *
 * Authors: Fedi Nabli, Chad Davis
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_KALMAN_KALMAN_H
#define MTS_CORE_KALMAN_KALMAN_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Covers a 2-state oscillator, two harmonics, or constant-acceleration tracking
#define KALMAN_N_MAX 4

typedef struct Kalman
{
  uint32_t n; // states in use
  uint32_t pad;
  double x[KALMAN_N_MAX]; // state estimate
  double P[KALMAN_N_MAX][KALMAN_N_MAX]; // its covariance
  double F[KALMAN_N_MAX][KALMAN_N_MAX]; // transition
  double Q[KALMAN_N_MAX][KALMAN_N_MAX]; // process noise covariance
  double H[KALMAN_N_MAX]; // measurement row
  double r; // measurement noise variance
  double innovation; // last z - H x before the update (diagnostics, adaptive tuning)
  double innovation_var; // last S = H P H' + r
} Kalman;

/* Sets up an n-state filter. F, Q, P0: n x n row-major. H, x0: n.
 * false (and a logged error): n not in [1, KALMAN_N_MAX], r < 0, or a value
 * not finite. */
bool kalman_init(Kalman* k, uint32_t n, const double* F, const double* H, const double* Q, double r, const double* x0,
                 const double* P0);

// Back to x0, P0 (same model). x0: n, P0: n x n
void kalman_reset(Kalman* k, const double* x0, const double* P0);

// Time update: x = F x, P = F P F' + Q
void kalman_predict(Kalman* k);

/* Measurement update with z. false (nothing changed): S <= 0 or a value not
 * finite, so the filter is never poisoned by one bad sample. */
bool kalman_update(Kalman* k, double z);

/* The measurement expected 1 .. steps ahead, H F^j x for j = 1 .. steps,
 * into out[0 .. steps-1]. The filter is not changed. */
void kalman_project(const Kalman* k, uint32_t steps, double* out);

// Model changes (e.g. a new frequency for an oscillator). n x n row-major
void kalman_set_F(Kalman* k, const double* F);
void kalman_set_Q(Kalman* k, const double* Q);
void kalman_set_r(Kalman* k, double r);

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_KALMAN_KALMAN_H */
