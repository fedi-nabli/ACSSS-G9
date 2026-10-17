import assert from 'node:assert/strict'
import { describe, it } from 'node:test'
import { decodeServerMessage, NdjsonBuffer } from './parse.ts'
import { PROTOCOL_VERSION, type FrameMessage, type StatusMessage } from './types.ts'

const EXAMPLE = `
{"type":"status","protocol_version":1,"state":"needs_calibration","sample_rate_hz":100,"horizon_ms":30,"calibration":null,"calibration_age_s":null,"enabled_predictor_ids":["inversion","wflc"],"active_predictor_id":"wflc","metrics_hz":2,"spectrum_hz":5,"drops":{"hat_to_processing":0,"processing_to_prediction":0,"processing_to_server":0}}
{"type":"error","id":1,"code":"needs_calibration","msg":"No valid calibration; run calibrate first."}
{"type":"ack","id":2}
{"type":"status","protocol_version":1,"state":"calibrating","sample_rate_hz":100,"horizon_ms":30,"calibration":{"step":"hold_still","progress":0.4},"calibration_age_s":null,"enabled_predictor_ids":["inversion","wflc"],"active_predictor_id":"wflc","metrics_hz":2,"spectrum_hz":5,"drops":{"hat_to_processing":0,"processing_to_prediction":0,"processing_to_server":0}}
{"type":"frame","sequence":4200,"timestamp_ns":42000000000,"gap_before":false,"tremor_active":false,"tremor_displacement_mm":[0.02,-0.01,0.00],"tremor_acceleration_ms2":[0.03,-0.01,0.01],"orientation_wxyz":[0.998,0.03,-0.05,0.01],"tremor_frequency_hz":5.2,"tremor_amplitude_mm":0.03,"tremor_confidence":0.12,"pipeline_delay_ms":42,"counter_signal_mm":null,"predictions":[{"predictor_id":"inversion","forecast_mm":[0.01,0.00,0.00],"residual_mm":null},{"predictor_id":"wflc","forecast_mm":[0.02,-0.01,0.00],"residual_mm":null}]}
{"type":"frame","sequence":4201,"timestamp_ns":42010000000,"gap_before":false,"tremor_active":true,"tremor_displacement_mm":[0.61,-0.10,0.07],"tremor_acceleration_ms2":[0.87,-0.14,0.10],"orientation_wxyz":[0.998,0.03,-0.05,0.01],"tremor_frequency_hz":6.1,"tremor_amplitude_mm":0.8,"tremor_confidence":0.93,"pipeline_delay_ms":42,"counter_signal_mm":[-0.55,0.12,-0.08],"predictions":[{"predictor_id":"inversion","forecast_mm":[0.30,-0.05,0.04],"residual_mm":[0.31,-0.05,0.03]},{"predictor_id":"wflc","forecast_mm":[0.55,-0.12,0.08],"residual_mm":[0.06,0.02,-0.01]}]}
{"type":"frame","sequence":4205,"timestamp_ns":42050000000,"gap_before":true,"tremor_displacement_mm":[-0.42,0.08,-0.05],"tremor_acceleration_ms2":[-0.60,0.11,-0.07],"orientation_wxyz":[0.998,0.03,-0.05,0.01],"tremor_frequency_hz":6.1,"tremor_amplitude_mm":0.8,"tremor_confidence":0.93,"pipeline_delay_ms":42,"counter_signal_mm":[0.38,-0.07,0.05],"tremor_active":true,"predictions":[{"predictor_id":"inversion","forecast_mm":[-0.20,0.03,-0.02],"residual_mm":[-0.22,0.05,-0.03]},{"predictor_id":"wflc","forecast_mm":[-0.38,0.07,-0.05],"residual_mm":[-0.04,0.01,0.00]}]}
{"type":"metrics","predictor_id":"wflc","timestamp_ns":42500000000,"suppression_ratio":0.18,"residual_rms_mm":0.09,"window_ms":2000}
{"type":"spectrum","timestamp_ns":42500000000,"bin_width_hz":0.39,"x_ms2":[0.01,0.02,0.01,0.40,0.85],"y_ms2":[0.00,0.01,0.01,0.06,0.14],"z_ms2":[0.00,0.01,0.00,0.04,0.09]}
`.trim()

