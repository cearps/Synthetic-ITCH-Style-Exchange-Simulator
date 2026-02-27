import type { OrderEvent } from "../types";

interface Props {
  events: OrderEvent[];
}

const TYPE_CLASS: Record<string, string> = {
  "ADD BID": "ev-add-bid",
  "ADD ASK": "ev-add-ask",
  "CANCEL BID": "ev-cancel",
  "CANCEL ASK": "ev-cancel",
  "EXEC BUY": "ev-exec-buy",
  "EXEC SELL": "ev-exec-sell",
};

export default function EventFeed({ events }: Props) {
  if (!events.length) return <div className="feed-empty">Waiting for orders...</div>;

  const visible = events.slice(-40);

  return (
    <div className="event-feed">
      {visible.map((ev, i) => (
        <span key={i} className={`ev-tag ${TYPE_CLASS[ev.type] ?? ""}`}>
          {ev.type} ${ev.price.toFixed(2)} ×{ev.qty}
        </span>
      ))}
    </div>
  );
}
