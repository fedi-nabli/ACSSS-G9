/* ===================================================================================================
 * tests/app/ws/ws_server_tests.cc
 *
 * Ws_Server over real loopback sockets, single-threaded: a raw test client
 * writes bytes, the test calls server.poll(), then reads what the server
 * sent. This works without threads because the kernel completes TCP
 * connects and buffers loopback data on its own.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/ws/ws_codec.hpp>
#include <app/ws/ws_server.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <functional>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

namespace
{

const char* KEY = "dGhlIHNhbXBsZSBub25jZQ==";

/* ---------------------------------------------------------------------------------------------------
 * A minimal raw client
 * ------------------------------------------------------------------------------------------------- */

struct Client
{
  int fd = -1;
  std::string in; // bytes received, not consumed yet

  ~Client()
  {
    if (fd >= 0)
    {
      ::close(fd);
    }
  }
};

bool client_connect(Client& c, uint16_t port)
{
  c.fd = socket(AF_INET, SOCK_STREAM, 0);
  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
  if (connect(c.fd, (const sockaddr*)&addr, sizeof(addr)) != 0)
  {
    return false;
  }
  // Non-blocking reads: the test decides when to look
  return fcntl(c.fd, F_SETFL, fcntl(c.fd, F_GETFL, 0) | O_NONBLOCK) == 0;
}

void client_send(Client& c, const std::string& bytes)
{
  size_t off = 0;
  while (off < bytes.size())
  {
    ssize_t n = send(c.fd, bytes.data() + off, bytes.size() - off, 0);
    if (n > 0)
    {
      off += (size_t)n;
    }
  }
}

// Reads whatever arrived. false if the server closed the connection
bool client_read(Client& c)
{
  char buf[65536];
  for (;;)
  {
    ssize_t n = recv(c.fd, buf, sizeof(buf), 0);
    if (n > 0)
    {
      c.in.append(buf, (size_t)n);
      continue;
    }
    if (n == 0)
    {
      return false;
    }
    return errno == EAGAIN || errno == EWOULDBLOCK;
  }
}

std::string handshake_request()
{
  return std::string("GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\nConnection: Upgrade\r\n"
                     "Sec-WebSocket-Key: ")
         + KEY + "\r\nSec-WebSocket-Version: 13\r\n\r\n";
}

std::string masked(mts::Ws_Opcode op, const std::string& payload, bool fin = true)
{
  std::string out;
  mts::ws_encode_masked_frame(out, op, fin, payload.data(), payload.size(), 0x12345678);
  return out;
}

/* ---------------------------------------------------------------------------------------------------
 * Driving the server
 * ------------------------------------------------------------------------------------------------- */

// Polls until pred() holds or 1 s passes. Events accumulate in events
bool pump(mts::Ws_Server& server, std::vector<mts::Ws_Event>& events, const std::function<bool()>& pred)
{
  int64_t deadline = mts_time_now_ns() + MTS_TIME_NS_PER_S;
  while (mts_time_now_ns() < deadline)
  {
    server.poll(5, events);
    if (pred())
    {
      return true;
    }
  }
  return false;
}

size_t count(const std::vector<mts::Ws_Event>& events, mts::Ws_Event_Kind kind)
{
  size_t n = 0;
  for (const mts::Ws_Event& e : events)
  {
    if (e.kind == kind)
    {
      n++;
    }
  }
  return n;
}

// Reads the 101 response off the client. true if it is a valid one
bool read_handshake_response(mts::Ws_Server& server, std::vector<mts::Ws_Event>& events, Client& c)
{
  bool got = pump(server, events, [&] {
    client_read(c);
    return c.in.find("\r\n\r\n") != std::string::npos;
  });
  if (!got)
  {
    return false;
  }
  size_t end = c.in.find("\r\n\r\n") + 4;
  std::string response = c.in.substr(0, end);
  c.in.erase(0, end);
  return response.find("HTTP/1.1 101") == 0
         && response.find("Sec-WebSocket-Accept: " + mts::ws_accept_key(KEY)) != std::string::npos;
}

// Reads one frame the server sent
bool read_frame(mts::Ws_Server& server, std::vector<mts::Ws_Event>& events, Client& c, mts::Ws_Frame& f)
{
  return pump(server, events, [&] {
    client_read(c);
    size_t consumed = 0;
    if (mts::ws_parse_frame((const uint8_t*)c.in.data(), c.in.size(), 1 << 20, f, consumed) != mts::Ws_Parse::frame)
    {
      return false;
    }
    c.in.erase(0, consumed);
    return true;
  });
}

