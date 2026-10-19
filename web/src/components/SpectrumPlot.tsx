import type { SpectrumMessage } from '../mts/types.ts'

type Props = {
  spectrum: SpectrumMessage | null
}

const COLORS = ['#7dd3fc', '#f9a8d4', '#86efac']

function bars(values: number[], max: number, color: string) {
  return values.map((v, i) => {
    const h = max > 0 ? Math.max(1, (v / max) * 100) : 1
    return (
      <span
        key={i}
        className="spec-bar"
        style={{ height: `${h}%`, background: color }}
      />
    )
  })
}

export function SpectrumPlot({ spectrum }: Props) {
  if (!spectrum) {
    return <p className="muted">No spectrum yet</p>
  }
  const max = Math.max(
    0.001,
    ...spectrum.x_ms2,
    ...spectrum.y_ms2,
    ...spectrum.z_ms2,
  )
  const fMax = (spectrum.x_ms2.length - 1) * spectrum.bin_width_hz

  return (
    <div className="spectrum">
      <div className="spec-row">
        <span>X</span>
        <div className="spec-bars">{bars(spectrum.x_ms2, max, COLORS[0]!)}</div>
      </div>
      <div className="spec-row">
        <span>Y</span>
        <div className="spec-bars">{bars(spectrum.y_ms2, max, COLORS[1]!)}</div>
      </div>
      <div className="spec-row">
        <span>Z</span>
        <div className="spec-bars">{bars(spectrum.z_ms2, max, COLORS[2]!)}</div>
      </div>
      <p className="muted">
        0–{fMax.toFixed(1)} Hz · bin {spectrum.bin_width_hz.toFixed(2)} Hz
      </p>
    </div>
  )
}
