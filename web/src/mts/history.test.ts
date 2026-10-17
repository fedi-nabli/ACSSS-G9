import assert from 'node:assert/strict'
import { describe, it } from 'node:test'
import { FrameHistory } from './history.ts'
import type { FrameMessage } from './types.ts'

function frame(sequence: number): FrameMessage {
  return {
    type: 'frame',
    sequence,
    timestamp_ns: sequence * 10_000_000,
    gap_before: false,
    tremor_active: false,
    tremor_displacement_mm: [0, 0, 0],
    tremor_acceleration_ms2: [0, 0, 0],
    orientation_wxyz: [1, 0, 0, 0],
    tremor_frequency_hz: 0,
    tremor_amplitude_mm: 0,
    tremor_confidence: 0,
    pipeline_delay_ms: 0,
    counter_signal_mm: null,
    predictions: [],
  }
}

describe('FrameHistory', () => {
  it('sizes the buffer from sample_rate_hz in seconds', () => {
    const hist = new FrameHistory(1.5, 100)
    for (let i = 0; i < 200; i++) hist.push(frame(i))
    assert.equal(hist.length, 150)

    hist.setSampleRate(200)
    assert.equal(hist.length, 150)
    for (let i = 200; i < 500; i++) hist.push(frame(i))
    assert.equal(hist.length, 300)
    assert.equal(hist.latest()?.sequence, 499)
  })
})
