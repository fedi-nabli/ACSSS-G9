/* ===================================================================================================
 * tests/app/protocol/protocol_tests.cc
 *
 * The UI protocol against docs/ui_contract.md:
 *  - ack / error: byte-exact against the contract's examples
 *  - the contract's example commands decode
 *  - every encoded message is parsed back and checked key by key: the
 *    contract's key names and order, wire units, nulls, array lengths
 *  - every error code protocol is responsible for
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <mts_test.h>
#include <app/protocol/json.hpp>
#include <app/protocol/protocol.hpp>
#include <core/types/units.h>

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

namespace
{

constexpr int64_t ORIGIN_NS = 1000000000000LL; // Hat opened 1000 s after boot

// Splits NDJSON into lines (each must end with '\n')
std::vector<std::string> lines_of(const std::string& out)
{
  std::vector<std::string> lines;
  size_t start = 0;
  while (start < out.size())
  {
    size_t nl = out.find('\n', start);
    if (nl == std::string::npos)
    {
      lines.push_back("<missing newline>");
      break;
    }
    lines.push_back(out.substr(start, nl - start));
    start = nl + 1;
  }
  return lines;
}

bool parse_line(const std::string& line, mts::Json_Value& v)
{
  return mts::json_parse(line.data(), line.size(), v) && v.type == mts::Json_Type::object;
}

std::vector<std::string> keys_of(const mts::Json_Value& v)
{
  std::vector<std::string> keys;
  for (const auto& m : v.object)
  {
    keys.push_back(m.first);
  }
  return keys;
}

// Number field, NaN if absent or not a number
double num(const mts::Json_Value& v, const char* key)
{
  const mts::Json_Value* f = v.find(key);
  return f && f->type == mts::Json_Type::number ? f->number : NAN;
}

bool is_null(const mts::Json_Value& v, const char* key)
{
  const mts::Json_Value* f = v.find(key);
  return f && f->type == mts::Json_Type::null_value;
}

// [x,y,z] field matching expected within %.6g precision
bool vec3_is(const mts::Json_Value& v, const char* key, double x, double y, double z)
{
  const mts::Json_Value* f = v.find(key);
  if (!f || f->type != mts::Json_Type::array || f->array.size() != 3)
  {
    return false;
  }
  const double want[3] = { x, y, z };
  for (int i = 0; i < 3; i++)
  {
    if (f->array[i].type != mts::Json_Type::number || std::fabs(f->array[i].number - want[i]) > 1e-5 * (1.0 + std::fabs(want[i])))
    {
      return false;
    }
  }
  return true;
}

Command_Reply reply(uint32_t error, bool has_id, uint32_t id, const char* msg)
{
  Command_Reply r;
  std::memset(&r, 0, sizeof(r));
  r.error = error;
  r.has_id = has_id ? 1u : 0u;
  r.id = id;
  std::strncpy(r.msg, msg, sizeof(r.msg) - 1);
  return r;
}

bool decode(const std::string& line, Command& cmd, Command_Reply& err)
{
  return mts::protocol_decode_command(line.data(), line.size(), cmd, err);
}

Server_Message sample_frame()
{
  Server_Message m;
  std::memset(&m, 0, sizeof(m));
  m.kind = SERVER_MSG_FRAME;
  Output_Frame& f = m.frame;
  f.seq = 4201;
  f.t_ns = ORIGIN_NS + 42010000000LL;
  f.flags = FRAME_TREMOR_ACTIVE | FRAME_COUNTER_VALID;
  f.tremor_disp = { { 0.00061, -0.0001, 0.00007 } };
  f.tremor_acc = { { 0.87, -0.14, 0.10 } };
  f.orientation = { { 0.998, 0.03, -0.05, 0.01 } };
  f.tremor_hz = 6.1;
  f.tremor_amp = 0.0008;
  f.confidence = 0.93;
  f.pipeline_delay_ns = 42000000;
  f.counter = { { -0.00055, 0.00012, -0.00008 } };
  f.n_predictions = 2;
  f.predictions[0].predictor_id = PREDICTOR_INVERSION;
  f.predictions[0].flags = PRED_FORECAST_VALID | PRED_RESIDUAL_VALID;
  f.predictions[0].forecast = { { 0.0003, -0.00005, 0.00004 } };
  f.predictions[0].residual = { { 0.00031, -0.00005, 0.00003 } };
  f.predictions[1].predictor_id = PREDICTOR_WFLC;
  f.predictions[1].flags = 0; // warm-up: all null

  // Rotation (rad), and the sensor as read (m/s^2, rad/s)
  f.flags |= FRAME_ROTATION_ACTIVE | FRAME_COUNTER_ROT_VALID;
  f.tremor_rot = { { 1.2e-3, -0.3e-3, 8.1e-3 } };
  f.tremor_rot_rate = { { 0.05, -0.01, 0.30 } };
  f.rot_hz = 6.1;
  f.rot_amp = 8.2e-3;
  f.rot_confidence = 0.91;
  f.counter_rot = { { -1.1e-3, 0.3e-3, -7.9e-3 } };
  f.imu_acc = { { 0.10 * MTS_STANDARD_GRAVITY_MS2, -0.02 * MTS_STANDARD_GRAVITY_MS2, 1.01 * MTS_STANDARD_GRAVITY_MS2 } };
  f.imu_gyro = { { 0.060, -0.002, 0.310 } };
  f.predictions[0].flags |= PRED_RESIDUAL_ROT_VALID;
  f.predictions[0].forecast_rot = { { 0.6e-3, -0.1e-3, 4.0e-3 } };
  f.predictions[0].residual_rot = { { 0.6e-3, -0.2e-3, 4.1e-3 } };
  return m;
}

Server_Message sample_status(uint32_t state)
{
  Server_Message m;
  std::memset(&m, 0, sizeof(m));
  m.kind = SERVER_MSG_STATUS;
  Status_Message& s = m.status;
  s.state = state;
  s.calibration_step = CAL_STEP_HOLD_STILL;
  s.calibration_progress = 0.4;
  s.calibration_age_s = NAN;
  s.sample_rate_hz = 100.0;
  s.horizon_ns = 30000000;
  s.metrics_hz = 2.0;
  s.spectrum_hz = 5.0;
  s.enabled_mask = (1u << PREDICTOR_INVERSION) | (1u << PREDICTOR_WFLC);
  s.active_predictor_id = PREDICTOR_WFLC;
  s.drops_hat_to_processing = 1;
  s.drops_processing_to_prediction = 2;
  s.drops_processing_to_server = 3;
  return m;
}

} // namespace

/* ---------------------------------------------------------------------------------------------------
 * Replies: byte-exact against the contract (section 3.5)
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(ack_matches_contract)
{
  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode_reply(reply(ERR_NONE, true, 7, ""), out);
  MTS_ASSERT(out == "{\"type\":\"ack\",\"id\":7}\n");
}

MTS_TEST(error_matches_contract)
{
  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode_reply(reply(ERR_NEEDS_CALIBRATION, true, 7, "No valid calibration; run calibrate first."), out);
  MTS_ASSERT(out == "{\"type\":\"error\",\"id\":7,\"code\":\"needs_calibration\",\"msg\":\"No valid calibration; run calibrate first.\"}\n");
}

MTS_TEST(error_without_id)
{
  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode_reply(reply(ERR_HAT_ERROR, false, 0, "sensor \"lost\"\n"), out);
  MTS_ASSERT(out == "{\"type\":\"error\",\"code\":\"hat_error\",\"msg\":\"sensor \\\"lost\\\"\\n\"}\n");

  // An ack without an id answers nothing: no line at all
  out.clear();
  enc.encode_reply(reply(ERR_NONE, false, 0, ""), out);
  MTS_ASSERT(out.empty());
}

MTS_TEST(error_msg_unterminated_buffer_is_safe)
{
  Command_Reply r = reply(ERR_BUSY, true, 1, "");
  std::memset(r.msg, 'x', sizeof(r.msg)); // no NUL anywhere
  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode_reply(r, out);

  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));
  MTS_ASSERT_EQ(v.find("msg")->string.size(), REPLY_MSG_BYTES);
}

MTS_TEST(every_error_code_has_its_contract_name)
{
  MTS_ASSERT(std::strcmp(mts::protocol_error_code_name(ERR_NEEDS_CALIBRATION), "needs_calibration") == 0);
  MTS_ASSERT(std::strcmp(mts::protocol_error_code_name(ERR_BUSY), "busy") == 0);
  MTS_ASSERT(std::strcmp(mts::protocol_error_code_name(ERR_INVALID_STATE), "invalid_state") == 0);
  MTS_ASSERT(std::strcmp(mts::protocol_error_code_name(ERR_INVALID_ARGUMENT), "invalid_argument") == 0);
  MTS_ASSERT(std::strcmp(mts::protocol_error_code_name(ERR_UNKNOWN_COMMAND), "unknown_command") == 0);
  MTS_ASSERT(std::strcmp(mts::protocol_error_code_name(ERR_MALFORMED_MESSAGE), "malformed_message") == 0);
  MTS_ASSERT(std::strcmp(mts::protocol_error_code_name(ERR_HAT_ERROR), "hat_error") == 0);
  MTS_ASSERT(std::strcmp(mts::protocol_error_code_name(ERR_CALIBRATION_FAILED), "calibration_failed") == 0);
  MTS_ASSERT(std::strcmp(mts::protocol_error_code_name(999), "unknown") == 0);
}

/* ---------------------------------------------------------------------------------------------------
 * frame (section 3.1)
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(frame_keys_in_contract_order)
{
  mts::Protocol_Encoder enc;
  enc.set_origin(ORIGIN_NS);
  std::string out;
  MTS_ASSERT(enc.encode(sample_frame(), out));

  std::vector<std::string> lines = lines_of(out);
  MTS_ASSERT_EQ(lines.size(), 1);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines[0], v));

  const std::vector<std::string> expected = {
    "type", "sequence", "timestamp_ns", "gap_before", "tremor_active", "rotation_active",
    "tremor_displacement_mm", "tremor_acceleration_ms2", "tremor_rotation_mrad", "tremor_angular_rate_rad_s", "imu",
    "orientation_wxyz", "tremor_frequency_hz", "tremor_amplitude_mm", "tremor_confidence", "rotation_frequency_hz",
    "rotation_amplitude_mrad", "rotation_confidence", "pipeline_delay_ms", "counter_signal_mm", "counter_rotation_mrad",
    "predictions",
  };
  MTS_ASSERT(keys_of(v) == expected);
  const std::vector<std::string> imu = { "ax_g", "ay_g", "az_g", "gx_rad_s", "gy_rad_s", "gz_rad_s" };
  MTS_ASSERT(keys_of(*v.find("imu")) == imu);
}

MTS_TEST(frame_rotation_and_imu_units)
{
  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode(sample_frame(), out);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));

  // rad -> mrad, rad/s unchanged
  MTS_ASSERT(v.find("rotation_active")->boolean);
  MTS_ASSERT(vec3_is(v, "tremor_rotation_mrad", 1.2, -0.3, 8.1));
  MTS_ASSERT(vec3_is(v, "tremor_angular_rate_rad_s", 0.05, -0.01, 0.30));
  MTS_ASSERT_NEAR(num(v, "rotation_frequency_hz"), 6.1, 1e-9);
  MTS_ASSERT_NEAR(num(v, "rotation_amplitude_mrad"), 8.2, 1e-9);
  MTS_ASSERT_NEAR(num(v, "rotation_confidence"), 0.91, 1e-9);
  MTS_ASSERT(vec3_is(v, "counter_rotation_mrad", -1.1, 0.3, -7.9));

  // m/s^2 -> g with the one shared constant, rad/s unchanged
  const mts::Json_Value& imu = *v.find("imu");
  MTS_ASSERT_NEAR(num(imu, "ax_g"), 0.10, 1e-6);
  MTS_ASSERT_NEAR(num(imu, "az_g"), 1.01, 1e-6);
  MTS_ASSERT_NEAR(num(imu, "gz_rad_s"), 0.310, 1e-9);

  // Rotation forecast and residual next to the translation ones; wflc in warm-up: all null
  const mts::Json_Value& inv = v.find("predictions")->array[0];
  MTS_ASSERT(vec3_is(inv, "forecast_rotation_mrad", 0.6, -0.1, 4.0));
  MTS_ASSERT(vec3_is(inv, "residual_rotation_mrad", 0.6, -0.2, 4.1));
  const mts::Json_Value& wflc = v.find("predictions")->array[1];
  MTS_ASSERT(is_null(wflc, "forecast_rotation_mrad"));
  MTS_ASSERT(is_null(wflc, "residual_rotation_mrad"));
}

MTS_TEST(rotation_gate_is_its_own)
{
  // Rotation active, translation not: rotation residual and counter out, translation ones null
  Server_Message m = sample_frame();
  m.frame.flags = FRAME_ROTATION_ACTIVE | FRAME_COUNTER_ROT_VALID;
  m.frame.predictions[0].flags = PRED_FORECAST_VALID | PRED_RESIDUAL_ROT_VALID;
  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode(m, out);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));
  MTS_ASSERT(!v.find("tremor_active")->boolean);
  MTS_ASSERT(is_null(v, "counter_signal_mm"));
  MTS_ASSERT(!is_null(v, "counter_rotation_mrad"));
  const mts::Json_Value& inv = v.find("predictions")->array[0];
  MTS_ASSERT(is_null(inv, "residual_mm"));
  MTS_ASSERT(!is_null(inv, "residual_rotation_mrad"));
  MTS_ASSERT(!is_null(inv, "forecast_mm")); // forecasts: one flag for all 6 axes
}


MTS_TEST(frame_values_and_units)
{
  mts::Protocol_Encoder enc;
  enc.set_origin(ORIGIN_NS);
  std::string out;
  enc.encode(sample_frame(), out);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));

  MTS_ASSERT(v.find("type")->string == "frame");
  MTS_ASSERT_NEAR(num(v, "sequence"), 4201.0, 0.0);
  // Relative to the Hat opening, exact
  MTS_ASSERT_NEAR(num(v, "timestamp_ns"), 42010000000.0, 0.0);
  MTS_ASSERT(!v.find("gap_before")->boolean);
  MTS_ASSERT(v.find("tremor_active")->boolean);

  // m -> mm, m/s^2 unchanged
  MTS_ASSERT(vec3_is(v, "tremor_displacement_mm", 0.61, -0.1, 0.07));
  MTS_ASSERT(vec3_is(v, "tremor_acceleration_ms2", 0.87, -0.14, 0.10));
  MTS_ASSERT_EQ(v.find("orientation_wxyz")->array.size(), 4);
  MTS_ASSERT_NEAR(v.find("orientation_wxyz")->array[0].number, 0.998, 1e-9); // w first

  MTS_ASSERT_NEAR(num(v, "tremor_frequency_hz"), 6.1, 1e-9);
  MTS_ASSERT_NEAR(num(v, "tremor_amplitude_mm"), 0.8, 1e-9);
  MTS_ASSERT_NEAR(num(v, "tremor_confidence"), 0.93, 1e-9);
  MTS_ASSERT_NEAR(num(v, "pipeline_delay_ms"), 42.0, 1e-9); // ns -> ms
  MTS_ASSERT(vec3_is(v, "counter_signal_mm", -0.55, 0.12, -0.08));
}

MTS_TEST(frame_predictions)
{
  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode(sample_frame(), out);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));

  const mts::Json_Value* preds = v.find("predictions");
  MTS_ASSERT(preds != nullptr && preds->array.size() == 2);

  const mts::Json_Value& inv = preds->array[0];
  const std::vector<std::string> keys = { "predictor_id", "forecast_mm", "residual_mm", "forecast_rotation_mrad",
                                          "residual_rotation_mrad" };
  MTS_ASSERT(keys_of(inv) == keys);
  MTS_ASSERT(inv.find("predictor_id")->string == "inversion");
  MTS_ASSERT(vec3_is(inv, "forecast_mm", 0.3, -0.05, 0.04));
  MTS_ASSERT(vec3_is(inv, "residual_mm", 0.31, -0.05, 0.03));

  // Warm-up: forecast and residual null
  const mts::Json_Value& wflc = preds->array[1];
  MTS_ASSERT(wflc.find("predictor_id")->string == "wflc");
  MTS_ASSERT(is_null(wflc, "forecast_mm"));
  MTS_ASSERT(is_null(wflc, "residual_mm"));
}

MTS_TEST(frame_nulls_and_flags)
{
  Server_Message m = sample_frame();
  m.frame.flags = FRAME_GAP_BEFORE; // tremor inactive, counter invalid
  m.frame.tremor_hz = NAN; // not meaningful: must still be valid JSON
  m.frame.n_predictions = 0;

  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode(m, out);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));

  MTS_ASSERT(v.find("gap_before")->boolean);
  MTS_ASSERT(!v.find("tremor_active")->boolean);
  MTS_ASSERT(is_null(v, "counter_signal_mm"));
  MTS_ASSERT(is_null(v, "tremor_frequency_hz"));
  MTS_ASSERT(v.find("predictions")->array.empty());
}

MTS_TEST(frame_skips_unknown_predictor_and_caps_count)
{
  Server_Message m = sample_frame();
  m.frame.predictions[0].predictor_id = 99;
  m.frame.n_predictions = 1000; // corrupt count: capped at P_MAX, no overread

  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode(m, out);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));

  // Entry 0 (unknown id) skipped; entries 2..7 are zeroed = inversion
  const mts::Json_Value* preds = v.find("predictions");
  MTS_ASSERT_EQ(preds->array.size(), P_MAX - 1);
  MTS_ASSERT(preds->array[0].find("predictor_id")->string == "wflc");
}

/* ---------------------------------------------------------------------------------------------------
 * metrics, spectrum, status (sections 3.2 - 3.4)
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(metrics_one_line_per_predictor)
{
  Server_Message m;
  std::memset(&m, 0, sizeof(m));
  m.kind = SERVER_MSG_METRICS;
  m.metrics.t_ns = ORIGIN_NS + 42500000000LL;
  m.metrics.window_ns = 2000000000LL;
  m.metrics.count = 2;
  m.metrics.items[0] = { PREDICTOR_WFLC, 0, 0.18, 0.00009 };
  m.metrics.items[1] = { PREDICTOR_INVERSION, 0, 0.62, 0.00031 };

  mts::Protocol_Encoder enc;
  enc.set_origin(ORIGIN_NS);
  std::string out;
  enc.encode(m, out);
  std::vector<std::string> lines = lines_of(out);
  MTS_ASSERT_EQ(lines.size(), 2);

  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines[0], v));
  const std::vector<std::string> keys = {
    "type", "predictor_id", "timestamp_ns", "suppression_ratio", "residual_rms_mm", "window_ms",
  };
  MTS_ASSERT(keys_of(v) == keys);
  MTS_ASSERT(v.find("predictor_id")->string == "wflc");
  MTS_ASSERT_NEAR(num(v, "timestamp_ns"), 42500000000.0, 0.0);
  MTS_ASSERT_NEAR(num(v, "suppression_ratio"), 0.18, 1e-9);
  MTS_ASSERT_NEAR(num(v, "residual_rms_mm"), 0.09, 1e-9);
  MTS_ASSERT_NEAR(num(v, "window_ms"), 2000.0, 0.0);

  MTS_ASSERT(parse_line(lines[1], v));
  MTS_ASSERT(v.find("predictor_id")->string == "inversion");
}

MTS_TEST(spectrum_one_line_all_axes)
{
  Server_Message m;
  std::memset(&m, 0, sizeof(m));
  m.kind = SERVER_MSG_SPECTRUM;
  m.spectrum.t_ns = ORIGIN_NS + 1;
  m.spectrum.bin_width_hz = 0.390625;
  m.spectrum.n_bins = 65;
  for (uint32_t i = 0; i < 65; i++)
  {
    m.spectrum.amp[0][i] = 0.01 * i;
    m.spectrum.amp[2][i] = 1.0;
  }

  mts::Protocol_Encoder enc;
  enc.set_origin(ORIGIN_NS);
  std::string out;
  enc.encode(m, out);
  std::vector<std::string> lines = lines_of(out);
  MTS_ASSERT_EQ(lines.size(), 1);

  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines[0], v));
  const std::vector<std::string> keys = { "type", "timestamp_ns", "bin_width_hz", "x_ms2", "y_ms2", "z_ms2" };
  MTS_ASSERT(keys_of(v) == keys);
  MTS_ASSERT_NEAR(num(v, "timestamp_ns"), 1.0, 0.0);
  MTS_ASSERT_NEAR(num(v, "bin_width_hz"), 0.390625, 1e-9);
  MTS_ASSERT_EQ(v.find("x_ms2")->array.size(), 65);
  MTS_ASSERT_EQ(v.find("y_ms2")->array.size(), 65);
  MTS_ASSERT_NEAR(v.find("x_ms2")->array[64].number, 0.64, 1e-9);
  MTS_ASSERT_NEAR(v.find("z_ms2")->array[10].number, 1.0, 0.0);

  // A corrupt bin count is capped, never read past the array
  m.spectrum.n_bins = 100000;
  out.clear();
  enc.encode(m, out);
  MTS_ASSERT(parse_line(lines_of(out)[0], v));
  MTS_ASSERT_EQ(v.find("x_ms2")->array.size(), SPECTRUM_MAX_BINS);
}

MTS_TEST(status_keys_and_values)
{
  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode(sample_status(APP_STATE_RUNNING), out);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));

  const std::vector<std::string> keys = {
    "type", "protocol_version", "state", "sample_rate_hz", "horizon_ms", "calibration", "calibration_age_s",
    "enabled_predictor_ids", "active_predictor_id", "metrics_hz", "spectrum_hz", "drops",
  };
  MTS_ASSERT(keys_of(v) == keys);
  MTS_ASSERT_NEAR(num(v, "protocol_version"), 1.0, 0.0);
  MTS_ASSERT(v.find("state")->string == "running");
  MTS_ASSERT_NEAR(num(v, "sample_rate_hz"), 100.0, 0.0);
  MTS_ASSERT_NEAR(num(v, "horizon_ms"), 30.0, 1e-9);
  MTS_ASSERT(is_null(v, "calibration")); // not calibrating
  MTS_ASSERT(is_null(v, "calibration_age_s")); // NaN = none

  const mts::Json_Value* enabled = v.find("enabled_predictor_ids");
  MTS_ASSERT_EQ(enabled->array.size(), 2);
  MTS_ASSERT(enabled->array[0].string == "inversion");
  MTS_ASSERT(enabled->array[1].string == "wflc");
  MTS_ASSERT(v.find("active_predictor_id")->string == "wflc");
  MTS_ASSERT_NEAR(num(v, "metrics_hz"), 2.0, 0.0);
  MTS_ASSERT_NEAR(num(v, "spectrum_hz"), 5.0, 0.0);

  const mts::Json_Value* drops = v.find("drops");
  const std::vector<std::string> drop_keys = { "hat_to_processing", "processing_to_prediction", "processing_to_server" };
  MTS_ASSERT(keys_of(*drops) == drop_keys);
  MTS_ASSERT_NEAR(num(*drops, "processing_to_server"), 3.0, 0.0);
}

MTS_TEST(status_calibrating_has_step_and_progress)
{
  Server_Message m = sample_status(APP_STATE_CALIBRATING);
  m.status.calibration_age_s = 12.0;

  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode(m, out);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));

  const mts::Json_Value* cal = v.find("calibration");
  MTS_ASSERT(cal != nullptr && cal->type == mts::Json_Type::object);
  MTS_ASSERT(cal->find("step")->string == "hold_still");
  MTS_ASSERT_NEAR(num(*cal, "progress"), 0.4, 1e-9);
  MTS_ASSERT_NEAR(num(v, "calibration_age_s"), 12.0, 0.0);
}

MTS_TEST(status_state_names)
{
  const char* names[] = { "boot", "needs_calibration", "ready", "calibrating", "running", "stopped" };
  for (uint32_t s = 0; s < 6; s++)
  {
    MTS_ASSERT(std::strcmp(mts::protocol_state_name(s), names[s]) == 0);
  }
}

MTS_TEST(unknown_kind_appends_nothing)
{
  Server_Message m;
  std::memset(&m, 0, sizeof(m));
  m.kind = 42;
  mts::Protocol_Encoder enc;
  std::string out;
  MTS_ASSERT(!enc.encode(m, out));
  MTS_ASSERT(out.empty());
}

/* ---------------------------------------------------------------------------------------------------
 * Commands (section 4)
 * ------------------------------------------------------------------------------------------------- */

