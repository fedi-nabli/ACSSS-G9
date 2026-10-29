import type { FrameMessage, Imu6, Vec3 } from "../mts/types";
import type { SignalPoint } from "./SignalGraph";

/* export function buildSeries(
  frames: FrameMessage[],
  axis: keyof Imu6,
  id: string,
  hz: number
): SignalPoint[] {
  const recent = frames.slice(-150);
  const last = recent.at(-1);

const forecast = last?.predictions
  ?.find(p => p.predictor_id === id)
  ?.forecast_imu?.slice(0, 5) ?? [];

  // historical measurements
  const history = recent.map((f, i) => {
    const value = f.imu?.[axis] ?? null;
    return {
      time: f.timestamp_ns / 1e9,
      input: value,
      prediction: i === recent.length - 1 && forecast.length ? value : null,
      // counter: value === null ? null : -value,
      counter: f.counter_signal?.[axis] === null ? null : f.counter_signal[axis],
      result: value === null ? null : value - value
    };
  });

  if (!last || hz <= 0) return history;

  // future predictions
  const future = forecast.map((f, i) => ({
    time: last.timestamp_ns / 1e9 + (i + 1) / hz,
    input: null,
    prediction: f[axis],
    counter: null,
    result: null
  }));

  return [...history, ...future];
} */

// Which tremor signal each panel key shows: the ax/ay/az panels show translation (mm),
// the gx/gy/gz panels show rotation (mrad), on the same axis
const AXIS: Record<keyof Imu6, { rotation: boolean; i: 0 | 1 | 2 }> = {
  ax_g: { rotation: false, i: 0 },
  ay_g: { rotation: false, i: 1 },
  az_g: { rotation: false, i: 2 },
  gx_rad_s: { rotation: true, i: 0 },
  gy_rad_s: { rotation: true, i: 1 },
  gz_rad_s: { rotation: true, i: 2 },
};

const at = (v: Vec3 | null | undefined, i: number): number | null => v?.[i] ?? null;

export function buildSeries(
  frames: FrameMessage[],
  axis: keyof Imu6,
  id: string,
  _hz: number // kept so the callers stay as they are; the protocol has no future points
): SignalPoint[] {
  const { rotation, i } = AXIS[axis];

  return frames.slice(-150).map((f) => {
    const p = f.predictions.find((p) => p.predictor_id === id);
    return {
      time: f.timestamp_ns / 1e9,
      // the tremor on this axis: what has to be cancelled
      input: rotation ? f.tremor_rotation_mrad[i] : f.tremor_displacement_mm[i],
      // what this predictor said, horizon ago, this instant would be: on top of input when it predicts well
      prediction: at(rotation ? p?.forecast_rotation_mrad : p?.forecast_mm, i),
      // what the actuator would do: minus the active predictor's forecast (null while the gate is closed)
      counter: at(rotation ? f.counter_rotation_mrad : f.counter_signal_mm, i),
      // what is left: input + counter, computed by the server (null while the gate is closed)
      result: (() => {
        const input = rotation ? f.tremor_rotation_mrad[i] : f.tremor_displacement_mm[i];
        const counter = at(rotation ? f.counter_rotation_mrad : f.counter_signal_mm, i);
        return counter === null ? null : input + counter;
      })(),
    };
  });
}

