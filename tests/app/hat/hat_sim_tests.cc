/* ===================================================================================================
 * tests/app/hat/hat_sim_tests.cc
 *
 * Tests for hat_open() and the simulated hat, through the public interface
 * only (Hat_Sim itself is private to the module).
 *
 * Statistical tolerances are ~5 standard errors: they fail on a real bug,
 * not by chance. The seed is fixed, so a pass is a pass every time anyway.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/hat/hat.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <memory>
#include <vector>

namespace
{

constexpr double TWO_PI = 2.0 * 3.14159265358979323846;

// Unpaced sim config: tests never wait
mts::Hat_Config fast_config()
{
  mts::Hat_Config cfg;
  cfg.sim.paced = false;
  return cfg;
}

// Opens with the logger silenced: for configs that are expected to fail
std::unique_ptr<mts::Hat> open_quietly(const mts::Hat_Config& cfg)
{
  FILE* sink = std::tmpfile();
  mts_log_set_stream(sink);
  std::unique_ptr<mts::Hat> hat = mts::hat_open(cfg);
  mts_log_set_stream(nullptr);
  std::fclose(sink);
  return hat;
}

std::vector<Raw_Sample> read_n(mts::Hat& hat, size_t n)
{
  std::vector<Raw_Sample> samples(n);
  for (Raw_Sample& s : samples)
  {
    if (hat.read_sample(s) != mts::Hat_Status::ok)
    {
      samples.clear();
      break;
    }
  }
  return samples;
}

} // namespace

/* ---------------------------------------------------------------------------------------------------
 * hat_open
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(open_rejects_bad_rate)
{
  const double bad[] = { 0.0, -1.0, NAN, mts::HAT_MAX_RATE_HZ + 1.0 };
  for (double rate : bad)
  {
    mts::Hat_Config cfg = fast_config();
    cfg.sample_rate_hz = rate;
    MTS_ASSERT(open_quietly(cfg) == nullptr);
  }
}

MTS_TEST(open_rejects_bad_ranges)
{
  mts::Hat_Config cfg = fast_config();
  cfg.accel_range_g = 6;
  MTS_ASSERT(open_quietly(cfg) == nullptr);

  cfg = fast_config();
  cfg.gyro_range_dps = 250;
  MTS_ASSERT(open_quietly(cfg) == nullptr);
}

#if defined(MTS_TEST_NO_RTIMU)
MTS_TEST(open_real_without_rtimu_fails)
{
  mts::Hat_Config cfg = fast_config();
  cfg.mode = mts::Hat_Mode::real;
  MTS_ASSERT(open_quietly(cfg) == nullptr);
}
#endif

MTS_TEST(open_sim_reports_info)
{
  mts::Hat_Config cfg = fast_config();
  cfg.sample_rate_hz = 200.0;
  cfg.accel_range_g = 8;
  cfg.gyro_range_dps = 2000;

  std::unique_ptr<mts::Hat> hat = open_quietly(cfg);
  MTS_ASSERT(hat != nullptr);

  mts::Hat_Info info = hat->info();
  MTS_ASSERT(info.is_sim);
  MTS_ASSERT_NEAR(info.rate_hz, 200.0, 0.0);
  MTS_ASSERT_NEAR(info.accel_range_ms2, 8 * mts::STANDARD_GRAVITY_MS2, 1e-12);
  MTS_ASSERT_NEAR(info.gyro_range_rads, 2000 * mts::DEG_TO_RAD, 1e-12);
}

/* ---------------------------------------------------------------------------------------------------
 * Sequence, timestamps, pacing
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(seq_and_timestamps_exact)
{
  // 333 Hz: the period is not a whole number of ns, so rounding matters
  const double rates[] = { 100.0, 333.0 };
  for (double rate : rates)
  {
    mts::Hat_Config cfg = fast_config();
    cfg.sample_rate_hz = rate;
    std::unique_ptr<mts::Hat> hat = open_quietly(cfg);
    MTS_ASSERT(hat != nullptr);

    std::vector<Raw_Sample> s = read_n(*hat, 1000);
    MTS_ASSERT_EQ(s.size(), 1000);

    for (size_t n = 0; n < s.size(); n++)
    {
      MTS_ASSERT_EQ(s[n].seq, n);
      // Virtual time, computed from n: exact, no drift
      MTS_ASSERT_EQ(s[n].t_ns - s[0].t_ns, std::llround(static_cast<double>(n) * 1e9 / rate));
    }
  }
}

MTS_TEST(first_timestamp_is_now)
{
  int64_t before = mts_time_now_ns();
  std::unique_ptr<mts::Hat> hat = open_quietly(fast_config());
  MTS_ASSERT(hat != nullptr);

  Raw_Sample s{};
  MTS_ASSERT(hat->read_sample(s) == mts::Hat_Status::ok);
  MTS_ASSERT_GE(s.t_ns, before);
  MTS_ASSERT_LT(s.t_ns - before, MTS_TIME_NS_PER_S);
  // The protocol's origin: sample 0 is stamped exactly at the opening
  MTS_ASSERT_EQ(hat->info().opened_t_ns, s.t_ns);
}

MTS_TEST(paced_waits_for_each_sample)
{
  mts::Hat_Config cfg;
  cfg.sample_rate_hz = 1000.0;
  std::unique_ptr<mts::Hat> hat = open_quietly(cfg);
  MTS_ASSERT(hat != nullptr);

  int64_t start = mts_time_now_ns();
  std::vector<Raw_Sample> s = read_n(*hat, 50);
  int64_t elapsed = mts_time_now_ns() - start;

  MTS_ASSERT_EQ(s.size(), 50);
  // Sample 49 is due 49 ms after sample 0: lower bound only
  MTS_ASSERT_GE(elapsed, 49 * MTS_TIME_NS_PER_MS - MTS_TIME_NS_PER_MS);
  // And no sample comes back before its timestamp
  MTS_ASSERT_GE(mts_time_now_ns(), s.back().t_ns);
}

MTS_TEST(unpaced_never_waits)
{
  std::unique_ptr<mts::Hat> hat = open_quietly(fast_config());
  MTS_ASSERT(hat != nullptr);

  int64_t start = mts_time_now_ns();
  std::vector<Raw_Sample> s = read_n(*hat, 100000); // 1000 s of data at 100 Hz
  int64_t elapsed = mts_time_now_ns() - start;

  MTS_ASSERT_EQ(s.size(), 100000);
  MTS_ASSERT_LT(elapsed, 5 * MTS_TIME_NS_PER_S);
}

/* ---------------------------------------------------------------------------------------------------
 * The data itself
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(statistics_match_config)
{
  mts::Hat_Config cfg = fast_config();
  const mts::Hat_Sim_Params& p = cfg.sim;
  std::unique_ptr<mts::Hat> hat = open_quietly(cfg);
  MTS_ASSERT(hat != nullptr);

  const size_t n = 100000;
  std::vector<Raw_Sample> s = read_n(*hat, n);
  MTS_ASSERT_EQ(s.size(), n);

  double accel_z = 0.0;
  double accel_y = 0.0;
  double accel_y2 = 0.0;
  double gyro[3] = { 0.0, 0.0, 0.0 };
  double mag[3] = { 0.0, 0.0, 0.0 };
  size_t mag_n = 0;

  for (const Raw_Sample& x : s)
  {
    accel_z += x.accel[2];
    accel_y += x.accel[1];
    accel_y2 += static_cast<double>(x.accel[1]) * x.accel[1];
    for (int a = 0; a < 3; a++)
    {
      gyro[a] += x.gyro[a];
    }
    if (x.flags & SAMPLE_MAG_FRESH)
    {
      for (int a = 0; a < 3; a++)
      {
        mag[a] += x.mag[a];
      }
      mag_n++;
    }
  }

  const double dn = static_cast<double>(n);
  const double root_n = std::sqrt(dn);

  // Gravity on z, nothing but noise on y (x carries the tremor)
  MTS_ASSERT_NEAR(accel_z / dn, mts::STANDARD_GRAVITY_MS2, 5 * p.accel_noise_ms2 / root_n + 1e-6);
  double mean_y = accel_y / dn;
  MTS_ASSERT_NEAR(mean_y, 0.0, 5 * p.accel_noise_ms2 / root_n);
  double std_y = std::sqrt(accel_y2 / dn - mean_y * mean_y);
  MTS_ASSERT_NEAR(std_y, p.accel_noise_ms2, 5 * p.accel_noise_ms2 / std::sqrt(2 * dn));

  // Constant bias on every gyro axis
  for (int a = 0; a < 3; a++)
  {
    MTS_ASSERT_NEAR(gyro[a] / dn, p.gyro_bias_rads, 5 * p.gyro_noise_rads / root_n + 1e-7);
  }

  // Earth field on fresh mag samples
  const double field[3] = { 16.0, 0.0, -48.0 };
  const double root_mag = std::sqrt(static_cast<double>(mag_n));
  for (int a = 0; a < 3; a++)
  {
    MTS_ASSERT_NEAR(mag[a] / mag_n, field[a], 5 * p.mag_noise_ut / root_mag + 1e-5);
  }
}

// Amplitude of the component at freq_hz in accel x: one-bin DFT
static double amplitude_at(const std::vector<Raw_Sample>& s, double rate, double freq_hz)
{
  double re = 0.0;
  double im = 0.0;
  for (size_t n = 0; n < s.size(); n++)
  {
    double phase = TWO_PI * freq_hz * static_cast<double>(n) / rate;
    re += s[n].accel[0] * std::cos(phase);
    im -= s[n].accel[0] * std::sin(phase);
  }
  return 2.0 * std::sqrt(re * re + im * im) / static_cast<double>(s.size());
}

MTS_TEST(tremor_is_where_configured)
{
  mts::Hat_Config cfg = fast_config();
  std::unique_ptr<mts::Hat> hat = open_quietly(cfg);
  MTS_ASSERT(hat != nullptr);

  // 10000 samples at 100 Hz = exactly 600 periods of 6 Hz: no leakage
  std::vector<Raw_Sample> s = read_n(*hat, 10000);
  MTS_ASSERT_EQ(s.size(), 10000);

  // Noise floor per bin: ~ sigma * sqrt(2 / N) = 2.8e-4
  MTS_ASSERT_NEAR(amplitude_at(s, cfg.sample_rate_hz, cfg.sim.tremor_hz), cfg.sim.tremor_amp_ms2, 2e-3);
  // Doubles: MTS_ASSERT_LT would compare them as long long (0 < 0)
  MTS_ASSERT(amplitude_at(s, cfg.sample_rate_hz, 9.0) < 2e-3);
}

MTS_TEST(same_seed_same_data)
{
  mts::Hat_Config cfg = fast_config();
  cfg.sim.seed = 42;

  std::unique_ptr<mts::Hat> a = open_quietly(cfg);
  std::unique_ptr<mts::Hat> b = open_quietly(cfg);
  cfg.sim.seed = 43;
  std::unique_ptr<mts::Hat> c = open_quietly(cfg);
  MTS_ASSERT(a != nullptr && b != nullptr && c != nullptr);

  std::vector<Raw_Sample> sa = read_n(*a, 1000);
  std::vector<Raw_Sample> sb = read_n(*b, 1000);
  std::vector<Raw_Sample> sc = read_n(*c, 1000);
  MTS_ASSERT_EQ(sa.size(), 1000);

  // t_ns depends on when each hat was opened: compare everything else
  for (size_t n = 0; n < sa.size(); n++)
  {
    sa[n].t_ns = 0;
    sb[n].t_ns = 0;
    sc[n].t_ns = 0;
  }
  MTS_ASSERT(std::memcmp(sa.data(), sb.data(), sa.size() * sizeof(Raw_Sample)) == 0);
  MTS_ASSERT(std::memcmp(sa.data(), sc.data(), sa.size() * sizeof(Raw_Sample)) != 0);
}

MTS_TEST(mag_updates_at_50_hz)
{
  // 100 Hz: every second sample is fresh, the others repeat it exactly
  std::unique_ptr<mts::Hat> hat = open_quietly(fast_config());
  MTS_ASSERT(hat != nullptr);
  std::vector<Raw_Sample> s = read_n(*hat, 1000);
  MTS_ASSERT_EQ(s.size(), 1000);

  for (size_t n = 0; n < s.size(); n++)
  {
    bool fresh = (s[n].flags & SAMPLE_MAG_FRESH) != 0;
    MTS_ASSERT_EQ(fresh, n % 2 == 0);
    if (!fresh)
    {
      MTS_ASSERT(std::memcmp(s[n].mag, s[n - 1].mag, sizeof(s[n].mag)) == 0);
    }
  }

  // Below 50 Hz every sample is fresh
  mts::Hat_Config slow = fast_config();
  slow.sample_rate_hz = 30.0;
  hat = open_quietly(slow);
  MTS_ASSERT(hat != nullptr);
  s = read_n(*hat, 300);
  for (const Raw_Sample& x : s)
  {
    MTS_ASSERT(x.flags & SAMPLE_MAG_FRESH);
  }
}

MTS_TEST(out_of_range_values_saturate)
{
  // Defaults: 0.5 m/s^2 of tremor against a 4 g range, never saturates
  std::unique_ptr<mts::Hat> hat = open_quietly(fast_config());
  MTS_ASSERT(hat != nullptr);
  for (const Raw_Sample& x : read_n(*hat, 10000))
  {
    MTS_ASSERT_EQ(x.flags & SAMPLE_SATURATED, 0);
  }

  // 100 m/s^2 of tremor against 2 g, and a gyro bias above 245 dps
  mts::Hat_Config cfg = fast_config();
  cfg.accel_range_g = 2;
  cfg.gyro_range_dps = 245;
  cfg.sim.tremor_amp_ms2 = 100.0;
  cfg.sim.gyro_bias_rads = 10.0;
  hat = open_quietly(cfg);
  MTS_ASSERT(hat != nullptr);

  const float accel_limit = static_cast<float>(2 * mts::STANDARD_GRAVITY_MS2);
  const float gyro_limit = static_cast<float>(245 * mts::DEG_TO_RAD);
  size_t saturated = 0;
  for (const Raw_Sample& x : read_n(*hat, 1000))
  {
    MTS_ASSERT(std::fabs(x.accel[0]) <= accel_limit);
    MTS_ASSERT(std::fabs(x.gyro[0]) <= gyro_limit);
    if (x.flags & SAMPLE_SATURATED)
    {
      saturated++;
    }
  }
  // The gyro bias alone saturates every sample
  MTS_ASSERT_EQ(saturated, 1000);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(open_rejects_bad_rate),
  MTS_TEST_ENTRY(open_rejects_bad_ranges),
#if defined(MTS_TEST_NO_RTIMU)
  MTS_TEST_ENTRY(open_real_without_rtimu_fails),
#endif
  MTS_TEST_ENTRY(open_sim_reports_info),
  MTS_TEST_ENTRY(seq_and_timestamps_exact),
  MTS_TEST_ENTRY(first_timestamp_is_now),
  MTS_TEST_ENTRY(paced_waits_for_each_sample),
  MTS_TEST_ENTRY(unpaced_never_waits),
  MTS_TEST_ENTRY(statistics_match_config),
  MTS_TEST_ENTRY(tremor_is_where_configured),
  MTS_TEST_ENTRY(same_seed_same_data),
  MTS_TEST_ENTRY(mag_updates_at_50_hz),
  MTS_TEST_ENTRY(out_of_range_values_saturate),
};

MTS_TEST_MAIN(tests)

