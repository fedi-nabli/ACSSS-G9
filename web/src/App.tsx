import { useMemo, useState } from 'react'
import { SpectrumPlot } from './components/SpectrumPlot.tsx'
import { useMtsClient } from './hooks/useMtsClient.ts'
import { isCommandAllowed } from './mts/commands.ts'
import {SignalGraph} from './signals/SignalGraph copy.tsx';
import type { CommandName, ServerState } from './mts/types.ts'
import './App.css'

function fmt(n: number | null | undefined, digits = 2): string {
  if (n === null || n === undefined || !Number.isFinite(n)) return '—'
  return n.toFixed(digits)
}

function commandAllowed(state: ServerState | undefined, command: CommandName): boolean {
  return state !== undefined && isCommandAllowed(state, command)
}

const MAX_POINTS = 150;

type Axis = "x" | "y" | "z" | "rx" | "ry" | "rz";

type PlotPoint = {
  time: number;
  input: number;
  counter: number;
  result: number;
};

function convertFrame(
  frame: unknown,
  axis: Axis,
  sampleRate: number
): PlotPoint | null {

  // Example protocol structure.
  // Replace raw and counter with the actual
  // field names from mts/types.ts.
  const data = frame as {
    sequence: number;
    raw?: Partial<Record<Axis, number>>;
    counter?: Partial<Record<Axis, number>>;
  };

  const input = data.raw?.[axis];
  const counter = data.counter?.[axis];

  if (
    !Number.isFinite(input) ||
    !Number.isFinite(counter)
  ) {
    return null;
  }

  return {
    time: data.sequence / sampleRate,
    input: input!,
    counter: counter!,
    result: input! + counter!
  };
}

