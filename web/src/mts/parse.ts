import type {
  AckMessage,
  DropCounters,
  ErrorMessage,
  FrameMessage,
  MetricsMessage,
  Prediction,
  ServerMessage,
  ServerState,
  SpectrumMessage,
  StatusMessage,
  Vec3,
} from './types.ts'

const SERVER_STATES = new Set<ServerState>([
  'boot',
  'needs_calibration',
  'ready',
  'calibrating',
  'running',
  'stopped',
])

export class NdjsonBuffer {
  private leftover = ''

  /**
   * Split on `\n` (contract §1). Incomplete last lines stay buffered.
   * Pass `endOfMessage: true` after a WebSocket frame so a complete JSON
   * object without a trailing newline is still delivered.
   */
  push(chunk: string, options?: { endOfMessage?: boolean }): string[] {
    const text = this.leftover + chunk.replace(/\r\n/g, '\n').replace(/\r/g, '\n')
    const parts = text.split('\n')
    this.leftover = parts.pop() ?? ''
    const lines: string[] = []
    for (const line of parts) {
      if (line.length > 0) lines.push(line)
    }
    if (options?.endOfMessage) {
      const flushed = this.flushCompleteLeftover()
      if (flushed) lines.push(flushed)
    }
    return lines
  }

  reset(): void {
    this.leftover = ''
  }

  private flushCompleteLeftover(): string | undefined {
    const candidate = this.leftover.trim()
    if (!candidate) {
      this.leftover = ''
      return undefined
    }
    try {
      JSON.parse(candidate)
      this.leftover = ''
      return candidate
    } catch {
      return undefined
    }
  }
}

export function decodeWsPayload(data: unknown): string {
  if (typeof data === 'string') return data
  if (data instanceof ArrayBuffer) return new TextDecoder().decode(data)
  if (ArrayBuffer.isView(data)) {
    return new TextDecoder().decode(data)
  }
  throw new Error('Unsupported WebSocket payload type')
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}

function asNumber(value: unknown): number | undefined {
  return typeof value === 'number' && Number.isFinite(value) ? value : undefined
}

function asBool(value: unknown): boolean | undefined {
  return typeof value === 'boolean' ? value : undefined
}

function asString(value: unknown): string | undefined {
  return typeof value === 'string' ? value : undefined
}

function asVec3(value: unknown): Vec3 | undefined {
  if (!Array.isArray(value) || value.length !== 3) return undefined
  const x = asNumber(value[0])
  const y = asNumber(value[1])
  const z = asNumber(value[2])
  if (x === undefined || y === undefined || z === undefined) return undefined
  return [x, y, z]
}

function asVec3OrNull(value: unknown): Vec3 | null | undefined {
  if (value === null) return null
  return asVec3(value)
}

function asNumberArray(value: unknown): number[] | undefined {
  if (!Array.isArray(value)) return undefined
  const out: number[] = []
  for (const item of value) {
    const n = asNumber(item)
    if (n === undefined) return undefined
    out.push(n)
  }
  return out
}

function asStringArray(value: unknown): string[] | undefined {
  if (!Array.isArray(value)) return undefined
  const out: string[] = []
  for (const item of value) {
    if (typeof item !== 'string') return undefined
    out.push(item)
  }
  return out
}

function asPrediction(value: unknown): Prediction | undefined {
  if (!isRecord(value)) return undefined
  const predictor_id = asString(value.predictor_id)
  const forecast_mm = asVec3OrNull(value.forecast_mm)
  const residual_mm = asVec3OrNull(value.residual_mm)
  if (
    predictor_id === undefined ||
    forecast_mm === undefined ||
    residual_mm === undefined
  ) {
    return undefined
  }
  return { predictor_id, forecast_mm, residual_mm }
}

function decodeFrame(raw: Record<string, unknown>): FrameMessage | undefined {
  const sequence = asNumber(raw.sequence)
  const timestamp_ns = asNumber(raw.timestamp_ns)
  const gap_before = asBool(raw.gap_before)
  const tremor_active = asBool(raw.tremor_active)
  const tremor_displacement_mm = asVec3(raw.tremor_displacement_mm)
  const tremor_acceleration_ms2 = asVec3(raw.tremor_acceleration_ms2)
  const orientation = asNumberArray(raw.orientation_wxyz)
  const tremor_frequency_hz = asNumber(raw.tremor_frequency_hz)
  const tremor_amplitude_mm = asNumber(raw.tremor_amplitude_mm)
  const tremor_confidence = asNumber(raw.tremor_confidence)
  const pipeline_delay_ms = asNumber(raw.pipeline_delay_ms)
  const counter_signal_mm = asVec3OrNull(raw.counter_signal_mm)
  if (
    sequence === undefined ||
    timestamp_ns === undefined ||
    gap_before === undefined ||
    tremor_active === undefined ||
    tremor_displacement_mm === undefined ||
    tremor_acceleration_ms2 === undefined ||
    orientation === undefined ||
    orientation.length !== 4 ||
    tremor_frequency_hz === undefined ||
    tremor_amplitude_mm === undefined ||
    tremor_confidence === undefined ||
    pipeline_delay_ms === undefined ||
    counter_signal_mm === undefined ||
    !Array.isArray(raw.predictions)
  ) {
    return undefined
  }

  const predictions: Prediction[] = []
  for (const entry of raw.predictions) {
    const parsed = asPrediction(entry)
    if (!parsed) return undefined
    predictions.push(parsed)
  }

  return {
    type: 'frame',
    sequence,
    timestamp_ns,
    gap_before,
    tremor_active,
    tremor_displacement_mm,
    tremor_acceleration_ms2,
    orientation_wxyz: [
      orientation[0]!,
      orientation[1]!,
      orientation[2]!,
      orientation[3]!,
    ],
    tremor_frequency_hz,
    tremor_amplitude_mm,
    tremor_confidence,
    pipeline_delay_ms,
    counter_signal_mm,
    predictions,
  }
}

