/* ===================================================================================================
 * app/src/ws/ws_server.cc
 *
 * WebSocket server transport, see app/ws/ws_server.hpp
 *
 * Every socket is non-blocking; poll() is the only place that waits.
 * A client that leaves (either side, or an error) is marked dead and
 * removed at the end of poll(), so the client vector is never modified
 * while it is being walked.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <app/ws/ws_codec.hpp>
#include <app/ws/ws_server.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

namespace mts
{

namespace
{

// Writing to a socket the peer closed raises SIGPIPE, which kills the
// process by default. Linux: per call (MSG_NOSIGNAL). macOS: per socket (SO_NOSIGPIPE)
#if defined(MSG_NOSIGNAL)
constexpr int SEND_FLAGS = MSG_NOSIGNAL;
#else
constexpr int SEND_FLAGS = 0;
#endif

// Bytes read per client per poll(): keeps one busy client from starving the others
constexpr size_t READ_CHUNK = 16 * 1024;
constexpr size_t MAX_READ_PER_POLL = 64 * 1024;

bool set_nonblocking(int fd)
{
  int flags = fcntl(fd, F_GETFL, 0);
  return flags >= 0 && fcntl(fd, F_SETFL, flags | O_NONBLOCK) == 0;
}

// Options every client socket gets
void configure_client_socket(int fd)
{
  // Small frames at 100+ Hz: send now, do not wait to coalesce (Nagle)
  int one = 1;
  setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
#if defined(SO_NOSIGPIPE)
  setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
}

} // namespace

Ws_Server::Ws_Server(const Ws_Limits& limits)
  : limits_(limits)
  , listen_fd_(-1)
  , port_(0)
  , next_id_(1)
  , dropped_(0)
{
}

Ws_Server::~Ws_Server()
{
  for (Client& c : clients_)
  {
    ::close(c.fd);
  }
  if (listen_fd_ >= 0)
  {
    ::close(listen_fd_);
  }
}

bool Ws_Server::listen(const char* bind_addr, uint16_t port)
{
  if (listen_fd_ >= 0)
  {
    MTS_LOG_ERROR("ws: already listening on port %u", port_);
    return false;
  }

  sockaddr_in addr;
  std::memset(&addr, 0, sizeof(addr));
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (inet_pton(AF_INET, bind_addr, &addr.sin_addr) != 1)
  {
    MTS_LOG_ERROR("ws: invalid bind address '%s'", bind_addr);
    return false;
  }

  int fd = socket(AF_INET, SOCK_STREAM, 0);
  if (fd < 0)
  {
    MTS_LOG_ERROR("ws: socket: %s", std::strerror(errno));
    return false;
  }

  // Restarting during development must not fail with "address in use"
  int one = 1;
  setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

  if (bind(fd, (const sockaddr*)&addr, sizeof(addr)) != 0)
  {
    MTS_LOG_ERROR("ws: bind %s:%u: %s", bind_addr, port, std::strerror(errno));
    ::close(fd);
    return false;
  }
  if (::listen(fd, 8) != 0 || !set_nonblocking(fd))
  {
    MTS_LOG_ERROR("ws: listen: %s", std::strerror(errno));
    ::close(fd);
    return false;
  }

  sockaddr_in bound;
  socklen_t bound_len = sizeof(bound);
  getsockname(fd, (sockaddr*)&bound, &bound_len);

  listen_fd_ = fd;
  port_ = ntohs(bound.sin_port);
  MTS_LOG_INFO("ws: listening on %s:%u", bind_addr, port_);
  return true;
}

uint16_t Ws_Server::port() const
{
  return port_;
}

void Ws_Server::poll(int timeout_ms, std::vector<Ws_Event>& events)
{
  std::vector<pollfd> fds;
  fds.reserve(clients_.size() + 1);

  if (listen_fd_ >= 0)
  {
    fds.push_back({ listen_fd_, POLLIN, 0 });
  }
  for (const Client& c : clients_)
  {
    short want = POLLIN;
    if (pending(c) > 0)
    {
      want |= POLLOUT;
    }
    fds.push_back({ c.fd, want, 0 });
  }

  int ready = ::poll(fds.data(), (nfds_t)fds.size(), timeout_ms);
  if (ready < 0 && errno != EINTR)
  {
    MTS_LOG_ERROR("ws: poll: %s", std::strerror(errno));
  }

  // fds[1..] match clients_ by index: clients accepted below are appended
  // after this loop, so the indices stay valid
  size_t first_client = listen_fd_ >= 0 ? 1 : 0;
  size_t polled_clients = clients_.size();
  for (size_t i = 0; i < polled_clients && ready > 0; i++)
  {
    Client& c = clients_[i];
    short revents = fds[first_client + i].revents;

    if (revents & (POLLIN | POLLHUP | POLLERR))
    {
      read_client(c, events);
    }
    if (!c.dead && (revents & POLLOUT))
    {
      write_client(c);
    }
  }

  if (listen_fd_ >= 0 && ready > 0 && (fds[0].revents & POLLIN))
  {
    accept_clients();
  }

  // Flush what the calls above queued (handshake answers, pongs, closes),
  // close finished clients, drop stalled ones
  const int64_t now = mts_time_now_ns();
  for (Client& c : clients_)
  {
    if (c.dead)
    {
      continue;
    }
    if (pending(c) > 0)
    {
      write_client(c);
    }
    if (c.state == State::closing && pending(c) == 0)
    {
      c.dead = true;
    }
    else if (pending(c) > 0 && now - c.waiting_since_ns > limits_.stalled_close_ns)
    {
      MTS_LOG_WARN("ws: client %u stalled, dropping it", c.id);
      c.dead = true;
    }
  }

  remove_dead(events);
}

bool Ws_Server::send_text(uint32_t client, const char* data, size_t len)
{
  Client* c = find(client);
  if (!c || c->state != State::open)
  {
    return false;
  }

  // Header is at most 10 bytes from server to client
  if (pending(*c) + len + 10 > limits_.send_buffer_bytes)
  {
    dropped_++;
    return false;
  }

  std::string frame;
  frame.reserve(len + 10);
  ws_encode_frame(frame, Ws_Opcode::text, data, len);
  queue(*c, frame);
  return true;
}

size_t Ws_Server::broadcast_text(const char* data, size_t len)
{
  // Encode once, queue the same bytes for everyone
  std::string frame;
  frame.reserve(len + 10);
  ws_encode_frame(frame, Ws_Opcode::text, data, len);

  size_t taken = 0;
  for (Client& c : clients_)
  {
    if (c.dead || c.state != State::open)
    {
      continue;
    }
    if (pending(c) + frame.size() > limits_.send_buffer_bytes)
    {
      dropped_++;
      continue;
    }
    queue(c, frame);
    taken++;
  }
  return taken;
}

void Ws_Server::close(uint32_t client)
{
  Client* c = find(client);
  if (c && c->state == State::open)
  {
    fail(*c, WS_CLOSE_NORMAL);
  }
}

size_t Ws_Server::open_count() const
{
  size_t n = 0;
  for (const Client& c : clients_)
  {
    if (!c.dead && c.state == State::open)
    {
      n++;
    }
  }
  return n;
}

uint64_t Ws_Server::dropped_messages() const
{
  return dropped_;
}

void Ws_Server::accept_clients()
{
  for (;;)
  {
    int fd = accept(listen_fd_, nullptr, nullptr);
    if (fd < 0)
    {
      // EAGAIN: no more pending connections
      return;
    }

    if (clients_.size() >= limits_.max_clients)
    {
      MTS_LOG_WARN("ws: %zu clients already, refusing a new one", clients_.size());
      ::close(fd);
      continue;
    }

    if (!set_nonblocking(fd))
    {
      ::close(fd);
      continue;
    }
    configure_client_socket(fd);

    Client c;
    c.fd = fd;
    c.id = next_id_++;
    c.state = State::handshake;
    c.out_off = 0;
    c.waiting_since_ns = mts_time_now_ns();
    c.in_fragment = false;
    c.reported_open = false;
    c.dead = false;
    clients_.push_back(std::move(c));
  }
}

void Ws_Server::read_client(Client& c, std::vector<Ws_Event>& events)
{
  char buf[READ_CHUNK];
  size_t total = 0;

  while (total < MAX_READ_PER_POLL)
  {
    ssize_t n = recv(c.fd, buf, sizeof(buf), 0);
    if (n > 0)
    {
      c.in.append(buf, (size_t)n);
      total += (size_t)n;
      continue;
    }
    if (n == 0)
    {
      // Peer closed the connection
      c.dead = true;
      return;
    }
    if (errno == EAGAIN || errno == EWOULDBLOCK)
    {
      break;
    }
    if (errno == EINTR)
    {
      continue;
    }
    c.dead = true;
    return;
  }

  parse_client(c, events);
}

void Ws_Server::parse_client(Client& c, std::vector<Ws_Event>& events)
{
  if (c.state == State::handshake)
  {
    std::string key;
    size_t consumed = 0;
    Ws_Handshake hs = ws_parse_handshake(c.in, key, consumed);

    if (hs == Ws_Handshake::incomplete)
    {
      return;
    }
    if (hs == Ws_Handshake::bad)
    {
      queue(c, ws_bad_request_response());
      c.in.clear();
      c.state = State::closing;
      return;
    }

    queue(c, ws_handshake_response(key));
    c.in.erase(0, consumed);
    c.state = State::open;
    c.reported_open = true;
    events.push_back({ Ws_Event_Kind::open, c.id, std::string() });
  }

  // Every complete frame in the buffer; a partial one waits for more bytes
  size_t offset = 0;
  while (c.state == State::open)
  {
    Ws_Frame frame;
    size_t consumed = 0;
    Ws_Parse result = ws_parse_frame((const uint8_t*)c.in.data() + offset, c.in.size() - offset,
                                     limits_.max_message_bytes, frame, consumed);

    if (result == Ws_Parse::incomplete)
    {
      break;
    }
    if (result == Ws_Parse::too_big)
    {
      fail(c, WS_CLOSE_TOO_BIG);
      break;
    }
    if (result == Ws_Parse::error)
    {
      fail(c, WS_CLOSE_PROTOCOL_ERROR);
      break;
    }

    offset += consumed;
    handle_frame(c, frame, events);
  }

  if (c.state == State::open)
  {
    c.in.erase(0, offset);
  }
  else
  {
    // Closing: nothing more from this client is read
    c.in.clear();
  }
}

void Ws_Server::handle_frame(Client& c, Ws_Frame& frame, std::vector<Ws_Event>& events)
{
  // RFC 6455: every frame from a client is masked
  if (!frame.masked)
  {
    fail(c, WS_CLOSE_PROTOCOL_ERROR);
    return;
  }

  switch (frame.opcode)
  {
    case Ws_Opcode::text:
    case Ws_Opcode::binary:
    {
      // A new data frame while a fragmented message is open is an error
      if (c.in_fragment)
      {
        fail(c, WS_CLOSE_PROTOCOL_ERROR);
        return;
      }
      if (frame.opcode == Ws_Opcode::binary)
      {
        fail(c, WS_CLOSE_UNSUPPORTED);
        return;
      }
      if (frame.fin)
      {
        events.push_back({ Ws_Event_Kind::text, c.id, std::move(frame.payload) });
      }
      else
      {
        c.in_fragment = true;
        c.fragments = std::move(frame.payload);
      }
      return;
    }

    case Ws_Opcode::continuation:
    {
      if (!c.in_fragment)
      {
        fail(c, WS_CLOSE_PROTOCOL_ERROR);
        return;
      }
      if (c.fragments.size() + frame.payload.size() > limits_.max_message_bytes)
      {
        fail(c, WS_CLOSE_TOO_BIG);
        return;
      }
      c.fragments += frame.payload;
      if (frame.fin)
      {
        c.in_fragment = false;
        events.push_back({ Ws_Event_Kind::text, c.id, std::move(c.fragments) });
        c.fragments.clear();
      }
      return;
    }

    case Ws_Opcode::ping:
    {
      std::string pong;
      ws_encode_frame(pong, Ws_Opcode::pong, frame.payload.data(), frame.payload.size());
      queue(c, pong);
      return;
    }

    case Ws_Opcode::pong:
    {
      return;
    }

    case Ws_Opcode::close:
    {
      // Echo the status code (if any), then close once it is flushed
      std::string payload = frame.payload.size() >= 2 ? frame.payload.substr(0, 2) : std::string();
      std::string reply;
      ws_encode_frame(reply, Ws_Opcode::close, payload.data(), payload.size());
      queue(c, reply);
      c.state = State::closing;
      return;
    }
  }
}

void Ws_Server::write_client(Client& c)
{
  while (pending(c) > 0)
  {
    ssize_t n = send(c.fd, c.out.data() + c.out_off, pending(c), SEND_FLAGS);
    if (n > 0)
    {
      c.out_off += (size_t)n;
      c.waiting_since_ns = mts_time_now_ns();
      continue;
    }
    if (n < 0 && errno == EINTR)
    {
      continue;
    }
    if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
    {
      break;
    }
    // EPIPE, ECONNRESET, ...: the client is gone
    c.dead = true;
    return;
  }

  // Everything sent: start the buffer over instead of growing it
  if (pending(c) == 0)
  {
    c.out.clear();
    c.out_off = 0;
  }
}

void Ws_Server::queue(Client& c, const std::string& bytes)
{
  if (pending(c) == 0)
  {
    // Starts waiting now (the stall clock must not count idle time)
    c.waiting_since_ns = mts_time_now_ns();
  }
  c.out += bytes;
}

void Ws_Server::fail(Client& c, uint16_t code)
{
  if (code != WS_CLOSE_NORMAL)
  {
    MTS_LOG_WARN("ws: closing client %u with code %u", c.id, code);
  }
  std::string payload = ws_close_payload(code);
  std::string frame;
  ws_encode_frame(frame, Ws_Opcode::close, payload.data(), payload.size());
  queue(c, frame);
  c.state = State::closing;
}

void Ws_Server::remove_dead(std::vector<Ws_Event>& events)
{
  size_t kept = 0;
  for (size_t i = 0; i < clients_.size(); i++)
  {
    Client& c = clients_[i];
    if (c.dead)
    {
      ::close(c.fd);
      if (c.reported_open)
      {
        events.push_back({ Ws_Event_Kind::close, c.id, std::string() });
      }
      continue;
    }
    if (kept != i)
    {
      clients_[kept] = std::move(c);
    }
    kept++;
  }
  clients_.resize(kept);
}

Ws_Server::Client* Ws_Server::find(uint32_t id)
{
  for (Client& c : clients_)
  {
    if (c.id == id && !c.dead)
    {
      return &c;
    }
  }
  return nullptr;
}

size_t Ws_Server::pending(const Client& c) const
{
  return c.out.size() - c.out_off;
}

} // namespace mts

