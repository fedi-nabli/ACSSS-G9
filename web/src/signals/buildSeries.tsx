import type { FrameMessage, Imu6 } from "../mts/types";
import type { SignalPoint } from "./SignalGraph";

export function buildSeries(
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
      counter: value === null ? null : -value,
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
}