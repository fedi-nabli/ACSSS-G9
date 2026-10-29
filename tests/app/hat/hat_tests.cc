/* ===================================================================================================
 * tests/app/hat/hat_tests.cc
 *
 * Tests for the Hat interface itself (no implementation exists yet):
 * the defaults are valid, the range helpers match the LSM9DS1, and the
 * interface can be implemented and used through a pointer to Hat, which is
 * exactly what Hat_Sim and Hat_Real will do.
 *
 * hat_open() is not tested here: it is defined together with Hat_Sim.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/hat/hat.hpp>

#include <cstring>
#include <memory>
#include <type_traits>

static_assert(std::is_abstract_v<mts::Hat>, "Hat must stay an interface");
static_assert(std::has_virtual_destructor_v<mts::Hat>, "deleting through Hat* must call the implementation's destructor");
static_assert(!std::is_copy_constructible_v<mts::Hat>, "a Hat owns hardware: no copies");

MTS_TEST(default_config_is_valid)
{
  mts::Hat_Config cfg;
  MTS_ASSERT(cfg.mode == mts::Hat_Mode::sim);
  MTS_ASSERT(cfg.sample_rate_hz > 0.0);
  MTS_ASSERT(mts::hat_accel_range_supported(cfg.accel_range_g));
  MTS_ASSERT(mts::hat_gyro_range_supported(cfg.gyro_range_dps));
  MTS_ASSERT(cfg.sim.paced);
  MTS_ASSERT_NEAR(cfg.sim.tremor_hz, 6.0, 0.0);
}

MTS_TEST(range_helpers_match_lsm9ds1)
{
  const uint32_t accel_ok[] = { 2, 4, 8, 16 };
  for (uint32_t g : accel_ok)
  {
    MTS_ASSERT(mts::hat_accel_range_supported(g));
  }
  MTS_ASSERT(!mts::hat_accel_range_supported(0));
  MTS_ASSERT(!mts::hat_accel_range_supported(6));
  MTS_ASSERT(!mts::hat_accel_range_supported(32));

  const uint32_t gyro_ok[] = { 245, 500, 2000 };
  for (uint32_t dps : gyro_ok)
  {
    MTS_ASSERT(mts::hat_gyro_range_supported(dps));
  }
  MTS_ASSERT(!mts::hat_gyro_range_supported(250));
  MTS_ASSERT(!mts::hat_gyro_range_supported(1000));
}

MTS_TEST(unit_constants)
{
  MTS_ASSERT_NEAR(4 * mts::STANDARD_GRAVITY_MS2, 39.2266, 1e-9);
  MTS_ASSERT_NEAR(180.0 * mts::DEG_TO_RAD, 3.14159265358979, 1e-12);
}

// The smallest possible implementation: proves the interface is complete
// and usable through std::unique_ptr<Hat>, like hat_open() returns it
class Fake_Hat : public mts::Hat
{
public:
  explicit Fake_Hat(bool* destroyed)
    : destroyed_(destroyed)
  {
  }

  ~Fake_Hat() override
  {
    *destroyed_ = true;
  }

  mts::Hat_Status read_sample(Raw_Sample& out) override
  {
    out = Raw_Sample{};
    out.seq = seq_++;
    out.accel[2] = static_cast<float>(mts::STANDARD_GRAVITY_MS2);
    return mts::Hat_Status::ok;
  }

  void show(const Led_Command& cmd) override
  {
    last_ = cmd;
  }

  mts::Hat_Info info() const override
  {
    return { true, 100.0, 4 * mts::STANDARD_GRAVITY_MS2, 500 * mts::DEG_TO_RAD, 0 };
  }

  Led_Command last_{};

private:
  bool* destroyed_;
  uint64_t seq_ = 0;
};

MTS_TEST(interface_usable_through_pointer)
{
  bool destroyed = false;
  {
    std::unique_ptr<mts::Hat> hat = std::make_unique<Fake_Hat>(&destroyed);

    Raw_Sample s{};
    MTS_ASSERT(hat->read_sample(s) == mts::Hat_Status::ok);
    MTS_ASSERT_EQ(s.seq, 0);
    MTS_ASSERT(hat->read_sample(s) == mts::Hat_Status::ok);
    MTS_ASSERT_EQ(s.seq, 1);

    Led_Command cmd{};
    cmd.mode = LED_MODE_AMPLITUDE;
    cmd.level = 0.4f;
    hat->show(cmd);
    MTS_ASSERT_EQ(static_cast<Fake_Hat*>(hat.get())->last_.mode, LED_MODE_AMPLITUDE);

    mts::Hat_Info info = hat->info();
    MTS_ASSERT(info.is_sim);
    MTS_ASSERT_NEAR(info.rate_hz, 100.0, 0.0);
  }
  // Destroyed through std::unique_ptr<Hat>: the virtual destructor ran
  MTS_ASSERT(destroyed);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(default_config_is_valid),
  MTS_TEST_ENTRY(range_helpers_match_lsm9ds1),
  MTS_TEST_ENTRY(unit_constants),
  MTS_TEST_ENTRY(interface_usable_through_pointer),
};

MTS_TEST_MAIN(tests)

