#include <app/hat/hat_real.hpp>

#include <utils/time/time.h>
#include <utils/logger/logger.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <chrono>
#include <thread>

namespace mts
{

Hat_Real::Hat_Real(const Hat_Config& cfg, RTIMU* imu)
  : cfg_(cfg)
  , imu_(imu)
  , seq_(0)
  , last_t_ns_(0)
  , measured_rate_hz_(0.0)
  , last_mag_{0.0f, 0.0f, 0.0f}
  , led_{}
{
}

Hat_Real::~Hat_Real()
{
  delete imu_;
}


Hat_Status Hat_Real::read_sample(Raw_Sample& out)
{
  if (imu_ == nullptr)
    return Hat_Status::error;

  // RTIMULib may need several polls before a fresh sample is ready.
  // Do not treat a temporarily unavailable sample as a hardware error.
  while (!imu_->IMURead())
  {
    // Avoid busy-spinning while waiting for the next sensor update.
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  const RTIMU_DATA data = imu_->getIMUData();
  const int64_t now_ns = mts_time_now_ns();

  Raw_Sample s{};
  s.seq = seq_;
  s.t_ns = now_ns;

  // Estimate the actual sampling rate from monotonic timestamps.
  if (last_t_ns_ != 0 && now_ns > last_t_ns_)
  {
    const double dt = static_cast<double>(now_ns - last_t_ns_) * 1e-9;
    const double instantaneous_rate = 1.0 / dt;

    if (std::isfinite(instantaneous_rate))
    {
      measured_rate_hz_ = measured_rate_hz_ == 0.0
          ? instantaneous_rate
          : 0.9 * measured_rate_hz_ + 0.1 * instantaneous_rate;
    }
  }

  if (last_t_ns_ != 0)
  {
    const double expected_ns = 1e9 / cfg_.sample_rate_hz;
    const double elapsed_ns = static_cast<double>(now_ns - last_t_ns_);

    if (elapsed_ns > 1.5 * expected_ns)
      s.flags |= SAMPLE_GAP;
  }

  // RTIMULib gyro: rad/s.
  s.gyro[0] = static_cast<float>(data.gyro.x());
  s.gyro[1] = static_cast<float>(data.gyro.y());
  s.gyro[2] = static_cast<float>(data.gyro.z());

  // RTIMULib accelerometer: g. Raw_Sample requires m/s^2.
  s.accel[0] = static_cast<float>(data.accel.x() * STANDARD_GRAVITY_MS2);
  s.accel[1] = static_cast<float>(data.accel.y() * STANDARD_GRAVITY_MS2);
  s.accel[2] = static_cast<float>(data.accel.z() * STANDARD_GRAVITY_MS2);

  // Retain the previous magnetometer reading if no valid compass
  // measurement is available for this sample.
  if (data.compassValid)
  {
    last_mag_[0] = static_cast<float>(data.compass.x());
    last_mag_[1] = static_cast<float>(data.compass.y());
    last_mag_[2] = static_cast<float>(data.compass.z());
    s.flags |= SAMPLE_MAG_FRESH;
  }

  for (int axis = 0; axis < 3; ++axis)
    s.mag[axis] = last_mag_[axis];

  const double accel_limit =
      cfg_.accel_range_g * STANDARD_GRAVITY_MS2;
  const double gyro_limit =
      cfg_.gyro_range_dps * DEG_TO_RAD;

  for (int axis = 0; axis < 3; ++axis)
  {
    if (std::abs(s.accel[axis]) >= 0.99 * accel_limit ||
        std::abs(s.gyro[axis]) >= 0.99 * gyro_limit)
    {
      s.flags |= SAMPLE_SATURATED;
    }
  }

  // Commit state only after the sample has been obtained and populated.
  last_t_ns_ = now_ns;
  ++seq_;
  out = s;

  return Hat_Status::ok;
}


void Hat_Real::show(const Led_Command& cmd)
{
  // LED output can be integrated separately. Do not let an unimplemented
  // display path interfere with sensor acquisition.
  led_ = cmd;
}

Hat_Info Hat_Real::info() const
{
  const double accel_range =
      cfg_.accel_range_g * STANDARD_GRAVITY_MS2;
  const double gyro_range =
      cfg_.gyro_range_dps * DEG_TO_RAD;

  return {
    false,
    measured_rate_hz_ > 0.0 ? measured_rate_hz_ : cfg_.sample_rate_hz,
    accel_range,
    gyro_range
  };
}

std::unique_ptr<Hat> hat_real_open(const Hat_Config& cfg)
{
  RTIMUSettings* settings = new RTIMUSettings("/etc/RTIMULib");
  RTIMU* imu = RTIMU::createIMU(settings);

  if (imu == nullptr)
  {
    MTS_LOG_ERROR("hat_real_open: RTIMULib could not create an IMU");
    delete settings;
    return nullptr;
  }

  if (!imu->IMUInit())
  {
    MTS_LOG_ERROR("hat_real_open: IMU initialization failed");
    delete imu;
    return nullptr;
  }

  MTS_LOG_INFO("hat_real_open: real Sense HAT initialized");

  return std::make_unique<Hat_Real>(cfg, imu);
}

} // namespace mts