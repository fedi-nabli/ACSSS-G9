# MTS UI Protocol, version 1

Draft for the UI team, 2026-10-15. This is the contract between the C++ server on the Pi and the web UI.

## 1. Transport

- WebSocket only. The UI connects to `ws://<pi>:<port>`, and the address is configurable in the UI.
- The C++ side serves no files.
- Each WebSocket message contains one or more JSON lines separated by `\n` (NDJSON). The server flushes about every 30 ms, so at 100-200 Hz a message holds roughly 3-6 lines. The UI must split on `\n` and must not assume one line per message.
- Every line is a single JSON object with a `type` field.

## 2. Conventions

- **Units are in the key names**: `_mm`, `_ms`, `_ms2` (m/s²), `_hz`, `_ns`, `_s`. The C++ core works in SI and the serializer converts.
- **`timestamp_ns`** is nanoseconds since the server opened the Hat. It is monotonic, never resets across start/stop, and is not wall-clock time. Use it for ordering and time differences only. In sim mode it is virtual time.
- **`null`** means "not applicable right now" (for example `residual_mm` while no tremor is active).
- **Unknown fields must be ignored** by the UI, so fields can be added without breaking it.
- **Vectors** are `[x, y, z]` in the sensor body frame unless stated otherwise. The body frame rotates with the hand. `orientation_wxyz` gives its orientation in the world.
- **`msg`** is human-readable text, and **`code`** is a stable string the UI can switch on.

## 3. Server to UI messages

### 3.1 `frame`

One per sample (100-200 Hz). Everything in a frame refers to the same instant. No frames are sent while the state is `stopped`.

| field | type | meaning |
|---|---|---|
| `sequence` | integer | Sample counter assigned at the Hat, +1 per sample, never wraps. |
| `timestamp_ns` | integer | When the sample was taken. |
| `gap_before` | bool | `true` if samples were dropped immediately before this one. Break the plotted line here, don't interpolate. |
| `tremor_active` | bool | The detector's flag (band power, peak narrowness, amplitude, with hysteresis). |
| `tremor_displacement_mm` | `[x,y,z]` | Band-passed (4-12 Hz) displacement of the sensor from its slowly moving mean position. It oscillates around zero. Slow voluntary motion is removed. It lags real motion by `pipeline_delay_ms`. |
| `tremor_acceleration_ms2` | `[x,y,z]` | Band-passed tremor acceleration, for display. |
| `orientation_wxyz` | `[w,x,y,z]` | Unit quaternion, body to world. Order is **w first**; three.js expects x,y,z,w, so reorder it. |
| `tremor_frequency_hz` | number | Estimated dominant tremor frequency. |
| `tremor_amplitude_mm` | number | Estimated tremor amplitude (peak, not RMS). |
| `tremor_confidence` | number 0-1 | Detector confidence. |
| `pipeline_delay_ms` | number | Total causal delay of all processing stages. |
| `counter_signal_mm` | `[x,y,z]` or `null` | Negative of the active predictor's forecast for this instant: what an actuator would be commanded. `null` while the tremor gate is closed. |
| `predictions` | array | One entry per enabled predictor (below). |

`tremor_frequency_hz`, `tremor_amplitude_mm` and `tremor_confidence` are always sent. They are not meaningful while `tremor_active` is false, so the UI should hide or grey them in that case.

Each entry in `predictions`:

| field | type | meaning |
|---|---|---|
| `predictor_id` | string | For example `inversion`, `wflc`. |
| `forecast_mm` | `[x,y,z]` | What this predictor, `horizon_ms` earlier, said would happen at this instant. `null` during warm-up. |
| `residual_mm` | `[x,y,z]` or `null` | Actual displacement minus forecast: what remains uncancelled. `null` while `tremor_active` is false or during warm-up. |

### 3.2 `metrics`

About 2 Hz by default (`metrics_hz`), one line per enabled predictor. Statistics are computed in C++, so the UI never needs to recompute them.

| field | type | meaning |
|---|---|---|
| `predictor_id` | string | Which predictor. |
| `timestamp_ns` | integer | End of the window. |
| `suppression_ratio` | number | Residual RMS divided by tremor RMS over the window. 1 means no effect, below 1 means cancelling, above 1 means making it worse. |
| `residual_rms_mm` | number | RMS of the residual over the window. |
| `window_ms` | number | Window length used. |

Only samples with `tremor_active` true count towards these numbers.

### 3.3 `spectrum`

About 5 Hz by default (`spectrum_hz`), one line per update, per axis.

| field | type | meaning |
|---|---|---|
| `timestamp_ns` | integer | End of the analysis window. |
| `bin_width_hz` | number | Bin *i* is at *i* × `bin_width_hz`. |
| `x_ms2`, `y_ms2`, `z_ms2` | number arrays | Amplitude per bin, 0-25 Hz only. |

