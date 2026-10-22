/* ===================================================================================================
 * tests/common/ws_client.hpp
 *
 * A small blocking WebSocket client for tests that talk to a server running
 * on another thread. Every call that waits takes a timeout, so a broken
 * server fails the test instead of hanging it.
 *
 *   mts::test::Ws_Client c;
 *   MTS_ASSERT(c.connect(port));
 *   MTS_ASSERT(c.send_text("{\"type\":\"command\",\"id\":1,\"command\":\"start\"}"));
 *   std::string line;
 *   MTS_ASSERT(c.read_line_containing("\"ack\"", line));
 *
 * Header-only, test use only: loopback IPv4, no TLS, no fragmentation on
 * send. Built on ws_codec, so it speaks exactly what the server parses.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_TESTS_COMMON_WS_CLIENT_HPP
#define MTS_TESTS_COMMON_WS_CLIENT_HPP

#include <app/ws/ws_codec.hpp>
#include <utils/time/time.h>

#include <arpa/inet.h>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <deque>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

namespace mts
{
namespace test
{

constexpr int64_t WS_CLIENT_TIMEOUT_NS = 2 * MTS_TIME_NS_PER_S;

class Ws_Client
{
public:
  Ws_Client() = default;

  ~Ws_Client()
  {
    disconnect();
  }

  Ws_Client(const Ws_Client&) = delete;
  Ws_Client& operator=(const Ws_Client&) = delete;

  // TCP connect to 127.0.0.1:port, then the upgrade. true once the 101 arrived
  bool connect(uint16_t port, int64_t timeout_ns = WS_CLIENT_TIMEOUT_NS)
  {
    fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (fd_ < 0)
    {
      return false;
    }
#ifdef SO_NOSIGPIPE
    // macOS has no MSG_NOSIGNAL: a send to a closed server must not kill the test
    int one = 1;
    setsockopt(fd_, SOL_SOCKET, SO_NOSIGPIPE, &one, sizeof(one));
#endif
    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
    if (::connect(fd_, (const sockaddr*)&addr, sizeof(addr)) != 0)
    {
      return false;
    }

    const std::string request = "GET / HTTP/1.1\r\nHost: localhost\r\nUpgrade: websocket\r\n"
                                "Connection: Upgrade\r\nSec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                                "Sec-WebSocket-Version: 13\r\n\r\n";
    if (!send_all(request))
    {
      return false;
    }

    // The response ends with an empty line. Bytes after it are already frames
    const int64_t deadline = mts_time_now_ns() + timeout_ns;
    size_t end;
    while ((end = in_.find("\r\n\r\n")) == std::string::npos)
    {
      if (!receive(deadline))
      {
        return false;
      }
    }
    const bool ok = in_.compare(0, 12, "HTTP/1.1 101") == 0;
    in_.erase(0, end + 4);
    return ok;
  }

  void disconnect()
  {
    if (fd_ >= 0)
    {
      ::close(fd_);
      fd_ = -1;
    }
  }

  bool send_text(const std::string& text)
  {
    std::string frame;
    ws_encode_masked_frame(frame, Ws_Opcode::text, true, text.data(), text.size(), 0x5a5a1234);
    return send_all(frame);
  }

  /* Waits for the next complete text message (one server flush). false on
   * timeout, or once the server closed: see closed(), close_code(). */
  bool read_message(std::string& out, int64_t timeout_ns = WS_CLIENT_TIMEOUT_NS)
  {
    const int64_t deadline = mts_time_now_ns() + timeout_ns;
    for (;;)
    {
      // Bytes may already be here: the first frames can arrive with the 101
      parse();
      if (!messages_.empty())
      {
        out = std::move(messages_.front());
        messages_.pop_front();
        return true;
      }
      if (closed_ || !receive(deadline))
      {
        return false;
      }
    }
  }

  /* Waits for the next line (messages split on '\n'), across messages.
   * The lines of one message come out in order. */
  bool read_line(std::string& out, int64_t timeout_ns = WS_CLIENT_TIMEOUT_NS)
  {
    const int64_t deadline = mts_time_now_ns() + timeout_ns;
    while (lines_.empty())
    {
      std::string msg;
      const int64_t left = deadline - mts_time_now_ns();
      if (left <= 0 || !read_message(msg, left))
      {
        return false;
      }
      split(msg);
    }
    out = std::move(lines_.front());
    lines_.pop_front();
    return true;
  }

  // Skips lines until one contains needle. false on timeout
  bool read_line_containing(const char* needle, std::string& out, int64_t timeout_ns = WS_CLIENT_TIMEOUT_NS)
  {
    const int64_t deadline = mts_time_now_ns() + timeout_ns;
    for (;;)
    {
      const int64_t left = deadline - mts_time_now_ns();
      if (left <= 0 || !read_line(out, left))
      {
        return false;
      }
      if (out.find(needle) != std::string::npos)
      {
        return true;
      }
    }
  }

  // Reads until the server closes (close frame or EOF). false on timeout
  bool wait_closed(int64_t timeout_ns = WS_CLIENT_TIMEOUT_NS)
  {
    const int64_t deadline = mts_time_now_ns() + timeout_ns;
    parse();
    while (!closed_)
    {
      if (!receive(deadline))
      {
        return closed_;
      }
      parse();
    }
    return true;
  }

  bool closed() const
  {
    return closed_;
  }

  // Code of the server's close frame, 0 if none came (EOF, or still open)
  uint16_t close_code() const
  {
    return close_code_;
  }

