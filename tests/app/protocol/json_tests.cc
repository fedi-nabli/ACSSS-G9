/* ===================================================================================================
 * tests/app/protocol/json_tests.cc
 *
 * The strict JSON parser: what it accepts, and everything it must refuse
 * (it reads untrusted network input).
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/protocol/json.hpp>

#include <string>

namespace
{

bool parses(const std::string& text, mts::Json_Value& out)
{
  return mts::json_parse(text.data(), text.size(), out);
}

bool parses(const std::string& text)
{
  mts::Json_Value v;
  return parses(text, v);
}

std::string nested_arrays(int depth)
{
  return std::string(depth, '[') + std::string(depth, ']');
}

} // namespace

MTS_TEST(scalars)
{
  mts::Json_Value v;

  MTS_ASSERT(parses("true", v) && v.type == mts::Json_Type::boolean && v.boolean);
  MTS_ASSERT(parses("false", v) && v.type == mts::Json_Type::boolean && !v.boolean);
  MTS_ASSERT(parses("null", v) && v.type == mts::Json_Type::null_value);

  MTS_ASSERT(parses("0", v) && v.type == mts::Json_Type::number);
  MTS_ASSERT_NEAR(v.number, 0.0, 0.0);
  MTS_ASSERT(parses("-12.5e3", v));
  MTS_ASSERT_NEAR(v.number, -12500.0, 0.0);
  MTS_ASSERT(parses("1.25E-2", v));
  MTS_ASSERT_NEAR(v.number, 0.0125, 1e-15);
  MTS_ASSERT(parses("4294967295", v) && v.is_integer());
  MTS_ASSERT(parses("1.5", v) && !v.is_integer());

  MTS_ASSERT(parses("\"plain\"", v) && v.type == mts::Json_Type::string && v.string == "plain");
}

MTS_TEST(string_escapes)
{
  mts::Json_Value v;
  MTS_ASSERT(parses("\"a\\\"b\\\\c\\/d\\b\\f\\n\\r\\t\"", v));
  MTS_ASSERT(v.string == "a\"b\\c/d\b\f\n\r\t");

  // \u escapes become UTF-8: e-acute (2 bytes), euro (3 bytes)
  MTS_ASSERT(parses("\"\\u00e9\\u20AC\"", v));
  MTS_ASSERT(v.string == "\xC3\xA9\xE2\x82\xAC");

  // A surrogate pair is one code point (U+1F600, 4 bytes)
  MTS_ASSERT(parses("\"\\ud83d\\ude00\"", v));
  MTS_ASSERT(v.string == "\xF0\x9F\x98\x80");

  // Raw UTF-8 passes through
  MTS_ASSERT(parses("\"\xC3\xA9\"", v));
  MTS_ASSERT(v.string == "\xC3\xA9");
}

MTS_TEST(objects_and_arrays)
{
  mts::Json_Value v;
  MTS_ASSERT(parses(" { \"b\" : [1, 2, {\"c\": null}] , \"a\":true }\r\n", v));
  MTS_ASSERT(v.type == mts::Json_Type::object);

  // Document order is kept
  MTS_ASSERT_EQ(v.object.size(), 2);
  MTS_ASSERT(v.object[0].first == "b");
  MTS_ASSERT(v.object[1].first == "a");

  const mts::Json_Value* b = v.find("b");
  MTS_ASSERT(b != nullptr && b->type == mts::Json_Type::array);
  MTS_ASSERT_EQ(b->array.size(), 3);
  MTS_ASSERT(b->array[2].find("c") != nullptr);
  MTS_ASSERT(v.find("missing") == nullptr);

  MTS_ASSERT(parses("{}", v) && v.object.empty());
  MTS_ASSERT(parses("[]", v) && v.array.empty());
}

MTS_TEST(rejects_bad_syntax)
{
  const char* bad[] = {
    "", " ", "{", "}", "[1,]", "{\"a\":1,}", "[1 2]", "{\"a\" 1}", "{1:2}", "{\"a\":}",
    "true false", "nul", "tru", "[", "\"abc",
  };
  for (const char* text : bad)
  {
    MTS_ASSERT(!parses(text));
  }
}

MTS_TEST(rejects_bad_numbers)
{
  const char* bad[] = {
    "01", "-01", "1.", ".5", "+1", "-", "1e", "1e+", "0x1A", "NaN", "Infinity", "-Infinity", "inf",
    "1e999", "-1e999",
  };
  for (const char* text : bad)
  {
    MTS_ASSERT(!parses(text));
  }
}

MTS_TEST(rejects_bad_strings)
{
  const char* bad[] = {
    "\"a\nb\"", // raw control character
    "\"a\tb\"",
    "\"\\x\"", // unknown escape
    "\"\\u12\"", // truncated \u
    "\"\\u12G4\"", // bad hex
    "\"\\udc00\"", // lone low surrogate
    "\"\\ud83d\"", // high surrogate alone
    "\"\\ud83d\\u0041\"", // high surrogate + not a low one
    "\"abc\\", // ends in a backslash
  };
  for (const char* text : bad)
  {
    MTS_ASSERT(!parses(text));
  }
}

MTS_TEST(rejects_duplicate_keys)
{
  MTS_ASSERT(!parses("{\"id\":1,\"id\":2}"));
}

MTS_TEST(depth_limit)
{
  MTS_ASSERT(parses(nested_arrays(32)));
  MTS_ASSERT(!parses(nested_arrays(33)));
  // Far beyond: refused, not a stack overflow
  MTS_ASSERT(!parses(nested_arrays(100000)));
}

MTS_TEST(error_message_says_where)
{
  mts::Json_Value v;
  std::string error;
  MTS_ASSERT(!mts::json_parse("[1,]", 4, v, &error));
  MTS_ASSERT(error.find("offset 3") != std::string::npos);
}

MTS_TEST(not_null_terminated_input)
{
  // The parser must stop at len, not at a NUL
  const char text[] = { '[', '1', ']', 'x' };
  mts::Json_Value v;
  MTS_ASSERT(mts::json_parse(text, 3, v));
  MTS_ASSERT(!mts::json_parse(text, 4, v));
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(scalars),
  MTS_TEST_ENTRY(string_escapes),
  MTS_TEST_ENTRY(objects_and_arrays),
  MTS_TEST_ENTRY(rejects_bad_syntax),
  MTS_TEST_ENTRY(rejects_bad_numbers),
  MTS_TEST_ENTRY(rejects_bad_strings),
  MTS_TEST_ENTRY(rejects_duplicate_keys),
  MTS_TEST_ENTRY(depth_limit),
  MTS_TEST_ENTRY(error_message_says_where),
  MTS_TEST_ENTRY(not_null_terminated_input),
};

MTS_TEST_MAIN(tests)
