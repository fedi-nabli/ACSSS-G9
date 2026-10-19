
import * as ort from "onnxruntime-web";

let modelPromise = null;

export function loadTremorModel() {
  if (!modelPromise) {
    const base = import.meta.env.BASE_URL;

    modelPromise = Promise.all([
      ort.InferenceSession.create(
        `${base}models/tremor_ann.onnx`
      ),
      fetch(`${base}models/normalization.json`).then(r => {
        if (!r.ok) throw new Error("Normalization file missing");
        return r.json();
      })
    ]).then(([session, config]) => ({ session, config }))
      .catch(error => {
        modelPromise = null;
        throw error;
      });
  }

  return modelPromise;
}

export async function predictTremor(window) {
  const { session, config } = await loadTremorModel();

  const { mean, std, columns, future } = config;
  const size = config.window;
  const channels = columns.length;

  if (
    window.length !== size ||
    window.some(row => row.length !== channels)
  ) {
    throw new Error(`Expected ${size} x ${channels} input`);
  }

  // Normalize the window
  const values = new Float32Array(size * channels);

  for (let t = 0; t < size; t++) {
    for (let c = 0; c < channels; c++) {
      values[t * channels + c] =
        (window[t][c] - mean[c]) / std[c];
    }
  }

  const input = new ort.Tensor(
    "float32",
    values,
    [1, size, channels]
  );

  // Run neural network
  const output = await session.run({
    sensor_window: input
  });

  const predictions = output.future_predictions.data;

  // Denormalize and return [future][channels]
  return Array.from({ length: future }, (_, t) =>
    Array.from({ length: channels }, (_, c) =>
      predictions[t * channels + c] * std[c] + mean[c]
    )
  );
}