MTS_TEST(contract_example_commands)
{
  Command cmd;
  Command_Reply err;

  MTS_ASSERT(decode("{\"type\":\"command\",\"id\":7,\"command\":\"start\"}", cmd, err));
  MTS_ASSERT_EQ(cmd.kind, CMD_START);
  MTS_ASSERT_EQ(cmd.id, 7);
  MTS_ASSERT_EQ(err.error, ERR_NONE);

  MTS_ASSERT(decode("{\"type\":\"command\",\"id\":2,\"command\":\"calibrate\"}", cmd, err));
  MTS_ASSERT_EQ(cmd.kind, CMD_CALIBRATE);

  MTS_ASSERT(decode("{\"type\":\"command\",\"id\":3,\"command\":\"stop\"}", cmd, err));
  MTS_ASSERT_EQ(cmd.kind, CMD_STOP);
  MTS_ASSERT_EQ(cmd.id, 3);
}

MTS_TEST(setup_all_fields)
{
  Command cmd;
  Command_Reply err;
  MTS_ASSERT(decode("{\"type\":\"command\",\"id\":9,\"command\":\"setup\","
                    "\"enabled_predictor_ids\":[\"inversion\",\"wflc\",\"wflc_kalman\"],"
                    "\"active_predictor_id\":\"wflc\",\"horizon_ms\":40,\"metrics_hz\":4,\"spectrum_hz\":2.5}",
                    cmd, err));

  MTS_ASSERT_EQ(cmd.kind, CMD_SETUP);
  MTS_ASSERT_EQ(cmd.set_mask, SETTING_ENABLED_PREDICTORS | SETTING_ACTIVE_PREDICTOR | SETTING_HORIZON
                                | SETTING_METRICS_HZ | SETTING_SPECTRUM_HZ);
  MTS_ASSERT_EQ(cmd.settings.enabled_mask,
                (1u << PREDICTOR_INVERSION) | (1u << PREDICTOR_WFLC) | (1u << PREDICTOR_WFLC_KALMAN));
  MTS_ASSERT_EQ(cmd.settings.active_predictor_id, PREDICTOR_WFLC);
  MTS_ASSERT_EQ(cmd.settings.horizon_ns, 40000000);
  MTS_ASSERT_NEAR(cmd.settings.metrics_hz, 4.0, 0.0);
  MTS_ASSERT_NEAR(cmd.settings.spectrum_hz, 2.5, 0.0);
}

