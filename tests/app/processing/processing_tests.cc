/* ===================================================================================================
 * tests/app/processing/processing_tests.cc
 *
 * Processing_Core sample by sample, on Hat_Sim's data (unpaced: as fast as
 * the test runs): a flat board, gravity on z, a 6 Hz 0.5 m/s^2 tremor on x,
 * which is 0.35 mm of displacement. Then the thread between real queues,
 * with the test playing the Hat_Worker and the orchestrator.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/hat/hat.hpp>
#include <app/processing/processing.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <memory>
#include <vector>

namespace
{

constexpr double RATE = 100.0;
constexpr double PI = 3.14159265358979323846;
const double TREMOR_MM = 0.5 / std::pow(2.0 * PI * 6.0, 2.0) * 1e3; // what Hat_Sim's tremor integrates to

void quiet()
{
  static FILE* sink = std::fopen("/dev/null", "w");
  mts_log_set_stream(sink);
}

void loud()
{
  mts_log_set_stream(nullptr);
}

std::unique_ptr<mts::Hat> sim()
{
  mts::Hat_Config cfg;
  cfg.sample_rate_hz = RATE;
  cfg.sim.paced = false;
  quiet();
  std::unique_ptr<mts::Hat> hat = mts::hat_open(cfg);
  loud();
  return hat;
}

Pipeline_Control control(uint32_t gen, uint32_t enabled, Predictor_Id active, int64_t horizon_ms, bool running = true)
{
  Pipeline_Control c;
  std::memset(&c, 0, sizeof(c));
  c.running = running ? 1 : 0;
  c.cfg_gen = gen;
  c.settings.enabled_mask = enabled;
  c.settings.active_predictor_id = active;
  c.settings.horizon_ns = horizon_ms * MTS_TIME_NS_PER_MS;
  c.settings.metrics_hz = 2.0;
  c.settings.spectrum_hz = 5.0;
  return c;
}

constexpr uint32_t INV = UINT32_C(1) << PREDICTOR_INVERSION;
constexpr uint32_t WFLC = UINT32_C(1) << PREDICTOR_WFLC;
constexpr uint32_t HARMONIC = UINT32_C(1) << PREDICTOR_HARMONIC;

// n frames of Hat_Sim through a core configured with ctl
std::vector<Output_Frame> run(mts::Processing_Core& core, mts::Hat& hat, int n)
{
  std::vector<Output_Frame> frames((size_t)n);
  for (int i = 0; i < n; i++)
  {
    Raw_Sample s;
    hat.read_sample(s);
    core.process(s, s.t_ns, frames[(size_t)i]);
  }
  return frames;
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

} // namespace

/* ---------------------------------------------------------------------------------------------------
 * Processing_Core: stage 1
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(stage_one_finds_the_tremor)
{
  mts::Processing_Core core(RATE);
  MTS_ASSERT(core.valid());
  core.apply(control(1, INV, PREDICTOR_INVERSION, 30));
  std::unique_ptr<mts::Hat> hat = sim();
  const std::vector<Output_Frame> f = run(core, *hat, 400);

  // The last second: tremor on, 6 Hz, its amplitude, on x only
  double peak[3] = { 0.0, 0.0, 0.0 };
  for (size_t i = 300; i < 400; i++)
  {
    MTS_ASSERT_NE(f[i].flags & FRAME_TREMOR_ACTIVE, 0);
    for (int a = 0; a < 3; a++)
    {
      peak[a] = std::fabs(f[i].tremor_disp.v[a]) > peak[a] ? std::fabs(f[i].tremor_disp.v[a]) : peak[a];
    }
  }
  const Output_Frame& last = f.back();
  MTS_ASSERT_NEAR(last.tremor_hz, 6.0, 0.1);
  MTS_ASSERT_NEAR(last.tremor_amp * 1e3, TREMOR_MM, 0.1 * TREMOR_MM);
  MTS_ASSERT_NEAR(peak[0] * 1e3, TREMOR_MM, 0.1 * TREMOR_MM);
  MTS_ASSERT(peak[1] < 0.1 * peak[0]); // y and z: noise only
  MTS_ASSERT(peak[2] < 0.1 * peak[0]);
  MTS_ASSERT(last.confidence > 0.8);

  // The board is flat and faces north: orientation is (almost) identity
  MTS_ASSERT(last.orientation.q[QUAT_W] > 0.9999);

  // The filters' delay is in the pipeline delay (processing time is 0 here: now = t)
  MTS_ASSERT_GT(last.pipeline_delay_ns, 5 * MTS_TIME_NS_PER_MS);
}

/* ---------------------------------------------------------------------------------------------------
 * Processing_Core: predictors and the forecast rings
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(inversion_forecast_is_the_displacement_h_samples_ago)
{
  // Exactly: inversion's forecast for now was made h samples ago, and is what the displacement was then
  mts::Processing_Core core(RATE);
  core.apply(control(1, INV, PREDICTOR_INVERSION, 30));
  MTS_ASSERT_EQ(core.horizon_steps(), 3);
  std::unique_ptr<mts::Hat> hat = sim();
  const std::vector<Output_Frame> f = run(core, *hat, 300);

  for (size_t i = 0; i < 3; i++)
  {
    MTS_ASSERT_EQ(f[i].predictions[0].flags & PRED_FORECAST_VALID, 0); // nothing made for these yet
  }
  for (size_t i = 3; i < f.size(); i++)
  {
    const Frame_Prediction& p = f[i].predictions[0];
    MTS_ASSERT_EQ(p.predictor_id, PREDICTOR_INVERSION);
    MTS_ASSERT_NE(p.flags & PRED_FORECAST_VALID, 0);
    for (int a = 0; a < 3; a++)
    {
      MTS_ASSERT_NEAR(p.forecast.v[a], f[i - 3].tremor_disp.v[a], 0.0);
    }
  }
}

MTS_TEST(residual_and_counter_follow_the_tremor_gate)
{
  mts::Processing_Core core(RATE);
  core.apply(control(1, INV, PREDICTOR_INVERSION, 30));
  std::unique_ptr<mts::Hat> hat = sim();
  const std::vector<Output_Frame> f = run(core, *hat, 400);

  int gated = 0, open = 0;
  for (size_t i = 3; i < f.size(); i++)
  {
    const Frame_Prediction& p = f[i].predictions[0];
    const bool active = (f[i].flags & FRAME_TREMOR_ACTIVE) != 0;
    MTS_ASSERT_EQ((p.flags & PRED_RESIDUAL_VALID) != 0, active);
    MTS_ASSERT_EQ((f[i].flags & FRAME_COUNTER_VALID) != 0, active);
    if (active)
    {
      open++;
      for (int a = 0; a < 3; a++)
      {
        MTS_ASSERT_NEAR(p.residual.v[a], f[i].tremor_disp.v[a] - p.forecast.v[a], 0.0);
        MTS_ASSERT_NEAR(f[i].counter.v[a], -p.forecast.v[a], 0.0);
      }
    }
    else
    {
      gated++;
    }
  }
  MTS_ASSERT_GT(gated, 0); // the detector needed a moment
  MTS_ASSERT_GT(open, 0); // then let it through
}

MTS_TEST(wflc_cancels_where_inversion_cannot)
{
  // On Hat_Sim's tremor, the last 2 s: residual RMS against the tremor's.
  // Inversion is 30 ms late at 6 Hz: ~1.07. WFLC predicts the 30 ms
  mts::Processing_Core core(RATE);
  core.apply(control(1, INV | WFLC, PREDICTOR_WFLC, 30));
  std::unique_ptr<mts::Hat> hat = sim();
  const std::vector<Output_Frame> f = run(core, *hat, 600);
  double tremor = 0.0, inv = 0.0, wflc = 0.0;
  for (size_t i = 400; i < f.size(); i++)
  {
    MTS_ASSERT_NE(f[i].predictions[1].flags & PRED_RESIDUAL_VALID, 0); // locked by now
    tremor += f[i].tremor_disp.v[0] * f[i].tremor_disp.v[0];
    inv += f[i].predictions[0].residual.v[0] * f[i].predictions[0].residual.v[0];
    wflc += f[i].predictions[1].residual.v[0] * f[i].predictions[1].residual.v[0];
    MTS_ASSERT_NE(f[i].flags & FRAME_COUNTER_VALID, 0); // the active one: wflc
    MTS_ASSERT_NEAR(f[i].counter.v[0], -f[i].predictions[1].forecast.v[0], 0.0);
  }
  // y and z carry no tremor: their WFLCs do not lock, they forecast 0
  MTS_ASSERT_NEAR(f.back().predictions[1].forecast.v[1], 0.0, 0.0);
  const double inv_ratio = std::sqrt(inv / tremor), wflc_ratio = std::sqrt(wflc / tremor);
  MTS_ASSERT(inv_ratio > 0.9); // measured: 1.07
  MTS_ASSERT(wflc_ratio < 0.1); // measured: 0.034
}

MTS_TEST(unimplemented_predictor_keeps_its_place_all_null)
{
  // harmonic is enabled and active but has no implementation yet: its
  // entry is there (the final shape), null, and there is no counter signal
  mts::Processing_Core core(RATE);
  core.apply(control(1, INV | HARMONIC, PREDICTOR_HARMONIC, 30));
  std::unique_ptr<mts::Hat> hat = sim();
  const std::vector<Output_Frame> f = run(core, *hat, 300);
  const Output_Frame& last = f.back();
  MTS_ASSERT_EQ(last.n_predictions, 2);
  MTS_ASSERT_EQ(last.predictions[0].predictor_id, PREDICTOR_INVERSION);
  MTS_ASSERT_NE(last.predictions[0].flags & PRED_FORECAST_VALID, 0);
  MTS_ASSERT_EQ(last.predictions[1].predictor_id, PREDICTOR_HARMONIC);
  MTS_ASSERT_EQ(last.predictions[1].flags, 0);
  MTS_ASSERT_EQ(last.flags & FRAME_COUNTER_VALID, 0);
}

MTS_TEST(a_gap_resets_the_predictors)
{
  mts::Processing_Core core(RATE);
  core.apply(control(1, INV, PREDICTOR_INVERSION, 30));
  std::unique_ptr<mts::Hat> hat = sim();
  run(core, *hat, 100);

  // Samples lost: the next one says so, and the forecasts start over
  Output_Frame f;
  for (int i = 0; i < 6; i++)
  {
    Raw_Sample s;
    hat->read_sample(s);
    if (i == 0)
    {
      s.flags |= SAMPLE_GAP;
    }
    core.process(s, s.t_ns, f);
    MTS_ASSERT_EQ((f.flags & FRAME_GAP_BEFORE) != 0, i == 0);
    MTS_ASSERT_EQ((f.predictions[0].flags & PRED_FORECAST_VALID) != 0, i >= 3);
  }
}

MTS_TEST(a_new_horizon_applies_and_restarts)
{
  mts::Processing_Core core(RATE);
  core.apply(control(1, INV, PREDICTOR_INVERSION, 30));
  std::unique_ptr<mts::Hat> hat = sim();
  run(core, *hat, 100);

  core.apply(control(1, INV, PREDICTOR_INVERSION, 80)); // same generation: ignored
  MTS_ASSERT_EQ(core.horizon_steps(), 3);
  core.apply(control(2, INV, PREDICTOR_INVERSION, 80));
  MTS_ASSERT_EQ(core.horizon_steps(), 8);

  const std::vector<Output_Frame> f = run(core, *hat, 20);
  for (size_t i = 0; i < f.size(); i++)
  {
    MTS_ASSERT_EQ((f[i].predictions[0].flags & PRED_FORECAST_VALID) != 0, i >= 8);
    if (i >= 8)
    {
      MTS_ASSERT_NEAR(f[i].predictions[0].forecast.v[0], f[i - 8].tremor_disp.v[0], 0.0);
    }
  }
}

MTS_TEST(horizon_is_clamped_to_what_a_forecast_holds)
{
  mts::Processing_Core core(RATE);
  core.apply(control(1, INV, PREDICTOR_INVERSION, 1)); // 0.1 sample: at least 1
  MTS_ASSERT_EQ(core.horizon_steps(), 1);
  core.apply(control(2, INV, PREDICTOR_INVERSION, 10000)); // 1000 samples: at most K_MAX
  MTS_ASSERT_EQ(core.horizon_steps(), K_MAX);
}

/* ---------------------------------------------------------------------------------------------------
 * The thread
 * ------------------------------------------------------------------------------------------------- */

