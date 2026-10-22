
/*
 * Hardware integration test for Hat_Real.
 *
 * Run only on a machine with a connected Sense HAT and RTIMULib installed.
 */

#include <mts_test.h>
#include <app/hat/hat.hpp>

#include <cmath>
#include <cstdio>
#include <chrono>

namespace
{

constexpr unsigned SAMPLE_COUNT = 20;

} // namespace

MTS_TEST(real_hat_opens_and_reports_hardware)
{
  mts::Hat_Config cfg;
  cfg.mode = mts::Hat_Mode::real;
  cfg.sample_rate_hz = 100.0;

  auto hat = mts::hat_open(cfg);
  MTS_ASSERT(hat != nullptr);

  const mts::Hat_Info info = hat->info();

  MTS_ASSERT(!info.is_sim);
  MTS_ASSERT(std::isfinite(info.rate_hz));
  MTS_ASSERT(info.rate_hz > 0.0);
  MTS_ASSERT(std::isfinite(info.accel_range_ms2));
  MTS_ASSERT(info.accel_range_ms2 > 0.0);
  MTS_ASSERT(std::isfinite(info.gyro_range_rads));
  MTS_ASSERT(info.gyro_range_rads > 0.0);
}

MTS_TEST(real_hat_produces_valid_samples)
{
  mts::Hat_Config cfg;
  cfg.mode = mts::Hat_Mode::real;
  cfg.sample_rate_hz = 100.0;

  auto hat = mts::hat_open(cfg);
  MTS_ASSERT(hat != nullptr);

  uint64_t previous_seq = 0;
  int64_t previous_t_ns = 0;

  for (unsigned i = 0; i < SAMPLE_COUNT; ++i)
  {
    Raw_Sample sample{};

    MTS_ASSERT(hat->read_sample(sample) == mts::Hat_Status::ok);

    if (i == 0)
    {
      MTS_ASSERT_EQ(sample.seq, 0);
    }
    else
    {
      MTS_ASSERT_EQ(sample.seq, previous_seq + 1);
      MTS_ASSERT(sample.t_ns > previous_t_ns);
    }

    for (unsigned axis = 0; axis < 3; ++axis)
    {
      MTS_ASSERT(std::isfinite(sample.accel[axis]));
      MTS_ASSERT(std::isfinite(sample.gyro[axis]));
      MTS_ASSERT(std::isfinite(sample.mag[axis]));
    }

    previous_seq = sample.seq;
    previous_t_ns = sample.t_ns;

    if (i == 0 || i == SAMPLE_COUNT - 1)
    {
      std::printf(
        "sample=%llu t_ns=%lld "
        "accel=[%.3f, %.3f, %.3f] "
        "gyro=[%.4f, %.4f, %.4f] "
        "mag=[%.2f, %.2f, %.2f] flags=0x%x\n",
        static_cast<unsigned long long>(sample.seq),
        static_cast<long long>(sample.t_ns),
        sample.accel[0], sample.accel[1], sample.accel[2],
        sample.gyro[0], sample.gyro[1], sample.gyro[2],
        sample.mag[0], sample.mag[1], sample.mag[2],
        sample.flags);
    }
  }
}

MTS_TEST(real_hat_exceeds_200_samples_per_second)
{
  mts::Hat_Config cfg;
  cfg.mode = mts::Hat_Mode::real;
  cfg.sample_rate_hz = 230.0;

  auto hat = mts::hat_open(cfg);
  MTS_ASSERT(hat != nullptr);

  using clock = std::chrono::steady_clock;

  // Start timing immediately before collecting samples.
  const auto start = clock::now();
  const auto deadline = start + std::chrono::seconds(2);

  unsigned sample_count = 0;

  while (clock::now() < deadline)
  {
    Raw_Sample sample{};
    const auto status = hat->read_sample(sample);

    MTS_ASSERT(status == mts::Hat_Status::ok);
    ++sample_count;
  }

  const double elapsed_seconds =
      std::chrono::duration<double>(clock::now() - start).count();

  const double samples_per_second =
      static_cast<double>(sample_count) / elapsed_seconds;

  std::printf(
      "Collected %u samples in %.3f seconds: %.2f samples/s\n",
      sample_count,
      elapsed_seconds,
      samples_per_second);

  MTS_ASSERT(samples_per_second > 200.0);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(real_hat_opens_and_reports_hardware),
  MTS_TEST_ENTRY(real_hat_produces_valid_samples),
  MTS_TEST_ENTRY(real_hat_exceeds_200_samples_per_second),
};

MTS_TEST_MAIN(tests)
