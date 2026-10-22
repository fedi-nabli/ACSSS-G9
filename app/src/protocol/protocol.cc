/* ===================================================================================================
 * app/src/protocol/protocol.cc
 *
 * Authors: Fedi Nabli
 * Date: 22 Oct 2026
 * Last Modified: 22 Oct 2026
 * ===================================================================================================
 */

#include <app/protocol/json.hpp>
#include <app/protocol/protocol.hpp>

#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace mts
{

namespace
{

constexpr double M_TO_MM = 1000.0;
constexpr double NS_TO_MS = 1e-6;

/* ---------------------------------------------------------------------------------------------------
 * JSON writing
 * ------------------------------------------------------------------------------------------------- */

// %.6g: 6 significant digits. Not finite -> null (NaN is not JSON)
void append_number(std::string& out, double v)
{
  if (!std::isfinite(v))
  {
    out += "null";
    return;
  }
  char buf[32];
  std::snprintf(buf, sizeof(buf), "%.6g", v);
  out += buf;
}

void append_int(std::string& out, int64_t v)
{
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%lld", (long long)v);
  out += buf;
}

void append_uint(std::string& out, uint64_t v)
{
  char buf[24];
  std::snprintf(buf, sizeof(buf), "%llu", (unsigned long long)v);
  out += buf;
}

void append_bool(std::string& out, bool v)
{
  out += v ? "true" : "false";
}

// Quoted and escaped: ", \ and control characters
void append_string(std::string& out, const char* s)
{
  out.push_back('"');
  for (const unsigned char* p = (const unsigned char*)s; *p; p++)
  {
    switch (*p)
    {
      case '"':
        out += "\\\"";
        break;
      case '\\':
        out += "\\\\";
        break;
      case '\n':
        out += "\\n";
        break;
      case '\r':
        out += "\\r";
        break;
      case '\t':
        out += "\\t";
        break;
      default:
        if (*p < 0x20)
        {
          char buf[8];
          std::snprintf(buf, sizeof(buf), "\\u%04x", *p);
          out += buf;
        }
        else
        {
          out.push_back((char)*p);
        }
    }
  }
  out.push_back('"');
}

// ,"key": (every key but the first of a line starts with a comma)
void append_key(std::string& out, const char* key)
{
  out += ",\"";
  out += key;
  out += "\":";
}

// [x,y,z] scaled (m -> mm, ...)
void append_vec3(std::string& out, const Vec3& v, double scale)
{
  out.push_back('[');
  for (int a = 0; a < AXIS_COUNT; a++)
  {
    if (a > 0)
    {
      out.push_back(',');
    }
    append_number(out, v.v[a] * scale);
  }
  out.push_back(']');
}

void append_vec3_or_null(std::string& out, const Vec3& v, double scale, bool valid)
{
  if (valid)
  {
    append_vec3(out, v, scale);
  }
  else
  {
    out += "null";
  }
}

// Predictor name as a JSON string, null for an unknown id
void append_predictor(std::string& out, Predictor_Id id)
{
  const char* name = predictor_name(id);
  if (name)
  {
    append_string(out, name);
  }
  else
  {
    out += "null";
  }
}

void begin_line(std::string& out, const char* type)
{
  out += "{\"type\":\"";
  out += type;
  out += "\"";
}

void end_line(std::string& out)
{
  out += "}\n";
}

/* ---------------------------------------------------------------------------------------------------
 * Names
 * ------------------------------------------------------------------------------------------------- */

const char* const ERROR_NAMES[] = {
  "none", "needs_calibration", "busy", "invalid_state", "invalid_argument",
  "unknown_command", "malformed_message", "hat_error", "calibration_failed",
};

const char* const STATE_NAMES[] = {
  "boot", "needs_calibration", "ready", "calibrating", "running", "stopped",
};

const char* const CAL_STEP_NAMES[] = {
  "none", "quick_zero", "hold_still", "rotate",
};

/* ---------------------------------------------------------------------------------------------------
 * Decoding helpers
 * ------------------------------------------------------------------------------------------------- */

bool reject(Command_Reply& err, uint32_t code, const char* fmt, ...)
{
  err.error = code;
  va_list args;
  va_start(args, fmt);
  std::vsnprintf(err.msg, sizeof(err.msg), fmt, args);
  va_end(args);
  return false;
}

// A finite number, else invalid_argument naming the field
bool read_number(const Json_Value& v, const char* field, double& out, Command_Reply& err)
{
  if (v.type != Json_Type::number)
  {
    return reject(err, ERR_INVALID_ARGUMENT, "%s must be a number", field);
  }
  out = v.number;
  return true;
}

bool read_predictor(const Json_Value& v, const char* field, Predictor_Id& out, Command_Reply& err)
{
  if (v.type != Json_Type::string)
  {
    return reject(err, ERR_INVALID_ARGUMENT, "%s must be a predictor name", field);
  }
  Predictor_Id id = predictor_id_from_name(v.string.c_str());
  if (id >= PREDICTOR_COUNT)
  {
    return reject(err, ERR_INVALID_ARGUMENT, "unknown predictor '%.40s'", v.string.c_str());
  }
  out = id;
  return true;
}

// The optional fields of a setup command, at the top level of the line
bool decode_setup(const Json_Value& root, Command& out, Command_Reply& err)
{
  for (const auto& member : root.object)
  {
    const std::string& key = member.first;
    const Json_Value& v = member.second;

    if (key == "type" || key == "id" || key == "command")
    {
      continue;
    }

    if (key == "enabled_predictor_ids")
    {
      if (v.type != Json_Type::array)
      {
        return reject(err, ERR_INVALID_ARGUMENT, "enabled_predictor_ids must be an array of predictor names");
      }
      uint32_t mask = 0;
      for (const Json_Value& item : v.array)
      {
        Predictor_Id id = 0;
        if (!read_predictor(item, "enabled_predictor_ids", id, err))
        {
          return false;
        }
        mask |= UINT32_C(1) << id;
      }
      out.settings.enabled_mask = mask;
      out.set_mask |= SETTING_ENABLED_PREDICTORS;
    }
    else if (key == "active_predictor_id")
    {
      if (!read_predictor(v, "active_predictor_id", out.settings.active_predictor_id, err))
      {
        return false;
      }
      out.set_mask |= SETTING_ACTIVE_PREDICTOR;
    }
    else if (key == "horizon_ms")
    {
      double ms = 0.0;
      if (!read_number(v, "horizon_ms", ms, err))
      {
        return false;
      }
      // Upper bound only to keep the ns conversion far from overflow; the
      // orchestrator checks the real range (K_MAX at the measured rate)
      if (ms < 0.0 || ms > 1e6)
      {
        return reject(err, ERR_INVALID_ARGUMENT, "horizon_ms out of range");
      }
      out.settings.horizon_ns = std::llround(ms * 1e6);
      out.set_mask |= SETTING_HORIZON;
    }
    else if (key == "metrics_hz" || key == "spectrum_hz")
    {
      double hz = 0.0;
      if (!read_number(v, key.c_str(), hz, err))
      {
        return false;
      }
      if (!(hz > 0.0))
      {
        return reject(err, ERR_INVALID_ARGUMENT, "%s must be > 0", key.c_str());
      }
      if (key == "metrics_hz")
      {
        out.settings.metrics_hz = hz;
        out.set_mask |= SETTING_METRICS_HZ;
      }
      else
      {
        out.settings.spectrum_hz = hz;
        out.set_mask |= SETTING_SPECTRUM_HZ;
      }
    }
    else
    {
      // A typo must not look like an applied setting
      return reject(err, ERR_INVALID_ARGUMENT, "unknown setup field '%.40s'", key.c_str());
    }
  }
  return true;
}

} // namespace

/* ---------------------------------------------------------------------------------------------------
 * Encoder
 * ------------------------------------------------------------------------------------------------- */

bool Protocol_Encoder::encode(const Server_Message& msg, std::string& out) const
{
  switch (msg.kind)
  {
    case SERVER_MSG_FRAME:
      encode_frame(msg.frame, out);
      return true;
    case SERVER_MSG_METRICS:
      encode_metrics(msg.metrics, out);
      return true;
    case SERVER_MSG_SPECTRUM:
      encode_spectrum(msg.spectrum, out);
      return true;
    case SERVER_MSG_STATUS:
      encode_status(msg.status, out);
      return true;
    default:
      return false;
  }
}

void Protocol_Encoder::encode_frame(const Output_Frame& f, std::string& out) const
{
  begin_line(out, "frame");
  append_key(out, "sequence");
  append_uint(out, f.seq);
  append_key(out, "timestamp_ns");
  append_int(out, f.t_ns - origin_t_ns_);
  append_key(out, "gap_before");
  append_bool(out, (f.flags & FRAME_GAP_BEFORE) != 0);
  append_key(out, "tremor_active");
  append_bool(out, (f.flags & FRAME_TREMOR_ACTIVE) != 0);
  append_key(out, "tremor_displacement_mm");
  append_vec3(out, f.tremor_disp, M_TO_MM);
  append_key(out, "tremor_acceleration_ms2");
  append_vec3(out, f.tremor_acc, 1.0);

  append_key(out, "orientation_wxyz");
  out.push_back('[');
  for (int i = 0; i < 4; i++)
  {
    if (i > 0)
    {
      out.push_back(',');
    }
    append_number(out, f.orientation.q[i]);
  }
  out.push_back(']');

  append_key(out, "tremor_frequency_hz");
  append_number(out, f.tremor_hz);
  append_key(out, "tremor_amplitude_mm");
  append_number(out, f.tremor_amp * M_TO_MM);
  append_key(out, "tremor_confidence");
  append_number(out, f.confidence);
  append_key(out, "pipeline_delay_ms");
  append_number(out, (double)f.pipeline_delay_ns * NS_TO_MS);
  append_key(out, "counter_signal_mm");
  append_vec3_or_null(out, f.counter, M_TO_MM, (f.flags & FRAME_COUNTER_VALID) != 0);

  append_key(out, "predictions");
  out.push_back('[');
  uint32_t n = f.n_predictions < P_MAX ? f.n_predictions : P_MAX;
  bool first = true;
  for (uint32_t i = 0; i < n; i++)
  {
    const Frame_Prediction& p = f.predictions[i];
    if (!predictor_name(p.predictor_id))
    {
      continue; // an id the UI could not name: skip rather than send garbage
    }
    if (!first)
    {
      out.push_back(',');
    }
    first = false;

    out += "{\"predictor_id\":";
    append_predictor(out, p.predictor_id);
    append_key(out, "forecast_mm");
    append_vec3_or_null(out, p.forecast, M_TO_MM, (p.flags & PRED_FORECAST_VALID) != 0);
    append_key(out, "residual_mm");
    append_vec3_or_null(out, p.residual, M_TO_MM, (p.flags & PRED_RESIDUAL_VALID) != 0);
    out.push_back('}');
  }
  out.push_back(']');

  end_line(out);
}

void Protocol_Encoder::encode_metrics(const Metrics_Message& m, std::string& out) const
{
  uint32_t n = m.count < P_MAX ? m.count : P_MAX;
  for (uint32_t i = 0; i < n; i++)
  {
    const Predictor_Metrics& p = m.items[i];
    if (!predictor_name(p.predictor_id))
    {
      continue;
    }
    begin_line(out, "metrics");
    append_key(out, "predictor_id");
    append_predictor(out, p.predictor_id);
    append_key(out, "timestamp_ns");
    append_int(out, m.t_ns - origin_t_ns_);
    append_key(out, "suppression_ratio");
    append_number(out, p.suppression_ratio);
    append_key(out, "residual_rms_mm");
    append_number(out, p.residual_rms * M_TO_MM);
    append_key(out, "window_ms");
    append_number(out, (double)m.window_ns * NS_TO_MS);
    end_line(out);
  }
}

void Protocol_Encoder::encode_spectrum(const Spectrum_Message& s, std::string& out) const
{
  static const char* const AXIS_KEYS[3] = { "x_ms2", "y_ms2", "z_ms2" };
  uint32_t n = s.n_bins < SPECTRUM_MAX_BINS ? s.n_bins : SPECTRUM_MAX_BINS;

  begin_line(out, "spectrum");
  append_key(out, "timestamp_ns");
  append_int(out, s.t_ns - origin_t_ns_);
  append_key(out, "bin_width_hz");
  append_number(out, s.bin_width_hz);
  for (int a = 0; a < 3; a++)
  {
    append_key(out, AXIS_KEYS[a]);
    out.push_back('[');
    for (uint32_t i = 0; i < n; i++)
    {
      if (i > 0)
      {
        out.push_back(',');
      }
      append_number(out, s.amp[a][i]);
    }
    out.push_back(']');
  }
  end_line(out);
}

void Protocol_Encoder::encode_status(const Status_Message& s, std::string& out) const
{
  begin_line(out, "status");
  append_key(out, "protocol_version");
  append_int(out, PROTOCOL_VERSION);
  append_key(out, "state");
  append_string(out, protocol_state_name(s.state));
  append_key(out, "sample_rate_hz");
  append_number(out, s.sample_rate_hz);
  append_key(out, "horizon_ms");
  append_number(out, (double)s.horizon_ns * NS_TO_MS);

  // Only while calibrating: {"step": ..., "progress": 0-1}
  append_key(out, "calibration");
  if (s.state == APP_STATE_CALIBRATING && s.calibration_step != CAL_STEP_NONE && s.calibration_step <= CAL_STEP_ROTATE)
  {
    out += "{\"step\":";
    append_string(out, CAL_STEP_NAMES[s.calibration_step]);
    append_key(out, "progress");
    append_number(out, s.calibration_progress);
    out.push_back('}');
  }
  else
  {
    out += "null";
  }

  append_key(out, "calibration_age_s");
  append_number(out, s.calibration_age_s); // NaN (no calibration) -> null

  append_key(out, "enabled_predictor_ids");
  out.push_back('[');
  bool first = true;
  for (Predictor_Id id = 0; id < PREDICTOR_COUNT; id++)
  {
    if (s.enabled_mask & (UINT32_C(1) << id))
    {
      if (!first)
      {
        out.push_back(',');
      }
      first = false;
      append_string(out, predictor_name(id));
    }
  }
  out.push_back(']');

  append_key(out, "active_predictor_id");
  append_predictor(out, s.active_predictor_id);
  append_key(out, "metrics_hz");
  append_number(out, s.metrics_hz);
  append_key(out, "spectrum_hz");
  append_number(out, s.spectrum_hz);

  append_key(out, "drops");
  out += "{\"hat_to_processing\":";
  append_uint(out, s.drops_hat_to_processing);
  append_key(out, "processing_to_prediction");
  append_uint(out, s.drops_processing_to_prediction);
  append_key(out, "processing_to_server");
  append_uint(out, s.drops_processing_to_server);
  out.push_back('}');

  end_line(out);
}

void Protocol_Encoder::encode_reply(const Command_Reply& reply, std::string& out) const
{
  if (reply.error == ERR_NONE)
  {
    // An ack answers a command: without its id there is nothing to say
    if (reply.has_id)
    {
      begin_line(out, "ack");
      append_key(out, "id");
      append_uint(out, reply.id);
      end_line(out);
    }
    return;
  }

  begin_line(out, "error");
  if (reply.has_id)
  {
    append_key(out, "id");
    append_uint(out, reply.id);
  }
  append_key(out, "code");
  append_string(out, protocol_error_code_name(reply.error));

  // msg is a fixed buffer: never trust it to be terminated
  char msg[REPLY_MSG_BYTES + 1];
  std::memcpy(msg, reply.msg, REPLY_MSG_BYTES);
  msg[REPLY_MSG_BYTES] = '\0';
  append_key(out, "msg");
  append_string(out, msg);
  end_line(out);
}

/* ---------------------------------------------------------------------------------------------------
 * Decoder
 * ------------------------------------------------------------------------------------------------- */

bool protocol_decode_command(const char* data, size_t len, Command& out, Command_Reply& err)
{
  std::memset(&err, 0, sizeof(err));
  std::memset(&out, 0, sizeof(out));

  Json_Value root;
  std::string parse_error;
  if (!json_parse(data, len, root, &parse_error))
  {
    return reject(err, ERR_MALFORMED_MESSAGE, "invalid JSON: %.60s", parse_error.c_str());
  }
  if (root.type != Json_Type::object)
  {
    return reject(err, ERR_MALFORMED_MESSAGE, "a message must be a JSON object");
  }

  const Json_Value* type = root.find("type");
  if (!type || type->type != Json_Type::string || type->string != "command")
  {
    return reject(err, ERR_MALFORMED_MESSAGE, "type must be \"command\"");
  }

  // Read the id first: every later error can then echo it
  const Json_Value* id = root.find("id");
  if (!id || !id->is_integer() || id->number < 0.0 || id->number > 4294967295.0)
  {
    return reject(err, ERR_MALFORMED_MESSAGE, "id missing or not an integer in [0, 2^32)");
  }
  err.id = (uint32_t)id->number;
  err.has_id = 1;
  out.id = err.id;

  const Json_Value* command = root.find("command");
  if (!command || command->type != Json_Type::string)
  {
    return reject(err, ERR_MALFORMED_MESSAGE, "command missing or not a string");
  }

  const std::string& name = command->string;
  if (name == "start")
  {
    out.kind = CMD_START;
  }
  else if (name == "stop")
  {
    out.kind = CMD_STOP;
  }
  else if (name == "calibrate")
  {
    out.kind = CMD_CALIBRATE;
  }
  else if (name == "setup")
  {
    out.kind = CMD_SETUP;
    if (!decode_setup(root, out, err))
    {
      return false;
    }
  }
  else
  {
    return reject(err, ERR_UNKNOWN_COMMAND, "unknown command '%.40s'", name.c_str());
  }

  err.error = ERR_NONE;
  return true;
}

const char* protocol_error_code_name(uint32_t error)
{
  return error < sizeof(ERROR_NAMES) / sizeof(ERROR_NAMES[0]) ? ERROR_NAMES[error] : "unknown";
}

const char* protocol_state_name(uint32_t state)
{
  return state < sizeof(STATE_NAMES) / sizeof(STATE_NAMES[0]) ? STATE_NAMES[state] : "unknown";
}

} // namespace mts
