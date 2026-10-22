/* ===================================================================================================
 * app/main.cc
 *
 * The signal handler only sets a lock-free atomic flag: the main loop
 * notices it within one step (10 ms) and shuts the threads down in order.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <app/orchestrator/pipeline.hpp>
#include <utils/logger/logger.h>
#include <utils/time/time.h>

#include <atomic>
#include <csignal>
#include <cstring>
#include <signal.h>

namespace
{

// Signal-safe because lock-free: the handler touches nothing else
std::atomic<bool> g_quit(false);
static_assert(std::atomic<bool>::is_always_lock_free, "the signal handler needs a lock-free flag");

void on_signal(int)
{
  g_quit.store(true, std::memory_order_relaxed);
}

bool install_signal_handlers()
{
  struct sigaction sa;
  std::memset(&sa, 0, sizeof(sa));
  sa.sa_handler = on_signal;
  sigemptyset(&sa.sa_mask);
  return sigaction(SIGINT, &sa, nullptr) == 0 && sigaction(SIGTERM, &sa, nullptr) == 0;
}

} // namespace

int main()
{
  if (!install_signal_handlers())
  {
    MTS_LOG_ERROR("mts: could not install the signal handlers");
    return 1;
  }

  // Slice 1: the defaults are the configuration
  mts::Pipeline_Config cfg;
  mts::Pipeline pipeline(cfg);
  if (!pipeline.start())
  {
    return 1;
  }
  MTS_LOG_INFO("mts: running, UI on ws://<this machine>:%u (Ctrl-C to quit)", (unsigned)pipeline.port());

  while (!g_quit.load(std::memory_order_relaxed))
  {
    pipeline.step(10 * MTS_TIME_NS_PER_MS);
  }

  MTS_LOG_INFO("mts: shutting down");
  pipeline.stop();
  return 0;
}