private:
  bool send_all(const std::string& bytes)
  {
    size_t off = 0;
    while (off < bytes.size())
    {
      ssize_t n = ::send(fd_, bytes.data() + off, bytes.size() - off, MSG_NOSIGNAL_FLAG);
      if (n <= 0)
      {
        if (n < 0 && errno == EINTR)
        {
          continue;
        }
        return false;
      }
      off += (size_t)n;
    }
    return true;
  }

  // Waits until deadline for bytes and appends them to in_. false on timeout or EOF
  bool receive(int64_t deadline)
  {
    for (;;)
    {
      const int64_t left = deadline - mts_time_now_ns();
      if (left <= 0)
      {
        return false;
      }
      pollfd p = { fd_, POLLIN, 0 };
      const int ms = (int)((left + MTS_TIME_NS_PER_MS - 1) / MTS_TIME_NS_PER_MS);
      const int r = ::poll(&p, 1, ms);
      if (r < 0 && errno == EINTR)
      {
        continue;
      }
      if (r <= 0)
      {
        return false;
      }
      char buf[65536];
      ssize_t n = ::recv(fd_, buf, sizeof(buf), 0);
      if (n > 0)
      {
        in_.append(buf, (size_t)n);
        return true;
      }
      if (n < 0 && (errno == EINTR || errno == EAGAIN))
      {
        continue;
      }
      closed_ = true; // EOF or reset
      return false;
    }
  }

  // Turns every complete frame in in_ into a message (or the close)
  void parse()
  {
    for (;;)
    {
      Ws_Frame frame;
      size_t consumed = 0;
      Ws_Parse r = ws_parse_frame((const uint8_t*)in_.data(), in_.size(), SIZE_MAX, frame, consumed);
      if (r != Ws_Parse::frame)
      {
        if (r != Ws_Parse::incomplete)
        {
          closed_ = true; // the server sent garbage: the test will notice
        }
        return;
      }
      in_.erase(0, consumed);
      if (frame.opcode == Ws_Opcode::text)
      {
        messages_.push_back(std::move(frame.payload));
      }
      else if (frame.opcode == Ws_Opcode::close)
      {
        if (frame.payload.size() >= 2)
        {
          close_code_ = (uint16_t)(((uint8_t)frame.payload[0] << 8) | (uint8_t)frame.payload[1]);
        }
        closed_ = true;
        return;
      }
    }
  }

  void split(const std::string& msg)
  {
    size_t start = 0;
    while (start < msg.size())
    {
      size_t nl = msg.find('\n', start);
      if (nl == std::string::npos)
      {
        nl = msg.size();
      }
      if (nl > start)
      {
        lines_.push_back(msg.substr(start, nl - start));
      }
      start = nl + 1;
    }
  }

#ifdef MSG_NOSIGNAL
  static constexpr int MSG_NOSIGNAL_FLAG = MSG_NOSIGNAL;
#else
  static constexpr int MSG_NOSIGNAL_FLAG = 0; // macOS: SO_NOSIGPIPE, set in connect()
#endif

  int fd_ = -1;
  std::string in_; // received bytes not parsed yet
  std::deque<std::string> messages_;
  std::deque<std::string> lines_;
  bool closed_ = false;
  uint16_t close_code_ = 0;
};

} // namespace test
} // namespace mts

#endif /* MTS_TESTS_COMMON_WS_CLIENT_HPP */