MTS_TEST(setup_only_sent_fields)
{
  Command cmd;
  Command_Reply err;
  MTS_ASSERT(decode("{\"type\":\"command\",\"id\":4,\"command\":\"setup\",\"horizon_ms\":12.5}", cmd, err));
  MTS_ASSERT_EQ(cmd.set_mask, SETTING_HORIZON);
  MTS_ASSERT_EQ(cmd.settings.horizon_ns, 12500000);

  // No fields: a valid no-op
  MTS_ASSERT(decode("{\"type\":\"command\",\"id\":5,\"command\":\"setup\"}", cmd, err));
  MTS_ASSERT_EQ(cmd.set_mask, 0);
}

MTS_TEST(malformed_messages)
{
  const char* bad[] = {
    "not json",
    "[1,2]", // not an object
    "{\"id\":1,\"command\":\"start\"}", // no type
    "{\"type\":\"frame\",\"id\":1,\"command\":\"start\"}", // wrong type
    "{\"type\":\"command\",\"command\":\"start\"}", // no id
    "{\"type\":\"command\",\"id\":\"7\",\"command\":\"start\"}", // id not a number
    "{\"type\":\"command\",\"id\":1.5,\"command\":\"start\"}",
    "{\"type\":\"command\",\"id\":-1,\"command\":\"start\"}",
    "{\"type\":\"command\",\"id\":4294967296,\"command\":\"start\"}", // 2^32
    "{\"type\":\"command\",\"id\":1,\"id\":2,\"command\":\"start\"}", // duplicate key
  };
  for (const char* line : bad)
  {
    Command cmd;
    Command_Reply err;
    MTS_ASSERT(!decode(line, cmd, err));
    MTS_ASSERT_EQ(err.error, ERR_MALFORMED_MESSAGE);
    MTS_ASSERT_EQ(err.has_id, 0); // id unreadable in all of these
    MTS_ASSERT_GT(std::strlen(err.msg), 0);
  }
}

