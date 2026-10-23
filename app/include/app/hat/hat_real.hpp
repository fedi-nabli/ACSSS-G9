/* ===================================================================================================
 * app/src/hat/hat_real.hpp
 *
 * Hat_Real: the Sense HAT's LSM9DS1 through RTIMULib. Private to the hat
 * module (MTS_WITH_RTIMU builds only), reached through hat_open().
 *
 * RTIMULib is used as a driver only: it reads the registers, scales to
 * g / rad/s / uT, puts the three sensors in one frame, and applies the
 * calibration stored in its .ini (gyro bias, compass min/max + ellipsoid,
 * accel). Its own fusion is switched off: core/fusion does that.
 *
 * Rate: the LSM9DS1 has no 100 Hz. hat_open's rate is rounded UP to the
 * next rate the chip has (14.9, 59.5, 119, 238, 476, 952 Hz), written into
 * RTIMULib's settings before the IMU starts, and reported by info(): the
 * whole pipeline (filters, horizon in samples, status) runs at the rate
 * the samples really arrive at.
 *
 * Authors: Chad Davis, Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_HAT_HAT_REAL_HPP
#define MTS_APP_HAT_HAT_REAL_HPP

#include <app/hat/hat.hpp>

#include <RTIMULib.h>

#include <cstdint>
#include <memory>

namespace mts
{

class Hat_Real final : public Hat
{
public:
  // Takes ownership of both. rate_hz: the LSM9DS1 rate actually configured
  Hat_Real(const Hat_Config& cfg, RTIMUSettings* settings, RTIMU* imu, double rate_hz);
  ~Hat_Real() override;

  Hat_Status read_sample(Raw_Sample& out) override;
  void show(const Led_Command& cmd) override;
  Hat_Info info() const override;

private:
  Hat_Config cfg_;
  RTIMUSettings* settings_;
  RTIMU* imu_;
  double rate_hz_; // the chip's rate, not the requested one
  int64_t period_ns_;
  int64_t opened_t_ns_; // the protocol's timestamp origin
  uint64_t seq_;
  int64_t last_t_ns_; // 0 before the first sample
  float last_mag_[3];
  Led_Command led_;
};

std::unique_ptr<Hat> hat_real_open(const Hat_Config& cfg);

} // namespace mts

#endif /* MTS_APP_HAT_HAT_REAL_HPP */
