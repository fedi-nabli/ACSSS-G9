/* ===================================================================================================
 * tests/app/server/server_tests.cc
 *
 * The Server on its own thread, over real loopback sockets. The test thread
 * plays every other part: it produces messages and replies (Processing and
 * the orchestrator), consumes commands (the orchestrator), and drives
 * blocking WebSocket clients (the UI).
 *
 * Timing: the server flushes every 30 ms. Assertions wait for things to
 * happen (with timeouts), never for a fixed time to pass.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <common/ws_client.hpp>
#include <app/server/server.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <cmath>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>

namespace
{

using mts::test::Ws_Client;

// An arbitrary monotonic instant: the Hat opened here
constexpr int64_t ORIGIN_NS = 5 * MTS_TIME_NS_PER_S;

mts::Server_Config config()
{
  mts::Server_Config cfg;
  cfg.bind_addr = "127.0.0.1";
  cfg.port = 0;
  return cfg;
}

// Queues first: they must outlive the server, so they are built before it
// and destroyed after it
struct Rig
{
  mts::Queue<Server_Message> messages;
  mts::Queue<Status_Message> status;
  mts::Queue<Command> commands;
  mts::Queue<Command_Reply> replies;
  mts::Server server;

  explicit Rig(size_t message_capacity = 64, size_t command_capacity = 16)
    : messages(message_capacity)
    , status(16)
    , commands(command_capacity)
    , replies(16)
    , server(config(), ORIGIN_NS, messages, status, commands, replies)
  {
    // Connections are logged at info keep the test output readable
    mts_log_set_min_level(LEVEL_WARN);
  }
};

// Runs fn with the log going nowhere: for failures the test expects. The
// sink is never closed: a server thread may still hold the stream pointer
void silenced(const std::function<void()>& fn)
{
  static FILE* sink = std::fopen("/dev/null", "w");
  mts_log_set_stream(sink);
  fn();
  mts_log_set_stream(nullptr);
}

// Polls pred every millisecond until it holds or timeout_ns passes
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

Server_Message frame_at(int64_t t_ns, uint64_t seq)
{
  Server_Message m;
  std::memset(&m, 0, sizeof(m));
  m.kind = SERVER_MSG_FRAME;
  m.frame.seq = seq;
  m.frame.t_ns = t_ns;
  m.frame.orientation = { { 1.0, 0.0, 0.0, 0.0 } };
  return m;
}

Status_Message make_status(uint32_t state)
{
  Status_Message st;
  std::memset(&st, 0, sizeof(st));
  st.state = state;
  st.calibration_age_s = NAN;
  st.sample_rate_hz = 100.0;
  st.horizon_ns = 30 * MTS_TIME_NS_PER_MS;
  st.metrics_hz = 2.0;
  st.spectrum_hz = 5.0;
  st.enabled_mask = 1u << PREDICTOR_WFLC;
  st.active_predictor_id = PREDICTOR_WFLC;
  st.drops_processing_to_server = 12345; // must be overwritten by the server
  return st;
}

Command_Reply reply(uint32_t client, uint32_t id, bool has_id, uint32_t error, const char* msg)
{
  Command_Reply r;
  std::memset(&r, 0, sizeof(r));
  r.client = client;
  r.id = id;
  r.has_id = has_id ? 1 : 0;
  r.error = error;
  std::snprintf(r.msg, sizeof(r.msg), "%s", msg);
  return r;
}

std::string command_line(uint32_t id, const char* command)
{
  return "{\"type\":\"command\",\"id\":" + std::to_string(id) + ",\"command\":\"" + command + "\"}";
}

bool contains(const std::string& s, const std::string& needle)
{
  return s.find(needle) != std::string::npos;
}

} // namespace

/* ---------------------------------------------------------------------------------------------------
 * Lifecycle
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(start_reports_port_and_stop_joins)
{
  Rig rig;
  MTS_ASSERT_EQ(rig.server.port(), 0);
  MTS_ASSERT(rig.server.start());
  MTS_ASSERT_NE(rig.server.port(), 0);

  // A second start is refused, the running one keeps going
  bool again = true;
  silenced([&] { again = rig.server.start(); });
  MTS_ASSERT(!again);

  const int64_t t0 = mts_time_now_ns();
  rig.server.stop();
  MTS_ASSERT_LT(mts_time_now_ns() - t0, 500 * MTS_TIME_NS_PER_MS);

  rig.server.stop(); // twice is fine
}

MTS_TEST(start_fails_on_busy_port)
{
  Rig first;
  MTS_ASSERT(first.server.start());

  mts::Server_Config cfg = config();
  cfg.port = first.server.port();
  mts::Queue<Server_Message> messages(4);
  mts::Queue<Status_Message> status(4);
  mts::Queue<Command> commands(4);
  mts::Queue<Command_Reply> replies(4);
  mts::Server second(cfg, ORIGIN_NS, messages, status, commands, replies);

  bool started = true;
  silenced([&] { started = second.start(); }); // the failure is logged: expected here
  MTS_ASSERT(!started);
}

MTS_TEST(stop_closes_connected_clients)
{
  Rig rig;
  MTS_ASSERT(rig.server.start());
  Ws_Client c;
  MTS_ASSERT(c.connect(rig.server.port()));

  const int64_t t0 = mts_time_now_ns();
  rig.server.stop();
  MTS_ASSERT_LT(mts_time_now_ns() - t0, 500 * MTS_TIME_NS_PER_MS);

  // A proper close frame, not just a dropped socket
  MTS_ASSERT(c.wait_closed());
  MTS_ASSERT_EQ(c.close_code(), mts::WS_CLOSE_NORMAL);
}

/* ---------------------------------------------------------------------------------------------------
 * Processing -> UI
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(frame_reaches_client_with_relative_timestamp)
{
  Rig rig;
  MTS_ASSERT(rig.server.start());
  Ws_Client c;
  MTS_ASSERT(c.connect(rig.server.port()));

  MTS_ASSERT(rig.messages.try_push(frame_at(ORIGIN_NS + 5 * MTS_TIME_NS_PER_MS, 7)));

  std::string line;
  MTS_ASSERT(c.read_line(line));
  MTS_ASSERT(contains(line, "\"type\":\"frame\""));
  MTS_ASSERT(contains(line, "\"sequence\":7"));
  MTS_ASSERT(contains(line, "\"timestamp_ns\":5000000,"));
}

MTS_TEST(frames_are_batched)
{
  Rig rig;
  MTS_ASSERT(rig.server.start());
  Ws_Client c;
  MTS_ASSERT(c.connect(rig.server.port()));

  // Pushed within microseconds: one flush (two if it lands in between)
  const int n = 5;
  for (int i = 0; i < n; i++)
  {
    MTS_ASSERT(rig.messages.try_push(frame_at(ORIGIN_NS + i * 10 * MTS_TIME_NS_PER_MS, (uint64_t)i)));
  }

  int lines = 0;
  int messages = 0;
  while (lines < n)
  {
    std::string msg;
    MTS_ASSERT(c.read_message(msg));
    messages++;
    for (char ch : msg)
    {
      lines += ch == '\n';
    }
  }
  MTS_ASSERT_EQ(lines, n);
  MTS_ASSERT_LE(messages, 2);
}

MTS_TEST(late_client_gets_cached_status_with_real_drop_count)
{
  // A tiny queue, flooded with nobody connected: most frames are dropped
  Rig rig(4);
  MTS_ASSERT(rig.server.start());
  for (int i = 0; i < 200; i++)
  {
    rig.messages.try_push(frame_at(ORIGIN_NS, (uint64_t)i));
  }
  const uint64_t dropped = rig.messages.dropped();
  MTS_ASSERT_GT(dropped, 0);

  // Drained even with no client: room for the status again
  MTS_ASSERT(wait_until([&] { return rig.messages.count() == 0; }));
  MTS_ASSERT(rig.status.try_push(make_status(APP_STATE_READY)));
  MTS_ASSERT(wait_until([&] { return rig.status.count() == 0; }));

  // Connects after the status went by: gets it first, without waiting for a heartbeat
  Ws_Client c;
  MTS_ASSERT(c.connect(rig.server.port()));
  std::string line;
  MTS_ASSERT(c.read_line(line));
  MTS_ASSERT(contains(line, "\"type\":\"status\""));
  MTS_ASSERT(contains(line, "\"state\":\"ready\""));
  MTS_ASSERT(contains(line, "\"processing_to_server\":" + std::to_string(dropped) + "}"));
}

MTS_TEST(status_is_also_broadcast)
{
  Rig rig;
  MTS_ASSERT(rig.server.start());
  Ws_Client a;
  Ws_Client b;
  MTS_ASSERT(a.connect(rig.server.port()));
  MTS_ASSERT(b.connect(rig.server.port()));

  MTS_ASSERT(rig.status.try_push(make_status(APP_STATE_RUNNING)));
  std::string line;
  MTS_ASSERT(a.read_line_containing("\"state\":\"running\"", line));
  MTS_ASSERT(b.read_line_containing("\"state\":\"running\"", line));
}

MTS_TEST(status_leads_the_frames_of_its_flush)
{
  Rig rig;
  MTS_ASSERT(rig.server.start());
  Ws_Client c;
  MTS_ASSERT(c.connect(rig.server.port()));

  // Frame pushed first, status second. Same flush: the status comes out
  // first. A flush in between: the frame alone, then the status
  MTS_ASSERT(rig.messages.try_push(frame_at(ORIGIN_NS, 1)));
  MTS_ASSERT(rig.status.try_push(make_status(APP_STATE_RUNNING)));

  std::string msg;
  MTS_ASSERT(c.read_message(msg));
  const size_t frame = msg.find("\"type\":\"frame\"");
  const size_t status = msg.find("\"type\":\"status\"");
  MTS_ASSERT(frame != std::string::npos);
  if (status != std::string::npos)
  {
    MTS_ASSERT_LT(status, frame);
  }
  else
  {
    MTS_ASSERT(c.read_message(msg));
    MTS_ASSERT(contains(msg, "\"type\":\"status\""));
  }
}

/* ---------------------------------------------------------------------------------------------------
 * UI -> orchestrator, and back
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(command_is_queued_with_its_client)
{
  Rig rig;
  MTS_ASSERT(rig.server.start());
  Ws_Client a;
  Ws_Client b;
  MTS_ASSERT(a.connect(rig.server.port()));
  MTS_ASSERT(b.connect(rig.server.port()));

  MTS_ASSERT(a.send_text(command_line(7, "start")));
  Command first;
  MTS_ASSERT(rig.commands.pop(first, 2 * MTS_TIME_NS_PER_S));
  MTS_ASSERT_EQ(first.kind, CMD_START);
  MTS_ASSERT_EQ(first.id, 7);
  MTS_ASSERT_NE(first.client, 0);

  MTS_ASSERT(b.send_text(command_line(7, "stop")));
  Command second;
  MTS_ASSERT(rig.commands.pop(second, 2 * MTS_TIME_NS_PER_S));
  MTS_ASSERT_EQ(second.kind, CMD_STOP);
  MTS_ASSERT_NE(second.client, 0);

  // Same id from two UIs: only the client tells them apart
  MTS_ASSERT_NE(first.client, second.client);
}

MTS_TEST(malformed_command_is_answered_not_queued)
{
  Rig rig;
  MTS_ASSERT(rig.server.start());
  Ws_Client c;
  MTS_ASSERT(c.connect(rig.server.port()));

  MTS_ASSERT(c.send_text("this is not json"));
  std::string line;
  MTS_ASSERT(c.read_line_containing("\"type\":\"error\"", line));
  MTS_ASSERT(contains(line, "\"code\":\"malformed_message\""));

  MTS_ASSERT(c.send_text("{\"type\":\"command\",\"id\":3,\"command\":\"dance\"}"));
  MTS_ASSERT(c.read_line_containing("\"type\":\"error\"", line));
  MTS_ASSERT(contains(line, "\"id\":3"));
  MTS_ASSERT(contains(line, "\"code\":\"unknown_command\""));

  MTS_ASSERT_EQ(rig.commands.count(), 0);
}

MTS_TEST(full_command_queue_answers_busy)
{
  // Capacity 2, and nobody pops: the third command has no room
  Rig rig(64, 2);
  MTS_ASSERT(rig.server.start());
  Ws_Client c;
  MTS_ASSERT(c.connect(rig.server.port()));

  for (uint32_t id = 1; id <= 3; id++)
  {
    MTS_ASSERT(c.send_text(command_line(id, "start")));
  }
  std::string line;
  MTS_ASSERT(c.read_line_containing("\"type\":\"error\"", line));
  MTS_ASSERT(contains(line, "\"id\":3"));
  MTS_ASSERT(contains(line, "\"code\":\"busy\""));
  MTS_ASSERT_EQ(rig.commands.count(), 2);
}

MTS_TEST(reply_goes_to_its_client_only)
{
  Rig rig;
  MTS_ASSERT(rig.server.start());
  Ws_Client a;
  Ws_Client b;
  MTS_ASSERT(a.connect(rig.server.port()));
  MTS_ASSERT(b.connect(rig.server.port()));

  MTS_ASSERT(a.send_text(command_line(1, "calibrate")));
  Command cmd;
  MTS_ASSERT(rig.commands.pop(cmd, 2 * MTS_TIME_NS_PER_S));

  // The orchestrator copies the client into the reply
  MTS_ASSERT(rig.replies.try_push(reply(cmd.client, cmd.id, true, ERR_NONE, "")));
  std::string line;
  MTS_ASSERT(a.read_line(line));
  MTS_ASSERT(line == "{\"type\":\"ack\",\"id\":1}");

  // Then one for everyone. b's first line must be this one: it never got the ack
  MTS_ASSERT(rig.replies.try_push(reply(0, 0, false, ERR_HAT_ERROR, "sensor timeout")));
  MTS_ASSERT(a.read_line(line));
  MTS_ASSERT(contains(line, "\"code\":\"hat_error\""));
  MTS_ASSERT(b.read_line(line));
  MTS_ASSERT(contains(line, "\"code\":\"hat_error\""));
  MTS_ASSERT(!contains(line, "\"id\""));
}

MTS_TEST(reply_to_departed_client_is_harmless)
{
  Rig rig;
  MTS_ASSERT(rig.server.start());
  Ws_Client a;
  Ws_Client b;
  MTS_ASSERT(a.connect(rig.server.port()));
  MTS_ASSERT(b.connect(rig.server.port()));

  MTS_ASSERT(a.send_text(command_line(9, "start")));
  Command cmd;
  MTS_ASSERT(rig.commands.pop(cmd, 2 * MTS_TIME_NS_PER_S));
  a.disconnect();

  // The answer has nowhere to go; the server carries on for b
  MTS_ASSERT(rig.replies.try_push(reply(cmd.client, cmd.id, true, ERR_NONE, "")));
  MTS_ASSERT(rig.messages.try_push(frame_at(ORIGIN_NS, 1)));
  std::string line;
  MTS_ASSERT(b.read_line(line));
  MTS_ASSERT(contains(line, "\"type\":\"frame\""));
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(start_reports_port_and_stop_joins),
  MTS_TEST_ENTRY(start_fails_on_busy_port),
  MTS_TEST_ENTRY(stop_closes_connected_clients),
  MTS_TEST_ENTRY(frame_reaches_client_with_relative_timestamp),
  MTS_TEST_ENTRY(frames_are_batched),
  MTS_TEST_ENTRY(late_client_gets_cached_status_with_real_drop_count),
  MTS_TEST_ENTRY(status_is_also_broadcast),
  MTS_TEST_ENTRY(status_leads_the_frames_of_its_flush),
  MTS_TEST_ENTRY(command_is_queued_with_its_client),
  MTS_TEST_ENTRY(malformed_command_is_answered_not_queued),
  MTS_TEST_ENTRY(full_command_queue_answers_busy),
  MTS_TEST_ENTRY(reply_goes_to_its_client_only),
  MTS_TEST_ENTRY(reply_to_departed_client_is_harmless),
};

MTS_TEST_MAIN(tests)
