/* ===================================================================================================
 * app/src/ws/ws_codec.cc
 *
 * WebSocket codec, see app/ws/ws_codec.hpp
 *
 * Authors: Fedi Nabli
 * Date: 20 Oct 2026
 * Last Modified: 20 Oct 2026
 * ===================================================================================================
 */

#include <app/ws/ws_codec.hpp>

#include <cctype>
#include <cstring>

namespace mts
{

namespace
{

constexpr const char* WS_GUID = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr size_t MAX_HANDSHAKE_BYTES = 8192;

/* ---------------------------------------------------------------------------------------------------
 * SHA-1
 * ------------------------------------------------------------------------------------------------- */

uint32_t rotl32(uint32_t x, int k)
{
  return (x << k) | (x >> (32 - k));
}

// One 64-byte block into the state
void sha1_block(uint32_t h[5], const uint8_t block[64])
{
  uint32_t w[80];
  for (int i = 0; i < 16; i++)
  {
    w[i] = (uint32_t)block[4 * i] << 24 | (uint32_t)block[4 * i + 1] << 16 | (uint32_t)block[4 * i + 2] << 8
           | (uint32_t)block[4 * i + 3];
  }
  for (int i = 16; i < 80; i++)
  {
    w[i] = rotl32(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);
  }

  uint32_t a = h[0];
  uint32_t b = h[1];
  uint32_t c = h[2];
  uint32_t d = h[3];
  uint32_t e = h[4];

  for (int i = 0; i < 80; i++)
  {
    uint32_t f;
    uint32_t k;
    if (i < 20)
    {
      f = (b & c) | (~b & d);
      k = 0x5A827999;
    }
    else if (i < 40)
    {
      f = b ^ c ^ d;
      k = 0x6ED9EBA1;
    }
    else if (i < 60)
    {
      f = (b & c) | (b & d) | (c & d);
      k = 0x8F1BBCDC;
    }
    else
    {
      f = b ^ c ^ d;
      k = 0xCA62C1D6;
    }

    uint32_t temp = rotl32(a, 5) + f + e + k + w[i];
    e = d;
    d = c;
    c = rotl32(b, 30);
    b = a;
    a = temp;
  }

  h[0] += a;
  h[1] += b;
  h[2] += c;
  h[3] += d;
  h[4] += e;
}

/* ---------------------------------------------------------------------------------------------------
 * Handshake helpers
 * ------------------------------------------------------------------------------------------------- */

std::string to_lower(std::string s)
{
  for (char& c : s)
  {
    c = (char)std::tolower((unsigned char)c);
  }
  return s;
}

std::string trim(const std::string& s)
{
  size_t start = 0;
  size_t end = s.size();
  while (start < end && (s[start] == ' ' || s[start] == '\t'))
  {
    start++;
  }
  while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t'))
  {
    end--;
  }
  return s.substr(start, end - start);
}

// true if the comma-separated header value contains token (case-insensitive)
bool has_token(const std::string& value, const char* token)
{
  std::string lower = to_lower(value);
  size_t start = 0;
  while (start <= lower.size())
  {
    size_t comma = lower.find(',', start);
    if (comma == std::string::npos)
    {
      comma = lower.size();
    }
    if (trim(lower.substr(start, comma - start)) == token)
    {
      return true;
    }
    start = comma + 1;
  }
  return false;
}

void append_length(std::string& out, uint8_t first_bits, size_t len)
{
  if (len < 126)
  {
    out.push_back((char)(first_bits | (uint8_t)len));
  }
  else if (len <= 0xFFFF)
  {
    out.push_back((char)(first_bits | 126));
    out.push_back((char)(len >> 8));
    out.push_back((char)(len & 0xFF));
  }
  else
  {
    out.push_back((char)(first_bits | 127));
    for (int shift = 56; shift >= 0; shift -= 8)
    {
      out.push_back((char)(((uint64_t)len >> shift) & 0xFF));
    }
  }
}

} // namespace

std::array<uint8_t, 20> ws_sha1(const void* data, size_t len)
{
  uint32_t h[5] = { 0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0 };
  const uint8_t* bytes = (const uint8_t*)data;

  // Whole blocks straight from the input
  size_t full = len / 64;
  for (size_t i = 0; i < full; i++)
  {
    sha1_block(h, bytes + 64 * i);
  }

  // Tail + 0x80 + zeros + 64-bit big-endian bit length: one or two blocks
  uint8_t tail[128] = { 0 };
  size_t rest = len % 64;
  std::memcpy(tail, bytes + 64 * full, rest);
  tail[rest] = 0x80;
  size_t tail_len = rest < 56 ? 64 : 128;
  uint64_t bits = (uint64_t)len * 8;
  for (int i = 0; i < 8; i++)
  {
    tail[tail_len - 1 - i] = (uint8_t)(bits >> (8 * i));
  }
  sha1_block(h, tail);
  if (tail_len == 128)
  {
    sha1_block(h, tail + 64);
  }

  std::array<uint8_t, 20> digest;
  for (int i = 0; i < 5; i++)
  {
    digest[4 * i] = (uint8_t)(h[i] >> 24);
    digest[4 * i + 1] = (uint8_t)(h[i] >> 16);
    digest[4 * i + 2] = (uint8_t)(h[i] >> 8);
    digest[4 * i + 3] = (uint8_t)h[i];
  }
  return digest;
}

std::string ws_base64(const uint8_t* data, size_t len)
{
  static const char ALPHABET[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

  std::string out;
  out.reserve((len + 2) / 3 * 4);
  for (size_t i = 0; i < len; i += 3)
  {
    uint32_t chunk = (uint32_t)data[i] << 16;
    if (i + 1 < len)
    {
      chunk |= (uint32_t)data[i + 1] << 8;
    }
    if (i + 2 < len)
    {
      chunk |= data[i + 2];
    }

    out.push_back(ALPHABET[(chunk >> 18) & 0x3F]);
    out.push_back(ALPHABET[(chunk >> 12) & 0x3F]);
    out.push_back(i + 1 < len ? ALPHABET[(chunk >> 6) & 0x3F] : '=');
    out.push_back(i + 2 < len ? ALPHABET[chunk & 0x3F] : '=');
  }
  return out;
}

std::string ws_accept_key(const std::string& client_key)
{
  std::string input = client_key + WS_GUID;
  std::array<uint8_t, 20> digest = ws_sha1(input.data(), input.size());
  return ws_base64(digest.data(), digest.size());
}

Ws_Handshake ws_parse_handshake(const std::string& buf, std::string& key, size_t& consumed)
{
  size_t end = buf.find("\r\n\r\n");
  if (end == std::string::npos)
  {
    return buf.size() > MAX_HANDSHAKE_BYTES ? Ws_Handshake::bad : Ws_Handshake::incomplete;
  }
  if (end + 4 > MAX_HANDSHAKE_BYTES)
  {
    return Ws_Handshake::bad;
  }

  // Request line: GET <path> HTTP/1.1
  size_t line_end = buf.find("\r\n");
  std::string request_line = buf.substr(0, line_end);
  if (request_line.compare(0, 4, "GET ") != 0 || request_line.find(" HTTP/1.1") == std::string::npos)
  {
    return Ws_Handshake::bad;
  }

  bool upgrade = false;
  bool connection = false;
  bool version = false;
  std::string found_key;

  size_t pos = line_end + 2;
  while (pos < end)
  {
    size_t next = buf.find("\r\n", pos);
    std::string line = buf.substr(pos, next - pos);
    pos = next + 2;

    size_t colon = line.find(':');
    if (colon == std::string::npos)
    {
      return Ws_Handshake::bad;
    }
    // Header names are case-insensitive
    std::string name = to_lower(trim(line.substr(0, colon)));
    std::string value = trim(line.substr(colon + 1));

    if (name == "upgrade")
    {
      upgrade = has_token(value, "websocket");
    }
    else if (name == "connection")
    {
      connection = has_token(value, "upgrade");
    }
    else if (name == "sec-websocket-version")
    {
      version = value == "13";
    }
    else if (name == "sec-websocket-key")
    {
      found_key = value;
    }
  }

  // The key is 16 random bytes in base64: always 24 characters
  if (!upgrade || !connection || !version || found_key.size() != 24)
  {
    return Ws_Handshake::bad;
  }

  key = found_key;
  consumed = end + 4;
  return Ws_Handshake::ok;
}

std::string ws_handshake_response(const std::string& client_key)
{
  return "HTTP/1.1 101 Switching Protocols\r\n"
         "Upgrade: websocket\r\n"
         "Connection: Upgrade\r\n"
         "Sec-WebSocket-Accept: "
         + ws_accept_key(client_key) + "\r\n\r\n";
}

std::string ws_bad_request_response()
{
  return "HTTP/1.1 400 Bad Request\r\n"
         "Connection: close\r\n"
         "Content-Length: 0\r\n\r\n";
}

Ws_Parse ws_parse_frame(const uint8_t* data, size_t len, size_t max_payload, Ws_Frame& out, size_t& consumed)
{
  if (len < 2)
  {
    return Ws_Parse::incomplete;
  }

  const uint8_t b0 = data[0];
  const uint8_t b1 = data[1];

  // RSV1-3 are only set by extensions, and we negotiate none
  if (b0 & 0x70)
  {
    return Ws_Parse::error;
  }

  const uint8_t op = b0 & 0x0F;
  if (op != 0x0 && op != 0x1 && op != 0x2 && op != 0x8 && op != 0x9 && op != 0xA)
  {
    return Ws_Parse::error;
  }

  const bool fin = (b0 & 0x80) != 0;
  const bool masked = (b1 & 0x80) != 0;
  uint64_t payload_len = b1 & 0x7F;
  size_t pos = 2;

  if (payload_len == 126)
  {
    if (len < 4)
    {
      return Ws_Parse::incomplete;
    }
    payload_len = (uint64_t)data[2] << 8 | data[3];
    pos = 4;
  }
  else if (payload_len == 127)
  {
    if (len < 10)
    {
      return Ws_Parse::incomplete;
    }
    payload_len = 0;
    for (int i = 0; i < 8; i++)
    {
      payload_len = payload_len << 8 | data[2 + i];
    }
    // The most significant bit must be 0
    if (payload_len >> 63)
    {
      return Ws_Parse::error;
    }
    pos = 10;
  }

  // Control frames: never fragmented, at most 125 bytes
  const bool control = (op & 0x8) != 0;
  if (control && (!fin || payload_len > 125))
  {
    return Ws_Parse::error;
  }

  // Known from the header: refuse before buffering the payload
  if (payload_len > max_payload)
  {
    return Ws_Parse::too_big;
  }

  uint8_t mask[4] = { 0, 0, 0, 0 };
  if (masked)
  {
    if (len < pos + 4)
    {
      return Ws_Parse::incomplete;
    }
    std::memcpy(mask, data + pos, 4);
    pos += 4;
  }

  if (len - pos < payload_len)
  {
    return Ws_Parse::incomplete;
  }

  out.fin = fin;
  out.opcode = (Ws_Opcode)op;
  out.masked = masked;
  out.payload.assign((const char*)data + pos, (size_t)payload_len);
  if (masked)
  {
    for (size_t i = 0; i < out.payload.size(); i++)
    {
      out.payload[i] = (char)(out.payload[i] ^ mask[i % 4]);
    }
  }

  consumed = pos + (size_t)payload_len;
  return Ws_Parse::frame;
}

void ws_encode_frame(std::string& out, Ws_Opcode opcode, const char* payload, size_t len)
{
  out.push_back((char)(0x80 | (uint8_t)opcode));
  append_length(out, 0x00, len);
  out.append(payload, len);
}

void ws_encode_masked_frame(std::string& out, Ws_Opcode opcode, bool fin, const char* payload, size_t len, uint32_t mask_key)
{
  const uint8_t mask[4] = {
    (uint8_t)(mask_key >> 24),
    (uint8_t)(mask_key >> 16),
    (uint8_t)(mask_key >> 8),
    (uint8_t)mask_key,
  };

  out.push_back((char)((fin ? 0x80 : 0x00) | (uint8_t)opcode));
  append_length(out, 0x80, len);
  out.append((const char*)mask, 4);
  for (size_t i = 0; i < len; i++)
  {
    out.push_back((char)(payload[i] ^ mask[i % 4]));
  }
}

std::string ws_close_payload(uint16_t code)
{
  std::string payload;
  payload.push_back((char)(code >> 8));
  payload.push_back((char)(code & 0xFF));
  return payload;
}

} // namespace mts

