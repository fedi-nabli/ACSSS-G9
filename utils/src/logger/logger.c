/* ===================================================================================================
 * utils/src/logger/logger.c
 *
 * Authors: Fedi Nabli
 * Date: 19 Oct 2026
 * Last Modified: 19 Oct 2026
 * ===================================================================================================
 */

#include <utils/time/time.h>
#include <utils/logger/logger.h>

#include <stddef.h>
#include <stdint.h>
#include <stdatomic.h>

#define LOG_LINE_BYTES 512

static _Atomic int g_min_level = LEVEL_INFO;
static _Atomic(FILE*) g_stream = NULL; // NULL = stderr

static const char* level_name(Mts_Log_Level level)
{
  static const char* const names[] = { "DEBUG", "INFO", "WARN", "ERROR" };

  if ((int)level < 0 || (int)level > (int)LEVEL_ERROR)
  {
    return "?????";
  }

  return names[level];
}

void mts_log_set_min_level(Mts_Log_Level min_level)
{
  atomic_store_explicit(&g_min_level, (int)min_level, memory_order_relaxed);
}

Mts_Log_Level mts_log_get_level(void)
{
  return (Mts_Log_Level)atomic_load_explicit(&g_min_level, memory_order_relaxed);
}

bool mts_log_enabled(Mts_Log_Level level)
{
  return (int)level >= atomic_load_explicit(&g_min_level, memory_order_relaxed);
}

void mts_log_set_stream(FILE* stream)
{
  atomic_store_explicit(&g_stream, stream, memory_order_relaxed);
}

void mts_log_at(Mts_Log_Level level, const char* file, int line, const char* fmt, ...)
{
  va_list args;
  va_start(args, fmt);
  mts_vlog_at(level, file, line, fmt, args);
  va_end(args);
}

void mts_vlog_at(Mts_Log_Level level, const char* file, int line, const char* fmt, va_list args)
{
  (void)file;
  (void)line;

  if (!mts_log_enabled(level))
  {
    return;
  }

  char buf[LOG_LINE_BYTES];

  int64_t now = mts_time_now_ns();
  int prefix = snprintf(buf, sizeof(buf), "[%6lld.%06lld] %-5s ",
                        (long long)(now / MTS_TIME_NS_PER_S),
                        (long long)((now % MTS_TIME_NS_PER_S) / MTS_TIME_NS_PER_US),
                        level_name(level));

  if (prefix < 0)
  {
    return;
  }

  size_t used = (size_t)prefix;
  int msg = vsnprintf(buf + used, sizeof(buf) - used, fmt, args);
  if (msg < 0)
  {
    return;
  }

  size_t end = used + (size_t)msg;
  if (end + 2 <= sizeof(buf))
  {
    buf[end] = '\n';
    buf[end + 1] = '\0';
  }
  else
  {
    size_t last = sizeof(buf) - 1;
    buf[last - 4] = '.';
    buf[last - 3] = '.';
    buf[last - 2] = '.';
    buf[last - 1] = '\n';
    buf[last] = '\0';
  }

  FILE* stream = atomic_load_explicit(&g_stream, memory_order_relaxed);
  if (!stream)
  {
    stream = stderr;
  }

  fputs(buf, stream);
  fflush(stream);
}

