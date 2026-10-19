import type { FrameMessage } from './types.ts'

/** Ring-like history sized in seconds from `sample_rate_hz` (contract §7). */
export class FrameHistory {
  readonly durationSeconds: number
  private items: FrameMessage[] = []
  private capacity: number
  private sampleRateHz: number

  constructor(durationSeconds = 1.5, sampleRateHz = 100) {
    this.durationSeconds = durationSeconds
    this.sampleRateHz = sampleRateHz
    this.capacity = Math.max(1, Math.round(durationSeconds * sampleRateHz))
  }

  setSampleRate(hz: number): void {
    if (!Number.isFinite(hz) || hz <= 0) return
    this.sampleRateHz = hz
    this.capacity = Math.max(1, Math.round(this.durationSeconds * hz))
    if (this.items.length > this.capacity) {
      this.items = this.items.slice(this.items.length - this.capacity)
    }
  }

  get sampleRate(): number {
    return this.sampleRateHz
  }

  push(frame: FrameMessage): void {
    this.items.push(frame)
    if (this.items.length > this.capacity) {
      this.items.splice(0, this.items.length - this.capacity)
    }
  }

  clear(): void {
    this.items = []
  }

  get length(): number {
    return this.items.length
  }

  /** Oldest → newest. Do not mutate. */
  snapshot(): readonly FrameMessage[] {
    return this.items
  }

  latest(): FrameMessage | undefined {
    return this.items.at(-1)
  }
}
