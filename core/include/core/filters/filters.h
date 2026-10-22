/* ===================================================================================================
 * core/include/core/filters/filters.h
 *
 * IIR filters as a building block: second-order sections (biquads) and
 * chains of them, with Butterworth low-pass and high-pass designs of any
 * even order. Band-pass = a high-pass and a low-pass in one chain.
 *
 * Design: bilinear transform with the cutoff prewarped (exact -3 dB at
 * fc), section Qs from the Butterworth pole angles:
 *   Q_k = 1 / (2 cos(pi (2k - 1) / (2N))),  k = 1 .. N/2
 * Processing: direct form II transposed, the numerically sound form for
 * double precision.
 *
 * Plain data, no heap: stage 1 modules and predictors embed them by value.
 * filter_chain_response and filter_chain_group_delay_s report what a chain
 * does to a sine, so modules can report their delay and tests can check
 * against it.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_CORE_FILTERS_FILTERS_H
#define MTS_CORE_FILTERS_FILTERS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// A chain holds up to 8 sections: a band-pass of order 8 per edge
#define FILTER_SECTIONS_MAX 8

// Highest Butterworth order one design call accepts (even orders only)
#define FILTER_ORDER_MAX 8

typedef struct Biquad
{
  double b0, b1, b2; // numerator
  double a1, a2; // denominator (a0 normalized to 1)
  double z1, z2; // state
} Biquad;

typedef struct Filter_Chain
{
  uint32_t n; // sections in use
  uint32_t pad;
  Biquad s[FILTER_SECTIONS_MAX];
} Filter_Chain;

/* Single sections. false (and a logged error): fc not in (0, rate/2), q not
 * > 0, or a value not finite. State starts at zero. */
bool biquad_lowpass(Biquad* b, double fc_hz, double q, double rate_hz);
bool biquad_highpass(Biquad* b, double fc_hz, double q, double rate_hz);

double biquad_process(Biquad* b, double x);
void biquad_reset(Biquad* b);

// Gain and phase (rad) at f_hz
void biquad_response(const Biquad* b, double f_hz, double rate_hz, double* gain, double* phase_rad);

// Empty chain: passes the input through
void filter_chain_init(Filter_Chain* c);

/* Appends a Butterworth low-pass / high-pass of an even order 2 ..
 * FILTER_ORDER_MAX. false (logged, chain unchanged): bad order, bad cutoff,
 * or not enough room left in the chain. */
bool filter_chain_add_lowpass(Filter_Chain* c, uint32_t order, double fc_hz, double rate_hz);
bool filter_chain_add_highpass(Filter_Chain* c, uint32_t order, double fc_hz, double rate_hz);

double filter_chain_process(Filter_Chain* c, double x);
void filter_chain_reset(Filter_Chain* c);

// Product of the sections' gains, sum of their phases (rad, not wrapped)
void filter_chain_response(const Filter_Chain* c, double f_hz, double rate_hz, double* gain, double* phase_rad);

// -d(phase)/d(omega) at f_hz, in seconds: how late a narrowband signal at f comes out
double filter_chain_group_delay_s(const Filter_Chain* c, double f_hz, double rate_hz);

#ifdef __cplusplus
}
#endif

#endif /* MTS_CORE_FILTERS_FILTERS_H */
