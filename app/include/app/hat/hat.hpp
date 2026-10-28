/* ===================================================================================================
 * app/include/app/hat/hat.hpp
 *
 * The Sense HAT behind one interface. Two implementations, both private to
 * the hat module and only reachable through hat_open():
 *  - Hat_Real: RTIMULib (its own fusion off), units and axes normalized to
 *    Raw_Sample conventions, LED through the framebuffer. Only compiled with
 *    MTS_WITH_RTIMU.
 *  - Hat_Sim: random data that looks like the sensor (gravity, noise, a
 *    constant gyro bias, a tremor sinusoid on accel).
 *
 * Sim = real: same API, same units, same pacing. Code above this interface
 * cannot tell them apart except through Hat_Info::is_sim.
 *
 * No start/stop: the sensor measures continuously. "Running" vs "stopped"
 * is a pipeline state in the orchestrator, not a Hat state.
 *
 * Threads: a Hat is used by ONE thread, the Hat_Worker's. hat_open() runs in
 * the owner's thread before that thread starts, the destructor after it is
 * joined. No locking inside.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_HAT_HAT_HPP
#define MTS_APP_HAT_HAT_HPP

#include <app/link/led_command.h>
#include <core/types/raw_sample.h>
#include <core/types/units.h>

#include <cstdint>
#include <memory>

namespace mts
{

// Unit conversions shared by both implementations
constexpr double STANDARD_GRAVITY_MS2 = MTS_STANDARD_GRAVITY_MS2;
constexpr double DEG_TO_RAD = 3.14159265358979323846 / 180.0;

// Highest sample rate hat_open accepts (the LSM9DS1 tops out around 952 Hz)
constexpr double HAT_MAX_RATE_HZ = 1000.0;

enum class Hat_Mode
{
  real,
  sim,
};

enum class Hat_Status
{
  ok, // sample filled. A gap before it is flagged in the sample (SAMPLE_GAP), not here
  error, // no sample: sensor timeout, bus error, ... (logged; Hat_Worker turns it into an Event)
};

/* Hat_Sim only, Hat_Real ignores it. The defaults below are the defaults of
 * the program: the config table reads them from a default-constructed
 * Hat_Config, so there is one source of truth. */
struct Hat_Sim_Params
{
  uint64_t seed = 1; // same seed, same data
  double tremor_hz = 6.0; // frequency of the tremor sinusoid on accel
  double tremor_amp_ms2 = 0.5; // its amplitude
  double tremor_rot_rads = 0.3; // rotational tremor on gyro z, same frequency, peak rad/s (8 mrad of angle at 6 Hz)
  double accel_noise_ms2 = 0.02; // standard deviation, per axis
  double gyro_noise_rads = 0.002;
  double mag_noise_ut = 0.3;
  double gyro_bias_rads = 0.01; // constant offset on every gyro axis: something for calibration to remove
  bool paced = true; // false: read_sample never sleeps (tests run instantly)
};

struct Hat_Config
{
  Hat_Mode mode = Hat_Mode::sim;
  double sample_rate_hz = 100.0; // requested. The real rate is measured, see Hat_Info::rate_hz
  uint32_t accel_range_g = 4; // LSM9DS1 steps: 2, 4, 8, 16
  uint32_t gyro_range_dps = 500; // LSM9DS1 steps: 245, 500, 2000
  Hat_Sim_Params sim;
};

// Ranges in SI for everything downstream (saturation checks, filters, ...)
struct Hat_Info
{
  bool is_sim;
  double rate_hz; // measured actual rate (Hat_Sim: exactly the requested one)
  double accel_range_ms2;
  double gyro_range_rads;
  int64_t opened_t_ns; // when the Hat opened (CLOCK_MONOTONIC): the protocol's timestamp origin
};

class Hat
{
public:
  virtual ~Hat() = default;

  /* Blocks until the next sample is due, then fills out.
   * t_ns is stamped here, once, on CLOCK_MONOTONIC. seq starts at 0 at open.
   * error: no sample (out untouched). */
  virtual Hat_Status read_sample(Raw_Sample& out) = 0;

  /* Draws the command. Called at sample rate by the Hat_Worker: the
   * implementation rate-limits the actual drawing itself (~15 Hz). */
  virtual void show(const Led_Command& cmd) = 0;

  virtual Hat_Info info() const = 0;

protected:
  Hat() = default;
  Hat(const Hat&) = delete;
  Hat& operator=(const Hat&) = delete;
};

inline bool hat_accel_range_supported(uint32_t g)
{
  return g == 2 || g == 4 || g == 8 || g == 16;
}

inline bool hat_gyro_range_supported(uint32_t dps)
{
  return dps == 245 || dps == 500 || dps == 2000;
}

/* Opens the Hat described by cfg, ready to read. nullptr (and a logged
 * error) if:
 *  - cfg is invalid (sample_rate_hz not in (0, HAT_MAX_RATE_HZ], unsupported range)
 *  - mode is real but the build has no RTIMULib (MTS_WITH_RTIMU off)
 *  - the hardware cannot be opened */
std::unique_ptr<Hat> hat_open(const Hat_Config& cfg);

// true in builds with Hat_Real (MTS_WITH_RTIMU, ./build.sh --hat): Hat_Mode::real can be opened
bool hat_real_available();

} // namespace mts

#endif /* MTS_APP_HAT_HAT_HPP */