MTS_TEST(missing_command_echoes_id)
{
  Command cmd;
  Command_Reply err;
  MTS_ASSERT(!decode("{\"type\":\"command\",\"id\":11}", cmd, err));
  MTS_ASSERT_EQ(err.error, ERR_MALFORMED_MESSAGE);
  MTS_ASSERT_EQ(err.has_id, 1);
  MTS_ASSERT_EQ(err.id, 11);

  MTS_ASSERT(!decode("{\"type\":\"command\",\"id\":11,\"command\":5}", cmd, err));
  MTS_ASSERT_EQ(err.error, ERR_MALFORMED_MESSAGE);
}

MTS_TEST(unknown_command)
{
  Command cmd;
  Command_Reply err;
  MTS_ASSERT(!decode("{\"type\":\"command\",\"id\":12,\"command\":\"explode\"}", cmd, err));
  MTS_ASSERT_EQ(err.error, ERR_UNKNOWN_COMMAND);
  MTS_ASSERT_EQ(err.id, 12);
  MTS_ASSERT(std::strstr(err.msg, "explode") != nullptr);
}

MTS_TEST(invalid_setup_arguments)
{
  const char* bad[] = {
    "\"active_predictor_id\":\"magic\"", // unknown predictor
    "\"active_predictor_id\":3", // not a string
    "\"enabled_predictor_ids\":\"wflc\"", // not an array
    "\"enabled_predictor_ids\":[\"wflc\",7]", // non-string entry
    "\"enabled_predictor_ids\":[\"nope\"]",
    "\"horizon_ms\":-1",
    "\"horizon_ms\":\"40\"",
    "\"horizon_ms\":1e9",
    "\"metrics_hz\":0",
    "\"spectrum_hz\":-5",
    "\"horizon_mz\":40", // typo: unknown field
  };
  for (const char* field : bad)
  {
    Command cmd;
    Command_Reply err;
    std::string line = std::string("{\"type\":\"command\",\"id\":13,\"command\":\"setup\",") + field + "}";
    MTS_ASSERT(!decode(line, cmd, err));
    MTS_ASSERT_EQ(err.error, ERR_INVALID_ARGUMENT);
    MTS_ASSERT_EQ(err.id, 13);
  }
}

