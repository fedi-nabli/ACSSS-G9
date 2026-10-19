# MTS web UI handoff

Branch: `feat/websocket`. This is the browser client for **protocol v1** in [`ui_contract.md`](./ui_contract.md).

The Raspberry Pi / Sense HAT / C++ pipeline is **not** in this tree. The Pi serves WebSocket only (no files). This UI is served by Vite (or any static host) and connects to `ws://<pi>:<port>`.

## What you get

- Typed NDJSON WebSocket client (`web/src/mts/`)
- Operator UI: connect, calibrate, start/stop, setup, plots
- Local mock Pi so you can demo without hardware
- Tests against the example stream in the contract

Hardware, IMU fusion, band-pass, predictors, and Hat mode (real/sim) stay on the C++ side.

## Run locally (mock, no Pi)

Two terminals, from `web/`:

```bash
cd web
npm install
npm run mock    # WebSocket server at ws://localhost:8765
```

```bash
cd web
npm run dev     # UI at http://localhost:5173
```

1. Open http://localhost:5173
2. URL `ws://localhost:8765` → **Connect**
3. **Calibrate** (mock: `hold_still` then `rotate`) → state `ready`
4. **Start** (mock: `quick_zero`) → state `running`
5. **Stop** / **Disconnect** as needed

`Start` is disabled until `ready` or `stopped`. That matches contract §5 (`start` in `needs_calibration` returns `needs_calibration`).

```bash
cd web
npm test
npm run build
```

## Run against the Raspberry Pi

1. C++ server listening on the Pi (Sense HAT or their sim launch flag — not a UI `setup` field).
2. Same LAN as the laptop. Note host and port.
3. `npm run dev` as above (do **not** point `npm run mock` at the Pi).
4. In the UI, set `ws://<pi-ip-or-hostname>:<port>` → **Connect**.

Expected: `status` heartbeat (~1 Hz). State goes `boot` → `needs_calibration` or `ready`. Then the same Calibrate → Start flow as the mock.

Sample rate, Hat mode, and sim parameters are launch config on the Pi. They are not changeable through `setup`.

## Layout of the code

```
web/src/mts/           Protocol library (use this if you swap the UI)
  types.ts             Message types, protocol_version = 1
  parse.ts             NDJSON split + decode (unknown fields ignored)
  client.ts            WebSocket, command ids, ack/error
  commands.ts          State machine from contract §5
  history.ts           Frame buffer sized in seconds from sample_rate_hz
  quat.ts              orientation_wxyz → three.js xyzw
  index.ts             Public exports
web/src/hooks/useMtsClient.ts    React binding
web/src/components/    Displacement + spectrum plots
web/src/App.tsx        Operator chrome
web/scripts/mock-pi.mjs
```

The UI imports the client; Izzy’s charts (or anything else) can do the same. You do not need to reimplement NDJSON.

### Client usage

```ts
import { MtsClient } from './mts'

const client = new MtsClient({ url: 'ws://192.168.1.42:8765' })
client.on('status', (s) => { /* state, sample_rate_hz, … */ })
client.on('frame', (f) => { /* 100–200 Hz; prefer client.history for plots */ })
client.on('metrics', (m) => { /* per predictor_id */ })
client.on('spectrum', (s) => { /* use s.bin_width_hz */ })
client.on('serverError', (e) => { /* show e.msg; switch on e.code if known */ })
client.connect()

await client.sendCommand('calibrate')
await client.sendCommand('start')
await client.sendCommand('setup', { horizon_ms: 40, active_predictor_id: 'wflc' })
await client.sendCommand('stop')
client.disconnect()
```

Commands are one JSON line with a client-chosen integer `id`. The promise resolves on `ack` and rejects on `error` (`MtsCommandError` has `code` and `msg`). Unsolicited `error`s (no `id`, e.g. `hat_error`) still fire `serverError`.

Frames can be several JSON lines per WebSocket message. The client splits on `\n` and does not assume one object per message.

## What the current plots show

**Displacement (1.5 s)** — Sense HAT **body frame** `[x, y, z]` (rotates with the hand), **millimetres**:

- Solid: `tremor_displacement_mm` (4–12 Hz, slow voluntary motion removed, lags by `pipeline_delay_ms`)
- Dashed: `counter_signal_mm` (negative of the active predictor’s forecast; absent while the tremor gate is closed)
- Line breaks on `gap_before` (do not interpolate dropped samples)

World pose is `orientation_wxyz` (w first). It is parsed and `quatWxyzToXyzw` exists; there is no 3D view yet.

Freq / amp / confidence are always in the frame; the UI greys them when `tremor_active` is false.

**Metrics** — C++-computed `suppression_ratio` and `residual_rms_mm` per enabled predictor. Ratio &lt; 1 is cancelling. Do not recompute on the UI.

**Spectrum** — gravity-removed linear acceleration **before** the 4–12 Hz band-pass, 0–25 Hz. Bin *i* is at `i * bin_width_hz` (read from each message).

`tremor_acceleration_ms2` is in the frame (“for display”) but is not plotted.

## Izzy’s signals UI (`izzyFeatureBranch`)

Separate history: `advCompUserInterface/` on `origin/izzyFeatureBranch`. Six Recharts panels (X/Y/Z position and X/Y/Z tilt), each with **input / counter / result**. Data is a local `setInterval` simulation, not the socket.

Same story (motion vs anti-motion vs leftover), different layout and no protocol.

Optional follow-up: keep this operator UI, or feed Izzy’s charts from `MtsClient`:

| Izzy trace | Protocol field |
|---|---|
| input | `tremor_displacement_mm[axis]` |
| counter | `counter_signal_mm[axis]` |
| result | active predictor `residual_mm[axis]` |

Tilt is **not** a v1 field. You would convert `orientation_wxyz` to Euler angles in the UI, or extend the contract.

## Contract checklist (UI side)

Adhered to for v1:

- WebSocket only; URL configurable (remembered in `localStorage`)
- NDJSON; leftover if a line spans messages; complete object without trailing newline still parsed
- Unknown JSON fields and unknown `type`s ignored
- `null` for N/A (`counter_signal_mm`, `forecast_mm`, `residual_mm`, `calibration`, `calibration_age_s`)
- History length from `sample_rate_hz` (1.5 s → 150 frames at 100 Hz, 300 at 200 Hz)
- Command state machine §5
- Show `msg` for unrecognised error codes

Not this deliverable: C++ serializer, Hat I/O, predictor maths, serving the webpage from the Pi.

## If something fails

| Symptom | Likely cause |
|---|---|
| Connect never opens | Wrong IP/port, server down, firewall. Mock log: `MTS mock Pi listening on ws://localhost:8765` |
| `needs_calibration` / `busy` / `invalid_state` | Contract errors — show `msg` |
| Parse notice | Known `type` with a missing required field — check Pi JSON vs the contract |
| Plot gaps | `gap_before: true` (drops), not a renderer bug |
| No frames while connected | State is not `running` (`stop` / still calibrating) |

## Team one-liner

The UI is a protocol v1 WebSocket client. Point it at the C++ server. Do not serve the page from the Pi. Speak NDJSON as in `ui_contract.md`; the example stream is covered by `web` tests.
