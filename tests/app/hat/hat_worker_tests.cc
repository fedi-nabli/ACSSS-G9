/* ===================================================================================================
 * tests/app/hat/hat_worker_tests.cc
 *
 * Hat_Worker on its own thread. A scripted Hat stands in for the sensor:
 * its reads follow a script of ok/error, and what the worker does to it
 * (reads, show() calls, its destruction) is recorded in a Probe the test
 * keeps, because the worker owns the Hat itself.
 *
 * The test thread is the consumer of the sample queue and the writer of the
 * LED mailbox, as Processing will be.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/hat/hat.hpp>
#include <app/hat/hat_worker.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace
{

constexpr double RATE_HZ = 500.0; // fast: the tests are short
constexpr int64_t PERIOD_NS = 2 * MTS_TIME_NS_PER_MS;

// What the worker did to the Hat, readable from the test thread
struct Probe
{
  std::atomic<uint64_t> reads{ 0 };
  std::atomic<uint64_t> shows{ 0 };
  std::atomic<uint32_t> last_level_milli{ 0 }; // Led_Command.level * 1000 of the last show()
  std::atomic<bool> in_read{ false };
  std::atomic<bool> destroyed{ false };
  std::atomic<bool> destroyed_during_read{ false };
};

class Scripted_Hat : public mts::Hat
{
public:
  /* script[i]: the status of read i. Past its end every read is ok.
   * instant_errors: an error returns at once instead of after a period,
   * like a bus that fails fast: the worker must not spin on it. */
  Scripted_Hat(Probe& probe, std::vector<mts::Hat_Status> script, bool instant_errors = false)
    : probe_(probe)
    , script_(std::move(script))
    , instant_errors_(instant_errors)
    , next_seq_(0)
    , next_t_ns_(mts_time_now_ns())
  {
  }

  ~Scripted_Hat() override
  {
    probe_.destroyed_during_read.store(probe_.in_read.load());
    probe_.destroyed.store(true);
  }

  mts::Hat_Status read_sample(Raw_Sample& out) override
  {
    probe_.in_read.store(true);
    const uint64_t i = probe_.reads.fetch_add(1);
    const mts::Hat_Status status = i < script_.size() ? script_[i] : mts::Hat_Status::ok;

    if (status == mts::Hat_Status::error && instant_errors_)
    {
      probe_.in_read.store(false);
      return status;
    }

    // Paced like the real thing: one sample per period
    next_t_ns_ += PERIOD_NS;
    mts_time_sleep_until_ns(next_t_ns_);

    if (status == mts::Hat_Status::ok)
    {
      std::memset(&out, 0, sizeof(out));
      out.seq = next_seq_++;
      out.t_ns = next_t_ns_;
    }
    probe_.in_read.store(false);
    return status;
  }

  void show(const Led_Command& cmd) override
  {
    probe_.last_level_milli.store((uint32_t)(cmd.level * 1000.0f + 0.5f));
    probe_.shows.fetch_add(1);
  }

  mts::Hat_Info info() const override
  {
    return { true, RATE_HZ, 4 * mts::STANDARD_GRAVITY_MS2, 500 * mts::DEG_TO_RAD, 0 };
  }

private:
  Probe& probe_;
  std::vector<mts::Hat_Status> script_;
  bool instant_errors_;
  uint64_t next_seq_;
  int64_t next_t_ns_;
};

// Errors and give-ups are logged: expected in these tests. Never closed, a
// worker thread may still hold the pointer
void quiet()
{
  static FILE* sink = std::fopen("/dev/null", "w");
  mts_log_set_stream(sink);
}

void loud()
{
  mts_log_set_stream(nullptr);
}

bool wait_until(const std::function<bool()>& pred, int64_t timeout_ns = 2 * MTS_TIME_NS_PER_S)
{
  const int64_t deadline = mts_time_now_ns() + timeout_ns;
  while (mts_time_now_ns() < deadline)
  {
    if (pred())
    {
      return true;
    }
    mts_time_sleep_ns(MTS_TIME_NS_PER_MS);
  }
  return pred();
}

std::vector<mts::Hat_Status> oks(size_t n)
{
  return std::vector<mts::Hat_Status>(n, mts::Hat_Status::ok);
}

} // namespace

