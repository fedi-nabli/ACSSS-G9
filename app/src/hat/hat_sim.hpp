/* ===================================================================================================
 * app/src/hat/hat_sim.hpp
 *
 * Hat_Sim: random data that looks like the Sense HAT (private to the hat
 * module, created only by hat_open()).
 *
 * Per sample n, t = n / rate, board flat and still with a tremor on x:
 *   accel = (tremor_amp * sin(2 pi f t), 0, g) + noise
 *   gyro  = bias + noise
 *   mag   = Earth field (Uppsala-ish) + noise, updated at 50 Hz only:
 *           repeated in between, without SAMPLE_MAG_FRESH, like the real chip
 * Values past the configured range are clamped and flagged SAMPLE_SATURATED.
 *
 * Timestamps are virtual: t_ns = t0 + round(n * 1e9 / rate), computed from
 * n each time (no accumulated rounding). Paced: read_sample sleeps until
 * t_ns. Unpaced: never sleeps (tests run instantly, same data).
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_HAT_HAT_SIM_HPP
#define MTS_APP_HAT_HAT_SIM_HPP

#include "rng.hpp"

#include <app/hat/hat.hpp>

#include <cstdint>

namespace mts
{

class Hat_Sim final : public Hat
{
public:
  // cfg must already be valid (hat_open checks it)
  explicit Hat_Sim(const Hat_Config& cfg);

  Hat_Status read_sample(Raw_Sample& out) override;
  void show(const Led_Command& cmd) override;
  Hat_Info info() const override;

private:
  Hat_Config cfg_;
  Rng rng_;
  int64_t t0_ns_; // t_ns of sample 0
  uint64_t n_; // index of the next sample
  float mag_[3]; // last magnetometer reading, repeated until the next update
  double accel_range_ms2_;
  double gyro_range_rads_;
  Led_Command led_; // last command (drawing comes with led_render)
};

} // namespace mts

#endif /* MTS_APP_HAT_HAT_SIM_HPP */

