import { useMemo } from "react";
import {
  LineChart,
  Line,
  XAxis,
  YAxis,
  Tooltip,
  ResponsiveContainer,
  Area,
  AreaChart,
} from "recharts";
import type { PricePoint } from "../types";

interface Props {
  history: PricePoint[];
}

function formatTime(ts: number): string {
  const totalSec = Math.floor(ts);
  const h = Math.floor(totalSec / 3600);
  const m = Math.floor((totalSec % 3600) / 60);
  const s = totalSec % 60;
  return `${h.toString().padStart(2, "0")}:${m.toString().padStart(2, "0")}:${s.toString().padStart(2, "0")}`;
}

export default function PriceChart({ history }: Props) {
  const data = useMemo(() => {
    return history.map((p) => ({
      time: formatTime(p.t),
      mid: p.mid,
      bid: p.bid,
      ask: p.ask,
    }));
  }, [history]);

  if (data.length < 2) {
    return <div className="chart-placeholder">Waiting for data...</div>;
  }

  const allPrices = data.flatMap((d) => [d.bid, d.ask]);
  const yMin = Math.min(...allPrices) - 0.02;
  const yMax = Math.max(...allPrices) + 0.02;

  return (
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
        <XAxis
          dataKey="time"
          tick={{ fontSize: 11, fill: "#94a3b8" }}
          interval="preserveStartEnd"
        />
        <YAxis
          domain={[yMin, yMax]}
          tick={{ fontSize: 11, fill: "#94a3b8" }}
          tickFormatter={(v: number) => `$${v.toFixed(2)}`}
          width={70}
        />
        <Tooltip
          contentStyle={{ background: "#1e293b", border: "1px solid #334155", borderRadius: 8 }}
          labelStyle={{ color: "#94a3b8" }}
          formatter={(value: number, name: string) => [`$${value.toFixed(4)}`, name]}
        />
        <Area
          type="monotone"
          dataKey="bid"
          stroke="#22c55e"
          fill="url(#bidGrad)"
          strokeWidth={1}
          dot={false}
          isAnimationActive={false}
        />
        <Area
          type="monotone"
          dataKey="ask"
          stroke="#ef4444"
          fill="url(#askGrad)"
          strokeWidth={1}
          dot={false}
          isAnimationActive={false}
        />
        <Line
          type="monotone"
          dataKey="mid"
          stroke="#3b82f6"
          strokeWidth={2}
          dot={false}
          isAnimationActive={false}
        />
      </AreaChart>
    </ResponsiveContainer>
  );
}