MTS_TEST(samples_arrive_in_order)
{
  Probe probe;
  mts::Sample_Queue samples(64);
  mts::Mailbox<Led_Command> led;
  mts::Hat_Worker worker(std::make_unique<Scripted_Hat>(probe, oks(0)), samples, led);
  MTS_ASSERT(worker.start());

  for (uint64_t i = 0; i < 20; i++)
  {
    Raw_Sample s;
    MTS_ASSERT(samples.pop(s, MTS_TIME_NS_PER_S));
    MTS_ASSERT_EQ(s.seq, i);
    MTS_ASSERT_EQ(s.flags & SAMPLE_GAP, 0);
  }
  MTS_ASSERT(!worker.failed());
  MTS_ASSERT_EQ(worker.errors(), 0);
}

MTS_TEST(error_flags_the_next_sample)
{
  using S = mts::Hat_Status;
  Probe probe;
  mts::Sample_Queue samples(64);
  mts::Mailbox<Led_Command> led;
  quiet();
  mts::Hat_Worker worker(std::make_unique<Scripted_Hat>(probe, std::vector<S>{ S::ok, S::ok, S::error, S::error, S::ok, S::ok }),
                         samples, led);
  MTS_ASSERT(worker.start());

  // seq counts delivered samples here (the fake does not skip): 0 1 | 2 3
  uint32_t flags[4];
  for (int i = 0; i < 4; i++)
  {
    Raw_Sample s;
    MTS_ASSERT(samples.pop(s, MTS_TIME_NS_PER_S));
    MTS_ASSERT_EQ(s.seq, (uint64_t)i);
    flags[i] = s.flags;
  }
  worker.stop();
  loud();

  MTS_ASSERT_EQ(flags[0] & SAMPLE_GAP, 0);
  MTS_ASSERT_EQ(flags[1] & SAMPLE_GAP, 0);
  MTS_ASSERT_NE(flags[2] & SAMPLE_GAP, 0); // first good one after the errors
  MTS_ASSERT_EQ(flags[3] & SAMPLE_GAP, 0);
  MTS_ASSERT_EQ(worker.errors(), 2);
  MTS_ASSERT(!worker.failed()); // a short streak is survived
}

MTS_TEST(hat_failing_long_enough_is_given_up)
{
  Probe probe;
  mts::Sample_Queue samples(64);
  mts::Mailbox<Led_Command> led;
  mts::Hat_Worker_Config cfg;
  cfg.fail_after_ns = 100 * MTS_TIME_NS_PER_MS;
  quiet();
  // Errors that come back instantly, more than enough to outlast 100 ms
  mts::Hat_Worker worker(
    std::make_unique<Scripted_Hat>(probe, std::vector<mts::Hat_Status>(100000, mts::Hat_Status::error), true), samples, led,
    cfg);
  const int64_t t0 = mts_time_now_ns();
  MTS_ASSERT(worker.start());

  MTS_ASSERT(wait_until([&] { return worker.failed(); }));
  const int64_t took = mts_time_now_ns() - t0;
  loud();
  MTS_ASSERT_GE(took, cfg.fail_after_ns);
  MTS_ASSERT_LT(took, 10 * cfg.fail_after_ns);

  // The thread ended on its own: no more reads
  const uint64_t reads = probe.reads.load();
  mts_time_sleep_ns(20 * PERIOD_NS);
  MTS_ASSERT_EQ(probe.reads.load(), reads);
  MTS_ASSERT_GT(worker.errors(), 1);
  Raw_Sample s;
  MTS_ASSERT(!samples.pop(s));

  // Retried once per sample period, not in a tight loop: ~50 reads in 100 ms
  MTS_ASSERT_LT(reads, 200);
}

MTS_TEST(recovered_hat_starts_a_fresh_streak)
{
  using S = mts::Hat_Status;
  Probe probe;
  mts::Sample_Queue samples(1024);
  mts::Mailbox<Led_Command> led;
  mts::Hat_Worker_Config cfg;
  cfg.fail_after_ns = 100 * MTS_TIME_NS_PER_MS;

  // Two short streaks 200 ms apart: neither lasts 100 ms, their sum would
  std::vector<S> script(2, S::error);
  script.insert(script.end(), 100, S::ok);
  script.insert(script.end(), 2, S::error);
  quiet();
  mts::Hat_Worker worker(std::make_unique<Scripted_Hat>(probe, script), samples, led, cfg);
  MTS_ASSERT(worker.start());

  MTS_ASSERT(wait_until([&] { return probe.reads.load() >= script.size() + 10; }));
  worker.stop();
  loud();
  MTS_ASSERT(!worker.failed());
  MTS_ASSERT_EQ(worker.errors(), 4);
}

