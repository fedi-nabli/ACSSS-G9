/* ===================================================================================================
 * app/include/app/ws/ws_server.hpp
 *
 * WebSocket server transport: POSIX sockets + poll(), RFC 6455 text
 * messages. Knows nothing about our messages (that is protocol's job), so
 * a library could replace it behind this same small interface.
 *
 * Usage, from ONE thread (the server thread):
 *   Ws_Server ws;
 *   ws.listen("0.0.0.0", 8080);
 *   for (;;) {
 *     events.clear();
 *     ws.poll(time_until_next_flush_ms, events);   // accepts, reads, writes
 *     for (const Ws_Event& e : events) { ... }      // open / text / close
 *     ws.broadcast_text(batch.data(), batch.size());
 *   }
 *
 * Sending never blocks: send_text / broadcast_text only queue, poll()
 * writes whatever the socket accepts.
 *
 * Slow clients: each client has a bounded send buffer. A message that does
 * not fit is dropped and counted (the UI sees the gap through the protocol's
 * sequence numbers), the client stays connected. A client whose pending
 * data has not moved for stalled_close_ns is considered dead and closed.
 *
 * Plain HTTP requests (anything but a WebSocket upgrade) get a 400.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_WS_WS_SERVER_HPP
#define MTS_APP_WS_WS_SERVER_HPP

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace mts
{

struct Ws_Frame; // ws_codec.hpp

enum class Ws_Event_Kind
{
  open, // handshake done, the client can now send and receive
  text, // a complete text message from the client
  close, // the client is gone (closed by either side, or dropped)
};

struct Ws_Event
{
  Ws_Event_Kind kind;
  uint32_t client; // id, never reused while the server lives
  std::string text; // the message (text events only)
};

struct Ws_Limits
{
  size_t max_clients = 8; // further connections are closed right away
  size_t max_message_bytes = 64 * 1024; // larger incoming messages: close 1009
  size_t send_buffer_bytes = 1024 * 1024; // per client, pending outgoing bytes
  int64_t stalled_close_ns = 5000000000LL; // pending data not moving this long: close
};

class Ws_Server
{
public:
  explicit Ws_Server(const Ws_Limits& limits = Ws_Limits{});

  // Closes every client and the listening socket
  ~Ws_Server();

  Ws_Server(const Ws_Server&) = delete;
  Ws_Server& operator=(const Ws_Server&) = delete;

  /* Starts listening on bind_addr (IPv4, e.g. "0.0.0.0") and port.
   * Port 0 picks a free port (tests), see port(). false (logged) on failure. */
  bool listen(const char* bind_addr, uint16_t port);

  // The port actually listened on, 0 before listen()
  uint16_t port() const;

  /* Waits up to timeout_ms (0: just check, never block) for socket
   * activity, then accepts, reads, handshakes, parses, writes what it can.
   * Appends what happened to events, in order. */
  void poll(int timeout_ms, std::vector<Ws_Event>& events);

  /* Queues one text message for one open client. false: unknown or not yet
   * open client, or its send buffer is full (message dropped and counted). */
  bool send_text(uint32_t client, const char* data, size_t len);

  // Queues one text message for every open client. Returns how many took it
  size_t broadcast_text(const char* data, size_t len);

  // Sends a normal close and drops the client once it is flushed
  void close(uint32_t client);

  // Clients with a finished handshake
  size_t open_count() const;

  // Messages dropped because a send buffer was full, all clients together
  uint64_t dropped_messages() const;

private:
  enum class State
  {
    handshake, // waiting for the HTTP upgrade request
    open,
    closing, // flush what is queued, then close the socket
  };

  struct Client
  {
    int fd;
    uint32_t id;
    State state;
    std::string in; // bytes read but not parsed yet
    std::string out; // bytes queued but not written yet
    size_t out_off; // already written part of out
    int64_t waiting_since_ns; // when out last made progress (or became non-empty)
    bool in_fragment; // inside a fragmented message
    std::string fragments; // its payload so far
    bool reported_open; // an open event was emitted: emit close on removal
    bool dead; // socket gone: remove at the end of poll()
  };

  void accept_clients();
  void read_client(Client& c, std::vector<Ws_Event>& events);
  void parse_client(Client& c, std::vector<Ws_Event>& events);
  void handle_frame(Client& c, Ws_Frame& frame, std::vector<Ws_Event>& events);
  void write_client(Client& c);
  void queue(Client& c, const std::string& bytes);
  void fail(Client& c, uint16_t code);
  void remove_dead(std::vector<Ws_Event>& events);
  Client* find(uint32_t id);
  size_t pending(const Client& c) const;

  Ws_Limits limits_;
  int listen_fd_;
  uint16_t port_;
  uint32_t next_id_;
  uint64_t dropped_;
  std::vector<Client> clients_;
};

} // namespace mts

#endif /* MTS_APP_WS_WS_SERVER_HPP */

