import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  CartesianGrid,
  ResponsiveContainer,
  ReferenceLine
} from "recharts";

export default function SignalGraph({ title, data }) {
  const latest = data.at(-1) ?? {
  input: 0,
  prediction: null,
  counter: 0,
  result: 0
};

  return (
    <div className="bg-gray-900 rounded-xl p-4">

      <h2 className="text-white font-semibold mb-2">
        {title}
      </h2>

      <div className="h-52">
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

            <ReferenceLine
              y={0}
              strokeOpacity={0.3}
            />

            {/* Input */}
            <Line
              type="linear"
              dataKey="input"
              stroke="#0400ff"
              strokeWidth={2}
              dot={false}
              isAnimationActive={false}
            />

            {/* Counter */}
            <Line
              type="linear"
              dataKey="counter"
              stroke="#ef4444"
              strokeWidth={2}
              dot={false}
              isAnimationActive={false}
            />

            {/* Result */}
            <Line
              type="linear"
              dataKey="result"
              stroke="#22c55e"
              strokeWidth={2}
              dot={false}
              isAnimationActive={false}
            />

            {/* MLP prediction */}
            <Line
              type="linear"
              dataKey="prediction"
              stroke="#facc15"
              strokeWidth={2}
              strokeDasharray="5 5"
              dot={false}
              connectNulls={false}
              isAnimationActive={false}
            />

          </LineChart>
        </ResponsiveContainer>
      </div>

      <div className="flex justify-between mt-3 text-sm">

        <div>
          <p className="text-gray-400">Input</p>
          <p className="text-blue-500 font-mono">
            {latest.input.toFixed(3)}
          </p>
        </div>

        <div>
          <p className="text-gray-400">Counter</p>
          <p className="text-red-500 font-mono">
            {latest.counter.toFixed(3)}
          </p>
        </div>

        <div>
          <p className="text-gray-400">Result</p>
          <p className="text-green-500 font-mono">
            {latest.result.toFixed(3)}
          </p>
        </div>

      </div>

    </div>
  );
}