function App() {
  const mts = useMtsClient()
  const [horizon, setHorizon] = useState('30')
  const [metricsHz, setMetricsHz] = useState('2')
  const [spectrumHz, setSpectrumHz] = useState('5')
  const connected = mts.connectionState === 'open'
  const state = mts.status?.state
  const tremorOn = mts.lastFrame?.tremor_active === true
  const tick = mts.lastFrame?.sequence ?? 0

  const enabled = mts.status?.enabled_predictor_ids ?? []
  const activeId = mts.status?.active_predictor_id ?? ''

  const signals = useMemo(() => {
  const frames = mts.frames.slice(-MAX_POINTS);
  const rate = mts.status?.sample_rate_hz || 200;

  const getData = (axis: Axis) =>
    frames.flatMap((frame) => {
      const point = convertFrame(frame, axis, rate);
      return point ? [point] : [];
    });

  return {
    posX: getData("x"),
    posY: getData("y"),
    posZ: getData("z"),
    tiltX: getData("rx"),
    tiltY: getData("ry"),
    tiltZ: getData("rz"),
  };

}, [
  mts.frames,
  mts.lastFrame?.sequence,
  mts.status?.sample_rate_hz
]);

  const dropTotal = useMemo(() => {
    const d = mts.status?.drops
    if (!d) return 0
    return d.hat_to_processing + d.processing_to_prediction + d.processing_to_server
  }, [mts.status])

  return (
    <div className="app">
      <header className="top">
        <div>
          <h1>MTS</h1>
          <p className="muted">Tremor suppression · protocol v1</p>
        </div>
        <form
          className="connect"
          onSubmit={(e) => {
            e.preventDefault()
            if (connected) mts.disconnect()
            else mts.connect()
          }}
        >
          <input
            value={mts.url}
            onChange={(e) => mts.setUrl(e.target.value)}
            placeholder="ws://&lt;pi&gt;:&lt;port&gt;"
            spellCheck={false}
            disabled={connected || mts.connectionState === 'connecting'}
          />
          <button type="submit">
            {connected ? 'Disconnect' : mts.connectionState === 'connecting' ? 'Connecting…' : 'Connect'}
          </button>
        </form>
      </header>

      <section className="status-row">
        <Badge
          label="socket"
          value={mts.connectionState}
          tone={connected ? 'ok' : mts.connectionState === 'connecting' ? 'warn' : 'idle'}
        />
        <Badge
          label="state"
          value={state ?? '—'}
          tone={state === 'running' ? 'ok' : state === 'calibrating' ? 'warn' : 'idle'}
        />
        <Badge label="rate" value={mts.status ? `${mts.status.sample_rate_hz} Hz` : '—'} />
        <Badge label="horizon" value={mts.status ? `${mts.status.horizon_ms} ms` : '—'} />
        <Badge
          label="tremor"
          value={mts.lastFrame ? (tremorOn ? 'active' : 'idle') : '—'}
          tone={tremorOn ? 'ok' : 'idle'}
        />
        <Badge label="drops" value={String(dropTotal)} tone={dropTotal > 0 ? 'warn' : 'idle'} />
      </section>

      {mts.status?.calibration && (
        <p className="cal">
          Calibrating <strong>{mts.status.calibration.step}</strong>
          <progress value={mts.status.calibration.progress} max={1} />
          {Math.round(mts.status.calibration.progress * 100)}%
        </p>
      )}

      <section className="controls">
        <button
          type="button"
          disabled={!connected || mts.busy !== null || !commandAllowed(state, 'start')}
          onClick={() => void mts.start()}
        >
          Start
        </button>
        <button
          type="button"
          disabled={!connected || mts.busy !== null || !commandAllowed(state, 'stop')}
          onClick={() => void mts.stop()}
        >
          Stop
        </button>
        <button
          type="button"
          disabled={!connected || mts.busy !== null || !commandAllowed(state, 'calibrate')}
          onClick={() => void mts.calibrate()}
        >
          Calibrate
        </button>
        {enabled.length > 0 && (
          <label className="inline">
            Active predictor
            <select
              value={activeId}
              disabled={!connected || !commandAllowed(state, 'setup')}
              onChange={(e) => {
                void mts.setup({ active_predictor_id: e.target.value })
              }}
            >
              {enabled.map((id) => (
                <option key={id} value={id}>
                  {id}
                </option>
              ))}
            </select>
          </label>
        )}
        <label className="inline">
          horizon_ms
          <input
            value={horizon}
            onChange={(e) => setHorizon(e.target.value)}
            inputMode="numeric"
          />
        </label>
        <label className="inline">
          metrics_hz
          <input
            value={metricsHz}
            onChange={(e) => setMetricsHz(e.target.value)}
            inputMode="numeric"
          />
        </label>
        <label className="inline">
          spectrum_hz
          <input
            value={spectrumHz}
            onChange={(e) => setSpectrumHz(e.target.value)}
            inputMode="numeric"
          />
        </label>
        <button
          type="button"
          disabled={!connected || mts.busy !== null || !commandAllowed(state, 'setup')}
          onClick={() => {
            void mts.setup({
              horizon_ms: Number(horizon),
              metrics_hz: Number(metricsHz),
              spectrum_hz: Number(spectrumHz),
            })
          }}
        >
          Apply setup
        </button>
      </section>

      {(mts.lastError || mts.notice) && (
        <div className="alerts">
          {mts.lastError && (
            <p className="err">
              <code>{mts.lastError.code}</code> {mts.lastError.msg}
            </p>
          )}
          {mts.notice && <p className="warn">{mts.notice}</p>}
        </div>
      )}

      <section className="grid">
        <article className="card wide">
          <section className="grid grid-cols-3 gap-4">

            <SignalGraph title="X Position" data={signals.posX} />
            <SignalGraph title="Y Position" data={signals.posY} />
            <SignalGraph title="Z Position" data={signals.posZ} />

            <SignalGraph title="X Tilt" data={signals.tiltX} />
            <SignalGraph title="Y Tilt" data={signals.tiltY} />
            <SignalGraph title="Z Tilt" data={signals.tiltZ} />

          </section>
          <dl className="stats">
            <div>
              <dt>seq</dt>
              <dd>{mts.lastFrame?.sequence ?? '—'}</dd>
            </div>
            <div>
              <dt>freq</dt>
              <dd className={tremorOn ? '' : 'dim'}>{fmt(mts.lastFrame?.tremor_frequency_hz)} Hz</dd>
            </div>
            <div>
              <dt>amp</dt>
              <dd className={tremorOn ? '' : 'dim'}>{fmt(mts.lastFrame?.tremor_amplitude_mm)} mm</dd>
            </div>
            <div>
              <dt>conf</dt>
              <dd className={tremorOn ? '' : 'dim'}>{fmt(mts.lastFrame?.tremor_confidence)}</dd>
            </div>
            <div>
              <dt>delay</dt>
              <dd>{fmt(mts.lastFrame?.pipeline_delay_ms, 0)} ms</dd>
            </div>
            <div>
              <dt>buffer</dt>
              <dd>{mts.frames.length} frames</dd>
            </div>
          </dl>
        </article>

        <article className="card">
          <h2>Metrics</h2>
          <table>
            <thead>
              <tr>
                <th>predictor</th>
                <th>suppression</th>
                <th>residual RMS</th>
              </tr>
            </thead>
            <tbody>
              {Object.keys(mts.metrics).length === 0 && (
                <tr>
                  <td colSpan={3} className="muted">
                    Waiting for metrics…
                  </td>
                </tr>
              )}
              {Object.values(mts.metrics).map((row) => (
                <tr key={row.predictor_id}>
                  <td>
                    {row.predictor_id}
                    {row.predictor_id === activeId ? ' •' : ''}
                  </td>
                  <td>{fmt(row.suppression_ratio)}</td>
                  <td>{fmt(row.residual_rms_mm)} mm</td>
                </tr>
              ))}
            </tbody>
          </table>
          <p className="muted">
            Ratio &lt; 1 is cancelling. Window{' '}
            {Object.values(mts.metrics)[0]
              ? `${Object.values(mts.metrics)[0]!.window_ms} ms`
              : '—'}
          </p>
        </article>

        <article className="card">
          <h2>Spectrum</h2>
          <SpectrumPlot spectrum={mts.spectrum} />
        </article>
      </section>
    </div>
  )
}

function Badge({
  label,
  value,
  tone = 'idle',
}: {
  label: string
  value: string
  tone?: 'idle' | 'ok' | 'warn'
}) {
  return (
    <div className={`badge ${tone}`}>
      <span>{label}</span>
      <strong>{value}</strong>
    </div>
  )
}

export default App
