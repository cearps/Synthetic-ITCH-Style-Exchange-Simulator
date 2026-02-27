export interface Simulation {
  id: string;
  symbol: string;
  seconds: number;
  seed: number;
  speed: number;
  status: string;
  total_events: number;
  model: string;
}

export interface Level {
  price: number;
  depth: number;
}

export interface PricePoint {
  t: number;
  mid: number;
  bid: number;
  ask: number;
}

export interface StreamUpdate {
  type: "update" | "complete" | "error";
  idx?: number;
  total?: number;
  ts?: number;
  mid?: number;
  bestBid?: number;
  bestAsk?: number;
  spread?: number;
  bids?: Level[];
  asks?: Level[];
  priceHistory?: PricePoint[];
  totalEvents?: number;
  msg?: string;
}
