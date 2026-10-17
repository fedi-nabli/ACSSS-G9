import { useCallback, useEffect, useRef, useState } from 'react'
import { MtsClient } from '../mts/client.ts'
import type {
  ConnectionState,
  ErrorMessage,
  FrameMessage,
  MetricsMessage,
  SetupFields,
  SpectrumMessage,
  StatusMessage,
} from '../mts/types.ts'
import { MtsCommandError } from '../mts/types.ts'

const URL_KEY = 'mts.wsUrl'
export const DEFAULT_WS_URL = 'ws://localhost:8765'

export type CommandBusy = 'start' | 'stop' | 'calibrate' | 'setup' | null

function loadUrl(): string {
  try {
    return localStorage.getItem(URL_KEY) ?? DEFAULT_WS_URL
  } catch {
    return DEFAULT_WS_URL
  }
}

export function useMtsClient() {
  const clientRef = useRef<MtsClient | null>(null)
  const [url, setUrlState] = useState(loadUrl)
  const [connectionState, setConnectionState] = useState<ConnectionState>('idle')
  const [status, setStatus] = useState<StatusMessage | null>(null)
  const [lastFrame, setLastFrame] = useState<FrameMessage | null>(null)
  const [frames, setFrames] = useState<readonly FrameMessage[]>([])
  const [metrics, setMetrics] = useState<Record<string, MetricsMessage>>({})
  const [spectrum, setSpectrum] = useState<SpectrumMessage | null>(null)
  const [lastError, setLastError] = useState<ErrorMessage | null>(null)
  const [notice, setNotice] = useState<string | null>(null)
  const [busy, setBusy] = useState<CommandBusy>(null)

  const setUrl = useCallback((next: string) => {
    setUrlState(next)
    try {
      localStorage.setItem(URL_KEY, next)
    } catch {
      /* ignore quota / private mode */
    }
  }, [])

  const detach = useCallback(() => {
    const client = clientRef.current
    if (!client) return
    client.disconnect()
    clientRef.current = null
  }, [])

  const connect = useCallback(() => {
    detach()
    setStatus(null)
    setLastFrame(null)
    setFrames([])
    setMetrics({})
    setSpectrum(null)
    setLastError(null)
    setNotice(null)
    setConnectionState('connecting')

    const client = new MtsClient({ url })
    clientRef.current = client

    client.on('open', () => setConnectionState('open'))
    client.on('close', () => setConnectionState('closed'))
    client.on('socketError', () => {
      setNotice('Socket error')
    })
    client.on('status', (msg) => setStatus(msg))
    client.on('metrics', (msg) => {
      setMetrics((prev) => ({ ...prev, [msg.predictor_id]: msg }))
    })
    client.on('spectrum', (msg) => setSpectrum(msg))
    client.on('serverError', (msg) => setLastError(msg))
    client.on('parseError', (err) => setNotice(`Parse error: ${err.message}`))
    client.on('protocolWarning', (msg) => setNotice(msg))

    client.connect()
  }, [detach, url])

  const disconnect = useCallback(() => {
    detach()
    setConnectionState('closed')
  }, [detach])

  useEffect(() => {
    return () => {
      clientRef.current?.disconnect()
      clientRef.current = null
    }
  }, [])

  useEffect(() => {
    let raf = 0
    let lastSeq = -1
    const loop = () => {
      raf = requestAnimationFrame(loop)
      const history = clientRef.current?.history
      if (!history) return
      const latest = history.latest()
      if (!latest || latest.sequence === lastSeq) return
      lastSeq = latest.sequence
      setLastFrame(latest)
      setFrames(history.snapshot().slice())
    }
    raf = requestAnimationFrame(loop)
    return () => cancelAnimationFrame(raf)
  }, [connectionState])

  const runCommand = useCallback(
    async (name: Exclude<CommandBusy, null>, fields?: SetupFields) => {
      const client = clientRef.current
      if (!client) throw new Error('Not connected')
      setBusy(name)
      setLastError(null)
      setNotice(null)
      try {
        await client.sendCommand(name, fields)
      } catch (err) {
        if (err instanceof MtsCommandError) {
          setLastError({ type: 'error', id: err.id, code: err.code, msg: err.message })
        } else {
          setNotice(err instanceof Error ? err.message : String(err))
        }
        throw err
      } finally {
        setBusy(null)
      }
    },
    [],
  )

  return {
    url,
    setUrl,
    connectionState,
    status,
    lastFrame,
    frames,
    metrics,
    spectrum,
    lastError,
    notice,
    busy,
    connect,
    disconnect,
    start: () => runCommand('start'),
    stop: () => runCommand('stop'),
    calibrate: () => runCommand('calibrate'),
    setup: (fields: SetupFields) => runCommand('setup', fields),
  }
}