namespace
{

// Queues and mailboxes first: they outlive the thread
struct Rig
{
  mts::Sample_Queue samples{ 1024 };
  mts::Mailbox<Pipeline_Control> ctl;
  mts::Queue<Server_Message> frames{ 1024 };
  mts::Mailbox<Led_Command> led;
  mts::Processing processing{ RATE, samples, ctl, frames, led };
};

Raw_Sample flat(uint64_t seq)
{
  Raw_Sample s;
  std::memset(&s, 0, sizeof(s));
  s.seq = seq;
  s.t_ns = (int64_t)seq * 10 * MTS_TIME_NS_PER_MS;
  s.accel[2] = 9.80665f;
  s.mag[0] = 16.0f;
  s.mag[2] = -48.0f;
  s.flags = SAMPLE_MAG_FRESH;
  return s;
}

} // namespace

MTS_TEST(no_frames_unless_running)
{
  Rig rig;
  rig.ctl.write(control(1, INV, PREDICTOR_INVERSION, 30, false));
  MTS_ASSERT(rig.processing.start());
  for (uint64_t i = 0; i < 10; i++)
  {
    MTS_ASSERT(rig.samples.push(flat(i)));
  }
  mts_time_sleep_ns(30 * MTS_TIME_NS_PER_MS);
  Server_Message m;
  MTS_ASSERT(!rig.frames.pop(m));

  // Those ten went through the chain, not into a backlog: the first frame
  // is the first sample pushed after the switch
  rig.ctl.write(control(2, INV, PREDICTOR_INVERSION, 30, true));
  MTS_ASSERT(rig.samples.push(flat(100)));
  MTS_ASSERT(rig.frames.pop(m, MTS_TIME_NS_PER_S));
  MTS_ASSERT_EQ(m.frame.seq, 100);
}

