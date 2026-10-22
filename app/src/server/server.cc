/* ===================================================================================================
 * app/src/server/server.cc
 *
 * Server, see server.hpp. One loop on one thread:
 *
 *   until stopping:
 *     poll the sockets until the next flush is due
 *     open  -> the cached status line, to that client only
 *     text  -> decode: ok -> commands queue (full: busy), error -> that client
 *     flush due -> drain messages into one batch, broadcast it,
 *                  then send the replies to their clients
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <app/server/server.hpp>

#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <cstdio>
#include <cstring>

namespace mts
{

namespace
{

// stop(): how long to keep flushing the close frames before dropping the sockets
constexpr int64_t CLOSE_GRACE_NS = 100 * MTS_TIME_NS_PER_MS;

Command_Reply busy_reply(const Command& cmd)
{
  Command_Reply r;
  std::memset(&r, 0, sizeof(r));
  r.id = cmd.id;
  r.has_id = 1;
  r.error = ERR_BUSY;
  r.client = cmd.client;
  std::snprintf(r.msg, sizeof(r.msg), "too many commands in flight, retry");
  return r;
}

} // namespace

Server::Server(const Server_Config& cfg, int64_t origin_t_ns, Queue<Server_Message>& messages,
               Queue<Status_Message>& status, Queue<Command>& commands, Queue<Command_Reply>& replies)
  : cfg_(cfg)
  , messages_(messages)
  , status_(status)
  , commands_(commands)
  , replies_(replies)
  , ws_(new Ws_Server(cfg.limits))
  , msg_{}
  , msg_status_{}
  , port_(0)
  , stopping_(false)
{
  encoder_.set_origin(origin_t_ns);
}

Server::~Server()
{
  stop();
}

bool Server::start()
{
  if (thread_.joinable() || !ws_)
  {
    MTS_LOG_ERROR("server: already started (a stopped server cannot restart)");
    return false;
  }
  if (cfg_.flush_ns <= 0)
  {
    MTS_LOG_ERROR("server: flush period must be > 0");
    return false;
  }
  if (!ws_->listen(cfg_.bind_addr.c_str(), cfg_.port))
  {
    return false; // Ws_Server logged why
  }

  // Read here, before the thread exists: port() never touches ws_
  port_ = ws_->port();
  MTS_LOG_INFO("server: listening on %s:%u", cfg_.bind_addr.c_str(), (unsigned)port_);

  thread_ = std::thread(&Server::run, this);
  return true;
}

void Server::stop()
{
  if (!thread_.joinable())
  {
    return;
  }
  stopping_.store(true, std::memory_order_relaxed);
  thread_.join();
}

uint16_t Server::port() const
{
  return port_;
}

void Server::run()
{
  int64_t next_flush = mts_time_now_ns() + cfg_.flush_ns;

  while (!stopping_.load(std::memory_order_relaxed))
  {
    // Rounded up: waking a bit late is fine, spinning on a 0 ms poll is not
    const int64_t left = next_flush - mts_time_now_ns();
    const int timeout_ms = left > 0 ? (int)((left + MTS_TIME_NS_PER_MS - 1) / MTS_TIME_NS_PER_MS) : 0;

    events_.clear();
    ws_->poll(timeout_ms, events_);
    handle_events();

    const int64_t now = mts_time_now_ns();
    if (now >= next_flush)
    {
      flush();
      next_flush += cfg_.flush_ns;
      // Fell behind (stalled, debugger): restart the grid instead of bursting to catch up
      if (next_flush <= now)
      {
        next_flush = now + cfg_.flush_ns;
      }
    }
  }

  close_all();
}

void Server::handle_events()
{
  for (const Ws_Event& e : events_)
  {
    switch (e.kind)
    {
    case Ws_Event_Kind::open:
      MTS_LOG_INFO("server: client %u connected", (unsigned)e.client);
      clients_.push_back(e.client);
      if (!status_line_.empty())
      {
        ws_->send_text(e.client, status_line_.data(), status_line_.size());
      }
      break;
    case Ws_Event_Kind::text:
      handle_command(e.client, e.text);
      break;
    case Ws_Event_Kind::close:
      MTS_LOG_INFO("server: client %u disconnected", (unsigned)e.client);
      forget(e.client);
      break;
    }
  }
}

void Server::handle_command(uint32_t client, const std::string& text)
{
  Command cmd;
  Command_Reply err;
  if (!protocol_decode_command(text.data(), text.size(), cmd, err))
  {
    // Answered right away: the orchestrator never sees what could not be decoded
    line_.clear();
    encoder_.encode_reply(err, line_);
    ws_->send_text(client, line_.data(), line_.size());
    return;
  }

  cmd.client = client;
  if (!commands_.try_push(cmd))
  {
    line_.clear();
    encoder_.encode_reply(busy_reply(cmd), line_);
    ws_->send_text(client, line_.data(), line_.size());
  }
}

void Server::flush()
{
  const bool anyone = ws_->open_count() > 0;
  batch_.clear();

  // Status first: a state change leads the frames of the same flush.
  // At most one queue's worth per flush, for every queue: a producer faster
  // than us cannot keep this loop (and the sockets) waiting forever
  Status_Message st;
  for (size_t n = status_.capacity(); n > 0 && status_.pop(st); n--)
  {
    add_status(st, anyone);
  }

  for (size_t n = messages_.capacity(); n > 0 && messages_.pop(msg_); n--)
  {
    if (msg_.kind == SERVER_MSG_STATUS)
    {
      add_status(msg_.status, anyone); // not its path, but treated the same
      continue;
    }
    // Nobody to send it to: drain without the encoding work
    if (anyone && !encoder_.encode(msg_, batch_))
    {
      MTS_LOG_WARN("server: dropping message of unknown kind %u", (unsigned)msg_.kind);
    }
  }

  // Replies to everyone travel in the batch, replies to one client after it
  Command_Reply reply;
  direct_.clear();
  for (size_t n = replies_.capacity(); n > 0 && replies_.pop(reply); n--)
  {
    if (!anyone)
    {
      continue;
    }
    if (reply.client == 0)
    {
      encoder_.encode_reply(reply, batch_);
    }
    else
    {
      direct_.push_back(reply);
    }
  }

  if (!batch_.empty())
  {
    ws_->broadcast_text(batch_.data(), batch_.size());
  }
  for (const Command_Reply& r : direct_)
  {
    line_.clear();
    encoder_.encode_reply(r, line_);
    if (!line_.empty())
    {
      // false: that client left meanwhile. Nothing to do
      ws_->send_text(r.client, line_.data(), line_.size());
    }
  }
}

void Server::add_status(Status_Message& st, bool anyone)
{
  // Processing cannot know what it failed to push: the queue's own counter
  st.drops_processing_to_server = messages_.dropped();

  // Always encoded, even with nobody connected: it is what the next client gets first
  msg_status_.kind = SERVER_MSG_STATUS;
  msg_status_.status = st;
  status_line_.clear();
  encoder_.encode(msg_status_, status_line_);
  if (anyone)
  {
    batch_ += status_line_;
  }
}

void Server::close_all()
{
  for (uint32_t id : clients_)
  {
    ws_->close(id);
  }

  // Close frames are queued: keep polling until every client is gone (its
  // close event) or the grace period is over, whichever comes first
  const int64_t deadline = mts_time_now_ns() + CLOSE_GRACE_NS;
  while (!clients_.empty() && mts_time_now_ns() < deadline)
  {
    events_.clear();
    ws_->poll(5, events_);
    for (const Ws_Event& e : events_)
    {
      if (e.kind == Ws_Event_Kind::close)
      {
        forget(e.client);
      }
    }
  }

  // Whatever is left is dropped here, socket by socket
  ws_.reset();
  clients_.clear();
}

void Server::forget(uint32_t client)
{
  for (size_t i = 0; i < clients_.size(); i++)
  {
    if (clients_[i] == client)
    {
      clients_[i] = clients_.back();
      clients_.pop_back();
      return;
    }
  }
}

} // namespace mts
