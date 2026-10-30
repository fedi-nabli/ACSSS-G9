
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  ResponsiveContainer,
  ReferenceLine
} from "recharts";

export type SignalPoint = {
  time: number;
  input: number | null;
  prediction: number | null;
  counter: number | null;
  result: number | null;
};

type SignalGraphProps = {
  title: string;
  data: SignalPoint[];
};

export default function SignalGraph({ title, data }: SignalGraphProps) {
  const latest = data.filter(p => p.input !== null).at(-1);

  const fmt = (n: number | null | undefined) => n?.toFixed(3) ?? "—";

  return (
    <div className="bg-gray-900 rounded-xl p-4">
      <h2 className="text-white font-semibold mb-2">
        {title}
      </h2>

      <div style={{ width: "100%", height: 220, minWidth: 0 }}>
        <ResponsiveContainer width="100%" height="100%">
          <LineChart data={data}>
            <CartesianGrid
              strokeDasharray="3 3"
              strokeOpacity={0.15}
            />

            <XAxis
              dataKey="time"
              tick={false}
              axisLine={false}
              tickLine={false}
            />

            <YAxis
              domain={["auto", "auto"]}
              tick={{ fill: "#888" }}
              axisLine={false}
              tickLine={false}
            />

            <ReferenceLine y={0} strokeOpacity={0.3} />

            <Line
              type="linear"
              dataKey="input"
              stroke="#0400ff"
              strokeWidth={2}
              dot={false}
              isAnimationActive={false}
            />

            <Line
              type="linear"
              dataKey="counter"
              stroke="#ef4444"
              strokeWidth={2}
              dot={false}
              isAnimationActive={false}
            />

            <Line
              type="linear"
              dataKey="result"
              stroke="#22c55e"
              strokeWidth={2}
              dot={false}
              isAnimationActive={false}
            />

            <Line
              type="linear"
              dataKey="prediction"
              stroke="#facc15"
              strokeWidth={2}
              strokeDasharray="5 5"
              dot={false}
              isAnimationActive={false}
            />
          </LineChart>
        </ResponsiveContainer>
      </div>

      <div className="grid grid-cols-4 justify-between mt-3 text-sm">
        <div className="input-blue">
          <p>Input</p>
          <p style={{ fontFamily: "var(--mono)" }}>
            {fmt(latest?.input)}
          </p>
        </div>

        <div className="input-red">
          <p>Counter</p>
          <p style={{ fontFamily: "var(--mono)" }}>
            {fmt(latest?.counter)}
          </p>
        </div>
        <div className="input-green">
          <p>Resultant</p>
          <p style={{ fontFamily: "var(--mono)" }}>
            {fmt(latest?.result)}
          </p>
        </div>
      </div>
    </div>
  );
}
