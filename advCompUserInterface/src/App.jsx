import { useEffect, useState } from "react";
import SignalGraph from "./signals/SignalGraph";

const MAX_POINTS = 150;

function App() {
  const [signals, setSignals] = useState({
    posX: [],
    posY: [],
    posZ: [],
    tiltX: [],
    tiltY: [],
    tiltZ: []
  });

  /*  useEffect(() => {
     let time = 0;
 
     const interval = setInterval(() => {
       time += 0.1;
 
        setSignals((old) => {
           const createPoint = (
             amplitude,
             frequency,
             phase,
             noise = 0,
             wrong = false
           ) => {
             // Different simulated movement for each graph
             const input =
               amplitude * Math.sin(frequency * time + phase) +
               (Math.random() - 0.5) * noise;
   
             // Correct controller: opposite signal
             // Wrong controller: incorrect amplitude + delay/phase error
             const counter = wrong
               ? -0.45 * amplitude * Math.sin(frequency * time + phase + 0.8)
               : -input;
   
             const result = input + counter;
   
             return {
               time,
               input,
               counter,
               result
             };
           };
   
           return {
             posX: [
               ...old.posX,
               createPoint(1.0, 1.0, 0, 0.05)
             ].slice(-MAX_POINTS),
   
             posY: [
               ...old.posY,
               createPoint(1.5, 1.4, 0.5, 0.1)
             ].slice(-MAX_POINTS),
   
             posZ: [
               ...old.posZ,
               createPoint(0.7, 2.0, 1.2, 0.03)
             ].slice(-MAX_POINTS),
   
             tiltX: [
               ...old.tiltX,
               createPoint(2.0, 0.8, 2.0, 0.15)
             ].slice(-MAX_POINTS),
   
             tiltY: [
               ...old.tiltY,
               createPoint(1.2, 2.5, 0.3, 0.08)
             ].slice(-MAX_POINTS),
   
             // Intentionally incorrect suppression
             tiltZ: [
               ...old.tiltZ,
               createPoint(1.8, 1.7, 1.5, 0.1, true)
             ].slice(-MAX_POINTS)
           };
         });
       }, 50);
   
       return () => clearInterval(interval);
     }, []);*/


  useEffect(() => {
  let time = 0;

  const interval = setInterval(() => {
    time += 0.1;

    setSignals((old) => {
      const createPoint = (
        previousPoint,
        amplitude,
      ) => {
        const change = (Math.random() * 2 - 1) * amplitude;
        const previousInput = previousPoint?.input ?? 0;

        const input = previousInput * 0.98 + change;

        const counter = -input 

        const result = input + counter;

        return { time, input, counter, result };
      };

      return {
        posX: [
          ...old.posX,
          createPoint(old.posX.at(-1), 0.1, 0.05)
        ].slice(-MAX_POINTS),

        posY: [
          ...old.posY,
          createPoint(old.posY.at(-1), 0.15, 0.1)
        ].slice(-MAX_POINTS),

        posZ: [
          ...old.posZ,
          createPoint(old.posZ.at(-1), 0.07, 0.03)
        ].slice(-MAX_POINTS),

        tiltX: [
          ...old.tiltX,
          createPoint(old.tiltX.at(-1), 0.2, 0.15)
        ].slice(-MAX_POINTS),

        tiltY: [
          ...old.tiltY,
          createPoint(old.tiltY.at(-1), 0.12, 0.08)
        ].slice(-MAX_POINTS),

        tiltZ: [
          ...old.tiltZ,
          createPoint(old.tiltZ.at(-1), 0.18, 0.1)
        ].slice(-MAX_POINTS)
      };
    });
  }, 50);

  return () => clearInterval(interval);
}, []);

  return (
    <div className="min-h-screen bg-gray-950 p-6">

      <h1 className="text-white text-2xl font-bold mb-6">
        Tremor Suppression
      </h1>

      <div className="grid grid-cols-3 gap-4">

        <SignalGraph
          title="X Position"
          data={signals.posX}
        />

        <SignalGraph
          title="Y Position"
          data={signals.posY}
        />

        <SignalGraph
          title="Z Position"
          data={signals.posZ}
        />

        <SignalGraph
          title="X Tilt"
          data={signals.tiltX}
        />

        <SignalGraph
          title="Y Tilt"
          data={signals.tiltY}
        />

        <SignalGraph
          title="Z Tilt"
          data={signals.tiltZ}
        />

      </div>

    </div>
  );
}

export default App;