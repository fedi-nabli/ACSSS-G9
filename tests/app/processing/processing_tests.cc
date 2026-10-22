/* ===================================================================================================
 * tests/app/processing/processing_tests.cc
 *
 * Processing: the pass-through frame on its own, then the thread between a
 * real Sample_Queue and frame queue, with the test playing the Hat_Worker
 * (producer of samples) and the orchestrator (writer of control).
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/hat/hat.hpp>
#include <app/processing/processing.hpp>
#include <utils/time/time.h>

#include <cmath>
#include <cstring>
#include <functional>

namespace
{

Raw_Sample sample(uint64_t seq, uint32_t flags = 0)
{
  Raw_Sample s;
  std::memset(&s, 0, sizeof(s));
  s.seq = seq;
  s.t_ns = 1000 + (int64_t)seq * 10000000;
  s.accel[0] = 0.5f;
  s.accel[1] = -0.25f;
  s.accel[2] = 10.0f;
  s.flags = flags;
  return s;
}

Runtime_Settings settings(uint32_t enabled_mask)
{
  Runtime_Settings rs;
  std::memset(&rs, 0, sizeof(rs));
  rs.enabled_mask = enabled_mask;
  rs.active_predictor_id = PREDICTOR_WFLC;
  rs.horizon_ns = 30 * MTS_TIME_NS_PER_MS;
  rs.metrics_hz = 2.0;
  rs.spectrum_hz = 5.0;
  return rs;
}

Pipeline_Control control(bool running, uint32_t gen, uint32_t enabled_mask)
{
  Pipeline_Control c;
  std::memset(&c, 0, sizeof(c));
  c.running = running ? 1 : 0;
  c.cfg_gen = gen;
  c.settings = settings(enabled_mask);
  return c;
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

// Queues and mailboxes first: they outlive the thread
struct Rig
{
  mts::Sample_Queue samples{ 64 };
  mts::Mailbox<Pipeline_Control> ctl;
  mts::Queue<Server_Message> frames{ 64 };
  mts::Mailbox<Led_Command> led;
  mts::Processing processing{ samples, ctl, frames, led };
};

} // namespace

/* ---------------------------------------------------------------------------------------------------
 * The frame
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(frame_carries_the_sample)
{
  Output_Frame f;
  mts::processing_pass_through(sample(42), settings(0), sample(42).t_ns + 3 * MTS_TIME_NS_PER_MS, f);
  MTS_ASSERT_EQ(f.seq, 42);
  MTS_ASSERT_EQ(f.t_ns, sample(42).t_ns);
  MTS_ASSERT_EQ(f.pipeline_delay_ns, 3 * MTS_TIME_NS_PER_MS);
  MTS_ASSERT_EQ(f.flags, 0);
}

MTS_TEST(gravity_is_removed_from_z_only)
{
  Output_Frame f;
  mts::processing_pass_through(sample(0), settings(0), 0, f);
  MTS_ASSERT_NEAR(f.tremor_acc.v[AXIS_X], 0.5, 1e-6);
  MTS_ASSERT_NEAR(f.tremor_acc.v[AXIS_Y], -0.25, 1e-6);
  MTS_ASSERT_NEAR(f.tremor_acc.v[AXIS_Z], 10.0 - mts::STANDARD_GRAVITY_MS2, 1e-6);
}

MTS_TEST(unknowns_are_honest)
{
  Output_Frame f;
  mts::processing_pass_through(sample(0), settings(0), 0, f);
  MTS_ASSERT(std::isfinite(f.tremor_hz));
  MTS_ASSERT(std::isfinite(f.tremor_amp));
  MTS_ASSERT_NEAR(f.tremor_hz, 0.0, 0.0);
  MTS_ASSERT_NEAR(f.tremor_amp, 0.0, 0.0);
  MTS_ASSERT_NEAR(f.confidence, 0.0, 0.0);
  MTS_ASSERT_EQ(f.flags & (FRAME_TREMOR_ACTIVE | FRAME_COUNTER_VALID), 0);
  MTS_ASSERT_NEAR(f.orientation.q[QUAT_W], 1.0, 0.0);
  MTS_ASSERT_NEAR(f.tremor_disp.v[AXIS_X] + f.tremor_disp.v[AXIS_Y] + f.tremor_disp.v[AXIS_Z], 0.0, 0.0);
}

MTS_TEST(gap_becomes_gap_before)
{
  Output_Frame f;
  mts::processing_pass_through(sample(7, SAMPLE_GAP | SAMPLE_MAG_FRESH), settings(0), 0, f);
  MTS_ASSERT_EQ(f.flags, FRAME_GAP_BEFORE);
}

MTS_TEST(one_null_prediction_per_enabled_predictor)
{
  Output_Frame f;
  mts::processing_pass_through(sample(0), settings((1u << PREDICTOR_INVERSION) | (1u << PREDICTOR_WFLC)), 0, f);
  MTS_ASSERT_EQ(f.n_predictions, 2);
  MTS_ASSERT_EQ(f.predictions[0].predictor_id, PREDICTOR_INVERSION);
  MTS_ASSERT_EQ(f.predictions[1].predictor_id, PREDICTOR_WFLC);
  MTS_ASSERT_EQ(f.predictions[0].flags, 0); // forecast and residual null
  MTS_ASSERT_EQ(f.predictions[1].flags, 0);

  mts::processing_pass_through(sample(0), settings(0), 0, f);
  MTS_ASSERT_EQ(f.n_predictions, 0);
}

/* ---------------------------------------------------------------------------------------------------
 * The thread
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(no_frames_unless_running)
{
  Rig rig;
  rig.ctl.write(control(false, 1, 0));
  MTS_ASSERT(rig.processing.start());

  for (uint64_t i = 0; i < 10; i++)
  {
    MTS_ASSERT(rig.samples.push(sample(i)));
  }
  mts_time_sleep_ns(30 * MTS_TIME_NS_PER_MS);
  Server_Message m;
  MTS_ASSERT(!rig.frames.pop(m));

  // Those ten were read and dropped, not kept: once running, the first
  // frame is the first sample pushed after the switch
  rig.ctl.write(control(true, 2, 0));
  MTS_ASSERT(rig.samples.push(sample(100)));
  MTS_ASSERT(rig.frames.pop(m, MTS_TIME_NS_PER_S));
  MTS_ASSERT_EQ(m.frame.seq, 100);
}

MTS_TEST(running_turns_samples_into_frames_in_order)
{
  Rig rig;
  rig.ctl.write(control(true, 1, 1u << PREDICTOR_WFLC));
  MTS_ASSERT(rig.processing.start());

  for (uint64_t i = 0; i < 20; i++)
  {
    MTS_ASSERT(rig.samples.push(sample(i)));
  }
  for (uint64_t i = 0; i < 20; i++)
  {
    Server_Message m;
    MTS_ASSERT(rig.frames.pop(m, MTS_TIME_NS_PER_S));
    MTS_ASSERT_EQ(m.kind, SERVER_MSG_FRAME);
    MTS_ASSERT_EQ(m.frame.seq, i);
    MTS_ASSERT_EQ(m.frame.n_predictions, 1);
  }
}

MTS_TEST(control_applies_from_the_next_sample)
{
  Rig rig;
  rig.ctl.write(control(true, 1, 1u << PREDICTOR_WFLC));
  MTS_ASSERT(rig.processing.start());

  MTS_ASSERT(rig.samples.push(sample(0)));
  Server_Message m;
  MTS_ASSERT(rig.frames.pop(m, MTS_TIME_NS_PER_S));
  MTS_ASSERT_EQ(m.frame.n_predictions, 1);

  // New settings, then a sample: that sample already uses them
  rig.ctl.write(control(true, 2, (1u << PREDICTOR_INVERSION) | (1u << PREDICTOR_HARMONIC) | (1u << PREDICTOR_WFLC)));
  MTS_ASSERT(rig.samples.push(sample(1)));
  MTS_ASSERT(rig.frames.pop(m, MTS_TIME_NS_PER_S));
  MTS_ASSERT_EQ(m.frame.n_predictions, 3);

  // Stopped: the next samples give nothing
  rig.ctl.write(control(false, 3, 0));
  MTS_ASSERT(rig.samples.push(sample(2)));
  MTS_ASSERT(rig.samples.push(sample(3)));
  mts_time_sleep_ns(30 * MTS_TIME_NS_PER_MS);
  MTS_ASSERT(!rig.frames.pop(m));
}

MTS_TEST(led_is_set_off_at_start)
{
  Rig rig;
  rig.ctl.write(control(false, 1, 0));
  MTS_ASSERT(rig.processing.start());
  Led_Command cmd;
  MTS_ASSERT(wait_until([&] { return rig.led.read(cmd); }));
  MTS_ASSERT_EQ(cmd.mode, LED_MODE_OFF);
}

MTS_TEST(stop_is_quick_while_waiting_for_samples)
{
  Rig rig;
  rig.ctl.write(control(true, 1, 0));
  MTS_ASSERT(rig.processing.start());
  mts_time_sleep_ns(5 * MTS_TIME_NS_PER_MS);

  const int64_t t0 = mts_time_now_ns();
  rig.processing.stop();
  MTS_ASSERT_LT(mts_time_now_ns() - t0, mts::PROCESSING_POLL_NS + 100 * MTS_TIME_NS_PER_MS);
  rig.processing.stop(); // twice is fine
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(frame_carries_the_sample),
  MTS_TEST_ENTRY(gravity_is_removed_from_z_only),
  MTS_TEST_ENTRY(unknowns_are_honest),
  MTS_TEST_ENTRY(gap_becomes_gap_before),
  MTS_TEST_ENTRY(one_null_prediction_per_enabled_predictor),
  MTS_TEST_ENTRY(no_frames_unless_running),
  MTS_TEST_ENTRY(running_turns_samples_into_frames_in_order),
  MTS_TEST_ENTRY(control_applies_from_the_next_sample),
  MTS_TEST_ENTRY(led_is_set_off_at_start),
  MTS_TEST_ENTRY(stop_is_quick_while_waiting_for_samples),
};

MTS_TEST_MAIN(tests)