MTS_TEST(decode_error_roundtrips_through_encoder)
{
  // What the server will actually do: decode fails -> send err back
  Command cmd;
  Command_Reply err;
  MTS_ASSERT(!decode("{\"type\":\"command\",\"id\":21,\"command\":\"setup\",\"active_predictor_id\":\"x\\\"y\"}", cmd, err));

  mts::Protocol_Encoder enc;
  std::string out;
  enc.encode_reply(err, out);
  mts::Json_Value v;
  MTS_ASSERT(parse_line(lines_of(out)[0], v));
  MTS_ASSERT(v.find("type")->string == "error");
  MTS_ASSERT_NEAR(num(v, "id"), 21.0, 0.0);
  MTS_ASSERT(v.find("code")->string == "invalid_argument");
}

/* ---------------------------------------------------------------------------------------------------
 * The contract's example stream (section 8), verbatim: our parser reads
 * every line, and our encoder produces exactly the same keys, in the same
 * order, as the contract's example of each message type.
 * ------------------------------------------------------------------------------------------------- */

const char* const CONTRACT_EXAMPLE[] = {
  "{\"type\":\"status\",\"protocol_version\":1,\"state\":\"needs_calibration\",\"sample_rate_hz\":100,\"horizon_ms\":30,\"calibration\":null,\"calibration_age_s\":null,\"enabled_predictor_ids\":[\"inversion\",\"wflc\"],\"active_predictor_id\":\"wflc\",\"metrics_hz\":2,\"spectrum_hz\":5,\"drops\":{\"hat_to_processing\":0,\"processing_to_prediction\":0,\"processing_to_server\":0}}",
  "{\"type\":\"command\",\"id\":1,\"command\":\"start\"}",
  "{\"type\":\"error\",\"id\":1,\"code\":\"needs_calibration\",\"msg\":\"No valid calibration; run calibrate first.\"}",
  "{\"type\":\"command\",\"id\":2,\"command\":\"calibrate\"}",
  "{\"type\":\"ack\",\"id\":2}",
  "{\"type\":\"status\",\"protocol_version\":1,\"state\":\"calibrating\",\"sample_rate_hz\":100,\"horizon_ms\":30,\"calibration\":{\"step\":\"hold_still\",\"progress\":0.4},\"calibration_age_s\":null,\"enabled_predictor_ids\":[\"inversion\",\"wflc\"],\"active_predictor_id\":\"wflc\",\"metrics_hz\":2,\"spectrum_hz\":5,\"drops\":{\"hat_to_processing\":0,\"processing_to_prediction\":0,\"processing_to_server\":0}}",
  "{\"type\":\"frame\",\"sequence\":4201,\"timestamp_ns\":42010000000,\"gap_before\":false,\"tremor_active\":true,\"rotation_active\":true,\"tremor_displacement_mm\":[0.61,-0.10,0.07],\"tremor_acceleration_ms2\":[0.87,-0.14,0.10],\"tremor_rotation_mrad\":[1.2,-0.3,8.1],\"tremor_angular_rate_rad_s\":[0.05,-0.01,0.30],\"imu\":{\"ax_g\":0.10,\"ay_g\":-0.02,\"az_g\":1.01,\"gx_rad_s\":0.060,\"gy_rad_s\":-0.002,\"gz_rad_s\":0.310},\"orientation_wxyz\":[0.998,0.03,-0.05,0.01],\"tremor_frequency_hz\":6.1,\"tremor_amplitude_mm\":0.8,\"tremor_confidence\":0.93,\"rotation_frequency_hz\":6.1,\"rotation_amplitude_mrad\":8.2,\"rotation_confidence\":0.91,\"pipeline_delay_ms\":42,\"counter_signal_mm\":[-0.55,0.12,-0.08],\"counter_rotation_mrad\":[-1.1,0.3,-7.9],\"predictions\":[{\"predictor_id\":\"inversion\",\"forecast_mm\":[0.30,-0.05,0.04],\"residual_mm\":[0.31,-0.05,0.03],\"forecast_rotation_mrad\":[0.6,-0.1,4.0],\"residual_rotation_mrad\":[0.6,-0.2,4.1]},{\"predictor_id\":\"wflc\",\"forecast_mm\":[0.55,-0.12,0.08],\"residual_mm\":[0.06,0.02,-0.01],\"forecast_rotation_mrad\":[1.1,-0.3,7.9],\"residual_rotation_mrad\":[0.1,0.0,0.2]}]}",
  "{\"type\":\"metrics\",\"predictor_id\":\"wflc\",\"timestamp_ns\":42500000000,\"suppression_ratio\":0.18,\"residual_rms_mm\":0.09,\"window_ms\":2000}",
  "{\"type\":\"spectrum\",\"timestamp_ns\":42500000000,\"bin_width_hz\":0.39,\"x_ms2\":[0.01,0.02,0.01,0.40,0.85],\"y_ms2\":[0.00,0.01,0.01,0.06,0.14],\"z_ms2\":[0.00,0.01,0.00,0.04,0.09]}",
};

