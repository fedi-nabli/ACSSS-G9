import { useEffect, useRef } from 'react'
import type { FrameMessage, Vec3 } from '../mts/types.ts'

type Props = {
  frames: readonly FrameMessage[]
  tick: number
}

const COLORS = ['#7dd3fc', '#f9a8d4', '#86efac']
const LABELS = ['x', 'y', 'z']

export function DisplacementPlot({ frames, tick }: Props) {
  const canvasRef = useRef<HTMLCanvasElement>(null)

  useEffect(() => {
    const canvas = canvasRef.current
    if (!canvas) return
    const ctx = canvas.getContext('2d')
    if (!ctx) return

    const dpr = window.devicePixelRatio || 1
    const width = canvas.clientWidth
    const height = canvas.clientHeight
    canvas.width = Math.max(1, Math.floor(width * dpr))
    canvas.height = Math.max(1, Math.floor(height * dpr))
    ctx.setTransform(dpr, 0, 0, dpr, 0, 0)
    ctx.clearRect(0, 0, width, height)

    ctx.fillStyle = '#12141c'
    ctx.fillRect(0, 0, width, height)

    if (frames.length < 2) {
      ctx.fillStyle = '#6b7280'
      ctx.font = '13px ui-monospace, monospace'
      ctx.fillText('Waiting for frames…', 12, height / 2)
      return
    }

    let maxAbs = 0.05
    for (const f of frames) {
      for (let i = 0; i < 3; i++) {
        const v = Math.abs(f.tremor_displacement_mm[i] as number)
        if (v > maxAbs) maxAbs = v
        const c = f.counter_signal_mm
        if (c) {
          const cv = Math.abs(c[i] as number)
          if (cv > maxAbs) maxAbs = cv
        }
      }
    }

    const mid = height / 2
    const amp = height * 0.4
    const n = frames.length - 1

    ctx.strokeStyle = '#2e303a'
    ctx.beginPath()
    ctx.moveTo(0, mid)
    ctx.lineTo(width, mid)
    ctx.stroke()

    const drawSeries = (color: string, pick: (f: FrameMessage) => Vec3 | null, axis: number, dashed = false) => {
      ctx.strokeStyle = color
      ctx.lineWidth = dashed ? 1 : 1.5
      ctx.setLineDash(dashed ? [4, 4] : [])
      let drawing = false
      ctx.beginPath()
      for (let i = 0; i < frames.length; i++) {
        const f = frames[i]!
        const vec = pick(f)
        if (!vec) {
          drawing = false
          continue
        }
        if (f.gap_before) drawing = false
        const x = (i / n) * width
        const y = mid - ((vec[axis] as number) / maxAbs) * amp
        if (!drawing) {
          ctx.moveTo(x, y)
          drawing = true
        } else {
          ctx.lineTo(x, y)
        }
      }
      ctx.stroke()
    }

    for (let axis = 0; axis < 3; axis++) {
      drawSeries(COLORS[axis]!, (f) => f.tremor_displacement_mm, axis)
      drawSeries(COLORS[axis]!, (f) => f.counter_signal_mm, axis, true)
    }

    ctx.setLineDash([])
    ctx.font = '12px ui-monospace, monospace'
    LABELS.forEach((label, i) => {
      ctx.fillStyle = COLORS[i]!
      ctx.fillText(label, 10 + i * 28, 16)
    })
    ctx.fillStyle = '#9ca3af'
    ctx.fillText('solid: displacement   dashed: counter', 100, 16)
    ctx.fillText(`±${maxAbs.toFixed(2)} mm`, width - 92, 16)
  }, [frames, tick])

  return <canvas ref={canvasRef} className="plot" />
}
