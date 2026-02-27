import { useState, useCallback } from "react";
import type { Simulation, StreamUpdate } from "./types";
import CreateSimulation from "./components/CreateSimulation";
import SimulationList from "./components/SimulationList";
import SimulationView from "./components/SimulationView";

const API = "/api";

export default function App() {
  const [sims, setSims] = useState<Simulation[]>([]);
  const [activeSim, setActiveSim] = useState<Simulation | null>(null);
  const [streamData, setStreamData] = useState<StreamUpdate | null>(null);
  const [ws, setWs] = useState<WebSocket | null>(null);

  const refreshSims = useCallback(async () => {
    const res = await fetch(`${API}/simulations`);
    setSims(await res.json());
  }, []);

  const handleCreate = useCallback(
    async (symbol: string, seconds: number, seed: number, speed: number) => {
      const res = await fetch(`${API}/simulations`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify({ symbol, seconds, seed, speed }),
      });
      const sim: Simulation = await res.json();
      setSims((prev) => [...prev, sim]);
      return sim;
    },
    []
  );

  const handleView = useCallback(
    (sim: Simulation) => {
      if (ws) {
        ws.close();
        setWs(null);
      }
      setActiveSim(sim);
      setStreamData(null);

      const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
      const socket = new WebSocket(
        `${proto}//${window.location.host}${API}/simulations/${sim.id}/stream`
      );
      socket.onmessage = (e) => {
        const data: StreamUpdate = JSON.parse(e.data);
        if (data.type === "complete") {
          setStreamData((prev) =>
            prev ? { ...prev, type: "complete", totalEvents: data.totalEvents } : data
          );
        } else {
          setStreamData(data);
        }
      };
      socket.onclose = () => setWs(null);
      setWs(socket);
    },
    [ws]
  );

  const handleDelete = useCallback(
    async (id: string) => {
      await fetch(`${API}/simulations/${id}`, { method: "DELETE" });
      setSims((prev) => prev.filter((s) => s.id !== id));
      if (activeSim?.id === id) {
        setActiveSim(null);
        setStreamData(null);
        if (ws) ws.close();
      }
    },
    [activeSim, ws]
  );

  const handleStop = useCallback(() => {
    if (ws) {
      ws.close();
      setWs(null);
    }
  }, [ws]);

  return (
    <div className="app">
      <header className="app-header">
        <h1>QRSDP Exchange Simulator</h1>
        <p className="subtitle">Real-time synthetic market data engine</p>
      </header>

      <div className="app-layout">
        <aside className="sidebar">
          <CreateSimulation onCreate={handleCreate} />
          <SimulationList
            sims={sims}
            activeSim={activeSim}
            onView={handleView}
            onDelete={handleDelete}
            onRefresh={refreshSims}
          />
        </aside>

        <main className="main-panel">
          {activeSim && streamData ? (
            <SimulationView
              sim={activeSim}
              data={streamData}
              streaming={ws !== null}
              onStop={handleStop}
            />
          ) : (
            <div className="placeholder">
              <p>Create a simulation and click Stream to view real-time data</p>
            </div>
          )}
        </main>
      </div>
    </div>
  );
}
