export interface Simulation {
  id: string;
  symbol: string;
  seconds: number;
  days: number;
  seed: number;
  speed: number;
  p0: number;
  status: string;
  total_events: number;
  preset: string;
}

export interface Level {
  price: number;
  depth: number;
}

export interface PricePoint {
  absT: number;
  t: number;
  mid: number;
  bid: number;
  ask: number;
  day: number;
}

export interface OrderEvent {
  type: string;
  price: number;
  qty: number;
}

export interface TickUpdate {
  type: "tick";
  idx: number;
  total: number;
  day: number;
  totalDays: number;
  date: string;
  ts: number;
  dayOffset: number;
  mid: number;
  bestBid: number;
  bestAsk: number;
  spread: number;
  bids: Level[];
  asks: Level[];
  events: OrderEvent[];
}

export interface NightUpdate {
  type: "night";
  day: number;
  totalDays: number;
  date: string;
  nextDate: string;
  close: number;
}

export interface CompleteUpdate {
  type: "complete";
  totalEvents: number;
  totalDays: number;
}

export type StreamMessage = TickUpdate | NightUpdate | CompleteUpdate | { type: "error"; msg: string };

export type TimeScale = "5m" | "10m" | "30m" | "1h" | "4h" | "1d" | "1w" | "30d" | "1y" | "5y" | "all";

export const TIME_SCALES: { key: TimeScale; label: string; seconds: number }[] = [
  { key: "5m", label: "5 min", seconds: 300 },
  { key: "10m", label: "10 min", seconds: 600 },
  { key: "30m", label: "30 min", seconds: 1800 },
  { key: "1h", label: "1 hour", seconds: 3600 },
  { key: "4h", label: "4 hours", seconds: 14400 },
  { key: "1d", label: "1 day", seconds: 86400 },
  { key: "1w", label: "1 week", seconds: 604800 },
  { key: "30d", label: "30 days", seconds: 2592000 },
  { key: "1y", label: "1 year", seconds: 31536000 },
  { key: "5y", label: "5 years", seconds: 157680000 },
  { key: "all", label: "All", seconds: Infinity },
];

export interface Preset {
  label: string;
  model: string;
  base_L: number;
  base_C: number;
  base_M: number;
  imbalance_sens: number;
  cancel_sens: number;
  epsilon_exec: number;
  spread_sens: number;
}
