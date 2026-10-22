/* ===================================================================================================
 * app/include/app/ws/ws_codec.hpp
 *
 * WebSocket (RFC 6455) as pure functions: no sockets, no state, no
 * allocation beyond the strings they return. Ws_Server is built on these;
 * they are public so they can be tested exhaustively without a network.
 *
 *  - ws_sha1, ws_base64: just enough crypto/encoding for the handshake
 *  - ws_parse_handshake / ws_handshake_response: the HTTP upgrade
 *  - ws_parse_frame: one frame from a byte stream, incrementally (returns
 *    incomplete until the whole frame is there)
 *  - ws_encode_frame: one server -> client frame (unmasked, FIN set)
 *  - ws_encode_masked_frame: client -> server, for tests and tools
 *
 * Not implemented (not needed by our UI): extensions, subprotocols, UTF-8
 * validation of text payloads.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_WS_WS_CODEC_HPP
#define MTS_APP_WS_WS_CODEC_HPP

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>

namespace mts
{

// SHA-1 digest of len bytes (FIPS 180-4). Only for the handshake: SHA-1 is
// not a secure hash any more, and nothing here relies on it being one
std::array<uint8_t, 20> ws_sha1(const void* data, size_t len);

// Standard base64 with '=' padding
std::string ws_base64(const uint8_t* data, size_t len);

// Sec-WebSocket-Accept for a client's Sec-WebSocket-Key:
// base64(sha1(key + "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"))
std::string ws_accept_key(const std::string& client_key);

enum class Ws_Handshake
{
  incomplete, // no blank line yet: read more
  ok, // valid upgrade request: key filled
  bad, // not a WebSocket upgrade (or too large): answer 400 and close
};

/* Parses the HTTP request at the start of buf. On ok, key is the client's
 * Sec-WebSocket-Key and consumed the request's length (bytes after it are
 * already WebSocket frames). Requests over 8 KiB are bad. */
Ws_Handshake ws_parse_handshake(const std::string& buf, std::string& key, size_t& consumed);

// "101 Switching Protocols" response for a valid key
std::string ws_handshake_response(const std::string& client_key);

// Response to anything that is not a valid upgrade
std::string ws_bad_request_response();

enum class Ws_Opcode : uint8_t
{
  continuation = 0x0,
  text = 0x1,
  binary = 0x2,
  close = 0x8,
  ping = 0x9,
  pong = 0xA,
};

// Close status codes we send
constexpr uint16_t WS_CLOSE_NORMAL = 1000;
constexpr uint16_t WS_CLOSE_PROTOCOL_ERROR = 1002;
constexpr uint16_t WS_CLOSE_UNSUPPORTED = 1003;
constexpr uint16_t WS_CLOSE_TOO_BIG = 1009;

struct Ws_Frame
{
  bool fin;
  Ws_Opcode opcode;
  bool masked; // client frames must be masked, server frames must not
  std::string payload; // already unmasked
};

enum class Ws_Parse
{
  incomplete, // not a whole frame yet: read more
  frame, // out filled, consumed bytes used
  too_big, // payload larger than max_payload (detected from the header alone)
  error, // protocol error: reserved bits, unknown opcode, bad control frame
};

/* Parses one frame from data[0 .. len). Never reads past len and never
 * waits for a payload it is going to refuse: too_big is known from the
 * header. */
Ws_Parse ws_parse_frame(const uint8_t* data, size_t len, size_t max_payload, Ws_Frame& out, size_t& consumed);

// Appends one unmasked frame with FIN set (server -> client)
void ws_encode_frame(std::string& out, Ws_Opcode opcode, const char* payload, size_t len);

// Appends one masked frame (client -> server), for tests and tools
void ws_encode_masked_frame(std::string& out, Ws_Opcode opcode, bool fin, const char* payload, size_t len, uint32_t mask_key);

// Close frame payload: the 2-byte status code, big-endian
std::string ws_close_payload(uint16_t code);

} // namespace mts

#endif /* MTS_APP_WS_WS_CODEC_HPP */

