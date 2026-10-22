/* ===================================================================================================
 * app/include/app/protocol/json.hpp
 *
 * A small, strict JSON parser (RFC 8259) for the commands coming from the
 * UI: untrusted network input, so strict on purpose.
 *
 * Rejected: trailing commas, leading zeros, NaN/Infinity, numbers that
 * overflow a double, control characters inside strings, lone surrogates,
 * duplicate object keys, nesting deeper than 32, anything after the value.
 * \uXXXX escapes (surrogate pairs included) are decoded to UTF-8.
 *
 * Objects keep their keys in document order (a vector, not a map), which
 * also lets tests check the key order of what the encoder writes.
 *
 * Numbers are doubles: integers are exact up to 2^53, plenty for ids.
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#ifndef MTS_APP_PROTOCOL_JSON_HPP
#define MTS_APP_PROTOCOL_JSON_HPP

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

namespace mts
{

enum class Json_Type
{
  null_value,
  boolean,
  number,
  string,
  array,
  object,
};

struct Json_Value
{
  Json_Type type = Json_Type::null_value;
  bool boolean = false;
  double number = 0.0;
  std::string string;
  std::vector<Json_Value> array;
  std::vector<std::pair<std::string, Json_Value>> object; // document order

  // Object member by key, nullptr if absent (or not an object)
  const Json_Value* find(const std::string& key) const;

  // A number with no fractional part
  bool is_integer() const;
};

/* Parses exactly one JSON value from data[0 .. len), surrounded by optional
 * whitespace. false on any error; *error (if given) says what and where. */
bool json_parse(const char* data, size_t len, Json_Value& out, std::string* error = nullptr);

} // namespace mts

#endif /* MTS_APP_PROTOCOL_JSON_HPP */
