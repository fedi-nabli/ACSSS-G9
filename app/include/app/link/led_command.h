/* ===================================================================================================
 * app/include/app/link/led_command.h
 *
 * Led_Command: what the LED matrix should show. Processing sends meaning
 * (a level, a message), the Hat only draws. Travels through a latest-wins
 * mailbox, so its size is a multiple of 8 (checked below).
 *
 * Plain C struct: written by C++ (Processing), read by C++ (Hat), usable
 * from C. mode is a uint32_t with defines, not an enum, because the size of
 * a C enum is not fixed by the language and this layout must be.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_LINK_LED_COMMAND_H
#define MTS_APP_LINK_LED_COMMAND_H

#include <assert.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Led_Command.mode values
#define LED_MODE_OFF 0u // matrix dark
#define LED_MODE_MESSAGE 1u // scroll text
#define LED_MODE_AMPLITUDE 2u // show level (residual amplitude, or raw tremor amplitude as fallback)

// text holds at most 15 characters plus the terminating NUL
#define LED_TEXT_BYTES 16

typedef struct Led_Command
{
  uint32_t mode; // LED_MODE_*
  float level; // 0..1, used in LED_MODE_AMPLITUDE
  char text[LED_TEXT_BYTES]; // NUL-terminated, used in LED_MODE_MESSAGE
  uint32_t msg_id; // changes whenever text changes: restarts the scroller
  uint32_t pad; // keeps the size a multiple of 8 for the mailbox
} Led_Command;

static_assert(sizeof(Led_Command) == 32, "Led_Command layout changed");
static_assert(sizeof(Led_Command) % 8 == 0, "Led_Command must be a multiple of 8 bytes (mailbox)");
static_assert(offsetof(Led_Command, mode) == 0, "Led_Command layout changed");
static_assert(offsetof(Led_Command, level) == 4, "Led_Command layout changed");
static_assert(offsetof(Led_Command, text) == 8, "Led_Command layout changed");
static_assert(offsetof(Led_Command, msg_id) == 24, "Led_Command layout changed");

#ifdef __cplusplus
}
#endif

#endif /* MTS_APP_LINK_LED_COMMAND_H */

