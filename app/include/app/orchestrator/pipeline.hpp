/* ===================================================================================================
 * app/include/app/orchestrator/pipeline.hpp
 *
 * The whole program, wired: the queues and mailboxes, the three threads,
 * and the orchestrator driven by the caller's thread (main).
 *
 *              Sample_Queue              Queue<Server_Message> (frames)
 *  Hat_Worker ─────────────▶ Processing ───────────────────────────────▶ Server ◀─▶ UI
 *      ▲                        ▲                                       │  ▲  ▲
 *      └── Mailbox<Led_Command> ┘                         Queue<Command>│  │  │Queue<Command_Reply>
 *                               │                                       ▼  │  │
 *                               └── Mailbox<Pipeline_Control> ── Orchestrator (caller's thread)
 *                                                                Queue<Status_Message>
 *
 * Every arrow has one producer and one consumer. step() is the
 * orchestrator's side of all of them, so it must always run on the same
 * thread.
 *
 * start(): open the Hat (its opened_t_ns is the protocol's origin), listen
 * (a busy port fails here, before any thread exists), then the consumers
 * before the producers: Processing, then the Hat_Worker. stop() is the
 * reverse: Hat_Worker, Processing, Server.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_ORCHESTRATOR_PIPELINE_HPP
#define MTS_APP_ORCHESTRATOR_PIPELINE_HPP

#include <app/hat/hat.hpp>
#include <app/hat/hat_worker.hpp>
#include <app/link/command.h>
#include <app/link/led_command.h>
#include <app/link/pipeline_control.h>
#include <app/link/sample_queue.hpp>
#include <app/link/server_message.h>
#include <app/orchestrator/orchestrator.hpp>
#include <app/server/server.hpp>
#include <utils/mailbox/mailbox.hpp>
#include <utils/ring_buffer/queue.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>

namespace mts
{

class Processing;

/* Slice 1: the defaults are the configuration (sim Hat at 100 Hz, server on
 * 0.0.0.0:8080). The config module will fill this from a file later. */
struct Pipeline_Config
{
  Hat_Config hat;
  Hat_Worker_Config worker;
  Server_Config server;
  Orchestrator_Config orchestrator;
  double sample_queue_s = 0.25; // jitter absorption, not storage (sample_queue.hpp)
  size_t frame_queue = 256; // ~2.5 s of frames at 100 Hz
  size_t status_queue = 16;
  size_t command_queue = 16;
  size_t reply_queue = 16;
};

class Pipeline
{
public:
  explicit Pipeline(const Pipeline_Config& cfg);

  // Calls stop()
  ~Pipeline();

  Pipeline(const Pipeline&) = delete;
  Pipeline& operator=(const Pipeline&) = delete;

  // Opens, listens, starts the threads. false (logged): nothing is left running
  bool start();

  /* One orchestrator turn: waits up to max_wait_ns for a command, then
   * answers every waiting command, advances time, checks the Hat, passes
   * control to Processing and status to the Server. Call it in a loop. */
  void step(int64_t max_wait_ns);

  // Hat_Worker, Processing, Server, in that order. Safe to call twice
  void stop();

  // The server's real port (0 before start()). Tests listen on 0
  uint16_t port() const;

private:
  void push_reply(const Command_Reply& reply);

  Pipeline_Config cfg_;

  // The links first: built before the threads, destroyed after them
  Sample_Queue samples_;
  Queue<Server_Message> frames_;
  Queue<Status_Message> status_;
  Queue<Command> commands_;
  Queue<Command_Reply> replies_;
  Mailbox<Pipeline_Control> control_;
  Mailbox<Led_Command> led_;

  std::unique_ptr<Orchestrator> orch_;
  std::unique_ptr<Server> server_;
  std::unique_ptr<Processing> processing_;
  std::unique_ptr<Hat_Worker> worker_;

  uint32_t control_gen_; // generation last written to control_
  bool hat_failure_reported_;
  uint16_t port_;
};

} // namespace mts

#endif /* MTS_APP_ORCHESTRATOR_PIPELINE_HPP */