// Connects and completes the handshake. Returns the client id (0 on failure)
uint32_t open_client(mts::Ws_Server& server, std::vector<mts::Ws_Event>& events, Client& c)
{
  if (!client_connect(c, server.port()))
  {
    return 0;
  }
  client_send(c, handshake_request());
  size_t opens_before = count(events, mts::Ws_Event_Kind::open);
  if (!pump(server, events, [&] { return count(events, mts::Ws_Event_Kind::open) > opens_before; }))
  {
    return 0;
  }
  if (!read_handshake_response(server, events, c))
  {
    return 0;
  }
  return events.back().kind == mts::Ws_Event_Kind::open ? events.back().client : 0;
}

uint16_t close_code(const mts::Ws_Frame& f)
{
  return f.payload.size() >= 2 ? (uint16_t)(((uint8_t)f.payload[0] << 8) | (uint8_t)f.payload[1]) : 0;
}

// Server with its info/warn logs out of the test output
struct Quiet_Server
{
  FILE* sink;
  mts::Ws_Server server;

  explicit Quiet_Server(const mts::Ws_Limits& limits = mts::Ws_Limits{})
    : sink(std::tmpfile())
    , server(limits)
  {
    mts_log_set_stream(sink);
  }

  ~Quiet_Server()
  {
    mts_log_set_stream(nullptr);
    std::fclose(sink);
  }
};

} // namespace

