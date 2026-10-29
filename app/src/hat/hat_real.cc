/* ===================================================================================================
 * app/src/hat/hat_real.cc
 *
 * Hat_Real, see hat_real.hpp. What changed from the first version, and why:
 *  - the rate and ranges are written into RTIMULib's settings before the
 *    IMU starts (they came from /etc/RTIMULib.ini before, so 100 Hz was
 *    never what the chip ran at, and the gap check fired on every sample)
 *  - info() reports that real rate, and opened_t_ns (was missing: the
 *    protocol's timestamps were uptime instead of time since open)
 *  - RTIMULib's fusion is off (RTFUSION_TYPE_NULL): core/fusion does it
 *  - one sample per period: RTIMULib's IMURead also says "new data" when
 *    only accel or only gyro is new, which doubled the rate (see read_sample)
 *  - read_sample gives up after 10 sample periods without data and returns
 *    error, so the Hat_Worker can report hat_error and stop() can join
 *    (it waited forever before)
 *  - mag freshness from the data itself: the LSM9DS1 driver never sets
 *    compassValid (it is uninitialized in RTIMULib), and the mag runs at its
 *    own 80 Hz, so a value that changed is a fresh one
 *  - the settings object is owned and freed (it leaked)
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <app/hat/hat_real.hpp>

#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <cmath>
#include <cstring>

namespace mts
{

namespace
{

// Give up on a read after this many periods without new data
constexpr int READ_TIMEOUT_PERIODS = 10;

// Log the measured rate once, after this many samples
constexpr int RATE_CHECK_SAMPLES = 1190;

struct Lsm9ds1_Rate
{
  double hz;
  int gyro_code;
  int accel_code;
};

// The rates the chip has. With the gyro on, the accel runs at the gyro's rate
constexpr Lsm9ds1_Rate RATES[] = {
  { 14.9, LSM9DS1_GYRO_SAMPLERATE_14_9, LSM9DS1_ACCEL_SAMPLERATE_14_9 },
  { 59.5, LSM9DS1_GYRO_SAMPLERATE_59_5, LSM9DS1_ACCEL_SAMPLERATE_59_5 },
  { 119.0, LSM9DS1_GYRO_SAMPLERATE_119, LSM9DS1_ACCEL_SAMPLERATE_119 },
  { 238.0, LSM9DS1_GYRO_SAMPLERATE_238, LSM9DS1_ACCEL_SAMPLERATE_238 },
  { 476.0, LSM9DS1_GYRO_SAMPLERATE_476, LSM9DS1_ACCEL_SAMPLERATE_476 },
  { 952.0, LSM9DS1_GYRO_SAMPLERATE_952, LSM9DS1_ACCEL_SAMPLERATE_952 },
};

// The first rate at or above the requested one (hat_open already capped it at 1000 Hz)
const Lsm9ds1_Rate& rate_for(double requested_hz)
{
  for (const Lsm9ds1_Rate& r : RATES)
  {
    if (r.hz >= requested_hz)
    {
      return r;
    }
  }
  return RATES[sizeof(RATES) / sizeof(RATES[0]) - 1];
}

int accel_fsr_code(uint32_t g)
{
  switch (g)
  {
    case 2:
      return LSM9DS1_ACCEL_FSR_2;
    case 8:
      return LSM9DS1_ACCEL_FSR_8;
    case 16:
      return LSM9DS1_ACCEL_FSR_16;
    default:
      return LSM9DS1_ACCEL_FSR_4;
  }
}

// RTIMULib calls the 245 dps range 250
int gyro_fsr_code(uint32_t dps)
{
  switch (dps)
  {
    case 245:
      return LSM9DS1_GYRO_FSR_250;
    case 2000:
      return LSM9DS1_GYRO_FSR_2000;
    default:
      return LSM9DS1_GYRO_FSR_500;
  }
}

} // namespace

Hat_Real::Hat_Real(const Hat_Config& cfg, RTIMUSettings* settings, RTIMU* imu, double rate_hz)
  : cfg_(cfg)
  , settings_(settings)
  , imu_(imu)
  , rate_hz_(rate_hz)
  , period_ns_(std::llround(1e9 / rate_hz))
  , opened_t_ns_(mts_time_now_ns())
  , seq_(0)
  , last_t_ns_(0)
  , first_t_ns_(0)
  , last_mag_{ 0.0f, 0.0f, 0.0f }
  , led_{}
{
}

Hat_Real::~Hat_Real()
{
  delete imu_;
  delete settings_; // RTIMU does not own it
}

Hat_Status Hat_Real::read_sample(Raw_Sample& out)
{
  /* RTIMULib reports "no new data yet" as false: poll, but not forever.
   * It also reports true when only ONE of accel and gyro has new data
   * (status & 0x3): their flags rise a moment apart, so a polling reader
   * sees each period twice, at ~2x the rate (measured: 4.6 ms steps at
   * 119 Hz). A reading less than 3/4 of a period after the last sample is
   * that second half of the same period: skipped, keep polling */
  const int64_t deadline = mts_time_now_ns() + READ_TIMEOUT_PERIODS * period_ns_;
  for (;;)
  {
    if (imu_->IMURead() && (last_t_ns_ == 0 || mts_time_now_ns() - last_t_ns_ >= period_ns_ * 3 / 4))
    {
      break;
    }
    if (mts_time_now_ns() >= deadline)
    {
      MTS_LOG_WARN("hat_real: no data for %d sample periods", READ_TIMEOUT_PERIODS);
      return Hat_Status::error;
    }
    mts_time_sleep_ns(MTS_TIME_NS_PER_MS / 2); // a period is 8.4 ms at 119 Hz
  }

  const RTIMU_DATA data = imu_->getIMUData();
  const int64_t now_ns = mts_time_now_ns();

  Raw_Sample s{};
  s.seq = seq_;
  s.t_ns = now_ns;

  // Lost samples: a step well over one period of the rate the chip really runs at
  if (last_t_ns_ != 0 && now_ns - last_t_ns_ > period_ns_ * 3 / 2)
  {
    s.flags |= SAMPLE_GAP;
  }

  // RTIMULib: gyro rad/s, accel g, compass uT, already in one frame
  s.gyro[0] = static_cast<float>(data.gyro.x());
  s.gyro[1] = static_cast<float>(data.gyro.y());
  s.gyro[2] = static_cast<float>(data.gyro.z());
  s.accel[0] = static_cast<float>(data.accel.x() * STANDARD_GRAVITY_MS2);
  s.accel[1] = static_cast<float>(data.accel.y() * STANDARD_GRAVITY_MS2);
  s.accel[2] = static_cast<float>(data.accel.z() * STANDARD_GRAVITY_MS2);

  // The mag updates at its own rate and the driver re-reads the same value
  // in between: a changed value is a fresh one
  const float mag[3] = { static_cast<float>(data.compass.x()), static_cast<float>(data.compass.y()),
                         static_cast<float>(data.compass.z()) };
  if (seq_ == 0 || std::memcmp(mag, last_mag_, sizeof(mag)) != 0)
  {
    std::memcpy(last_mag_, mag, sizeof(mag));
    s.flags |= SAMPLE_MAG_FRESH;
  }
  for (int a = 0; a < 3; a++)
  {
    s.mag[a] = last_mag_[a];
  }

  const double accel_limit = 0.99 * cfg_.accel_range_g * STANDARD_GRAVITY_MS2;
  const double gyro_limit = 0.99 * cfg_.gyro_range_dps * DEG_TO_RAD;
  for (int a = 0; a < 3; a++)
  {
    if (std::fabs(s.accel[a]) >= accel_limit || std::fabs(s.gyro[a]) >= gyro_limit)
    {
      s.flags |= SAMPLE_SATURATED;
    }
  }

  // Once, after ~10 s: the rate the samples really came at, to check against info()
  if (seq_ == 0)
  {
    first_t_ns_ = now_ns;
  }
  else if (seq_ == RATE_CHECK_SAMPLES)
  {
    MTS_LOG_INFO("hat_real: measured %.1f Hz over %d samples (configured %.1f Hz)",
                 RATE_CHECK_SAMPLES * 1e9 / (double)(now_ns - first_t_ns_), RATE_CHECK_SAMPLES, rate_hz_);
  }

  last_t_ns_ = now_ns;
  seq_++;
  out = s;
  return Hat_Status::ok;
}

