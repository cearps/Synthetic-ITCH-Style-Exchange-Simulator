import { useState } from "react";

interface Props {
  onCreate: (symbol: string, seconds: number, seed: number, speed: number, model: string) => Promise<any>;
}

export default function CreateSimulation({ onCreate }: Props) {
  const [symbol, setSymbol] = useState("AAPL");
  const [seconds, setSeconds] = useState(23400);
  const [seed, setSeed] = useState(42);
  const [speed, setSpeed] = useState(500);
  const [model, setModel] = useState("simple");
  const [loading, setLoading] = useState(false);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setLoading(true);
    try {
      await onCreate(symbol.toUpperCase(), seconds, seed, speed, model);
      setSeed((s) => s + 1);
    } finally {
      setLoading(false);
    }
  };

  return (
    <form className="create-form" onSubmit={handleSubmit}>
      <h3>New Simulation</h3>
      <label>
        Symbol
        <input
          type="text"
          value={symbol}
          onChange={(e) => setSymbol(e.target.value)}
          maxLength={8}
          placeholder="e.g. AAPL"
        />
      </label>
      <label>
        Duration
        <select value={seconds} onChange={(e) => setSeconds(Number(e.target.value))}>
          <option value={3600}>1 hour (3,600s)</option>
          <option value={7200}>2 hours (7,200s)</option>
          <option value={23400}>Full day (23,400s)</option>
        </select>
      </label>
      <label>
        Intensity Model
        <select value={model} onChange={(e) => setModel(e.target.value)}>
          <option value="simple">Simple (high exec)</option>
          <option value="hlr">HLR 2014 (queue-reactive)</option>
        </select>
      </label>
      <label>
        RNG Seed
        <input
          type="number"
          value={seed}
          onChange={(e) => setSeed(Number(e.target.value))}
          min={1}
        />
      </label>
      <label>
        Replay Speed
        <select value={speed} onChange={(e) => setSpeed(Number(e.target.value))}>
          <option value={50}>50x (slow)</option>
          <option value={100}>100x</option>
          <option value={200}>200x</option>
          <option value={500}>500x</option>
          <option value={1000}>1000x (fast)</option>
        </select>
      </label>
      <button type="submit" disabled={loading || !symbol.trim()}>
        {loading ? "Generating..." : "Create Simulation"}
      </button>
    </form>
  );
}
