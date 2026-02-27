import type { Level } from "../types";

interface Props {
  bids: Level[];
  asks: Level[];
}

export default function OrderBook({ bids, asks }: Props) {
  if (!bids.length && !asks.length) {
    return <div className="chart-placeholder">Waiting for data...</div>;
  }

  const maxDepth = Math.max(
    ...bids.map((b) => b.depth),
    ...asks.map((a) => a.depth),
    1
  );

  const reversedAsks = [...asks].reverse();

  return (
    <div className="orderbook">
      <div className="ob-header">
        <span>Price</span>
        <span>Depth</span>
        <span></span>
      </div>
      <div className="ob-body">
        {reversedAsks.map((a, i) => (
          <div key={`a-${i}`} className="ob-row ask-row">
            <span className="ob-price ask-price">${a.price.toFixed(2)}</span>
            <span className="ob-depth">{a.depth}</span>
            <div className="ob-bar-container">
              <div
                className="ob-bar ask-bar"
                style={{ width: `${(a.depth / maxDepth) * 100}%` }}
              />
            </div>
          </div>
        ))}
        <div className="ob-spread-row">
          <span className="ob-spread-label">Spread</span>
        </div>
        {bids.map((b, i) => (
          <div key={`b-${i}`} className="ob-row bid-row">
            <span className="ob-price bid-price">${b.price.toFixed(2)}</span>
            <span className="ob-depth">{b.depth}</span>
            <div className="ob-bar-container">
              <div
                className="ob-bar bid-bar"
                style={{ width: `${(b.depth / maxDepth) * 100}%` }}
              />
            </div>
          </div>
        ))}
      </div>
    </div>
  );
}
