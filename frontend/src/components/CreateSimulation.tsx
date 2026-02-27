import { useState, useEffect } from "react";
import type { Preset } from "../types";

interface Props {
  onCreate: (body: Record<string, unknown>) => Promise<any>;
}

export default function CreateSimulation({ onCreate }: Props) {
  const [presets, setPresets] = useState<Record<string, Preset>>({});
  const [symbol, setSymbol] = useState("AAPL");
  const [seconds, setSeconds] = useState(23400);
  const [days, setDays] = useState(5);
  const [seed, setSeed] = useState(42);
  const [speed, setSpeed] = useState(500);
  const [p0, setP0] = useState(150);
  const [preset, setPreset] = useState("simple_high_exec");
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState("");

  useEffect(() => {
    fetch("/api/presets")
      .then((r) => r.json())
      .then(setPresets)
      .catch(() => {});
  }, []);

  const handleSubmit = async (e: React.FormEvent) => {
    e.preventDefault();
    setError("");
    setLoading(true);
    try {
      await onCreate({
        symbol: symbol.toUpperCase(),
        seconds,
        days,
        seed,
        speed,
        p0,
        preset,
      });
      setSeed((s) => s + 1);
    } catch (err: any) {
      setError(err.message || "Failed");
    } finally {
      setLoading(false);
    }
  };

  const cur = presets[preset];

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
        Starting Price ($)
        <input
          type="number"
          value={p0}
          onChange={(e) => setP0(Number(e.target.value))}
          min={1}
          max={100000}
          step={0.01}
        />
      </label>
      <label>
        Duration per Day
        <select value={seconds} onChange={(e) => setSeconds(Number(e.target.value))}>
          <option value={3600}>1 hour</option>
          <option value={7200}>2 hours</option>
          <option value={23400}>Full day (6.5 hrs)</option>
        </select>
      </label>
      <label>
        Trading Days
        <select value={days} onChange={(e) => setDays(Number(e.target.value))}>
          <option value={1}>1 day</option>
          <option value={5}>5 days (1 week)</option>
          <option value={20}>20 days (1 month)</option>
          <option value={60}>60 days (3 months)</option>
          <option value={252}>252 days (1 year)</option>
        </select>
      </label>
      <label>
        Intensity Model
        <select value={preset} onChange={(e) => setPreset(e.target.value)}>
          {Object.entries(presets).map(([k, v]) => (
            <option key={k} value={k}>
              {v.label}
            </option>
          ))}
        </select>
      </label>
      {cur && (
        <div className="preset-params">
          {cur.model === "simple" ? (
            <span>
              M={cur.base_M} L={cur.base_L} C={cur.base_C} ε={cur.epsilon_exec} sprd={cur.spread_sens}
            </span>
          ) : (
            <span>Queue-reactive per-level curves</span>
          )}
        </div>
      )}
      <div className="form-row">
        <label className="half">
          Seed
          <input
            type="number"
            value={seed}
            onChange={(e) => setSeed(Number(e.target.value))}
            min={1}
          />
        </label>
        <label className="half">
          Speed
          <select value={speed} onChange={(e) => setSpeed(Number(e.target.value))}>
            <option value={50}>50x</option>
            <option value={100}>100x</option>
            <option value={200}>200x</option>
            <option value={500}>500x</option>
            <option value={1000}>1000x</option>
          </select>
        </label>
      </div>
      {error && <div className="form-error">{error}</div>}
      <button type="submit" disabled={loading || !symbol.trim()}>
        {loading ? "Generating..." : "Create Simulation"}
      </button>
    </form>
  );
}
