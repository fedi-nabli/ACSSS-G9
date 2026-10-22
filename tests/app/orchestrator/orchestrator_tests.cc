/* ===================================================================================================
 * tests/app/orchestrator/orchestrator_tests.cc
 *
 * The state machine, deterministically: time is a number the test moves.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/orchestrator/orchestrator.hpp>

#include <cmath>
#include <cstdio>
#include <cstring>

namespace
{

constexpr int64_t MS = MTS_TIME_NS_PER_MS;
constexpr int64_t S = MTS_TIME_NS_PER_S;
constexpr int64_t T0 = 1000 * S; // an arbitrary monotonic start
constexpr uint32_t CLIENT = 3;

Command command(uint32_t kind, uint32_t id = 1)
{
  Command c;
  std::memset(&c, 0, sizeof(c));
  c.kind = kind;
  c.id = id;
  c.client = CLIENT;
  return c;
}

uint32_t bit(Predictor_Id id)
{
  return UINT32_C(1) << id;
}

// An orchestrator and its clock
struct Rig
{
  int64_t now = T0;
  mts::Orchestrator orch{ mts::Orchestrator_Config{}, 100.0, T0 };

  void advance(int64_t dt)
  {
    now += dt;
    orch.tick(now);
  }

  uint32_t send(uint32_t kind)
  {
    return orch.handle(command(kind), now).error;
  }
};

// Drives a fresh rig into state. calibrating: the full calibration, just begun
void enter(Rig& r, uint32_t state)
{
  if (state == APP_STATE_BOOT)
  {
    return;
  }
  r.advance(0); // boot -> needs_calibration
  if (state == APP_STATE_NEEDS_CALIBRATION)
  {
    return;
  }
  r.send(CMD_CALIBRATE);
  if (state == APP_STATE_CALIBRATING)
  {
    return;
  }
  r.advance(5 * S); // hold_still 2 s + rotate 3 s
  if (state == APP_STATE_READY)
  {
    return;
  }
  r.send(CMD_START);
  r.advance(500 * MS); // quick zero
  if (state == APP_STATE_RUNNING)
  {
    return;
  }
  r.send(CMD_STOP);
}

} // namespace

/* ---------------------------------------------------------------------------------------------------
 * The contract's table
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(rig_reaches_every_state)
{
  const uint32_t states[] = { APP_STATE_BOOT,        APP_STATE_NEEDS_CALIBRATION, APP_STATE_READY,
                              APP_STATE_CALIBRATING, APP_STATE_RUNNING,           APP_STATE_STOPPED };
  for (uint32_t st : states)
  {
    Rig r;
    enter(r, st);
    MTS_ASSERT_EQ(r.orch.state(), st);
  }
}

MTS_TEST(every_cell_of_the_state_table)
{
  // Reply per (state, command), and the state right after it
  struct Cell
  {
    uint32_t state;
    uint32_t kind;
    uint32_t error;
    uint32_t then;
  };
  const uint32_t B = APP_STATE_BOOT, N = APP_STATE_NEEDS_CALIBRATION, R = APP_STATE_READY,
                 C = APP_STATE_CALIBRATING, U = APP_STATE_RUNNING, P = APP_STATE_STOPPED;
  const Cell cells[] = {
    { B, CMD_START, ERR_INVALID_STATE, B },     { B, CMD_STOP, ERR_INVALID_STATE, B },
    { B, CMD_CALIBRATE, ERR_INVALID_STATE, B }, { B, CMD_SETUP, ERR_INVALID_STATE, B },

    { N, CMD_START, ERR_NEEDS_CALIBRATION, N }, { N, CMD_STOP, ERR_INVALID_STATE, N },
    { N, CMD_CALIBRATE, ERR_NONE, C },          { N, CMD_SETUP, ERR_NONE, N },

    { R, CMD_START, ERR_NONE, C },              { R, CMD_STOP, ERR_INVALID_STATE, R },
    { R, CMD_CALIBRATE, ERR_NONE, C },          { R, CMD_SETUP, ERR_NONE, R },

    { C, CMD_START, ERR_BUSY, C },              { C, CMD_STOP, ERR_BUSY, C },
    { C, CMD_CALIBRATE, ERR_BUSY, C },          { C, CMD_SETUP, ERR_BUSY, C },

    { U, CMD_START, ERR_INVALID_STATE, U },     { U, CMD_STOP, ERR_NONE, P },
    { U, CMD_CALIBRATE, ERR_INVALID_STATE, U }, { U, CMD_SETUP, ERR_NONE, U },

    { P, CMD_START, ERR_NONE, C },              { P, CMD_STOP, ERR_INVALID_STATE, P },
    { P, CMD_CALIBRATE, ERR_NONE, C },          { P, CMD_SETUP, ERR_NONE, P },
  };
  MTS_ASSERT_EQ(sizeof(cells) / sizeof(cells[0]), 24);

  for (const Cell& cell : cells)
  {
    Rig r;
    enter(r, cell.state);
    Command_Reply reply = r.orch.handle(command(cell.kind, 77), r.now);
    if (reply.error != cell.error || r.orch.state() != cell.then)
    {
      std::printf("    state %u, command %u: error %u (want %u), then %u (want %u)\n", cell.state, cell.kind,
                  reply.error, cell.error, r.orch.state(), cell.then);
    }
    MTS_ASSERT_EQ(reply.error, cell.error);
    MTS_ASSERT_EQ(r.orch.state(), cell.then);

    // Every reply answers its command, to its client, and errors explain themselves
    MTS_ASSERT_EQ(reply.id, 77);
    MTS_ASSERT_EQ(reply.has_id, 1);
    MTS_ASSERT_EQ(reply.client, CLIENT);
    MTS_ASSERT_EQ(reply.error == ERR_NONE, reply.msg[0] == '\0');
  }
}

MTS_TEST(unknown_command_kind)
{
  Rig r;
  enter(r, APP_STATE_READY);
  MTS_ASSERT_EQ(r.orch.handle(command(99), r.now).error, ERR_UNKNOWN_COMMAND);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_READY);
}

/* ---------------------------------------------------------------------------------------------------
 * Calibration timelines
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(full_calibration_walks_its_steps)
{
  Rig r;
  enter(r, APP_STATE_NEEDS_CALIBRATION);
  Status_Message st;
  r.orch.take_status(st, r.now);
  MTS_ASSERT(std::isnan(st.calibration_age_s)); // none yet

  MTS_ASSERT_EQ(r.send(CMD_CALIBRATE), ERR_NONE);
  MTS_ASSERT(r.orch.take_status(st, r.now));
  MTS_ASSERT_EQ(st.state, APP_STATE_CALIBRATING);
  MTS_ASSERT_EQ(st.calibration_step, CAL_STEP_HOLD_STILL);
  MTS_ASSERT_NEAR(st.calibration_progress, 0.0, 1e-9);

  r.advance(1 * S);
  MTS_ASSERT(r.orch.take_status(st, r.now));
  MTS_ASSERT_EQ(st.calibration_step, CAL_STEP_HOLD_STILL);
  MTS_ASSERT_NEAR(st.calibration_progress, 0.5, 1e-9);

  r.advance(1 * S);
  MTS_ASSERT(r.orch.take_status(st, r.now));
  MTS_ASSERT_EQ(st.calibration_step, CAL_STEP_ROTATE);
  MTS_ASSERT_NEAR(st.calibration_progress, 0.0, 1e-9);

  r.advance(1500 * MS);
  MTS_ASSERT(r.orch.take_status(st, r.now));
  MTS_ASSERT_EQ(st.calibration_step, CAL_STEP_ROTATE);
  MTS_ASSERT_NEAR(st.calibration_progress, 0.5, 1e-9);

  r.advance(1499 * MS);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_CALIBRATING);
  r.advance(1 * MS);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_READY);

  MTS_ASSERT(r.orch.take_status(st, r.now));
  MTS_ASSERT_EQ(st.state, APP_STATE_READY);
  MTS_ASSERT_EQ(st.calibration_step, CAL_STEP_NONE);
  MTS_ASSERT_NEAR(st.calibration_age_s, 0.0, 1e-9);

  r.advance(12 * S);
  MTS_ASSERT(r.orch.take_status(st, r.now)); // heartbeat
  MTS_ASSERT_NEAR(st.calibration_age_s, 12.0, 1e-9);
}

MTS_TEST(late_tick_does_not_stretch_the_steps)
{
  // One tick far past both steps: straight to ready, the calibration dated
  // when it really ended
  Rig r;
  enter(r, APP_STATE_NEEDS_CALIBRATION);
  r.send(CMD_CALIBRATE);
  r.advance(7 * S);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_READY);
  Status_Message st;
  MTS_ASSERT(r.orch.take_status(st, r.now));
  MTS_ASSERT_NEAR(st.calibration_age_s, 2.0, 1e-9);
}

MTS_TEST(start_runs_the_quick_zero_then_runs)
{
  Rig r;
  enter(r, APP_STATE_READY);
  const Pipeline_Control before = r.orch.control();
  MTS_ASSERT_EQ(before.running, 0);

  MTS_ASSERT_EQ(r.send(CMD_START), ERR_NONE);
  Status_Message st;
  MTS_ASSERT(r.orch.take_status(st, r.now));
  MTS_ASSERT_EQ(st.calibration_step, CAL_STEP_QUICK_ZERO);
  MTS_ASSERT_EQ(r.orch.control().running, 0); // not yet: no frames during the quick zero

  r.advance(499 * MS);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_CALIBRATING);
  r.advance(1 * MS);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_RUNNING);

  const Pipeline_Control running = r.orch.control();
  MTS_ASSERT_EQ(running.running, 1);
  MTS_ASSERT_NE(running.cfg_gen, before.cfg_gen);

  MTS_ASSERT_EQ(r.send(CMD_STOP), ERR_NONE);
  const Pipeline_Control stopped = r.orch.control();
  MTS_ASSERT_EQ(stopped.running, 0);
  MTS_ASSERT_NE(stopped.cfg_gen, running.cfg_gen);
}

/* ---------------------------------------------------------------------------------------------------
 * setup
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(setup_changes_only_what_it_carries)
{
  Rig r;
  enter(r, APP_STATE_READY);
  const Pipeline_Control before = r.orch.control();
  Status_Message st;
  r.orch.take_status(st, r.now);

  Command c = command(CMD_SETUP);
  c.set_mask = SETTING_HORIZON;
  c.settings.horizon_ns = 50 * MS;
  c.settings.metrics_hz = 99.0; // not flagged: ignored
  MTS_ASSERT_EQ(r.orch.handle(c, r.now).error, ERR_NONE);

  const Pipeline_Control after = r.orch.control();
  MTS_ASSERT_EQ(after.settings.horizon_ns, 50 * MS);
  MTS_ASSERT_NEAR(after.settings.metrics_hz, before.settings.metrics_hz, 0.0);
  MTS_ASSERT_EQ(after.settings.enabled_mask, before.settings.enabled_mask);
  MTS_ASSERT_NE(after.cfg_gen, before.cfg_gen);

  // Shows in the next status, right away
  MTS_ASSERT(r.orch.take_status(st, r.now));
  MTS_ASSERT_EQ(st.horizon_ns, 50 * MS);
}

MTS_TEST(setup_without_change_keeps_the_generation)
{
  Rig r;
  enter(r, APP_STATE_READY);
  const uint32_t gen = r.orch.control().cfg_gen;
  Command c = command(CMD_SETUP);
  c.set_mask = SETTING_ACTIVE_PREDICTOR;
  c.settings.active_predictor_id = PREDICTOR_WFLC; // already active
  MTS_ASSERT_EQ(r.orch.handle(c, r.now).error, ERR_NONE);
  MTS_ASSERT_EQ(r.orch.control().cfg_gen, gen); // predictors are not reset for nothing
}

MTS_TEST(setup_active_must_be_enabled)
{
  Rig r;
  enter(r, APP_STATE_RUNNING);
  const Pipeline_Control before = r.orch.control();

  // Active outside the (unchanged) enabled set
  Command c = command(CMD_SETUP);
  c.set_mask = SETTING_ACTIVE_PREDICTOR;
  c.settings.active_predictor_id = PREDICTOR_HARMONIC;
  MTS_ASSERT_EQ(r.orch.handle(c, r.now).error, ERR_INVALID_ARGUMENT);

  // Enabled set that drops the (unchanged) active one
  c.set_mask = SETTING_ENABLED_PREDICTORS;
  c.settings.enabled_mask = bit(PREDICTOR_INVERSION);
  MTS_ASSERT_EQ(r.orch.handle(c, r.now).error, ERR_INVALID_ARGUMENT);

  // Refused as a whole: nothing changed
  const Pipeline_Control after = r.orch.control();
  MTS_ASSERT_MEM_EQ(&before, &after, sizeof(before));

  // Both together, consistent: fine
  c.set_mask = SETTING_ENABLED_PREDICTORS | SETTING_ACTIVE_PREDICTOR;
  c.settings.enabled_mask = bit(PREDICTOR_INVERSION) | bit(PREDICTOR_HARMONIC);
  c.settings.active_predictor_id = PREDICTOR_HARMONIC;
  MTS_ASSERT_EQ(r.orch.handle(c, r.now).error, ERR_NONE);
  MTS_ASSERT_EQ(r.orch.control().settings.active_predictor_id, PREDICTOR_HARMONIC);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_RUNNING);
}

/* ---------------------------------------------------------------------------------------------------
 * Status timing
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(status_on_change_and_heartbeat)
{
  Rig r;
  Status_Message st;
  MTS_ASSERT(r.orch.take_status(st, r.now)); // boot: there is something to say
  MTS_ASSERT_EQ(st.state, APP_STATE_BOOT);
  MTS_ASSERT(!r.orch.take_status(st, r.now));

  r.advance(0);
  MTS_ASSERT(r.orch.take_status(st, r.now)); // change
  MTS_ASSERT_EQ(st.state, APP_STATE_NEEDS_CALIBRATION);
  MTS_ASSERT_NEAR(st.sample_rate_hz, 100.0, 0.0);

  r.advance(999 * MS);
  MTS_ASSERT(!r.orch.take_status(st, r.now));
  r.advance(1 * MS);
  MTS_ASSERT(r.orch.take_status(st, r.now)); // heartbeat
  MTS_ASSERT(!r.orch.take_status(st, r.now));
}

MTS_TEST(status_at_ten_hz_while_calibrating)
{
  Rig r;
  enter(r, APP_STATE_NEEDS_CALIBRATION);
  Status_Message st;
  r.orch.take_status(st, r.now);
  r.send(CMD_CALIBRATE);
  MTS_ASSERT(r.orch.take_status(st, r.now));

  int sent = 0;
  for (int i = 0; i < 100; i++) // 1 s in 10 ms ticks
  {
    r.advance(10 * MS);
    sent += r.orch.take_status(st, r.now) ? 1 : 0;
  }
  MTS_ASSERT_EQ(sent, 10);
}

/* ---------------------------------------------------------------------------------------------------
 * The Hat failing
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(hat_failure_while_running_stops)
{
  Rig r;
  enter(r, APP_STATE_RUNNING);
  Status_Message st;
  r.orch.take_status(st, r.now);

  r.orch.hat_failed(r.now);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_STOPPED);
  MTS_ASSERT_EQ(r.orch.control().running, 0);
  MTS_ASSERT(r.orch.take_status(st, r.now));
  MTS_ASSERT_EQ(st.state, APP_STATE_STOPPED);

  // Nothing needing the sensor works any more; setup still does
  MTS_ASSERT_EQ(r.send(CMD_START), ERR_HAT_ERROR);
  MTS_ASSERT_EQ(r.send(CMD_CALIBRATE), ERR_HAT_ERROR);
  MTS_ASSERT_EQ(r.send(CMD_STOP), ERR_INVALID_STATE);
  MTS_ASSERT_EQ(r.send(CMD_SETUP), ERR_NONE);
}

MTS_TEST(hat_failure_while_calibrating_stops)
{
  Rig r;
  enter(r, APP_STATE_CALIBRATING);
  r.orch.hat_failed(r.now);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_STOPPED);
  r.advance(10 * S); // the steps do not resume
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_STOPPED);
}

MTS_TEST(hat_failure_when_idle_keeps_the_state)
{
  Rig r;
  enter(r, APP_STATE_NEEDS_CALIBRATION);
  r.orch.hat_failed(r.now);
  MTS_ASSERT_EQ(r.orch.state(), APP_STATE_NEEDS_CALIBRATION);
  MTS_ASSERT_EQ(r.send(CMD_START), ERR_NEEDS_CALIBRATION); // the table answers first
  MTS_ASSERT_EQ(r.send(CMD_CALIBRATE), ERR_HAT_ERROR);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(rig_reaches_every_state),
  MTS_TEST_ENTRY(every_cell_of_the_state_table),
  MTS_TEST_ENTRY(unknown_command_kind),
  MTS_TEST_ENTRY(full_calibration_walks_its_steps),
  MTS_TEST_ENTRY(late_tick_does_not_stretch_the_steps),
  MTS_TEST_ENTRY(start_runs_the_quick_zero_then_runs),
  MTS_TEST_ENTRY(setup_changes_only_what_it_carries),
  MTS_TEST_ENTRY(setup_without_change_keeps_the_generation),
  MTS_TEST_ENTRY(setup_active_must_be_enabled),
  MTS_TEST_ENTRY(status_on_change_and_heartbeat),
  MTS_TEST_ENTRY(status_at_ten_hz_while_calibrating),
  MTS_TEST_ENTRY(hat_failure_while_running_stops),
  MTS_TEST_ENTRY(hat_failure_while_calibrating_stops),
  MTS_TEST_ENTRY(hat_failure_when_idle_keeps_the_state),
};

MTS_TEST_MAIN(tests)
