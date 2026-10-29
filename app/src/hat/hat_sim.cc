/* ===================================================================================================
 * app/src/hat/hat_sim.cc
 *
 * Hat_Sim, see hat_sim.hpp
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include "hat_sim.hpp"

#include <utils/time/time.h>

#include <cmath>

namespace mts
{

namespace
{

constexpr double TWO_PI = 2.0 * 3.14159265358979323846;

// Earth field in the body frame with the board flat: ~50 uT, pointing
// steeply down (inclination ~72 deg at Uppsala). x north, z up
constexpr double MAG_FIELD_UT[3] = { 16.0, 0.0, -48.0 };

// The LSM9DS1 magnetometer updates slower than accel/gyro
constexpr double MAG_RATE_HZ = 50.0;

// Clamps v to [-limit, limit]. true if it had to
bool clamp(double& v, double limit)
{
  if (v > limit)
  {
    v = limit;
    return true;
  }
  if (v < -limit)
  {
    v = -limit;
    return true;
  }
  return false;
}

} // namespace

Hat_Sim::Hat_Sim(const Hat_Config& cfg)
  : cfg_(cfg)
  , rng_(cfg.sim.seed)
  , t0_ns_(mts_time_now_ns())
  , n_(0)
  , mag_{ 0.0f, 0.0f, 0.0f }
  , accel_range_ms2_(cfg.accel_range_g * STANDARD_GRAVITY_MS2)
  , gyro_range_rads_(cfg.gyro_range_dps * DEG_TO_RAD)
  , led_{}
{
}

Hat_Status Hat_Sim::read_sample(Raw_Sample& out)
{
  const uint64_t n = n_++;
  const double rate = cfg_.sample_rate_hz;
  const double t = static_cast<double>(n) / rate;
  const Hat_Sim_Params& p = cfg_.sim;

  Raw_Sample s{};
  s.seq = n;
  // From n every time: rounding never accumulates
  s.t_ns = t0_ns_ + std::llround(static_cast<double>(n) * 1e9 / rate);

  // Fixed draw order (accel x y z, gyro x y z, then mag when fresh): the
  // same seed gives the same data
  double accel[3] = {
    p.tremor_amp_ms2 * std::sin(TWO_PI * p.tremor_hz * t) + p.accel_noise_ms2 * rng_.gaussian(),
    p.accel_noise_ms2 * rng_.gaussian(),
    STANDARD_GRAVITY_MS2 + p.accel_noise_ms2 * rng_.gaussian(),
  };

  double gyro[3];
  for (int a = 0; a < 3; a++)
  {
    gyro[a] = p.gyro_bias_rads + p.gyro_noise_rads * rng_.gaussian();
  }
  // Rotational tremor: the board turning back and forth about z (no draw: the sequence stays the same)
  gyro[2] += p.tremor_rot_rads * std::sin(TWO_PI * p.tremor_hz * t);

  bool saturated = false;
  for (int a = 0; a < 3; a++)
  {
    saturated = clamp(accel[a], accel_range_ms2_) || saturated;
    saturated = clamp(gyro[a], gyro_range_rads_) || saturated;
    s.accel[a] = static_cast<float>(accel[a]);
    s.gyro[a] = static_cast<float>(gyro[a]);
  }

  // Fresh when this sample starts a new 1/50 s mag period (sample 0 always)
  const bool mag_fresh = n == 0 || std::floor(static_cast<double>(n) * MAG_RATE_HZ / rate)
                                     != std::floor(static_cast<double>(n - 1) * MAG_RATE_HZ / rate);
  if (mag_fresh)
  {
    for (int a = 0; a < 3; a++)
    {
      mag_[a] = static_cast<float>(MAG_FIELD_UT[a] + p.mag_noise_ut * rng_.gaussian());
    }
    s.flags |= SAMPLE_MAG_FRESH;
  }
  for (int a = 0; a < 3; a++)
  {
    s.mag[a] = mag_[a];
  }

  if (saturated)
  {
    s.flags |= SAMPLE_SATURATED;
  }

  // Same pacing as the real sensor: the sample is not "there" before t_ns
  if (p.paced)
  {
    mts_time_sleep_until_ns(s.t_ns);
  }

  out = s;
  return Hat_Status::ok;
}

void Hat_Sim::show(const Led_Command& cmd)
{
  led_ = cmd;
}

Hat_Info Hat_Sim::info() const
{
  // Virtual timestamps: the rate is exactly the requested one, and sample 0
  // is stamped exactly at the opening
  return { true, cfg_.sample_rate_hz, accel_range_ms2_, gyro_range_rads_, t0_ns_ };
}

} // namespace mts