The spectrum is computed over about 2 seconds of gravity-removed linear acceleration **before** the 4-12 Hz band-pass. Voluntary motion therefore shows at low frequency and the tremor as a peak at 4-12 Hz. The window is rounded to a power-of-two length, so `bin_width_hz` is whatever results, and the UI must read it from each message.

### 3.4 `status`

Sent on every change and as a heartbeat about once per second.

| field | type | meaning |
|---|---|---|
| `protocol_version` | integer | Currently `1`, bumped on breaking changes only. |
| `state` | string | `boot`, `needs_calibration`, `ready`, `calibrating`, `running`, `stopped`. |
| `sample_rate_hz` | number | Actual sample rate. Size the history buffer in seconds from this, not in a fixed frame count. |
| `horizon_ms` | number | How far ahead all predictors forecast. |
| `calibration` | object or `null` | While `state` is `calibrating`: `{"step": "...", "progress": 0-1}`. `step` is `quick_zero`, `hold_still` or `rotate`. Otherwise `null`. |
| `calibration_age_s` | number or `null` | Age of the stored calibration, `null` if none exists. |
| `enabled_predictor_ids` | string array | Predictors running and being scored. |
| `active_predictor_id` | string | The predictor whose forecast becomes `counter_signal_mm`. |
| `metrics_hz`, `spectrum_hz` | number | Current slow-message rates. |
| `drops` | object | Dropped-item counters: `hat_to_processing`, `processing_to_prediction`, `processing_to_server`. |

### 3.5 `ack` and `error`

Replies to a `command`. Both echo its `id`.

```
{"type":"ack","id":7}
{"type":"error","id":7,"code":"needs_calibration","msg":"No valid calibration; run calibrate first."}
```

`error` can also arrive without an `id`, for example for `malformed_message` or `hat_error`.

## 4. UI to server: `command`

```
{"type":"command","id":7,"command":"start"}
```

`id` is an integer chosen by the UI and echoed in the reply.

| command | payload | effect |
|---|---|---|
| `start` | none | Runs the quick zero (`calibrating` with `step: "quick_zero"`), then goes to `running`. |
| `stop` | none | Goes to `stopped`. Frames stop, `status` continues. |
| `calibrate` | none | Full calibration (`hold_still`, then `rotate`), then back to `ready`. |
| `setup` | optional fields (below) | Changes runtime settings. Send only the fields you change. |

`setup` fields, all optional:

| field | meaning |
|---|---|
| `enabled_predictor_ids` | Which predictors run and are scored. |
| `active_predictor_id` | Which forecast becomes `counter_signal_mm`. Must be in the enabled set. |
| `horizon_ms` | Forecast horizon. |
| `metrics_hz`, `spectrum_hz` | Slow-message rates. |

`setup` applies at a sample boundary. Changing a predictor resets its internal state. The new values show up in the next `status`.

Sample rate, Hat mode (real/sim) and sim parameters are launch configuration and are not changeable through `setup`.

## 5. State machine

| state | `start` | `stop` | `calibrate` | `setup` |
|---|---|---|---|---|
| `boot` | `invalid_state` | `invalid_state` | `invalid_state` | `invalid_state` |
| `needs_calibration` | `needs_calibration` | `invalid_state` | ok | ok |
| `ready` | ok | `invalid_state` | ok | ok |
| `calibrating` | `busy` | `busy` | `busy` | `busy` |
| `running` | `invalid_state` | ok | `invalid_state` | ok |
| `stopped` | ok | `invalid_state` | ok | ok |

Flow: `boot` → `needs_calibration` or `ready`. `start` goes `ready`/`stopped` → `calibrating` (quick zero) → `running`. `calibrate` goes to `calibrating` and returns to `ready`.

## 6. Error codes

This list grows as we need it. The UI must show `msg` for any code it doesn't recognise.

| code | when |
|---|---|
| `needs_calibration` | `start` with no valid calibration. |
| `busy` | A command arrives while calibrating. |
| `invalid_state` | The command is not valid in the current state. |
| `invalid_argument` | A `setup` value is bad, a predictor id is unknown, or the active predictor is not in the enabled set. |
| `unknown_command` | `command` is not recognised. |
| `malformed_message` | Bad JSON, or `id`/`command` is missing. |
| `hat_error` | The sensor failed or disconnected. |
| `calibration_failed` | The sensor moved during hold-still, or magnetometer rotation coverage was insufficient. |

## 7. Rates and buffering

- `frame` follows the sample rate (100-200 Hz).
- `metrics` and `spectrum` rates are time-based settings (defaults 2 Hz and 5 Hz). They are tunable through `setup`, and the current values are in `status`. The defaults are unmeasured on the Pi.
- Size the history buffer in seconds using `sample_rate_hz`. For example, 1.5 s is 150 frames at 100 Hz and 300 frames at 200 Hz.

## 8. Example stream

Values are illustrative. Spectrum arrays are shortened here, and real ones have about 65 bins for 0-25 Hz.

