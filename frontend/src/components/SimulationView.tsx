import type { Simulation, StreamUpdate } from "../types";
import OrderBook from "./OrderBook";
import PriceChart from "./PriceChart";

interface Props {
  sim: Simulation;
  data: StreamUpdate;
  streaming: boolean;
  onStop: () => void;
}

export default function SimulationView({ sim, data, streaming, onStop }: Props) {
  if (data.type === "error") {
    return <div className="error-msg">Error: {data.msg}</div>;
  }

  const pct = data.total ? Math.round(((data.idx ?? 0) / data.total) * 100) : 0;
  const done = data.type === "complete";

  return (
    <div className="sim-view">
      <div className="sim-view-header">
        <div>
          <h2>{sim.symbol}</h2>
          <span className="sim-view-meta">
            Seed {sim.seed} &middot; {sim.speed}x speed
          </span>
        </div>
        <div className="sim-view-stats">
          {data.bestBid !== undefined && (
            <>
              <div className="stat bid-stat">
                <span className="stat-label">Bid</span>
                <span className="stat-value">${data.bestBid?.toFixed(2)}</span>
              </div>
              <div className="stat mid-stat">
                <span className="stat-label">Mid</span>
                <span className="stat-value">${data.mid?.toFixed(2)}</span>
              </div>
              <div className="stat ask-stat">
                <span className="stat-label">Ask</span>
                <span className="stat-value">${data.bestAsk?.toFixed(2)}</span>
              </div>
              <div className="stat spread-stat">
                <span className="stat-label">Spread</span>
                <span className="stat-value">${data.spread?.toFixed(2)}</span>
              </div>
            </>
          )}
        </div>
        <div className="sim-view-controls">
          <div className="progress-info">
            {done ? "Complete" : `${pct}%`} &middot;{" "}
            {(data.idx ?? 0).toLocaleString()} / {(data.total ?? 0).toLocaleString()} events
          </div>
          <div className="progress-bar">
            <div className="progress-fill" style={{ width: `${pct}%` }} />
          </div>
          {streaming && !done && (
            <button className="btn-stop" onClick={onStop}>
              Stop
            </button>
          )}
        </div>
      </div>

      <div className="charts-row">
        <div className="chart-panel price-panel">
          <h3>Price</h3>
          <PriceChart history={data.priceHistory ?? []} />
        </div>
        <div className="chart-panel book-panel">
          <h3>Order Book</h3>
          <OrderBook bids={data.bids ?? []} asks={data.asks ?? []} />
        </div>
      </div>
    </div>
  );
}
