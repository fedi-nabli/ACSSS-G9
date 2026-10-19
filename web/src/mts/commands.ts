import type { CommandName, ServerState } from './types.ts'

/**
 * Contract §5. Returns the error code the server would send, or null if the
 * command is valid in this state.
 */
export function commandDeniedReason(
  state: ServerState,
  command: CommandName,
): string | null {
  if (state === 'calibrating') return 'busy'
  if (state === 'boot') return 'invalid_state'

  switch (command) {
    case 'start':
      if (state === 'needs_calibration') return 'needs_calibration'
      if (state === 'ready' || state === 'stopped') return null
      return 'invalid_state'
    case 'stop':
      return state === 'running' ? null : 'invalid_state'
    case 'calibrate':
      if (state === 'needs_calibration' || state === 'ready' || state === 'stopped') {
        return null
      }
      return 'invalid_state'
    case 'setup':
      if (
        state === 'needs_calibration' ||
        state === 'ready' ||
        state === 'running' ||
        state === 'stopped'
      ) {
        return null
      }
      return 'invalid_state'
  }
}

export function isCommandAllowed(state: ServerState, command: CommandName): boolean {
  return commandDeniedReason(state, command) === null
}
