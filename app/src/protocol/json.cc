/* ===================================================================================================
 * app/src/protocol/json.cc
 *
 * Strict JSON parser, see app/protocol/json.hpp. Recursive descent over the
 * RFC 8259 grammar, one function per production.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <app/protocol/json.hpp>

#include <cmath>
#include <cstdint>
#include <cstdlib>

namespace mts
{

namespace
{

// At most 32 nested arrays/objects (also bounds the recursion, so a hostile
// "[[[[..." cannot exhaust the stack)
constexpr int MAX_DEPTH = 32;

class Parser
{
public:
  Parser(const char* data, size_t len)
    : p_(data)
    , begin_(data)
    , end_(data + len)
  {
  }

  bool parse_document(Json_Value& out)
  {
    skip_whitespace();
    if (!parse_value(out, 0))
    {
      return false;
    }
    skip_whitespace();
    if (p_ != end_)
    {
      return fail("unexpected data after the value");
    }
    return true;
  }

  std::string error() const
  {
    return error_ + " at offset " + std::to_string(error_at_);
  }

private:
  bool fail(const char* what)
  {
    error_ = what;
    error_at_ = (size_t)(p_ - begin_);
    return false;
  }

  void skip_whitespace()
  {
    while (p_ < end_ && (*p_ == ' ' || *p_ == '\t' || *p_ == '\n' || *p_ == '\r'))
    {
      p_++;
    }
  }

  // Consumes the exact keyword (true / false / null)
  bool literal(const char* word)
  {
    const char* q = p_;
    for (const char* w = word; *w; w++, q++)
    {
      if (q == end_ || *q != *w)
      {
        return fail("invalid literal");
      }
    }
    p_ = q;
    return true;
  }

  bool parse_value(Json_Value& out, int depth)
  {
    if (depth >= MAX_DEPTH)
    {
      return fail("nesting too deep");
    }
    if (p_ == end_)
    {
      return fail("unexpected end of input");
    }

    switch (*p_)
    {
      case '{':
        return parse_object(out, depth);
      case '[':
        return parse_array(out, depth);
      case '"':
        out.type = Json_Type::string;
        return parse_string(out.string);
      case 't':
        out.type = Json_Type::boolean;
        out.boolean = true;
        return literal("true");
      case 'f':
        out.type = Json_Type::boolean;
        out.boolean = false;
        return literal("false");
      case 'n':
        out.type = Json_Type::null_value;
        return literal("null");
      default:
        return parse_number(out);
    }
  }

  bool parse_object(Json_Value& out, int depth)
  {
    out.type = Json_Type::object;
    p_++; // '{'
    skip_whitespace();
    if (p_ < end_ && *p_ == '}')
    {
      p_++;
      return true;
    }

    for (;;)
    {
      skip_whitespace();
      if (p_ == end_ || *p_ != '"')
      {
        return fail("expected a string key");
      }
      std::string key;
      if (!parse_string(key))
      {
        return false;
      }
      // Duplicate keys are legal JSON but ambiguous: which one wins?
      if (out.find(key) != nullptr)
      {
        return fail("duplicate key");
      }

      skip_whitespace();
      if (p_ == end_ || *p_ != ':')
      {
        return fail("expected ':'");
      }
      p_++;
      skip_whitespace();

      Json_Value value;
      if (!parse_value(value, depth + 1))
      {
        return false;
      }
      out.object.emplace_back(std::move(key), std::move(value));

      skip_whitespace();
      if (p_ == end_)
      {
        return fail("unterminated object");
      }
      if (*p_ == ',')
      {
        p_++;
        continue;
      }
      if (*p_ == '}')
      {
        p_++;
        return true;
      }
      return fail("expected ',' or '}'");
    }
  }

  bool parse_array(Json_Value& out, int depth)
  {
    out.type = Json_Type::array;
    p_++; // '['
    skip_whitespace();
    if (p_ < end_ && *p_ == ']')
    {
      p_++;
      return true;
    }

    for (;;)
    {
      skip_whitespace();
      Json_Value value;
      if (!parse_value(value, depth + 1))
      {
        return false;
      }
      out.array.push_back(std::move(value));

      skip_whitespace();
      if (p_ == end_)
      {
        return fail("unterminated array");
      }
      if (*p_ == ',')
      {
        p_++;
        continue;
      }
      if (*p_ == ']')
      {
        p_++;
        return true;
      }
      return fail("expected ',' or ']'");
    }
  }

  // Four hex digits after \u
  bool parse_hex4(uint32_t& code)
  {
    if (end_ - p_ < 4)
    {
      return fail("truncated \\u escape");
    }
    code = 0;
    for (int i = 0; i < 4; i++, p_++)
    {
      char c = *p_;
      code <<= 4;
      if (c >= '0' && c <= '9')
      {
        code |= (uint32_t)(c - '0');
      }
      else if (c >= 'a' && c <= 'f')
      {
        code |= (uint32_t)(c - 'a' + 10);
      }
      else if (c >= 'A' && c <= 'F')
      {
        code |= (uint32_t)(c - 'A' + 10);
      }
      else
      {
        return fail("invalid hex digit in \\u escape");
      }
    }
    return true;
  }

  static void append_utf8(std::string& out, uint32_t code)
  {
    if (code < 0x80)
    {
      out.push_back((char)code);
    }
    else if (code < 0x800)
    {
      out.push_back((char)(0xC0 | (code >> 6)));
      out.push_back((char)(0x80 | (code & 0x3F)));
    }
    else if (code < 0x10000)
    {
      out.push_back((char)(0xE0 | (code >> 12)));
      out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
      out.push_back((char)(0x80 | (code & 0x3F)));
    }
    else
    {
      out.push_back((char)(0xF0 | (code >> 18)));
      out.push_back((char)(0x80 | ((code >> 12) & 0x3F)));
      out.push_back((char)(0x80 | ((code >> 6) & 0x3F)));
      out.push_back((char)(0x80 | (code & 0x3F)));
    }
  }

  bool parse_string(std::string& out)
  {
    p_++; // opening quote
    out.clear();

    while (p_ < end_)
    {
      unsigned char c = (unsigned char)*p_;
      if (c == '"')
      {
        p_++;
        return true;
      }
      if (c < 0x20)
      {
        return fail("control character in string");
      }
      if (c != '\\')
      {
        out.push_back((char)c);
        p_++;
        continue;
      }

      p_++; // backslash
      if (p_ == end_)
      {
        break;
      }
      char e = *p_++;
      switch (e)
      {
        case '"':
          out.push_back('"');
          break;
        case '\\':
          out.push_back('\\');
          break;
        case '/':
          out.push_back('/');
          break;
        case 'b':
          out.push_back('\b');
          break;
        case 'f':
          out.push_back('\f');
          break;
        case 'n':
          out.push_back('\n');
          break;
        case 'r':
          out.push_back('\r');
          break;
        case 't':
          out.push_back('\t');
          break;
        case 'u':
        {
          uint32_t code = 0;
          if (!parse_hex4(code))
          {
            return false;
          }
          // UTF-16 surrogates: a high one must be followed by a low one
          if (code >= 0xDC00 && code <= 0xDFFF)
          {
            return fail("lone low surrogate");
          }
          if (code >= 0xD800 && code <= 0xDBFF)
          {
            if (end_ - p_ < 2 || p_[0] != '\\' || p_[1] != 'u')
            {
              return fail("high surrogate without a low one");
            }
            p_ += 2;
            uint32_t low = 0;
            if (!parse_hex4(low))
            {
              return false;
            }
            if (low < 0xDC00 || low > 0xDFFF)
            {
              return fail("high surrogate without a low one");
            }
            code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
          }
          append_utf8(out, code);
          break;
        }
        default:
          return fail("invalid escape");
      }
    }
    return fail("unterminated string");
  }

  bool parse_number(Json_Value& out)
  {
    // Check the grammar first: strtod alone would accept "+1", ".5", "0x1A",
    // "inf", "nan" and leading zeros
    const char* start = p_;
    const char* q = p_;

    if (q < end_ && *q == '-')
    {
      q++;
    }
    if (q == end_ || *q < '0' || *q > '9')
    {
      return fail("invalid value");
    }
    if (*q == '0')
    {
      q++;
      if (q < end_ && *q >= '0' && *q <= '9')
      {
        return fail("leading zero");
      }
    }
    else
    {
      while (q < end_ && *q >= '0' && *q <= '9')
      {
        q++;
      }
    }

    if (q < end_ && *q == '.')
    {
      q++;
      if (q == end_ || *q < '0' || *q > '9')
      {
        return fail("digit expected after '.'");
      }
      while (q < end_ && *q >= '0' && *q <= '9')
      {
        q++;
      }
    }

    if (q < end_ && (*q == 'e' || *q == 'E'))
    {
      q++;
      if (q < end_ && (*q == '+' || *q == '-'))
      {
        q++;
      }
      if (q == end_ || *q < '0' || *q > '9')
      {
        return fail("digit expected in exponent");
      }
      while (q < end_ && *q >= '0' && *q <= '9')
      {
        q++;
      }
    }

    // NUL-terminated copy for strtod (the input is not terminated).
    // The program never calls setlocale, so the decimal point is '.'
    std::string text(start, (size_t)(q - start));
    double value = std::strtod(text.c_str(), nullptr);
    if (!std::isfinite(value))
    {
      return fail("number out of range");
    }

    out.type = Json_Type::number;
    out.number = value;
    p_ = q;
    return true;
  }

  const char* p_;
  const char* begin_;
  const char* end_;
  std::string error_;
  size_t error_at_ = 0;
};

} // namespace

const Json_Value* Json_Value::find(const std::string& key) const
{
  if (type != Json_Type::object)
  {
    return nullptr;
  }
  for (const auto& member : object)
  {
    if (member.first == key)
    {
      return &member.second;
    }
  }
  return nullptr;
}

bool Json_Value::is_integer() const
{
  return type == Json_Type::number && std::floor(number) == number;
}

bool json_parse(const char* data, size_t len, Json_Value& out, std::string* error)
{
  Parser parser(data, len);
  Json_Value value;
  if (!parser.parse_document(value))
  {
    if (error)
    {
      *error = parser.error();
    }
    return false;
  }
  out = std::move(value);
  return true;
}

} // namespace mts
