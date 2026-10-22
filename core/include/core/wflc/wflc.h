/* ===================================================================================================
 * core/include/core/wflc/wflc.h
 *
 * Weighted-frequency Fourier linear combiner (Riviere), as a building
 * block: fits M harmonics of one adaptive frequency to a signal, sample by
 * sample, and forecasts by extrapolating the fit.
 *
 * Per sample s (input units, e.g. m), on the normalized signal sn:
 *
 *   p      = EWMA(s^2)                      running power, bias-corrected
 *   sn     = s / sqrt(p + power_floor)      unit RMS: the step sizes are dimensionless
 *   phi   += w0                             phase, rad, wrapped to [0, 2 pi)
 *   x      = [sin(r phi), cos(r phi)], r = 1..M
 *   e      = sn - sum(w x)                  fit error
 *   w0    += 2 mu_freq e sum_r r (w_r cos(r phi) - w_{M+r} sin(r phi)),  clamped to the band
 *   w     += 2 mu_amp e x
 *
 * Normalizing makes mu_freq and mu_amp mean the same at 0.05 mm and at
 * 2 mm, in m or in mm: unnormalized, the frequency gradient scales with the
 * amplitude squared. The band clamp keeps w0 from wandering when the tremor
 * stops. No bias weight: the input is band-passed, there is no drift to fit.
 *
 * error_ratio = EWMA(e^2) / EWMA(sn^2): near 0 when the fit holds the
 * signal (locked), near 1 when it does not.
 *
 * Plain data, no heap: predictors embed a Wflc by value.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_WFLC_WFLC_H
#define MTS_CORE_WFLC_WFLC_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define WFLC_HARMONICS_MAX 3

typedef struct Wflc_Config
{
  double rate_hz; // sample rate
  uint32_t n_harmonics; // M, 1 .. WFLC_HARMONICS_MAX
  uint32_t pad;
  double mu_freq; // frequency step (dimensionless: the signal is normalized)
  double mu_amp; // weight step (dimensionless)
  double f0_hz; // starting frequency
  double f_min_hz; // band the frequency is clamped to
  double f_max_hz;
  double power_tau_s; // time constant of the running power and error averages
  double power_floor; // added to the power before normalizing (input units^2): silence divides by this
} Wflc_Config;

typedef struct Wflc
{
  uint32_t m; // harmonics in use
  uint32_t pad;
  double w[2 * WFLC_HARMONICS_MAX]; // sin weights [0, m), cos weights [m, 2m), on the normalized signal
  double omega; // frequency, rad/sample
  double phase; // rad, [0, 2 pi)
  double omega0; // starting frequency (reset)
  double omega_min; // band, rad/sample
  double omega_max;
  double mu_freq;
  double mu_amp;
  double alpha; // EWMA weight per sample: 1 / (tau * rate)
  double power_floor;
  double power; // EWMA of s^2, not yet bias-corrected
  double norm_power; // EWMA of sn^2, idem
  double err_power; // EWMA of e^2, idem
  double decay; // (1 - alpha)^updates: the EWMAs' bias correction
  double scale; // sqrt(p + power_floor) at the last update: normalized -> input units
  double rate_hz;
  uint64_t updates;
} Wflc;

/* Defaults for rate_hz: one harmonic, 6 Hz start, 3-14 Hz band, 1 s
 * averages, a 1 nm floor (for an input in m), mu_freq 0.002 and mu_amp 0.05.
 * The steps were swept on a 6 Hz sine (start at 8 Hz, then a step to 8 Hz,
 * with and without 10 % noise): these lock in about 0.7 s, wander about
 * 0.06 Hz in noise, and forecast 30 ms ahead within about 3 % of the
 * amplitude. Larger mu_freq locks faster but wanders more in noise. */
Wflc_Config wflc_config_default(double rate_hz);

/* false (and a logged error): rate not > 0, M not in [1, WFLC_HARMONICS_MAX],
 * a step not > 0, f_min <= 0, f0 outside [f_min, f_max], the top harmonic
 * M * f_max not below Nyquist, tau not > 0, floor < 0, or a value not finite. */
bool wflc_init(Wflc* w, const Wflc_Config* cfg);

// Back to the starting frequency, zero weights, empty averages (same config)
void wflc_reset(Wflc* w);

/* One sample. Returns the fit error in the input's units. A sample that is
 * not finite is ignored (returns 0, nothing changes). */
double wflc_update(Wflc* w, double s);

/* The fit extrapolated 1 .. steps samples ahead (frequency and weights
 * frozen), in input units, into out[0 .. steps-1]. Nothing changes. */
void wflc_project(const Wflc* w, uint32_t steps, double* out);

double wflc_frequency_hz(const Wflc* w);

// EWMA(e^2) / EWMA(sn^2): ~0 locked, ~1 not. 1 before the first sample
double wflc_error_ratio(const Wflc* w);

uint64_t wflc_updates(const Wflc* w);

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_WFLC_WFLC_H */
