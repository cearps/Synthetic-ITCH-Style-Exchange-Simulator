import type { Simulation, TickUpdate, NightUpdate, PricePoint, OrderEvent } from "../types";
import OrderBook from "./OrderBook";
import PriceChart from "./PriceChart";
import EventFeed from "./EventFeed";
import PlaybackControls from "./PlaybackControls";

interface Props {
  sim: Simulation;
  tick: TickUpdate | null;
  night: NightUpdate | null;
  done: boolean;
  streaming: boolean;
  paused: boolean;
  replaySpeed: number;
  priceHistory: PricePoint[];
  eventFeed: OrderEvent[];
  onSetSpeed: (speed: number) => void;
  onPause: () => void;
  onResume: () => void;
  onStop: () => void;
}

export default function SimulationView({
  sim, tick, night, done, streaming, paused, replaySpeed,
  priceHistory, eventFeed, onSetSpeed, onPause, onResume, onStop,
}: Props) {
  const pct = tick ? Math.round((tick.idx / tick.total) * 100) : done ? 100 : 0;
  const dayLabel = tick ? `Day ${tick.day}/${tick.totalDays} · ${tick.date}` : "";

  return (
    <div className="sim-view">
      <div className="sim-view-header">
        <div>
          <h2>{sim.symbol}</h2>
          <span className="sim-view-meta">
            ${sim.p0} · Seed {sim.seed} · {sim.days}d
            {dayLabel && <> · {dayLabel}</>}
            {streaming && <> · <span className="replay-badge">{paused ? "PAUSED" : `${replaySpeed}x`}</span></>}
          </span>
        </div>
        {tick && (
          <div className="sim-view-stats">
            <div className="stat bid-stat"><span className="stat-label">Bid</span><span className="stat-value">${tick.bestBid.toFixed(2)}</span></div>
            <div className="stat mid-stat"><span className="stat-label">Mid</span><span className="stat-value">${tick.mid.toFixed(2)}</span></div>
            <div className="stat ask-stat"><span className="stat-label">Ask</span><span className="stat-value">${tick.bestAsk.toFixed(2)}</span></div>
            <div className="stat spread-stat"><span className="stat-label">Spread</span><span className="stat-value">${tick.spread.toFixed(2)}</span></div>
          </div>
        )}
        <div className="sim-view-controls">
          <div className="progress-info">
            {done ? "Complete" : paused ? "Paused" : `${pct}%`} · {(tick?.idx ?? 0).toLocaleString()} / {(tick?.total ?? sim.total_events).toLocaleString()}
          </div>
          <div className="progress-bar"><div className="progress-fill" style={{ width: `${pct}%` }} /></div>
        </div>
      </div>

      {streaming && (
        <PlaybackControls
          speed={replaySpeed}
          paused={paused}
          done={done}
          onSetSpeed={onSetSpeed}
          onPause={onPause}
          onResume={onResume}
          onStop={onStop}
        />
      )}

      {night && (
        <div className="night-overlay">
          <div className="night-content">
            <div className="night-stars">✦ ✧ ✦ ✧ ✦</div>
            <div className="night-title">Market Closed</div>
            <div className="night-detail">
              Day {night.day} ended · Close ${night.close.toFixed(2)}
            </div>
            <div className="night-next">Opening {night.nextDate}...</div>
            <div className="night-spinner" />
          </div>
        </div>
      )}

      <div className="charts-row">
        <div className="chart-panel price-panel">
          <PriceChart history={priceHistory} />
        </div>
        <div className="chart-panel book-panel">
          <h3>Order Book</h3>
          <OrderBook bids={tick?.bids ?? []} asks={tick?.asks ?? []} />
        </div>
      </div>

      <div className="chart-panel feed-panel">
        <h3>Order Feed</h3>
        <EventFeed events={eventFeed} />
      </div>
    </div>
  );
}
