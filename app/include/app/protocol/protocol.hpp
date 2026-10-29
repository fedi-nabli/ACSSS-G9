/* ===================================================================================================
 * app/include/app/protocol/protocol.hpp
 *
 * The UI protocol, version 1 (docs/ui_contract.md): our structs <-> NDJSON
 * lines. Knows nothing about sockets (that is ws) or batching and threads
 * (that is server).
 *
 * Encoding (server -> UI):
 *  - SI -> wire units: m -> mm, ns -> ms. timestamp_ns is made relative to
 *    the moment the Hat opened (set_origin), as the contract requires; that
 *    also keeps it below 2^53, where JavaScript numbers stay exact.
 *  - Measurements printed with %.6g (well past the sensor's precision),
 *    integers exactly. NaN/infinity are not JSON: they go out as null.
 *  - Keys in the contract's table order. Every line ends with '\n'.
 *
 * Decoding (UI -> server): one command line. Checks shape, types and
 * predictor names, and maps failures to the contract's codes:
 *   bad JSON, not an object, id/command missing -> malformed_message
 *   command not known                           -> unknown_command
 *   bad setup value, unknown predictor or field -> invalid_argument
 * Checks that need the state or the measured rate (busy, needs_calibration,
 * horizon against K_MAX) belong to the orchestrator.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_PROTOCOL_PROTOCOL_HPP
#define MTS_APP_PROTOCOL_PROTOCOL_HPP

#include <app/link/command.h>
#include <app/link/server_message.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace mts
{

constexpr int PROTOCOL_VERSION = 1;

class Protocol_Encoder
{
public:
  // t_ns of the moment the Hat opened: timestamp_ns = t_ns - origin
  void set_origin(int64_t origin_t_ns)
  {
    origin_t_ns_ = origin_t_ns;
  }

  /* Appends the message's line(s): one for frame, spectrum and status, one
   * per predictor for metrics. false (nothing appended) for an unknown kind. */
  bool encode(const Server_Message& msg, std::string& out) const;

  // Appends an ack (ERR_NONE) or an error line. Nothing for an ack without an id
  void encode_reply(const Command_Reply& reply, std::string& out) const;

private:
  void encode_frame(const Output_Frame& f, std::string& out) const;
  void encode_metrics(const Metrics_Message& m, std::string& out) const;
  void encode_spectrum(const Spectrum_Message& s, std::string& out) const;
  void encode_status(const Status_Message& s, std::string& out) const;

  int64_t origin_t_ns_ = 0;
};

/* Decodes one command line. true: out is filled. false: err holds the
 * error to send back (with the command's id whenever it could be read). */
bool protocol_decode_command(const char* data, size_t len, Command& out, Command_Reply& err);

// Wire name of an error code ("needs_calibration", ...), "unknown" if out of range
const char* protocol_error_code_name(uint32_t error);

// Wire name of a state ("running", ...), "unknown" if out of range
const char* protocol_state_name(uint32_t state);

} // namespace mts

#endif /* MTS_APP_PROTOCOL_PROTOCOL_HPP */