const mts::Json_Value* example_of(const char* type, std::vector<mts::Json_Value>& parsed)
{
  for (const mts::Json_Value& v : parsed)
  {
    if (v.find("type")->string == type)
    {
      return &v;
    }
  }
  return nullptr;
}

MTS_TEST(contract_example_stream)
{
  std::vector<mts::Json_Value> parsed;
  for (const char* line : CONTRACT_EXAMPLE)
  {
    mts::Json_Value v;
    MTS_ASSERT(parse_line(line, v));
    parsed.push_back(v);

    // Every example command decodes
    if (v.find("type")->string == "command")
    {
      Command cmd;
      Command_Reply err;
      MTS_ASSERT(decode(line, cmd, err));
    }
  }

  mts::Protocol_Encoder enc;
  enc.set_origin(ORIGIN_NS);

  // frame (with predictions), status (calibrating: calibration object), metrics, spectrum
  Server_Message frame = sample_frame();
  frame.frame.predictions[1].flags = PRED_FORECAST_VALID | PRED_RESIDUAL_VALID;
  Server_Message status = sample_status(APP_STATE_CALIBRATING);

  Server_Message metrics;
  std::memset(&metrics, 0, sizeof(metrics));
  metrics.kind = SERVER_MSG_METRICS;
  metrics.metrics.count = 1;
  metrics.metrics.items[0].predictor_id = PREDICTOR_WFLC;

  Server_Message spectrum;
  std::memset(&spectrum, 0, sizeof(spectrum));
  spectrum.kind = SERVER_MSG_SPECTRUM;
  spectrum.spectrum.n_bins = 5;

  const Server_Message* ours[] = { &frame, &status, &metrics, &spectrum };
  for (const Server_Message* m : ours)
  {
    std::string out;
    enc.encode(*m, out);
    mts::Json_Value v;
    MTS_ASSERT(parse_line(lines_of(out)[0], v));

    const mts::Json_Value* example = example_of(v.find("type")->string.c_str(), parsed);
    MTS_ASSERT(example != nullptr);
    MTS_ASSERT(keys_of(v) == keys_of(*example));
  }

  // Nested keys: one frame prediction entry, the calibration object, drops
  std::string out;
  enc.encode(frame, out);
  mts::Json_Value f;
  MTS_ASSERT(parse_line(lines_of(out)[0], f));
  const mts::Json_Value* example_frame = example_of("frame", parsed);
  MTS_ASSERT(keys_of(f.find("predictions")->array[0]) == keys_of(example_frame->find("predictions")->array[0]));

  out.clear();
  enc.encode(status, out);
  mts::Json_Value s;
  MTS_ASSERT(parse_line(lines_of(out)[0], s));
  const mts::Json_Value* example_status = &parsed[5];
  MTS_ASSERT(keys_of(*s.find("calibration")) == keys_of(*example_status->find("calibration")));
  MTS_ASSERT(keys_of(*s.find("drops")) == keys_of(*example_status->find("drops")));
}