void Hat_Real::show(const Led_Command& cmd)
{
  // LED drawing comes later (framebuffer). Never in the way of the sensor
  led_ = cmd;
}

Hat_Info Hat_Real::info() const
{
  return { false, rate_hz_, cfg_.accel_range_g * STANDARD_GRAVITY_MS2, cfg_.gyro_range_dps * DEG_TO_RAD,
           opened_t_ns_ };
}

std::unique_ptr<Hat> hat_real_open(const Hat_Config& cfg)
{
  // /etc/RTIMULib.ini (installed by the sense-hat package) holds the
  // calibration. RTIMULib also tries to write it back: "Failed to open
  // settings file for save" is that, and harmless
  RTIMUSettings* settings = new RTIMUSettings("/etc/RTIMULib");

  const Lsm9ds1_Rate& rate = rate_for(cfg.sample_rate_hz);
  settings->m_fusionType = RTFUSION_TYPE_NULL;
  settings->m_LSM9DS1GyroSampleRate = rate.gyro_code;
  settings->m_LSM9DS1AccelSampleRate = rate.accel_code;
  settings->m_LSM9DS1GyroFsr = gyro_fsr_code(cfg.gyro_range_dps);
  settings->m_LSM9DS1AccelFsr = accel_fsr_code(cfg.accel_range_g);
  settings->m_LSM9DS1CompassSampleRate = 5;

  RTIMU* imu = RTIMU::createIMU(settings);
  if (imu == nullptr || imu->IMUType() == RTIMU_TYPE_NULL)
  {
    MTS_LOG_ERROR("hat_real_open: RTIMULib found no IMU (is the Sense HAT on, and I2C enabled?)");
    delete imu;
    delete settings;
    return nullptr;
  }
  if (!imu->IMUInit())
  {
    MTS_LOG_ERROR("hat_real_open: IMU initialization failed");
    delete imu;
    delete settings;
    return nullptr;
  }

  MTS_LOG_INFO("hat_real_open: Sense HAT at %.1f Hz (asked %g), accel +-%u g, gyro +-%u dps", rate.hz,
               cfg.sample_rate_hz, cfg.accel_range_g, cfg.gyro_range_dps);
  return std::make_unique<Hat_Real>(cfg, settings, imu, rate.hz);
}

} // namespace mts
