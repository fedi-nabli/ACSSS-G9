/* ===================================================================================================
 * tests/app/ws/ws_tests.cc
 *
 * Tests for the WebSocket codec: pure functions, no sockets. Reference
 * values come from the standards (FIPS 180 for SHA-1, RFC 4648 for base64,
 * RFC 6455 for the handshake and frames), not from this implementation.
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/ws/ws_codec.hpp>

#include <cstring>
#include <string>

namespace
{

std::string hex(const uint8_t* data, size_t len)
{
  static const char DIGITS[] = "0123456789abcdef";
  std::string out;
  for (size_t i = 0; i < len; i++)
  {
    out.push_back(DIGITS[data[i] >> 4]);
    out.push_back(DIGITS[data[i] & 0xF]);
  }
  return out;
}

std::string sha1_hex(const std::string& s)
{
  std::array<uint8_t, 20> d = mts::ws_sha1(s.data(), s.size());
  return hex(d.data(), d.size());
}

std::string b64(const std::string& s)
{
  return mts::ws_base64((const uint8_t*)s.data(), s.size());
}

mts::Ws_Parse parse(const std::string& bytes, size_t max, mts::Ws_Frame& f, size_t& consumed)
{
  return mts::ws_parse_frame((const uint8_t*)bytes.data(), bytes.size(), max, f, consumed);
}

const char* VALID_REQUEST =
  "GET /chat HTTP/1.1\r\n"
  "Host: server.example.com\r\n"
  "Upgrade: websocket\r\n"
  "Connection: Upgrade\r\n"
  "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
  "Sec-WebSocket-Version: 13\r\n"
  "\r\n";

} // namespace

/* ---------------------------------------------------------------------------------------------------
 * SHA-1, base64, accept key
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(sha1_fips_vectors)
{
  MTS_ASSERT(sha1_hex("abc") == "a9993e364706816aba3e25717850c26c9cd0d89d");
  MTS_ASSERT(sha1_hex("") == "da39a3ee5e6b4b0d3255bfef95601890afd80709");
  // 56 bytes: the padding needs a second block
  MTS_ASSERT(sha1_hex("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq")
             == "84983e441c3bd26ebaae4aa1f95129e5e54670f1");
  // 1,000,000 x 'a': many full blocks
  MTS_ASSERT(sha1_hex(std::string(1000000, 'a')) == "34aa973cd4c4daa4f61eeb2bdbad27316534016f");
}

MTS_TEST(base64_rfc4648_vectors)
{
  MTS_ASSERT(b64("") == "");
  MTS_ASSERT(b64("f") == "Zg==");
  MTS_ASSERT(b64("fo") == "Zm8=");
  MTS_ASSERT(b64("foo") == "Zm9v");
  MTS_ASSERT(b64("foob") == "Zm9vYg==");
  MTS_ASSERT(b64("fooba") == "Zm9vYmE=");
  MTS_ASSERT(b64("foobar") == "Zm9vYmFy");
}

MTS_TEST(accept_key_rfc6455_example)
{
  // The worked example of RFC 6455, section 1.3
  MTS_ASSERT(mts::ws_accept_key("dGhlIHNhbXBsZSBub25jZQ==") == "s3pPLMBiTxaQ9kYGzzhZRbK+xOo=");
}

/* ---------------------------------------------------------------------------------------------------
 * Handshake
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(handshake_valid_request)
{
  std::string key;
  size_t consumed = 0;
  std::string buf = std::string(VALID_REQUEST) + "EXTRA";

  MTS_ASSERT(mts::ws_parse_handshake(buf, key, consumed) == mts::Ws_Handshake::ok);
  MTS_ASSERT(key == "dGhlIHNhbXBsZSBub25jZQ==");
  MTS_ASSERT_EQ(consumed, std::strlen(VALID_REQUEST)); // frames after it stay in the buffer

  std::string response = mts::ws_handshake_response(key);
  MTS_ASSERT(response.find("HTTP/1.1 101") == 0);
  MTS_ASSERT(response.find("Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n") != std::string::npos);
}

MTS_TEST(handshake_header_names_case_insensitive)
{
  std::string buf =
    "GET / HTTP/1.1\r\n"
    "upgrade: WebSocket\r\n"
    "CONNECTION: keep-alive, Upgrade\r\n"
    "sec-websocket-key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
    "Sec-WebSocket-Version: 13\r\n\r\n";
  std::string key;
  size_t consumed = 0;
  MTS_ASSERT(mts::ws_parse_handshake(buf, key, consumed) == mts::Ws_Handshake::ok);
}

MTS_TEST(handshake_incomplete_until_blank_line)
{
  std::string full = VALID_REQUEST;
  std::string key;
  size_t consumed = 0;
  for (size_t cut = 0; cut < full.size(); cut++)
  {
    MTS_ASSERT(mts::ws_parse_handshake(full.substr(0, cut), key, consumed) == mts::Ws_Handshake::incomplete);
  }
}

MTS_TEST(handshake_rejects_non_upgrades)
{
  std::string key;
  size_t consumed = 0;

  // A plain page request
  MTS_ASSERT(mts::ws_parse_handshake("GET / HTTP/1.1\r\nHost: x\r\n\r\n", key, consumed) == mts::Ws_Handshake::bad);

  // Wrong method, wrong version, missing key
  std::string post = VALID_REQUEST;
  post.replace(0, 3, "POST");
  MTS_ASSERT(mts::ws_parse_handshake(post, key, consumed) == mts::Ws_Handshake::bad);

  std::string v12 = VALID_REQUEST;
  v12.replace(v12.find("Version: 13"), 11, "Version: 12");
  MTS_ASSERT(mts::ws_parse_handshake(v12, key, consumed) == mts::Ws_Handshake::bad);

  std::string no_key = VALID_REQUEST;
  no_key.erase(no_key.find("Sec-WebSocket-Key"), std::strlen("Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"));
  MTS_ASSERT(mts::ws_parse_handshake(no_key, key, consumed) == mts::Ws_Handshake::bad);

  // Over 8 KiB without a blank line
  MTS_ASSERT(mts::ws_parse_handshake("GET / HTTP/1.1\r\n" + std::string(9000, 'x'), key, consumed)
             == mts::Ws_Handshake::bad);
}

/* ---------------------------------------------------------------------------------------------------
 * Frames
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(rfc6455_example_frames)
{
  // Section 5.7: unmasked "Hello" and masked "Hello"
  const std::string unmasked("\x81\x05\x48\x65\x6c\x6c\x6f", 7);
  const std::string masked("\x81\x85\x37\xfa\x21\x3d\x7f\x9f\x4d\x51\x58", 11);

  mts::Ws_Frame f;
  size_t consumed = 0;

  MTS_ASSERT(parse(unmasked, 1024, f, consumed) == mts::Ws_Parse::frame);
  MTS_ASSERT(f.fin && f.opcode == mts::Ws_Opcode::text && !f.masked && f.payload == "Hello");
  MTS_ASSERT_EQ(consumed, 7);

  MTS_ASSERT(parse(masked, 1024, f, consumed) == mts::Ws_Parse::frame);
  MTS_ASSERT(f.masked && f.payload == "Hello");
  MTS_ASSERT_EQ(consumed, 11);

  // Our encoders produce exactly those bytes
  std::string out;
  mts::ws_encode_frame(out, mts::Ws_Opcode::text, "Hello", 5);
  MTS_ASSERT(out == unmasked);

  out.clear();
  mts::ws_encode_masked_frame(out, mts::Ws_Opcode::text, true, "Hello", 5, 0x37fa213d);
  MTS_ASSERT(out == masked);
}

MTS_TEST(all_three_length_encodings_roundtrip)
{
  // 7-bit, 16-bit and 64-bit lengths, at their boundaries
  const size_t sizes[] = { 0, 125, 126, 65535, 65536, 200000 };
  for (size_t size : sizes)
  {
    std::string payload(size, 'x');
    for (size_t i = 0; i < size; i++)
    {
      payload[i] = (char)('a' + i % 26);
    }

    std::string wire;
    mts::ws_encode_masked_frame(wire, mts::Ws_Opcode::text, true, payload.data(), payload.size(), 0xA1B2C3D4);

    mts::Ws_Frame f;
    size_t consumed = 0;
    MTS_ASSERT(parse(wire, 1 << 20, f, consumed) == mts::Ws_Parse::frame);
    MTS_ASSERT_EQ(consumed, wire.size());
    MTS_ASSERT(f.payload == payload);

    // Every proper prefix is incomplete, never an error or a wrong frame
    const size_t cuts[] = { 0, 1, 2, 3, 9, 13, wire.size() - 1 };
    for (size_t cut : cuts)
    {
      if (cut < wire.size())
      {
        MTS_ASSERT(parse(wire.substr(0, cut), 1 << 20, f, consumed) == mts::Ws_Parse::incomplete);
      }
    }
  }
}

MTS_TEST(two_frames_back_to_back)
{
  std::string wire;
  mts::ws_encode_masked_frame(wire, mts::Ws_Opcode::text, true, "one", 3, 1);
  mts::ws_encode_masked_frame(wire, mts::Ws_Opcode::text, true, "two", 3, 2);

  mts::Ws_Frame f;
  size_t consumed = 0;
  MTS_ASSERT(parse(wire, 1024, f, consumed) == mts::Ws_Parse::frame);
  MTS_ASSERT(f.payload == "one");

  std::string rest = wire.substr(consumed);
  MTS_ASSERT(parse(rest, 1024, f, consumed) == mts::Ws_Parse::frame);
  MTS_ASSERT(f.payload == "two");
  MTS_ASSERT_EQ(consumed, rest.size());
}

MTS_TEST(too_big_known_from_header)
{
  // Only the header of a 1 MiB frame: refused without its payload
  std::string wire;
  std::string payload(1 << 20, 'x');
  mts::ws_encode_masked_frame(wire, mts::Ws_Opcode::text, true, payload.data(), payload.size(), 7);
  std::string header = wire.substr(0, 14);

  mts::Ws_Frame f;
  size_t consumed = 0;
  MTS_ASSERT(parse(header, 64 * 1024, f, consumed) == mts::Ws_Parse::too_big);
}

MTS_TEST(protocol_errors)
{
  mts::Ws_Frame f;
  size_t consumed = 0;

  // Reserved bit set
  MTS_ASSERT(parse(std::string("\xC1\x80\0\0\0\0", 6), 1024, f, consumed) == mts::Ws_Parse::error);
  // Unknown opcode 0x3
  MTS_ASSERT(parse(std::string("\x83\x80\0\0\0\0", 6), 1024, f, consumed) == mts::Ws_Parse::error);
  // Fragmented ping
  MTS_ASSERT(parse(std::string("\x09\x80\0\0\0\0", 6), 1024, f, consumed) == mts::Ws_Parse::error);
  // Ping with a 126-byte payload (control frames: 125 max)
  MTS_ASSERT(parse(std::string("\x89\xFE\x00\x7E", 4), 1024, f, consumed) == mts::Ws_Parse::error);
  // 64-bit length with the top bit set
  MTS_ASSERT(parse(std::string("\x81\xFF\x80\0\0\0\0\0\0\0", 10), 1024, f, consumed) == mts::Ws_Parse::error);
}

MTS_TEST(close_payload_is_big_endian_code)
{
  std::string p = mts::ws_close_payload(mts::WS_CLOSE_TOO_BIG);
  MTS_ASSERT_EQ(p.size(), 2);
  MTS_ASSERT_EQ((uint8_t)p[0], 0x03); // 1009 = 0x03F1
  MTS_ASSERT_EQ((uint8_t)p[1], 0xF1);
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(sha1_fips_vectors),
  MTS_TEST_ENTRY(base64_rfc4648_vectors),
  MTS_TEST_ENTRY(accept_key_rfc6455_example),
  MTS_TEST_ENTRY(handshake_valid_request),
  MTS_TEST_ENTRY(handshake_header_names_case_insensitive),
  MTS_TEST_ENTRY(handshake_incomplete_until_blank_line),
  MTS_TEST_ENTRY(handshake_rejects_non_upgrades),
  MTS_TEST_ENTRY(rfc6455_example_frames),
  MTS_TEST_ENTRY(all_three_length_encodings_roundtrip),
  MTS_TEST_ENTRY(two_frames_back_to_back),
  MTS_TEST_ENTRY(too_big_known_from_header),
  MTS_TEST_ENTRY(protocol_errors),
  MTS_TEST_ENTRY(close_payload_is_big_endian_code),
};

MTS_TEST_MAIN(tests)

