import { useMemo, useState } from "react";
import {
  Line,
  XAxis,
  YAxis,
  Tooltip,
  ResponsiveContainer,
  Area,
  AreaChart,
} from "recharts";
import type { PricePoint, TimeScale } from "../types";
import { TIME_SCALES } from "../types";

interface Props {
  history: PricePoint[];
}

function formatTime(ts: number): string {
  const h = Math.floor(ts / 3600);
  const m = Math.floor((ts % 3600) / 60);
  return `${h.toString().padStart(2, "0")}:${m.toString().padStart(2, "0")}`;
}

function formatAbsTime(absT: number, multiDay: boolean): string {
  if (multiDay) {
    const dayNum = Math.floor(absT / 86400) + 1;
    const withinDay = absT % 86400;
    return `D${dayNum} ${formatTime(withinDay)}`;
  }
  return formatTime(absT % 86400);
}

const MAX_CHART_POINTS = 300;

function downsample(data: { time: string; mid: number; bid: number; ask: number }[], maxPts: number) {
  if (data.length <= maxPts) return data;
  const step = Math.ceil(data.length / maxPts);
  const result: typeof data = [];
  for (let i = 0; i < data.length; i += step) {
    result.push(data[i]);
  }
  if (result[result.length - 1] !== data[data.length - 1]) {
    result.push(data[data.length - 1]);
  }
  return result;
}

export default function PriceChart({ history }: Props) {
  const [scale, setScale] = useState<TimeScale>("all");

  const multiDay = useMemo(() => {
    if (history.length < 2) return false;
    return history[history.length - 1].day > 1;
  }, [history]);

  const filtered = useMemo(() => {
    if (!history.length) return [];
    const scaleInfo = TIME_SCALES.find((s) => s.key === scale);
    const cutoff = scaleInfo && scaleInfo.seconds < Infinity
      ? history[history.length - 1].absT - scaleInfo.seconds
      : -Infinity;
    return history.filter((p) => p.absT >= cutoff);
  }, [history, scale]);

  const data = useMemo(() => {
    const mapped = filtered.map((p) => ({
      time: formatAbsTime(p.absT, multiDay),
      mid: p.mid,
      bid: p.bid,
      ask: p.ask,
    }));
    return downsample(mapped, MAX_CHART_POINTS);
  }, [filtered, multiDay]);

  if (data.length < 2) {
    return (
      <div>
        <div className="chart-header">
          <h3>Price</h3>
          <ScaleSelector scale={scale} setScale={setScale} />
        </div>
        <div className="chart-placeholder">Waiting for data...</div>
      </div>
    );
  }

  const allPrices = data.flatMap((d) => [d.bid, d.ask]);
  const yMin = Math.min(...allPrices) - 0.05;
  const yMax = Math.max(...allPrices) + 0.05;

  return (
    <div>
      <div className="chart-header">
        <h3>Price</h3>
        <ScaleSelector scale={scale} setScale={setScale} />
      </div>
      <ResponsiveContainer width="100%" height={300}>
        <AreaChart data={data} margin={{ top: 5, right: 20, bottom: 5, left: 10 }}>
          <defs>
            <linearGradient id="bidGrad" x1="0" y1="0" x2="0" y2="1">
              <stop offset="5%" stopColor="#22c55e" stopOpacity={0.15} />
              <stop offset="95%" stopColor="#22c55e" stopOpacity={0} />
            </linearGradient>
            <linearGradient id="askGrad" x1="0" y1="1" x2="0" y2="0">
              <stop offset="5%" stopColor="#ef4444" stopOpacity={0.15} />
              <stop offset="95%" stopColor="#ef4444" stopOpacity={0} />
            </linearGradient>
          </defs>
          <XAxis dataKey="time" tick={{ fontSize: 10, fill: "#94a3b8" }} interval="preserveStartEnd" minTickGap={50} />
          <YAxis domain={[yMin, yMax]} tick={{ fontSize: 11, fill: "#94a3b8" }} tickFormatter={(v: number) => `$${v.toFixed(2)}`} width={70} />
          <Tooltip
            contentStyle={{ background: "#1e293b", border: "1px solid #334155", borderRadius: 8, fontSize: 12 }}
            labelStyle={{ color: "#94a3b8" }}
            formatter={(value: number, name: string) => [`$${value.toFixed(4)}`, name]}
          />
          <Area type="monotone" dataKey="bid" stroke="#22c55e" fill="url(#bidGrad)" strokeWidth={1} dot={false} isAnimationActive={false} />
          <Area type="monotone" dataKey="ask" stroke="#ef4444" fill="url(#askGrad)" strokeWidth={1} dot={false} isAnimationActive={false} />
          <Line type="monotone" dataKey="mid" stroke="#3b82f6" strokeWidth={2} dot={false} isAnimationActive={false} />
        </AreaChart>
      </ResponsiveContainer>
    </div>
  );
}

function ScaleSelector({ scale, setScale }: { scale: TimeScale; setScale: (s: TimeScale) => void }) {
  return (
    <div className="scale-selector">
      {TIME_SCALES.map((s) => (
        <button
          key={s.key}
          className={`scale-btn ${scale === s.key ? "active" : ""}`}
          onClick={() => setScale(s.key)}
        >
          {s.label}
        </button>
      ))}
    </div>
  );
}
