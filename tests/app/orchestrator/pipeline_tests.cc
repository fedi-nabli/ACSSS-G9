/* ===================================================================================================
 * tests/app/orchestrator/pipeline_tests.cc
 *
 * The whole program in one process: Pipeline with Hat_Sim (paced, 100 Hz),
 * its orchestrator stepped on a thread of its own as main would, and the UI
 * played by the blocking test client over real loopback. Calibration steps
 * are shortened, everything else is the shipping configuration.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <common/ws_client.hpp>
#include <app/orchestrator/pipeline.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <atomic>
#include <cstdlib>
#include <initializer_list>
#include <string>
#include <thread>
#include <vector>

namespace
{

using mts::test::Ws_Client;

mts::Pipeline_Config config()
{
  mts::Pipeline_Config cfg;
  cfg.server.bind_addr = "127.0.0.1";
  cfg.server.port = 0;
  cfg.orchestrator.hold_still_ns = 100 * MTS_TIME_NS_PER_MS;
  cfg.orchestrator.rotate_ns = 100 * MTS_TIME_NS_PER_MS;
  cfg.orchestrator.quick_zero_ns = 50 * MTS_TIME_NS_PER_MS;
  return cfg;
}

// The pipeline and the thread that steps it, as main does
struct Running_Pipeline
{
  mts::Pipeline pipeline{ config() };
  std::atomic<bool> quit{ false };
  std::thread loop;

  bool start()
  {
    mts_log_set_min_level(LEVEL_WARN);
    if (!pipeline.start())
    {
      return false;
    }
    loop = std::thread([this] {
      while (!quit.load())
      {
        pipeline.step(10 * MTS_TIME_NS_PER_MS);
      }
    });
    return true;
  }

  ~Running_Pipeline()
  {
    quit.store(true);
    if (loop.joinable())
    {
      loop.join();
    }
    pipeline.stop();
  }
};

bool has(const std::string& line, const char* needle)
{
  return line.find(needle) != std::string::npos;
}

// Value of an integer field in a line, -1 if absent
long long field(const std::string& line, const char* key)
{
  const std::string k = std::string("\"") + key + "\":";
  const size_t at = line.find(k);
  return at == std::string::npos ? -1 : std::atoll(line.c_str() + at + k.size());
}

/* Reads until every needle has shown up in some line, in any order: the
 * ack and the status a command causes travel in different queues, so
 * either may come first. found[i]: the line that held needles[i] */
bool read_all(Ws_Client& ui, std::initializer_list<const char*> needles, std::vector<std::string>& found)
{
  found.assign(needles.size(), std::string());
  size_t missing = needles.size();
  const int64_t deadline = mts_time_now_ns() + 3 * MTS_TIME_NS_PER_S;
  std::string line;
  while (missing > 0)
  {
    const int64_t left = deadline - mts_time_now_ns();
    if (left <= 0 || !ui.read_line(line, left))
    {
      return false;
    }
    size_t i = 0;
    for (const char* n : needles)
    {
      if (found[i].empty() && has(line, n))
      {
        found[i] = line;
        missing--;
      }
      i++;
    }
  }
  return true;
}

std::string command(int id, const char* name)
{
  return "{\"type\":\"command\",\"id\":" + std::to_string(id) + ",\"command\":\"" + name + "\"}";
}

} // namespace