describe('NdjsonBuffer', () => {
  it('splits several JSON lines in one WebSocket payload', () => {
    const buf = new NdjsonBuffer()
    const lines = buf.push('{"type":"ack","id":1}\n{"type":"ack","id":2}\n', {
      endOfMessage: true,
    })
    assert.equal(lines.length, 2)
    assert.deepEqual(decodeServerMessage(JSON.parse(lines[0]!)), { type: 'ack', id: 1 })
    assert.deepEqual(decodeServerMessage(JSON.parse(lines[1]!)), { type: 'ack', id: 2 })
  })

  it('keeps a split line until the next chunk', () => {
    const buf = new NdjsonBuffer()
    assert.deepEqual(buf.push('{"type":"ack",'), [])
    assert.deepEqual(buf.push('"id":7}\n'), ['{"type":"ack","id":7}'])
  })

  it('emits a complete object that has no trailing newline at message end', () => {
    const buf = new NdjsonBuffer()
    assert.deepEqual(buf.push('{"type":"ack","id":3}', { endOfMessage: true }), [
      '{"type":"ack","id":3}',
    ])
  })

  it('accepts CRLF', () => {
    const buf = new NdjsonBuffer()
    assert.deepEqual(buf.push('{"type":"ack","id":1}\r\n', { endOfMessage: true }), [
      '{"type":"ack","id":1}',
    ])
  })
})

describe('decodeServerMessage', () => {
  it('parses every line in the contract example stream', () => {
    const types: string[] = []
    for (const line of EXAMPLE.split('\n')) {
      const msg = decodeServerMessage(JSON.parse(line))
      assert.ok(msg, line)
      types.push(msg!.type)
    }
    assert.deepEqual(types, [
      'status',
      'error',
      'ack',
      'status',
      'frame',
      'frame',
      'frame',
      'metrics',
      'spectrum',
    ])
  })

  it('ignores unknown fields and unknown message types', () => {
    const status = decodeServerMessage({
      type: 'status',
      protocol_version: PROTOCOL_VERSION,
      state: 'ready',
      sample_rate_hz: 200,
      horizon_ms: 30,
      calibration: null,
      calibration_age_s: 1.5,
      enabled_predictor_ids: ['wflc'],
      active_predictor_id: 'wflc',
      metrics_hz: 2,
      spectrum_hz: 5,
      drops: {
        hat_to_processing: 1,
        processing_to_prediction: 0,
        processing_to_server: 2,
      },
      extra_future_field: true,
    }) as StatusMessage
    assert.equal(status.state, 'ready')
    assert.equal(status.sample_rate_hz, 200)
    assert.equal(status.drops.hat_to_processing, 1)
    assert.equal('extra_future_field' in status, false)
    assert.equal(decodeServerMessage({ type: 'future_kind', foo: 1 }), undefined)
  })

  it('keeps forecast_mm null during warm-up', () => {
    const frame = decodeServerMessage({
      type: 'frame',
      sequence: 1,
      timestamp_ns: 10,
      gap_before: false,
      tremor_active: true,
      tremor_displacement_mm: [0, 0, 0],
      tremor_acceleration_ms2: [0, 0, 0],
      orientation_wxyz: [1, 0, 0, 0],
      tremor_frequency_hz: 6,
      tremor_amplitude_mm: 1,
      tremor_confidence: 1,
      pipeline_delay_ms: 40,
      counter_signal_mm: null,
      predictions: [{ predictor_id: 'wflc', forecast_mm: null, residual_mm: null }],
    }) as FrameMessage
    assert.equal(frame.predictions[0]?.forecast_mm, null)
  })

  it('rejects a known type with missing required fields', () => {
    assert.equal(decodeServerMessage({ type: 'ack' }), undefined)
    assert.equal(decodeServerMessage({ type: 'error', code: 'busy' }), undefined)
  })
})