MTS_TEST(latest_led_command_is_shown_every_turn)
{
  Probe probe;
  mts::Sample_Queue samples(1024);
  mts::Mailbox<Led_Command> led;
  mts::Hat_Worker worker(std::make_unique<Scripted_Hat>(probe, oks(0)), samples, led);
  MTS_ASSERT(worker.start());

  // Nothing written yet: nothing shown
  MTS_ASSERT(wait_until([&] { return probe.reads.load() >= 5; }));
  MTS_ASSERT_EQ(probe.shows.load(), 0);

  Led_Command cmd;
  std::memset(&cmd, 0, sizeof(cmd));
  cmd.mode = LED_MODE_AMPLITUDE;
  cmd.level = 0.25f;
  led.write(cmd);
  MTS_ASSERT(wait_until([&] { return probe.last_level_milli.load() == 250; }));

  cmd.level = 0.75f;
  led.write(cmd);
  MTS_ASSERT(wait_until([&] { return probe.last_level_milli.load() == 750; }));

  // No new command: still shown each sample, so the Hat can animate
  const uint64_t shows = probe.shows.load();
  MTS_ASSERT(wait_until([&] { return probe.shows.load() >= shows + 5; }));
  MTS_ASSERT_EQ(probe.last_level_milli.load(), 750);
}

MTS_TEST(stop_is_quick_and_the_hat_dies_after_the_join)
{
  Probe probe;
  mts::Sample_Queue samples(1024);
  mts::Mailbox<Led_Command> led;
  {
    mts::Hat_Worker worker(std::make_unique<Scripted_Hat>(probe, oks(0)), samples, led);
    MTS_ASSERT(worker.start());
    MTS_ASSERT(wait_until([&] { return probe.reads.load() >= 3; }));

    const int64_t t0 = mts_time_now_ns();
    worker.stop();
    MTS_ASSERT_LT(mts_time_now_ns() - t0, 100 * MTS_TIME_NS_PER_MS);
    MTS_ASSERT(!probe.destroyed.load()); // stop() joins, the worker still owns the Hat
    worker.stop(); // twice is fine
  }
  MTS_ASSERT(probe.destroyed.load());
  MTS_ASSERT(!probe.destroyed_during_read.load());
}

MTS_TEST(start_needs_a_hat_and_only_once)
{
  Probe probe;
  mts::Sample_Queue samples(64);
  mts::Mailbox<Led_Command> led;
  quiet();
  mts::Hat_Worker empty(nullptr, samples, led);
  MTS_ASSERT(!empty.start());

  mts::Hat_Worker worker(std::make_unique<Scripted_Hat>(probe, oks(0)), samples, led);
  MTS_ASSERT(worker.start());
  MTS_ASSERT(!worker.start());
  worker.stop();
  loud();
}

MTS_TEST(drives_hat_sim_at_its_rate)
{
  // The real pairing: hat_open + Hat_Sim, paced
  mts::Hat_Config cfg;
  cfg.sample_rate_hz = 200.0;
  quiet(); // hat_open logs what it opened
  std::unique_ptr<mts::Hat> hat = mts::hat_open(cfg);
  loud();
  MTS_ASSERT(hat != nullptr);
  const int64_t opened = hat->info().opened_t_ns;

  mts::Sample_Queue samples(64);
  mts::Mailbox<Led_Command> led;
  mts::Hat_Worker worker(std::move(hat), samples, led);
  MTS_ASSERT(worker.start());

  Raw_Sample first;
  Raw_Sample s;
  MTS_ASSERT(samples.pop(first, MTS_TIME_NS_PER_S));
  MTS_ASSERT_EQ(first.seq, 0);
  MTS_ASSERT_EQ(first.t_ns, opened);
  for (int i = 1; i < 40; i++)
  {
    MTS_ASSERT(samples.pop(s, MTS_TIME_NS_PER_S));
    MTS_ASSERT_EQ(s.seq, (uint64_t)i);
  }
  // Paced: 39 periods of 5 ms in between, and not delivered early
  MTS_ASSERT_EQ(s.t_ns - first.t_ns, 39 * 5 * MTS_TIME_NS_PER_MS);
  MTS_ASSERT_GE(mts_time_now_ns(), s.t_ns);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(samples_arrive_in_order),
  MTS_TEST_ENTRY(error_flags_the_next_sample),
  MTS_TEST_ENTRY(hat_failing_long_enough_is_given_up),
  MTS_TEST_ENTRY(recovered_hat_starts_a_fresh_streak),
  MTS_TEST_ENTRY(latest_led_command_is_shown_every_turn),
  MTS_TEST_ENTRY(stop_is_quick_and_the_hat_dies_after_the_join),
  MTS_TEST_ENTRY(start_needs_a_hat_and_only_once),
  MTS_TEST_ENTRY(drives_hat_sim_at_its_rate),
};

MTS_TEST_MAIN(tests)
