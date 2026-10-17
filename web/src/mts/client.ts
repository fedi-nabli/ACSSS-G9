import { FrameHistory } from './history.ts'
import {
  decodeServerMessage,
  decodeWsPayload,
  NdjsonBuffer,
  parseJsonLine,
} from './parse.ts'
import {
  MtsCommandError,
  PROTOCOL_VERSION,
  type CommandName,
  type ConnectionState,
  type ErrorMessage,
  type FrameMessage,
  type MetricsMessage,
  type SetupFields,
  type SpectrumMessage,
  type StatusMessage,
} from './types.ts'

export type MtsClientEvents = {
  open: () => void
  close: (event: CloseEvent) => void
  socketError: () => void
  status: (msg: StatusMessage) => void
  frame: (msg: FrameMessage) => void
  metrics: (msg: MetricsMessage) => void
  spectrum: (msg: SpectrumMessage) => void
  serverError: (msg: ErrorMessage) => void
  parseError: (err: Error, line: string) => void
  protocolWarning: (msg: string) => void
}

type Handler<K extends keyof MtsClientEvents> = MtsClientEvents[K]

type Pending = {
  resolve: () => void
  reject: (err: Error) => void
  timer: ReturnType<typeof setTimeout>
}

export type MtsClientOptions = {
  url: string
  historySeconds?: number
  commandTimeoutMs?: number
}

export class MtsClient {
  readonly history: FrameHistory
  private readonly url: string
  private readonly commandTimeoutMs: number
  private readonly ndjson = new NdjsonBuffer()
  private readonly pending = new Map<number, Pending>()
  private readonly listeners: {
    [K in keyof MtsClientEvents]: Set<Handler<K>>
  } = {
    open: new Set(),
    close: new Set(),
    socketError: new Set(),
    status: new Set(),
    frame: new Set(),
    metrics: new Set(),
    spectrum: new Set(),
    serverError: new Set(),
    parseError: new Set(),
    protocolWarning: new Set(),
  }

  private socket: WebSocket | null = null
  private nextId = 1
  private _state: ConnectionState = 'idle'

  constructor(options: MtsClientOptions) {
    this.url = options.url
    this.commandTimeoutMs = options.commandTimeoutMs ?? 8000
    this.history = new FrameHistory(options.historySeconds ?? 1.5)
  }

  get connectionState(): ConnectionState {
    return this._state
  }

  on<K extends keyof MtsClientEvents>(event: K, handler: Handler<K>): () => void {
    this.listeners[event].add(handler)
    return () => {
      this.listeners[event].delete(handler)
    }
  }

  connect(): void {
    if (this.socket && (this._state === 'connecting' || this._state === 'open')) {
      return
    }
    this.ndjson.reset()
    this._state = 'connecting'
    const socket = new WebSocket(this.url)
    this.socket = socket
    socket.binaryType = 'arraybuffer'

    socket.onopen = () => {
      if (this.socket !== socket) return
      this._state = 'open'
      this.emit('open')
    }

    socket.onmessage = (event: MessageEvent<unknown>) => {
      if (this.socket !== socket) return
      try {
        const chunk = decodeWsPayload(event.data)
        for (const line of this.ndjson.push(chunk, { endOfMessage: true })) {
          this.handleLine(line)
        }
      } catch (err) {
        this.emit('parseError', err instanceof Error ? err : new Error(String(err)), '')
      }
    }

    socket.onerror = () => {
      if (this.socket !== socket) return
      this.emit('socketError')
    }

    socket.onclose = (event) => {
      if (this.socket !== socket) return
      this.socket = null
      this._state = 'closed'
      this.rejectAllPending(new Error('WebSocket closed'))
      this.emit('close', event)
    }
  }

  disconnect(): void {
    const socket = this.socket
    if (!socket) {
      this._state = 'closed'
      return
    }
    this._state = 'closing'
    socket.close()
  }

