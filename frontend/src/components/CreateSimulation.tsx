import { useState } from "react";

interface Props {
  onCreate: (symbol: string, seconds: number, seed: number, speed: number) => Promise<any>;
}

export default function CreateSimulation({ onCreate }: Props) {
  const [symbol, setSymbol] = useState("AAPL");
  const [seconds, setSeconds] = useState(3600);
  const [seed, setSeed] = useState(42);
  const [speed, setSpeed] = useState(200);
  const [loading, setLoading] = useState(false);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setLoading(true);
    try {
      await onCreate(symbol.toUpperCase(), seconds, seed, speed);
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
        Duration (seconds)
        <input
          type="number"
          value={seconds}
          onChange={(e) => setSeconds(Number(e.target.value))}
          min={10}
          max={86400}
        />
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
          <option value={10}>10x</option>
          <option value={50}>50x</option>
          <option value={100}>100x</option>
          <option value={200}>200x</option>
          <option value={500}>500x</option>
          <option value={1000}>1000x</option>
        </select>
      </label>
      <button type="submit" disabled={loading || !symbol.trim()}>
        {loading ? "Generating..." : "Create Simulation"}
      </button>
    </form>
  );
}