function decodeMetrics(raw: Record<string, unknown>): MetricsMessage | undefined {
  const predictor_id = asString(raw.predictor_id)
  const timestamp_ns = asNumber(raw.timestamp_ns)
  const suppression_ratio = asNumber(raw.suppression_ratio)
  const residual_rms_mm = asNumber(raw.residual_rms_mm)
  const window_ms = asNumber(raw.window_ms)
  if (
    predictor_id === undefined ||
    timestamp_ns === undefined ||
    suppression_ratio === undefined ||
    residual_rms_mm === undefined ||
    window_ms === undefined
  ) {
    return undefined
  }
  return {
    type: 'metrics',
    predictor_id,
    timestamp_ns,
    suppression_ratio,
    residual_rms_mm,
    window_ms,
  }
}

function decodeSpectrum(raw: Record<string, unknown>): SpectrumMessage | undefined {
  const timestamp_ns = asNumber(raw.timestamp_ns)
  const bin_width_hz = asNumber(raw.bin_width_hz)
  const x_ms2 = asNumberArray(raw.x_ms2)
  const y_ms2 = asNumberArray(raw.y_ms2)
  const z_ms2 = asNumberArray(raw.z_ms2)
  if (
    timestamp_ns === undefined ||
    bin_width_hz === undefined ||
    x_ms2 === undefined ||
    y_ms2 === undefined ||
    z_ms2 === undefined
  ) {
    return undefined
  }
  return {
    type: 'spectrum',
    timestamp_ns,
    bin_width_hz,
    x_ms2,
    y_ms2,
    z_ms2,
  }
}

function decodeDrops(value: unknown): DropCounters {
  if (!isRecord(value)) {
    return {
      hat_to_processing: 0,
      processing_to_prediction: 0,
      processing_to_server: 0,
    }
  }
  return {
    hat_to_processing: asNumber(value.hat_to_processing) ?? 0,
    processing_to_prediction: asNumber(value.processing_to_prediction) ?? 0,
    processing_to_server: asNumber(value.processing_to_server) ?? 0,
  }
}

function decodeStatus(raw: Record<string, unknown>): StatusMessage | undefined {
  const protocol_version = asNumber(raw.protocol_version)
  const state = asString(raw.state)
  const sample_rate_hz = asNumber(raw.sample_rate_hz)
  const horizon_ms = asNumber(raw.horizon_ms)
  const enabled_predictor_ids = asStringArray(raw.enabled_predictor_ids)
  const active_predictor_id = asString(raw.active_predictor_id)
  const metrics_hz = asNumber(raw.metrics_hz)
  const spectrum_hz = asNumber(raw.spectrum_hz)
  if (
    protocol_version === undefined ||
    state === undefined ||
    !SERVER_STATES.has(state as ServerState) ||
    sample_rate_hz === undefined ||
    horizon_ms === undefined ||
    enabled_predictor_ids === undefined ||
    active_predictor_id === undefined ||
    metrics_hz === undefined ||
    spectrum_hz === undefined
  ) {
    return undefined
  }

  let calibration: StatusMessage['calibration'] = null
  if (raw.calibration !== null && raw.calibration !== undefined) {
    if (!isRecord(raw.calibration)) return undefined
    const step = asString(raw.calibration.step)
    const progress = asNumber(raw.calibration.progress)
    if (step === undefined || progress === undefined) return undefined
    calibration = { step, progress }
  }

  const age = raw.calibration_age_s
  const calibration_age_s = age === null ? null : asNumber(age)
  if (calibration_age_s === undefined) return undefined

  return {
    type: 'status',
    protocol_version,
    state: state as ServerState,
    sample_rate_hz,
    horizon_ms,
    calibration,
    calibration_age_s,
    enabled_predictor_ids,
    active_predictor_id,
    metrics_hz,
    spectrum_hz,
    drops: decodeDrops(raw.drops),
  }
}

function decodeAck(raw: Record<string, unknown>): AckMessage | undefined {
  const id = asNumber(raw.id)
  if (id === undefined) return undefined
  return { type: 'ack', id }
}

function decodeError(raw: Record<string, unknown>): ErrorMessage | undefined {
  const code = asString(raw.code)
  const msg = asString(raw.msg)
  if (code === undefined || msg === undefined) return undefined
  const id = raw.id === undefined ? undefined : asNumber(raw.id)
  if (raw.id !== undefined && id === undefined) return undefined
  return { type: 'error', id, code, msg }
}

/** Returns a typed message, or `undefined` for unknown `type` (ignore). */
export function decodeServerMessage(value: unknown): ServerMessage | undefined {
  if (!isRecord(value)) return undefined
  const type = asString(value.type)
  switch (type) {
    case 'frame':
      return decodeFrame(value)
    case 'metrics':
      return decodeMetrics(value)
    case 'spectrum':
      return decodeSpectrum(value)
    case 'status':
      return decodeStatus(value)
    case 'ack':
      return decodeAck(value)
    case 'error':
      return decodeError(value)
    default:
      return undefined
  }
}

export function parseJsonLine(line: string): unknown {
  return JSON.parse(line) as unknown
}