  sendCommand(command: CommandName, fields?: SetupFields): Promise<void> {
    if (!this.socket || this.socket.readyState !== WebSocket.OPEN) {
      return Promise.reject(new Error('Not connected'))
    }
    const id = this.nextId++
    const payload: Record<string, unknown> = {
      type: 'command',
      id,
      command,
    }
    if (command === 'setup' && fields) {
      if (fields.enabled_predictor_ids !== undefined) {
        payload.enabled_predictor_ids = fields.enabled_predictor_ids
      }
      if (fields.active_predictor_id !== undefined) {
        payload.active_predictor_id = fields.active_predictor_id
      }
      if (fields.horizon_ms !== undefined) payload.horizon_ms = fields.horizon_ms
      if (fields.metrics_hz !== undefined) payload.metrics_hz = fields.metrics_hz
      if (fields.spectrum_hz !== undefined) payload.spectrum_hz = fields.spectrum_hz
    }

    return new Promise<void>((resolve, reject) => {
      const timer = setTimeout(() => {
        this.pending.delete(id)
        reject(new Error(`Command ${command} (id ${id}) timed out`))
      }, this.commandTimeoutMs)
      this.pending.set(id, { resolve, reject, timer })
      this.socket?.send(`${JSON.stringify(payload)}\n`)
    })
  }

  private handleLine(line: string): void {
    let json: unknown
    try {
      json = parseJsonLine(line)
    } catch (err) {
      this.emit(
        'parseError',
        err instanceof Error ? err : new Error(String(err)),
        line,
      )
      return
    }

    const msg = decodeServerMessage(json)
    if (!msg) {
      const type = isRecord(json) ? json.type : undefined
      const known =
        type === 'frame' ||
        type === 'metrics' ||
        type === 'spectrum' ||
        type === 'status' ||
        type === 'ack' ||
        type === 'error'
      if (typeof type === 'string' && !known) return
      this.emit(
        'parseError',
        new Error(
          known
            ? `Invalid ${String(type)} message`
            : 'Line is not a valid protocol object',
        ),
        line,
      )
      return
    }

    switch (msg.type) {
      case 'frame':
        this.history.push(msg)
        this.emit('frame', msg)
        break
      case 'metrics':
        this.emit('metrics', msg)
        break
      case 'spectrum':
        this.emit('spectrum', msg)
        break
      case 'status':
        this.history.setSampleRate(msg.sample_rate_hz)
        if (msg.protocol_version !== PROTOCOL_VERSION) {
          this.emit(
            'protocolWarning',
            `Server protocol_version ${msg.protocol_version} (UI expects ${PROTOCOL_VERSION})`,
          )
        }
        this.emit('status', msg)
        break
      case 'ack':
        this.settle(msg.id, true)
        break
      case 'error':
        this.emit('serverError', msg)
        if (msg.id !== undefined) {
          this.settle(
            msg.id,
            false,
            new MtsCommandError(msg.code, msg.msg, msg.id),
          )
        }
        break
    }
  }

  private settle(id: number, ok: boolean, err?: Error): void {
    const pending = this.pending.get(id)
    if (!pending) return
    this.pending.delete(id)
    clearTimeout(pending.timer)
    if (ok) pending.resolve()
    else pending.reject(err ?? new Error('Command failed'))
  }

  private rejectAllPending(err: Error): void {
    for (const [id, pending] of this.pending) {
      clearTimeout(pending.timer)
      pending.reject(err)
      this.pending.delete(id)
    }
  }

  private emit<K extends keyof MtsClientEvents>(
    event: K,
    ...args: Parameters<MtsClientEvents[K]>
  ): void {
    for (const handler of this.listeners[event]) {
      ;(handler as (...a: Parameters<MtsClientEvents[K]>) => void)(...args)
    }
  }
}

function isRecord(value: unknown): value is Record<string, unknown> {
  return typeof value === 'object' && value !== null && !Array.isArray(value)
}
