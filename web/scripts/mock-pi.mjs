/**
 * Local stand-in for the C++ WebSocket server (docs/ui_contract.md).
 * Speaks NDJSON, batches ~every 30 ms, 100 Hz frames.
 *
 *   npm run mock
 */
import { WebSocketServer } from 'ws'

const PORT = Number(process.env.MTS_MOCK_PORT ?? 8765)
const SAMPLE_HZ = 100
const BATCH_MS = 30

const wss = new WebSocketServer({ port: PORT })
console.log(`MTS mock Pi listening on ws://localhost:${PORT}`)

wss.on('connection', (socket) => {
  const session = createSession(socket)
  session.start()
  socket.on('message', (data) => session.onMessage(String(data)))
  socket.on('close', () => session.stop())
})

function createSession(socket) {
  let state = 'needs_calibration'
  let calibration = null
  let enabled = ['inversion', 'wflc']
  let active = 'wflc'
  let horizonMs = 30
  let metricsHz = 2
  let spectrumHz = 5
  let sequence = 0
  let timestampNs = 0
  let lastMetricsNs = 0
  let lastSpectrumNs = 0
  let lastStatusNs = 0
  let calReadyAt = null
  let timers = []

  const sendLines = (objs) => {
    if (socket.readyState !== socket.OPEN) return
    socket.send(objs.map((o) => JSON.stringify(o)).join('\n') + '\n')
  }

  const statusMsg = () => ({
    type: 'status',
    protocol_version: 1,
    state,
    sample_rate_hz: SAMPLE_HZ,
    horizon_ms: horizonMs,
    calibration,
    calibration_age_s: calReadyAt === null ? null : (Date.now() - calReadyAt) / 1000,
    enabled_predictor_ids: enabled,
    active_predictor_id: active,
    metrics_hz: metricsHz,
    spectrum_hz: spectrumHz,
    drops: {
      hat_to_processing: 0,
      processing_to_prediction: 0,
      processing_to_server: 0,
    },
  })

  const setState = (next, cal = null) => {
    state = next
    calibration = cal
    sendLines([statusMsg()])
  }

  const replyAck = (id) => sendLines([{ type: 'ack', id }])
  const replyErr = (id, code, msg) => sendLines([{ type: 'error', id, code, msg }])

  const allowed = (command) => {
    const table = {
      start: { ready: true, stopped: true, needs_calibration: 'needs_calibration' },
      stop: { running: true },
      calibrate: { needs_calibration: true, ready: true, stopped: true },
      setup: { needs_calibration: true, ready: true, running: true, stopped: true },
    }
    const row = table[command]
    if (!row) return 'unknown_command'
    if (state === 'calibrating') return 'busy'
    if (state === 'boot') return 'invalid_state'
    const cell = row[state]
    if (cell === true) return null
    if (typeof cell === 'string') return cell
    return 'invalid_state'
  }

  const runCalibration = (kind, thenState) => {
    const steps =
      kind === 'full'
        ? [
          ['hold_still', 1200],
          ['rotate', 1600],
        ]
        : [['quick_zero', 700]]
    let i = 0
    const tick = () => {
      if (i >= steps.length) {
        calibration = null
        if (kind === 'full') calReadyAt = Date.now()
        setState(thenState, null)
        return
      }
      const [step, ms] = steps[i]
      const t0 = Date.now()
      const id = setInterval(() => {
        const p = Math.min(1, (Date.now() - t0) / ms)
        calibration = { step, progress: p }
        sendLines([statusMsg()])
        if (p >= 1) {
          clearInterval(id)
          i += 1
          tick()
        }
      }, 120)
      timers.push(id)
    }
    setState('calibrating', { step: steps[0][0], progress: 0 })
    tick()
  }

  const makeFrame = () => {
    sequence += 1
    timestampNs += 1e9 / SAMPLE_HZ
    const t = timestampNs / 1e9
    const tremorOn = sequence > 80 && Math.sin(t * 0.4) > -0.2
    const f = 6.1
    const amp = tremorOn ? 0.8 : 0.03
    const phase = 2 * Math.PI * f * t
    const disp = [
      amp * Math.sin(phase),
      amp * 0.18 * Math.sin(phase + 0.4),
      amp * 0.12 * Math.cos(phase),
    ]
    const acc = disp.map((v) => v * (2 * Math.PI * f) ** 2 * 0.001)
    const wflc = disp.map((v) => v * (tremorOn ? 0.9 : 0.4))
    const inv = disp.map((v) => v * (tremorOn ? 0.45 : 0.2))
    const residual = (pred) =>
      tremorOn ? disp.map((v, i) => v - pred[i]) : null
    const gap = sequence % 240 === 0


    // simulated IMU measurements
    const makeImu = (time) => {
      const p = 2 * Math.PI * f * time;
      const factor = (2 * Math.PI * f) ** 2 * 0.001 / 9.80665;

      return {
        ax_g: amp * Math.sin(p) * factor,
        ay_g: amp * 0.18 * Math.sin(p + 0.4) * factor,
        az_g: amp * 0.12 * Math.cos(p) * factor,

        gx_rad_s: amp * 0.05 * Math.cos(p),
        gy_rad_s: amp * 0.015 * Math.cos(p + 0.4),
        gz_rad_s: -amp * 0.012 * Math.sin(p),
      };
    };

    // current measurement
    const imu = makeImu(t);

    // five future predicted measurements
    const forecastImu = Array.from(
      { length: 5 },
      (_, i) => makeImu(t + (i + 1) / SAMPLE_HZ)
    );


    return {
      type: 'frame',
      sequence,
      timestamp_ns: Math.round(timestampNs),
      imu: imu,
      gap_before: gap,
      tremor_active: tremorOn,
      tremor_displacement_mm: disp,
      tremor_acceleration_ms2: acc,
      orientation_wxyz: [0.998, 0.03, -0.05, 0.01],
      tremor_frequency_hz: tremorOn ? f : 5.2,
      tremor_amplitude_mm: tremorOn ? amp : 0.03,
      tremor_confidence: tremorOn ? 0.93 : 0.12,
      pipeline_delay_ms: 42,
      counter_signal_mm: tremorOn
        ? (active === 'wflc' ? wflc : inv).map((v) => -v)
        : null,
      predictions: [
        {
          predictor_id: 'inversion',
          forecast_mm: inv,
          forecast_imu: forecastImu,
          residual_mm: residual(inv),
        },
        {
          predictor_id: 'wflc',
          forecast_mm: wflc,
          forecast_imu: forecastImu,
          residual_mm: residual(wflc),
        },
      ],
    }
  }

  const makeSpectrum = () => {
    const bins = 65
    const binWidth = 25 / (bins - 1)
    const peakBin = Math.round(6.1 / binWidth)
    const axis = (scale) =>
      Array.from({ length: bins }, (_, i) => {
        const tremor = Math.exp(-((i - peakBin) ** 2) / 4) * scale
        const voluntary = i < 3 ? 0.08 * scale : 0
        return Number((voluntary + tremor).toFixed(4))
      })
    return {
      type: 'spectrum',
      timestamp_ns: Math.round(timestampNs),
      bin_width_hz: Number(binWidth.toFixed(4)),
      x_ms2: axis(0.9),
      y_ms2: axis(0.16),
      z_ms2: axis(0.1),
    }
  }

  const loop = () => {
    const batchCount = Math.max(1, Math.round((SAMPLE_HZ * BATCH_MS) / 1000))
    const lines = []
    if (state === 'running') {
      for (let i = 0; i < batchCount; i++) lines.push(makeFrame())
      if (timestampNs - lastMetricsNs >= 1e9 / metricsHz) {
        lastMetricsNs = timestampNs
        for (const id of enabled) {
          lines.push({
            type: 'metrics',
            predictor_id: id,
            timestamp_ns: Math.round(timestampNs),
            suppression_ratio: id === 'wflc' ? 0.18 : 0.62,
            residual_rms_mm: id === 'wflc' ? 0.09 : 0.31,
            window_ms: 2000,
          })
        }
      }
      if (timestampNs - lastSpectrumNs >= 1e9 / spectrumHz) {
        lastSpectrumNs = timestampNs
        lines.push(makeSpectrum())
      }
    }
    if (timestampNs - lastStatusNs >= 1e9 || lastStatusNs === 0) {
      lastStatusNs = timestampNs || 1
      lines.push(statusMsg())
    }
    if (state !== 'running') timestampNs += (BATCH_MS / 1000) * 1e9
    if (lines.length) sendLines(lines)
  }

  const onMessage = (text) => {
    for (const line of text.split('\n')) {
      if (!line) continue
      let msg
      try {
        msg = JSON.parse(line)
      } catch {
        sendLines([{ type: 'error', code: 'malformed_message', msg: 'Bad JSON' }])
        continue
      }
      if (msg?.type !== 'command' || typeof msg.id !== 'number' || typeof msg.command !== 'string') {
        sendLines([
          {
            type: 'error',
            code: 'malformed_message',
            msg: 'Bad JSON, or id/command is missing.',
          },
        ])
        continue
      }
      const deny = allowed(msg.command)
      if (deny === 'needs_calibration') {
        replyErr(msg.id, 'needs_calibration', 'No valid calibration; run calibrate first.')
        continue
      }
      if (deny === 'unknown_command') {
        replyErr(msg.id, 'unknown_command', `Unknown command ${msg.command}`)
        continue
      }
      if (deny) {
        replyErr(msg.id, deny, `Command ${msg.command} is not valid in state ${state}.`)
        continue
      }
      if (msg.command === 'setup') {
        if (msg.enabled_predictor_ids) enabled = msg.enabled_predictor_ids
        if (msg.active_predictor_id) {
          if (!enabled.includes(msg.active_predictor_id)) {
            replyErr(
              msg.id,
              'invalid_argument',
              'active_predictor_id must be in enabled_predictor_ids',
            )
            continue
          }
          active = msg.active_predictor_id
        }
        if (typeof msg.horizon_ms === 'number') horizonMs = msg.horizon_ms
        if (typeof msg.metrics_hz === 'number') metricsHz = msg.metrics_hz
        if (typeof msg.spectrum_hz === 'number') spectrumHz = msg.spectrum_hz
        replyAck(msg.id)
        sendLines([statusMsg()])
        continue
      }
      if (msg.command === 'calibrate') {
        replyAck(msg.id)
        runCalibration('full', 'ready')
        continue
      }
      if (msg.command === 'start') {
        replyAck(msg.id)
        runCalibration('quick', 'running')
        continue
      }
      if (msg.command === 'stop') {
        replyAck(msg.id)
        setState('stopped')
      }
    }
  }

  return {
    start() {
      sendLines([statusMsg()])
      timers.push(setInterval(loop, BATCH_MS))
    },
    stop() {
      for (const t of timers) clearInterval(t)
    },
    onMessage,
  }
}
