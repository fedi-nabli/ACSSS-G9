/* ===================================================================================================
 * app/include/app/server/server.hpp
 *
 * The server thread: the only place where the program meets the UI.
 *  - Server_Message from Processing (frames, metrics, spectrum) and
 *    Status_Message from the orchestrator -> NDJSON lines, batched,
 *    broadcast to every client once per flush period (~30 ms, ui_contract.md).
 *    Status has its own queue because each queue has one producer, and
 *    status belongs to the orchestrator.
 *  - Command lines from a client -> decoded Command, stamped with the
 *    client's id, pushed to the orchestrator
 *  - Command_Reply from the orchestrator -> ack/error line to the client
 *    that sent the command (Command_Reply::client, 0 = every client)
 *
 * Owns its Ws_Server and Protocol_Encoder. The queues belong to the caller
 * and must outlive the Server. The server is the consumer of messages,
 * status and replies, and the producer of commands: no other thread may use
 * those ends.
 *
 * Status lines are cached: a client that connects gets the last one right
 * away instead of waiting for the next heartbeat. drops.processing_to_server
 * is filled in here, from the messages queue's own counter: Processing
 * cannot know what it failed to push.
 *
 * Nothing in here blocks the pipeline: a slow client fills its own send
 * buffer (Ws_Limits) and loses messages, a full command queue answers busy.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_SERVER_SERVER_HPP
#define MTS_APP_SERVER_SERVER_HPP

#include <app/link/command.h>
#include <app/link/server_message.h>
#include <app/protocol/protocol.hpp>
#include <app/ws/ws_server.hpp>
#include <utils/ring_buffer/queue.hpp>
#include <utils/time/time.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace mts
{

struct Server_Config
{
  std::string bind_addr = "0.0.0.0";
  uint16_t port = 8080; // 0: any free port (tests), see Server::port()
  int64_t flush_ns = 30 * MTS_TIME_NS_PER_MS; // one WebSocket message per client per period
  Ws_Limits limits;
};

class Server
{
public:
  /* origin_t_ns: Hat_Info::opened_t_ns, the protocol's timestamp_ns origin.
   * The queues are only referenced: they must outlive the Server. */
  Server(const Server_Config& cfg, int64_t origin_t_ns, Queue<Server_Message>& messages, Queue<Status_Message>& status,
         Queue<Command>& commands, Queue<Command_Reply>& replies);

  // Calls stop()
  ~Server();

  Server(const Server&) = delete;
  Server& operator=(const Server&) = delete;

  /* Listens (in the calling thread, so a busy port fails here), then starts
   * the thread. false (logged) if listening fails or already started. */
  bool start();

  /* Closes every client and joins the thread. Takes at most about one flush
   * period plus the time to send the close frames. Safe to call twice. */
  void stop();

  // The port actually listened on (0 before start()). Tests listen on 0
  uint16_t port() const;

private:
  void run();
  void handle_events();
  void handle_command(uint32_t client, const std::string& text);
  void flush();
  void add_status(Status_Message& st, bool anyone);
  void close_all();
  void forget(uint32_t client);

  Server_Config cfg_;
  Queue<Server_Message>& messages_;
  Queue<Status_Message>& status_;
  Queue<Command>& commands_;
  Queue<Command_Reply>& replies_;

  // Server thread only (after start())
  std::unique_ptr<Ws_Server> ws_;
  Protocol_Encoder encoder_;
  std::vector<Ws_Event> events_;
  std::vector<uint32_t> clients_; // open clients, to close them on stop()
  std::vector<Command_Reply> direct_; // this flush's replies to one client
  std::string batch_; // reused between flushes: no allocation once warm
  std::string line_;
  std::string status_line_; // last status, sent to every new client. Empty until the first one
  Server_Message msg_; // ~3 KB (spectrum): a member, not on the stack every flush
  Server_Message msg_status_; // the encoder takes a Server_Message: status wrapped in one

  uint16_t port_;
  std::atomic<bool> stopping_;
  std::thread thread_;
};

} // namespace mts

#endif /* MTS_APP_SERVER_SERVER_HPP */
