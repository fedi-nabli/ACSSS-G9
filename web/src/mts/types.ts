/** Protocol types for MTS UI contract v1 (`docs/ui_contract.md`). Extra JSON fields are ignored. */

export const PROTOCOL_VERSION = 1

export type Vec3 = readonly [number, number, number]
export type QuatWxyz = readonly [w: number, x: number, y: number, z: number]
export type QuatXyzw = readonly [x: number, y: number, z: number, w: number]

export type ServerState =
  | 'boot'
  | 'needs_calibration'
  | 'ready'
  | 'calibrating'
  | 'running'
  | 'stopped'

export type CalibrationStep = 'quick_zero' | 'hold_still' | 'rotate'

export type CalibrationProgress = {
  step: string
  progress: number
}

export type DropCounters = {
  hat_to_processing: number
  processing_to_prediction: number
  processing_to_server: number
}

export type Prediction = {
  predictor_id: string
  forecast_mm: Vec3 | null
  residual_mm: Vec3 | null
}

export type FrameMessage = {
  type: 'frame'
  sequence: number
  timestamp_ns: number
  gap_before: boolean
  tremor_active: boolean
  tremor_displacement_mm: Vec3
  tremor_acceleration_ms2: Vec3
  orientation_wxyz: QuatWxyz
  tremor_frequency_hz: number
  tremor_amplitude_mm: number
  tremor_confidence: number
  pipeline_delay_ms: number
  counter_signal_mm: Vec3 | null
  predictions: Prediction[]
}

export type MetricsMessage = {
  type: 'metrics'
  predictor_id: string
  timestamp_ns: number
  suppression_ratio: number
  residual_rms_mm: number
  window_ms: number
}

export type SpectrumMessage = {
  type: 'spectrum'
  timestamp_ns: number
  bin_width_hz: number
  x_ms2: number[]
  y_ms2: number[]
  z_ms2: number[]
}

export type StatusMessage = {
  type: 'status'
  protocol_version: number
  state: ServerState
  sample_rate_hz: number
  horizon_ms: number
  calibration: CalibrationProgress | null
  calibration_age_s: number | null
  enabled_predictor_ids: string[]
  active_predictor_id: string
  metrics_hz: number
  spectrum_hz: number
  drops: DropCounters
}

export type AckMessage = {
  type: 'ack'
  id: number
}

export type ErrorMessage = {
  type: 'error'
  id?: number
  code: string
  msg: string
}

export type ServerMessage =
  | FrameMessage
  | MetricsMessage
  | SpectrumMessage
  | StatusMessage
  | AckMessage
  | ErrorMessage

export type CommandName = 'start' | 'stop' | 'calibrate' | 'setup'

export type SetupFields = {
  enabled_predictor_ids?: string[]
  active_predictor_id?: string
  horizon_ms?: number
  metrics_hz?: number
  spectrum_hz?: number
}

export type CommandMessage = {
  type: 'command'
  id: number
  command: CommandName
} & SetupFields

export type ConnectionState = 'idle' | 'connecting' | 'open' | 'closing' | 'closed'

export class MtsCommandError extends Error {
  readonly code: string
  readonly id: number | undefined

  constructor(code: string, msg: string, id?: number) {
    super(msg)
    this.name = 'MtsCommandError'
    this.code = code
    this.id = id
  }
}
