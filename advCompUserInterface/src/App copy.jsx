
import { useEffect, useState } from "react";
import SignalGraph from "./signals/SignalGraph";
import {
  loadTremorModel,
  predictTremor
} from "./services/tremorPrediction";

const MAX_POINTS = 150;
const SAMPLE_MS = 50;

const CHANNELS = [
  "posX", "posY", "posZ",
  "tiltX", "tiltY", "tiltZ"
];

const AMPLITUDES = [
  0.1, 0.15, 0.07,
  0.2, 0.12, 0.18
];

function App() {
  const [signals, setSignals] = useState(
    Object.fromEntries(CHANNELS.map(key => [key, []]))
  );

  const [status, setStatus] = useState("Loading ONNX model...");

  useEffect(() => {
    let active = true;
    let interval;
    let sampleIndex = 0;
    let inferenceRunning = false;
    let firstPrediction = true;

    // Current simulated sensor values
    let previous = Array(6).fill(0);

    // Last N measurements for the MLP
    const history = [];

    // Predictions scheduled for future sample indices
    const scheduled = new Map();

    async function start() {
      try {
        const { config } = await loadTremorModel();

        if (!active) return;

        const WINDOW = config.window;
        const FUTURE = config.future;

        setStatus(`Collecting ${WINDOW} measurements...`);

        function tick() {
          const index = sampleIndex++;
          const time = index * SAMPLE_MS / 1000;

          // Simulate six sensor measurements
          const input = previous.map((value, i) => {
            const change =
              (Math.random() * 2 - 1) * AMPLITUDES[i];

            return value * 0.98 + change;
          });

          previous = input;

          // Update rolling input buffer
          history.push(input);

          if (history.length > WINDOW) {
            history.shift();
          }

          // Get the prediction made for this time step
          const forecast = scheduled.get(index);
          scheduled.delete(index);

          // Demo counter-signal based on prediction
          const counter = forecast
            ? forecast.map(value => -value)
            : Array(6).fill(0);

          const result = input.map(
            (value, i) => value + counter[i]
          );

          // Update graphs
          setSignals(old => {
            const next = { ...old };

            CHANNELS.forEach((key, i) => {
              next[key] = [
                ...old[key],
                {
                  time,
                  input: input[i],
                  prediction: forecast?.[i] ?? null,
                  counter: counter[i],
                  result: result[i]
                }
              ].slice(-MAX_POINTS);
            });

            return next;
          });

          // Predict once the input buffer is full
          if (
            history.length === WINDOW &&
            !inferenceRunning
          ) {
            inferenceRunning = true;

            const snapshot = history.map(row => [...row]);

            // Fifth prediction belongs five samples ahead
            const targetIndex = index + FUTURE;

            predictTremor(snapshot)
              .then(predictions => {
                if (!active) return;

                // Discard forecasts that arrived too late
                if (targetIndex >= sampleIndex) {
                  scheduled.set(
                    targetIndex,
                    predictions[FUTURE - 1]
                  );

                  if (firstPrediction) {
                    setStatus("MLP predictions active");
                    firstPrediction = false;
                  }
                }
              })
              .catch(error => {
                if (active) {
                  setStatus(`Prediction error: ${error.message}`);
                }
              })
              .finally(() => {
                inferenceRunning = false;
              });
          }
        }

        interval = setInterval(tick, SAMPLE_MS);

      } catch (error) {
        if (active) {
          setStatus(`Model error: ${error.message}`);
        }
      }
    }

    start();

    return () => {
      active = false;
      clearInterval(interval);
    };
  }, []);

  return (
    <div className="min-h-screen bg-gray-950 p-6">

      <h1 className="text-white text-2xl font-bold mb-2">
        Tremor Suppression
      </h1>

      <p className="text-gray-400 mb-6">
        {status}
      </p>

      <div className="grid grid-cols-3 gap-4">

        <SignalGraph
          title="X Acceleration"
          data={signals.posX}
        />

        <SignalGraph
          title="Y Acceleration"
          data={signals.posY}
        />

        <SignalGraph
          title="Z Acceleration"
          data={signals.posZ}
        />

        <SignalGraph
          title="X Angular Velocity"
          data={signals.tiltX}
        />

        <SignalGraph
          title="Y Angular Velocity"
          data={signals.tiltY}
        />

        <SignalGraph
          title="Z Angular Velocity"
          data={signals.tiltZ}
        />

      </div>
    </div>
  );
}

export default App;