MTS_TEST(running_turns_samples_into_frames_in_order)
{
  Rig rig;
  rig.ctl.write(control(1, INV | WFLC, PREDICTOR_INVERSION, 30));
  MTS_ASSERT(rig.processing.start());
  for (uint64_t i = 0; i < 20; i++)
  {
    MTS_ASSERT(rig.samples.push(flat(i)));
  }
  for (uint64_t i = 0; i < 20; i++)
  {
    Server_Message m;
    MTS_ASSERT(rig.frames.pop(m, MTS_TIME_NS_PER_S));
    MTS_ASSERT_EQ(m.kind, SERVER_MSG_FRAME);
    MTS_ASSERT_EQ(m.frame.seq, i);
    MTS_ASSERT_EQ(m.frame.n_predictions, 2);
  }
}

MTS_TEST(control_applies_from_the_next_sample)
{
  Rig rig;
  rig.ctl.write(control(1, INV, PREDICTOR_INVERSION, 30));
  MTS_ASSERT(rig.processing.start());
  MTS_ASSERT(rig.samples.push(flat(0)));
  Server_Message m;
  MTS_ASSERT(rig.frames.pop(m, MTS_TIME_NS_PER_S));
  MTS_ASSERT_EQ(m.frame.n_predictions, 1);

  rig.ctl.write(control(2, INV | WFLC | (UINT32_C(1) << PREDICTOR_HARMONIC), PREDICTOR_INVERSION, 30));
  MTS_ASSERT(rig.samples.push(flat(1)));
  MTS_ASSERT(rig.frames.pop(m, MTS_TIME_NS_PER_S));
  MTS_ASSERT_EQ(m.frame.n_predictions, 3);

  rig.ctl.write(control(3, INV, PREDICTOR_INVERSION, 30, false));
  MTS_ASSERT(rig.samples.push(flat(2)));
  MTS_ASSERT(rig.samples.push(flat(3)));
  mts_time_sleep_ns(30 * MTS_TIME_NS_PER_MS);
  MTS_ASSERT(!rig.frames.pop(m));
}