/* ---------------------------------------------------------------------------------------------------
 * Tests
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(listen_on_free_port)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  MTS_ASSERT_NE(q.server.port(), 0);
  MTS_ASSERT(!q.server.listen("127.0.0.1", 0)); // only once
}

MTS_TEST(listen_rejects_bad_address)
{
  Quiet_Server q;
  MTS_ASSERT(!q.server.listen("not-an-ip", 0));
}

MTS_TEST(handshake_then_open_event)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;

  uint32_t id = open_client(q.server, events, c);
  MTS_ASSERT_NE(id, 0);
  MTS_ASSERT_EQ(q.server.open_count(), 1);
}

MTS_TEST(text_both_directions)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  uint32_t id = open_client(q.server, events, c);
  MTS_ASSERT_NE(id, 0);

  // Client -> server
  client_send(c, masked(mts::Ws_Opcode::text, "{\"cmd\":\"start\"}"));
  MTS_ASSERT(pump(q.server, events, [&] { return count(events, mts::Ws_Event_Kind::text) == 1; }));
  MTS_ASSERT(events.back().text == "{\"cmd\":\"start\"}");
  MTS_ASSERT_EQ(events.back().client, id);

  // Server -> client: unmasked text frame
  MTS_ASSERT(q.server.send_text(id, "frame 1", 7));
  mts::Ws_Frame f;
  MTS_ASSERT(read_frame(q.server, events, c, f));
  MTS_ASSERT(f.opcode == mts::Ws_Opcode::text && f.fin && !f.masked);
  MTS_ASSERT(f.payload == "frame 1");
}

MTS_TEST(broadcast_reaches_every_client)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client a;
  Client b;
  MTS_ASSERT_NE(open_client(q.server, events, a), 0);
  MTS_ASSERT_NE(open_client(q.server, events, b), 0);

  MTS_ASSERT_EQ(q.server.broadcast_text("all", 3), 2);
  mts::Ws_Frame fa;
  mts::Ws_Frame fb;
  MTS_ASSERT(read_frame(q.server, events, a, fa));
  MTS_ASSERT(read_frame(q.server, events, b, fb));
  MTS_ASSERT(fa.payload == "all" && fb.payload == "all");
}

MTS_TEST(bytes_one_at_a_time)
{
  // TCP may split anything anywhere: handshake and frames fed byte by byte
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  MTS_ASSERT(client_connect(c, q.server.port()));

  std::string wire = handshake_request() + masked(mts::Ws_Opcode::text, "split") + masked(mts::Ws_Opcode::text, "again");
  for (char byte : wire)
  {
    client_send(c, std::string(1, byte));
    q.server.poll(0, events);
  }

  MTS_ASSERT(pump(q.server, events, [&] { return count(events, mts::Ws_Event_Kind::text) == 2; }));
  MTS_ASSERT_EQ(count(events, mts::Ws_Event_Kind::open), 1);
  MTS_ASSERT(events[1].text == "split");
  MTS_ASSERT(events[2].text == "again");
}

MTS_TEST(handshake_and_first_frame_in_one_packet)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  MTS_ASSERT(client_connect(c, q.server.port()));

  client_send(c, handshake_request() + masked(mts::Ws_Opcode::text, "early"));
  MTS_ASSERT(pump(q.server, events, [&] { return count(events, mts::Ws_Event_Kind::text) == 1; }));
  MTS_ASSERT(events.back().text == "early");
}

MTS_TEST(fragmented_message_reassembled)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  MTS_ASSERT_NE(open_client(q.server, events, c), 0);

  // A ping in the middle of a fragmented message is allowed
  client_send(c, masked(mts::Ws_Opcode::text, "Hel", false) + masked(mts::Ws_Opcode::ping, "p")
                   + masked(mts::Ws_Opcode::continuation, "lo, ", false)
                   + masked(mts::Ws_Opcode::continuation, "world", true));

  MTS_ASSERT(pump(q.server, events, [&] { return count(events, mts::Ws_Event_Kind::text) == 1; }));
  MTS_ASSERT(events.back().text == "Hello, world");

  mts::Ws_Frame f;
  MTS_ASSERT(read_frame(q.server, events, c, f));
  MTS_ASSERT(f.opcode == mts::Ws_Opcode::pong && f.payload == "p");
}

MTS_TEST(ping_gets_pong_with_payload)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  MTS_ASSERT_NE(open_client(q.server, events, c), 0);

  client_send(c, masked(mts::Ws_Opcode::ping, "are you there"));
  mts::Ws_Frame f;
  MTS_ASSERT(read_frame(q.server, events, c, f));
  MTS_ASSERT(f.opcode == mts::Ws_Opcode::pong);
  MTS_ASSERT(f.payload == "are you there");
}

MTS_TEST(client_close_is_echoed)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  uint32_t id = open_client(q.server, events, c);
  MTS_ASSERT_NE(id, 0);

  client_send(c, masked(mts::Ws_Opcode::close, mts::ws_close_payload(mts::WS_CLOSE_NORMAL)));
  mts::Ws_Frame f;
  MTS_ASSERT(read_frame(q.server, events, c, f));
  MTS_ASSERT(f.opcode == mts::Ws_Opcode::close);
  MTS_ASSERT_EQ(close_code(f), mts::WS_CLOSE_NORMAL);

  MTS_ASSERT(pump(q.server, events, [&] { return count(events, mts::Ws_Event_Kind::close) == 1; }));
  MTS_ASSERT_EQ(events.back().client, id);
  MTS_ASSERT_EQ(q.server.open_count(), 0);
  // Then the socket is closed
  MTS_ASSERT(pump(q.server, events, [&] { return !client_read(c); }));
}

MTS_TEST(server_close)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  uint32_t id = open_client(q.server, events, c);
  MTS_ASSERT_NE(id, 0);

  q.server.close(id);
  mts::Ws_Frame f;
  MTS_ASSERT(read_frame(q.server, events, c, f));
  MTS_ASSERT(f.opcode == mts::Ws_Opcode::close);
  MTS_ASSERT_EQ(close_code(f), mts::WS_CLOSE_NORMAL);
  MTS_ASSERT(pump(q.server, events, [&] { return count(events, mts::Ws_Event_Kind::close) == 1; }));
  MTS_ASSERT(!q.server.send_text(id, "late", 4));
}

MTS_TEST(abrupt_disconnect_gives_close_event)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  uint32_t id = 0;
  {
    Client c;
    id = open_client(q.server, events, c);
    MTS_ASSERT_NE(id, 0);
  } // socket closed without a close frame

  MTS_ASSERT(pump(q.server, events, [&] { return count(events, mts::Ws_Event_Kind::close) == 1; }));
  MTS_ASSERT_EQ(events.back().client, id);
}

MTS_TEST(unmasked_client_frame_closes_1002)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  MTS_ASSERT_NE(open_client(q.server, events, c), 0);

  std::string unmasked;
  mts::ws_encode_frame(unmasked, mts::Ws_Opcode::text, "bad", 3);
  client_send(c, unmasked);

  mts::Ws_Frame f;
  MTS_ASSERT(read_frame(q.server, events, c, f));
  MTS_ASSERT(f.opcode == mts::Ws_Opcode::close);
  MTS_ASSERT_EQ(close_code(f), mts::WS_CLOSE_PROTOCOL_ERROR);
  MTS_ASSERT_EQ(count(events, mts::Ws_Event_Kind::text), 0);
}

MTS_TEST(oversized_message_closes_1009)
{
  mts::Ws_Limits limits;
  limits.max_message_bytes = 1024;
  Quiet_Server q(limits);
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  MTS_ASSERT_NE(open_client(q.server, events, c), 0);

  client_send(c, masked(mts::Ws_Opcode::text, std::string(2000, 'x')));
  mts::Ws_Frame f;
  MTS_ASSERT(read_frame(q.server, events, c, f));
  MTS_ASSERT_EQ(close_code(f), mts::WS_CLOSE_TOO_BIG);

  // Same limit across fragments
  Client d;
  MTS_ASSERT_NE(open_client(q.server, events, d), 0);
  client_send(d, masked(mts::Ws_Opcode::text, std::string(700, 'x'), false)
                   + masked(mts::Ws_Opcode::continuation, std::string(700, 'x'), true));
  MTS_ASSERT(read_frame(q.server, events, d, f));
  MTS_ASSERT_EQ(close_code(f), mts::WS_CLOSE_TOO_BIG);
}

MTS_TEST(plain_http_gets_400)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  MTS_ASSERT(client_connect(c, q.server.port()));

  client_send(c, "GET /index.html HTTP/1.1\r\nHost: localhost\r\n\r\n");
  MTS_ASSERT(pump(q.server, events, [&] {
    bool open = client_read(c);
    return !open && c.in.find("\r\n\r\n") != std::string::npos;
  }));
  MTS_ASSERT(c.in.find("HTTP/1.1 400") == 0);
  MTS_ASSERT_EQ(events.size(), 0); // never opened, so no open/close events
}

MTS_TEST(clients_beyond_limit_refused)
{
  mts::Ws_Limits limits;
  limits.max_clients = 2;
  Quiet_Server q(limits);
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client a;
  Client b;
  Client c;
  MTS_ASSERT_NE(open_client(q.server, events, a), 0);
  MTS_ASSERT_NE(open_client(q.server, events, b), 0);

  MTS_ASSERT(client_connect(c, q.server.port()));
  client_send(c, handshake_request());
  MTS_ASSERT(pump(q.server, events, [&] { return !client_read(c); })); // closed by the server
  MTS_ASSERT_EQ(q.server.open_count(), 2);
}

MTS_TEST(full_send_buffer_drops_and_counts)
{
  mts::Ws_Limits limits;
  limits.send_buffer_bytes = 4096;
  Quiet_Server q(limits);
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  uint32_t id = open_client(q.server, events, c);
  MTS_ASSERT_NE(id, 0);

  // Without poll() nothing is written, so the buffer fills from user space:
  // 1000-byte messages, 4096 bytes of room -> 4 fit
  std::string msg(1000, 'm');
  size_t accepted = 0;
  for (int i = 0; i < 10; i++)
  {
    if (q.server.send_text(id, msg.data(), msg.size()))
    {
      accepted++;
    }
  }
  MTS_ASSERT_EQ(accepted, 4);
  MTS_ASSERT_EQ(q.server.dropped_messages(), 6);
  MTS_ASSERT_EQ(q.server.broadcast_text(msg.data(), msg.size()), 0);
  MTS_ASSERT_EQ(q.server.dropped_messages(), 7);

  // The client is still connected and gets the 4 that fit, in order
  for (int i = 0; i < 4; i++)
  {
    mts::Ws_Frame f;
    MTS_ASSERT(read_frame(q.server, events, c, f));
    MTS_ASSERT(f.payload == msg);
  }
  MTS_ASSERT_EQ(q.server.open_count(), 1);
  MTS_ASSERT(q.server.send_text(id, "room again", 10));
}

MTS_TEST(binary_frames_unsupported)
{
  Quiet_Server q;
  MTS_ASSERT(q.server.listen("127.0.0.1", 0));
  std::vector<mts::Ws_Event> events;
  Client c;
  MTS_ASSERT_NE(open_client(q.server, events, c), 0);

  client_send(c, masked(mts::Ws_Opcode::binary, "\x01\x02"));
  mts::Ws_Frame f;
  MTS_ASSERT(read_frame(q.server, events, c, f));
  MTS_ASSERT_EQ(close_code(f), mts::WS_CLOSE_UNSUPPORTED);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(listen_on_free_port),
  MTS_TEST_ENTRY(listen_rejects_bad_address),
  MTS_TEST_ENTRY(handshake_then_open_event),
  MTS_TEST_ENTRY(text_both_directions),
  MTS_TEST_ENTRY(broadcast_reaches_every_client),
  MTS_TEST_ENTRY(bytes_one_at_a_time),
  MTS_TEST_ENTRY(handshake_and_first_frame_in_one_packet),
  MTS_TEST_ENTRY(fragmented_message_reassembled),
  MTS_TEST_ENTRY(ping_gets_pong_with_payload),
  MTS_TEST_ENTRY(client_close_is_echoed),
  MTS_TEST_ENTRY(server_close),
  MTS_TEST_ENTRY(abrupt_disconnect_gives_close_event),
  MTS_TEST_ENTRY(unmasked_client_frame_closes_1002),
  MTS_TEST_ENTRY(oversized_message_closes_1009),
  MTS_TEST_ENTRY(plain_http_gets_400),
  MTS_TEST_ENTRY(clients_beyond_limit_refused),
  MTS_TEST_ENTRY(full_send_buffer_drops_and_counts),
  MTS_TEST_ENTRY(binary_frames_unsupported),
};

MTS_TEST_MAIN(tests)

