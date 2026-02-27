import { useEffect } from "react";
import type { Simulation } from "../types";

interface Props {
  sims: Simulation[];
  activeSim: Simulation | null;
  onView: (sim: Simulation) => void;
  onDelete: (id: string) => void;
  onRefresh: () => void;
}

export default function SimulationList({ sims, activeSim, onView, onDelete, onRefresh }: Props) {
  useEffect(() => {
    onRefresh();
  }, [onRefresh]);

  return (
    <div className="sim-list">
      <h3>Simulations</h3>
      {sims.length === 0 && <p className="muted">No simulations yet</p>}
      {sims.map((s) => (
        <div key={s.id} className={`sim-card ${activeSim?.id === s.id ? "active" : ""}`}>
          <div className="sim-card-header">
            <span className="sim-symbol">{s.symbol}</span>
            <span className={`sim-status ${s.status}`}>{s.status}</span>
          </div>
          <div className="sim-card-meta">
            {s.total_events.toLocaleString()} events &middot; {s.seconds}s &middot; seed {s.seed}
          </div>
          <div className="sim-card-actions">
            <button
              className="btn-stream"
              onClick={() => onView(s)}
              disabled={s.status !== "ready"}
            >
              Stream
            </button>
            <button className="btn-delete" onClick={() => onDelete(s.id)}>
              Delete
            </button>
          </div>
        </div>
      ))}
    </div>
  );
}