MTS_TEST(led_is_set_off_at_start)
{
  Rig rig;
  rig.ctl.write(control(1, INV, PREDICTOR_INVERSION, 30, false));
  MTS_ASSERT(rig.processing.start());
  Led_Command cmd;
  MTS_ASSERT(wait_until([&] { return rig.led.read(cmd); }));
  MTS_ASSERT_EQ(cmd.mode, LED_MODE_OFF);
}

MTS_TEST(stop_is_quick_while_waiting_for_samples)
{
  Rig rig;
  rig.ctl.write(control(1, INV, PREDICTOR_INVERSION, 30));
  MTS_ASSERT(rig.processing.start());
  mts_time_sleep_ns(5 * MTS_TIME_NS_PER_MS);
  const int64_t t0 = mts_time_now_ns();
  rig.processing.stop();
  MTS_ASSERT_LT(mts_time_now_ns() - t0, mts::PROCESSING_POLL_NS + 100 * MTS_TIME_NS_PER_MS);
  rig.processing.stop();
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(stage_one_finds_the_tremor),
  MTS_TEST_ENTRY(inversion_forecast_is_the_displacement_h_samples_ago),
  MTS_TEST_ENTRY(residual_and_counter_follow_the_tremor_gate),
  MTS_TEST_ENTRY(wflc_cancels_where_inversion_cannot),
  MTS_TEST_ENTRY(unimplemented_predictor_keeps_its_place_all_null),
  MTS_TEST_ENTRY(a_gap_resets_the_predictors),
  MTS_TEST_ENTRY(a_new_horizon_applies_and_restarts),
  MTS_TEST_ENTRY(horizon_is_clamped_to_what_a_forecast_holds),
  MTS_TEST_ENTRY(no_frames_unless_running),
  MTS_TEST_ENTRY(running_turns_samples_into_frames_in_order),
  MTS_TEST_ENTRY(control_applies_from_the_next_sample),
  MTS_TEST_ENTRY(led_is_set_off_at_start),
  MTS_TEST_ENTRY(stop_is_quick_while_waiting_for_samples),
};

MTS_TEST_MAIN(tests)
