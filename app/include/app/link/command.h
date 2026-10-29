/* ===================================================================================================
 * app/include/app/link/command.h
 *
 * Commands from the UI and the replies to them, as plain data:
 *  - Command: decoded by protocol on the server thread, handed to the
 *    orchestrator
 *  - Command_Reply: the orchestrator's answer (ack or error), encoded by
 *    protocol. Also used for errors that answer nothing (hat_error).
 *
 * Runtime_Settings is the part of the configuration a "setup" command may
 * change. SI units (horizon in ns).
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_LINK_COMMAND_H
#define MTS_APP_LINK_COMMAND_H

#include <core/types/predictor_id.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Command.kind values
#define CMD_START 1u
#define CMD_STOP 2u
#define CMD_CALIBRATE 3u
#define CMD_SETUP 4u

// Command.set_mask bits: which Runtime_Settings fields a setup carries
#define SETTING_ENABLED_PREDICTORS (UINT32_C(1) << 0)
#define SETTING_ACTIVE_PREDICTOR (UINT32_C(1) << 1)
#define SETTING_HORIZON (UINT32_C(1) << 2)
#define SETTING_METRICS_HZ (UINT32_C(1) << 3)
#define SETTING_SPECTRUM_HZ (UINT32_C(1) << 4)

typedef struct Runtime_Settings
{
  uint32_t enabled_mask; // bit i = predictor id i enabled
  Predictor_Id active_predictor_id;
  int64_t horizon_ns;
  double metrics_hz;
  double spectrum_hz;
} Runtime_Settings;

typedef struct Command
{
  uint32_t kind; // CMD_*
  uint32_t id; // chosen by the UI, echoed in the reply
  uint32_t set_mask; // CMD_SETUP only: SETTING_* bits present
  uint32_t client; // server-side id of the client that sent it: copy it into the reply
  Runtime_Settings settings; // CMD_SETUP only: fields flagged in set_mask
} Command;

// Command_Reply.error values: ERR_NONE is an ack, the others the UI protocol's error codes
#define ERR_NONE 0u
#define ERR_NEEDS_CALIBRATION 1u
#define ERR_BUSY 2u
#define ERR_INVALID_STATE 3u
#define ERR_INVALID_ARGUMENT 4u
#define ERR_UNKNOWN_COMMAND 5u
#define ERR_MALFORMED_MESSAGE 6u
#define ERR_HAT_ERROR 7u
#define ERR_CALIBRATION_FAILED 8u

#define REPLY_MSG_BYTES 96

typedef struct Command_Reply
{
  uint32_t id; // the command's id (if has_id)
  uint32_t has_id; // 0: answers no command (or its id could not be read)
  uint32_t error; // ERR_*
  uint32_t client; // the client to answer (Command::client), 0 = every client
  char msg[REPLY_MSG_BYTES]; // human-readable, NUL-terminated (errors only)
} Command_Reply;

#ifdef __cplusplus
}
#endif

#endif /* MTS_APP_LINK_COMMAND_H */