static const Mts_Test tests[] = {
  MTS_TEST_ENTRY(contract_example_stream),
  MTS_TEST_ENTRY(ack_matches_contract),
  MTS_TEST_ENTRY(error_matches_contract),
  MTS_TEST_ENTRY(error_without_id),
  MTS_TEST_ENTRY(error_msg_unterminated_buffer_is_safe),
  MTS_TEST_ENTRY(every_error_code_has_its_contract_name),
  MTS_TEST_ENTRY(frame_keys_in_contract_order),
  MTS_TEST_ENTRY(frame_values_and_units),
  MTS_TEST_ENTRY(frame_predictions),
  MTS_TEST_ENTRY(frame_rotation_and_imu_units),
  MTS_TEST_ENTRY(rotation_gate_is_its_own),
  MTS_TEST_ENTRY(frame_nulls_and_flags),
  MTS_TEST_ENTRY(frame_skips_unknown_predictor_and_caps_count),
  MTS_TEST_ENTRY(metrics_one_line_per_predictor),
  MTS_TEST_ENTRY(spectrum_one_line_all_axes),
  MTS_TEST_ENTRY(status_keys_and_values),
  MTS_TEST_ENTRY(status_calibrating_has_step_and_progress),
  MTS_TEST_ENTRY(status_state_names),
  MTS_TEST_ENTRY(unknown_kind_appends_nothing),
  MTS_TEST_ENTRY(contract_example_commands),
  MTS_TEST_ENTRY(setup_all_fields),
  MTS_TEST_ENTRY(setup_only_sent_fields),
  MTS_TEST_ENTRY(malformed_messages),
  MTS_TEST_ENTRY(missing_command_echoes_id),
  MTS_TEST_ENTRY(unknown_command),
  MTS_TEST_ENTRY(invalid_setup_arguments),
  MTS_TEST_ENTRY(decode_error_roundtrips_through_encoder),
};

MTS_TEST_MAIN(tests)