```
{"type":"status","protocol_version":1,"state":"needs_calibration","sample_rate_hz":100,"horizon_ms":30,"calibration":null,"calibration_age_s":null,"enabled_predictor_ids":["inversion","wflc"],"active_predictor_id":"wflc","metrics_hz":2,"spectrum_hz":5,"drops":{"hat_to_processing":0,"processing_to_prediction":0,"processing_to_server":0}}
{"type":"command","id":1,"command":"start"}
{"type":"error","id":1,"code":"needs_calibration","msg":"No valid calibration; run calibrate first."}
{"type":"command","id":2,"command":"calibrate"}
{"type":"ack","id":2}
{"type":"status","protocol_version":1,"state":"calibrating","sample_rate_hz":100,"horizon_ms":30,"calibration":{"step":"hold_still","progress":0.4},"calibration_age_s":null,"enabled_predictor_ids":["inversion","wflc"],"active_predictor_id":"wflc","metrics_hz":2,"spectrum_hz":5,"drops":{"hat_to_processing":0,"processing_to_prediction":0,"processing_to_server":0}}
{"type":"status","protocol_version":1,"state":"ready","sample_rate_hz":100,"horizon_ms":30,"calibration":null,"calibration_age_s":0,"enabled_predictor_ids":["inversion","wflc"],"active_predictor_id":"wflc","metrics_hz":2,"spectrum_hz":5,"drops":{"hat_to_processing":0,"processing_to_prediction":0,"processing_to_server":0}}
{"type":"command","id":3,"command":"start"}
{"type":"ack","id":3}
{"type":"status","protocol_version":1,"state":"running","sample_rate_hz":100,"horizon_ms":30,"calibration":null,"calibration_age_s":12,"enabled_predictor_ids":["inversion","wflc"],"active_predictor_id":"wflc","metrics_hz":2,"spectrum_hz":5,"drops":{"hat_to_processing":0,"processing_to_prediction":0,"processing_to_server":0}}
{"type":"frame","sequence":4200,"timestamp_ns":42000000000,"gap_before":false,"tremor_active":false,"tremor_displacement_mm":[0.02,-0.01,0.00],"tremor_acceleration_ms2":[0.03,-0.01,0.01],"orientation_wxyz":[0.998,0.03,-0.05,0.01],"tremor_frequency_hz":5.2,"tremor_amplitude_mm":0.03,"tremor_confidence":0.12,"pipeline_delay_ms":42,"counter_signal_mm":null,"predictions":[{"predictor_id":"inversion","forecast_mm":[0.01,0.00,0.00],"residual_mm":null},{"predictor_id":"wflc","forecast_mm":[0.02,-0.01,0.00],"residual_mm":null}]}
{"type":"frame","sequence":4201,"timestamp_ns":42010000000,"gap_before":false,"tremor_active":true,"tremor_displacement_mm":[0.61,-0.10,0.07],"tremor_acceleration_ms2":[0.87,-0.14,0.10],"orientation_wxyz":[0.998,0.03,-0.05,0.01],"tremor_frequency_hz":6.1,"tremor_amplitude_mm":0.8,"tremor_confidence":0.93,"pipeline_delay_ms":42,"counter_signal_mm":[-0.55,0.12,-0.08],"predictions":[{"predictor_id":"inversion","forecast_mm":[0.30,-0.05,0.04],"residual_mm":[0.31,-0.05,0.03]},{"predictor_id":"wflc","forecast_mm":[0.55,-0.12,0.08],"residual_mm":[0.06,0.02,-0.01]}]}
{"type":"frame","sequence":4205,"timestamp_ns":42050000000,"gap_before":true,"tremor_active":true,"tremor_displacement_mm":[-0.42,0.08,-0.05],"tremor_acceleration_ms2":[-0.60,0.11,-0.07],"orientation_wxyz":[0.998,0.03,-0.05,0.01],"tremor_frequency_hz":6.1,"tremor_amplitude_mm":0.8,"tremor_confidence":0.93,"pipeline_delay_ms":42,"counter_signal_mm":[0.38,-0.07,0.05],"predictions":[{"predictor_id":"inversion","forecast_mm":[-0.20,0.03,-0.02],"residual_mm":[-0.22,0.05,-0.03]},{"predictor_id":"wflc","forecast_mm":[-0.38,0.07,-0.05],"residual_mm":[-0.04,0.01,0.00]}]}
{"type":"metrics","predictor_id":"wflc","timestamp_ns":42500000000,"suppression_ratio":0.18,"residual_rms_mm":0.09,"window_ms":2000}
{"type":"metrics","predictor_id":"inversion","timestamp_ns":42500000000,"suppression_ratio":0.62,"residual_rms_mm":0.31,"window_ms":2000}
{"type":"spectrum","timestamp_ns":42500000000,"bin_width_hz":0.39,"x_ms2":[0.01,0.02,0.01,0.40,0.85],"y_ms2":[0.00,0.01,0.01,0.06,0.14],"z_ms2":[0.00,0.01,0.00,0.04,0.09]}
```