MTS_TEST(ui_session_from_boot_to_stop)
{
  Running_Pipeline rp;
  MTS_ASSERT(rp.start());
  Ws_Client ui;
  MTS_ASSERT(ui.connect(rp.pipeline.port()));
  std::string line;

  // Boot has passed: the UI is told calibration is needed
  MTS_ASSERT(ui.read_line_containing("\"state\":\"needs_calibration\"", line));
  MTS_ASSERT(has(line, "\"sample_rate_hz\":100,"));
  MTS_ASSERT(has(line, "\"calibration_age_s\":null"));

  // start before calibrating: refused, over the wire
  MTS_ASSERT(ui.send_text(command(1, "start")));
  MTS_ASSERT(ui.read_line_containing("\"id\":1", line));
  MTS_ASSERT(has(line, "\"code\":\"needs_calibration\""));

  // calibrate: ack, the two steps, then ready
  std::vector<std::string> got;
  MTS_ASSERT(ui.send_text(command(2, "calibrate")));
  MTS_ASSERT(read_all(ui, { "\"id\":2", "\"step\":\"hold_still\"" }, got));
  MTS_ASSERT(got[0] == "{\"type\":\"ack\",\"id\":2}");
  MTS_ASSERT(ui.read_line_containing("\"step\":\"rotate\"", line));
  MTS_ASSERT(ui.read_line_containing("\"state\":\"ready\"", line));

  // start: quick zero, then running and frames
  MTS_ASSERT(ui.send_text(command(3, "start")));
  MTS_ASSERT(read_all(ui, { "\"id\":3", "\"step\":\"quick_zero\"" }, got));
  MTS_ASSERT(has(got[0], "\"type\":\"ack\""));
  MTS_ASSERT(ui.read_line_containing("\"state\":\"running\"", line));

  // 300 frames: consecutive, 10 ms apart, carrying the tremor once the
  // detector has seen it: displacement, inversion's forecast, the counter
  long long prev_seq = -1;
  long long prev_t = -1;
  int with_counter = 0;
  int with_rotation_counter = 0;
  const int64_t frames_deadline = mts_time_now_ns() + 8 * MTS_TIME_NS_PER_S; // 300 frames take 3 s
  for (int n = 0; n < 300;)
  {
    // Heartbeats keep coming without frames: the deadline, not read_line, ends a frameless run
    MTS_ASSERT_LT(mts_time_now_ns(), frames_deadline);
    MTS_ASSERT(ui.read_line(line));
    if (!has(line, "\"type\":\"frame\""))
    {
      continue; // heartbeat
    }
    const long long seq = field(line, "sequence");
    const long long t = field(line, "timestamp_ns");
    if (prev_seq >= 0)
    {
      MTS_ASSERT_EQ(seq, prev_seq + 1);
      MTS_ASSERT_EQ(t - prev_t, 10 * MTS_TIME_NS_PER_MS);
    }
    MTS_ASSERT(has(line, "\"gap_before\":false"));
    // Running reset the predictors: the first h = 3 frames have no forecast made for them yet
    MTS_ASSERT(has(line, n < 3 ? "\"predictions\":[{\"predictor_id\":\"inversion\",\"forecast_mm\":null"
                               : "\"predictions\":[{\"predictor_id\":\"inversion\",\"forecast_mm\":["));
    // wflc, the active one: null while it locks, then forecasting, and the counter follows it
    const bool wflc_forecasts = has(line, "{\"predictor_id\":\"wflc\",\"forecast_mm\":[");
    if (wflc_forecasts && has(line, "\"tremor_active\":true"))
    {
      MTS_ASSERT(!has(line, "\"counter_signal_mm\":null"));
      with_counter++;
    }
    if (!wflc_forecasts)
    {
      MTS_ASSERT(has(line, "\"counter_signal_mm\":null"));
      MTS_ASSERT(has(line, "\"counter_rotation_mrad\":null"));
    }
    // Rotation, gated by its own detector (Hat_Sim turns the board about z)
    if (wflc_forecasts && has(line, "\"rotation_active\":true"))
    {
      MTS_ASSERT(!has(line, "\"counter_rotation_mrad\":null"));
      with_rotation_counter++;
    }
    MTS_ASSERT(has(line, "\"imu\":{\"ax_g\":"));
    prev_seq = seq;
    prev_t = t;
    n++;
  }
  // Within the 3 s, the detector turned on and wflc locked
  MTS_ASSERT_GT(with_counter, 100);
  MTS_ASSERT_GT(with_rotation_counter, 100);

  // setup while running: acked, shows in status, frames follow the new set
  MTS_ASSERT(ui.send_text("{\"type\":\"command\",\"id\":4,\"command\":\"setup\",\"enabled_predictor_ids\":[\"wflc\"],"
                          "\"active_predictor_id\":\"wflc\"}"));
  MTS_ASSERT(read_all(ui, { "\"id\":4", "\"enabled_predictor_ids\":[\"wflc\"]" }, got));
  MTS_ASSERT(has(got[0], "\"type\":\"ack\""));
  MTS_ASSERT(ui.read_line_containing("\"predictions\":[{\"predictor_id\":\"wflc\",", line));

  // stop: frames end, status goes on
  MTS_ASSERT(ui.send_text(command(5, "stop")));
  MTS_ASSERT(ui.read_line_containing("\"state\":\"stopped\"", line));
  // Frames already in flight when stop landed may still come: let them pass
  while (ui.read_line(line, 200 * MTS_TIME_NS_PER_MS))
  {
  }
  int frames = 0;
  int statuses = 0;
  const int64_t until = mts_time_now_ns() + 1300 * MTS_TIME_NS_PER_MS;
  while (mts_time_now_ns() < until)
  {
    if (ui.read_line(line, until - mts_time_now_ns()))
    {
      frames += has(line, "\"type\":\"frame\"") ? 1 : 0;
      statuses += has(line, "\"type\":\"status\"") ? 1 : 0;
    }
  }
  MTS_ASSERT_EQ(frames, 0);
  MTS_ASSERT_GE(statuses, 1); // the heartbeat
}

MTS_TEST(second_ui_joins_mid_run)
{
  Running_Pipeline rp;
  MTS_ASSERT(rp.start());
  Ws_Client first;
  MTS_ASSERT(first.connect(rp.pipeline.port()));
  std::string line;
  MTS_ASSERT(first.send_text(command(1, "calibrate")));
  MTS_ASSERT(first.read_line_containing("\"state\":\"ready\"", line));
  MTS_ASSERT(first.send_text(command(2, "start")));
  MTS_ASSERT(first.read_line_containing("\"state\":\"running\"", line));

  // A latecomer gets the current state at once, then the frames
  Ws_Client second;
  MTS_ASSERT(second.connect(rp.pipeline.port()));
  MTS_ASSERT(second.read_line(line));
  MTS_ASSERT(has(line, "\"type\":\"status\""));
  MTS_ASSERT(has(line, "\"state\":\"running\""));
  MTS_ASSERT(second.read_line_containing("\"type\":\"frame\"", line));

  // Its commands are answered to it alone
  MTS_ASSERT(second.send_text(command(2, "calibrate"))); // same id as first's start
  MTS_ASSERT(second.read_line_containing("\"id\":2", line));
  MTS_ASSERT(has(line, "\"code\":\"invalid_state\""));
}

MTS_TEST(busy_port_fails_start_cleanly)
{
  Running_Pipeline first;
  MTS_ASSERT(first.start());

  mts::Pipeline_Config cfg = config();
  cfg.server.port = first.pipeline.port();
  mts::Pipeline second(cfg);
  static FILE* sink = std::fopen("/dev/null", "w");
  mts_log_set_stream(sink);
  const bool started = second.start();
  mts_log_set_stream(nullptr);
  MTS_ASSERT(!started);
  second.stop(); // nothing is running: harmless
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(ui_session_from_boot_to_stop),
  MTS_TEST_ENTRY(second_ui_joins_mid_run),
  MTS_TEST_ENTRY(busy_port_fails_start_cleanly),
};

MTS_TEST_MAIN(tests)
