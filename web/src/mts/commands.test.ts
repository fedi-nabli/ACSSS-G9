import assert from 'node:assert/strict'
import { describe, it } from 'node:test'
import { commandDeniedReason, isCommandAllowed } from './commands.ts'
import type { CommandName, ServerState } from './types.ts'

const STATES: ServerState[] = [
  'boot',
  'needs_calibration',
  'ready',
  'calibrating',
  'running',
  'stopped',
]
const COMMANDS: CommandName[] = ['start', 'stop', 'calibrate', 'setup']

const ALLOWED: Record<ServerState, Partial<Record<CommandName, true | string>>> = {
  boot: {},
  needs_calibration: {
    start: 'needs_calibration',
    calibrate: true,
    setup: true,
  },
  ready: { start: true, calibrate: true, setup: true },
  calibrating: {},
  running: { stop: true, setup: true },
  stopped: { start: true, calibrate: true, setup: true },
}

describe('commandDeniedReason', () => {
  it('matches contract §5', () => {
    for (const state of STATES) {
      for (const command of COMMANDS) {
        const cell = ALLOWED[state][command]
        const reason = commandDeniedReason(state, command)
        const label = `${state} ${command}`
        if (cell === true) {
          assert.equal(reason, null, label)
          assert.equal(isCommandAllowed(state, command), true, label)
        } else if (typeof cell === 'string') {
          assert.equal(reason, cell, label)
        } else if (state === 'calibrating') {
          assert.equal(reason, 'busy', label)
        } else {
          assert.equal(reason, 'invalid_state', label)
        }
      }
    }
  })
})
