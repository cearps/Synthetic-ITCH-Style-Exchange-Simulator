import { useState, useCallback, useRef } from "react";
import type { Simulation, TickUpdate, NightUpdate, PricePoint, OrderEvent, StreamMessage, Preset } from "./types";
import CreateSimulation from "./components/CreateSimulation";
import SimulationList from "./components/SimulationList";
import SimulationView from "./components/SimulationView";

const API = "/api";

export default function App() {
  const [sims, setSims] = useState<Simulation[]>([]);
  const [activeSim, setActiveSim] = useState<Simulation | null>(null);
  const [lastTick, setLastTick] = useState<TickUpdate | null>(null);
  const [night, setNight] = useState<NightUpdate | null>(null);
  const [done, setDone] = useState(false);
  const [streaming, setStreaming] = useState(false);
  const [priceHistory, setPriceHistory] = useState<PricePoint[]>([]);
  const [eventFeed, setEventFeed] = useState<OrderEvent[]>([]);
  const wsRef = useRef<WebSocket | null>(null);

  const refreshSims = useCallback(async () => {
    const res = await fetch(`${API}/simulations`);
    setSims(await res.json());
  }, []);

  const handleCreate = useCallback(
    async (body: Record<string, unknown>) => {
      const res = await fetch(`${API}/simulations`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      if (!res.ok) {
        const err = await res.json();
        throw new Error(err.detail || "Failed to create simulation");
      }
      const sim: Simulation = await res.json();
      setSims((prev) => [...prev, sim]);
      return sim;
    },
    []
  );

  const handleView = useCallback(
    (sim: Simulation) => {
      if (wsRef.current) wsRef.current.close();
      setActiveSim(sim);
      setLastTick(null);
      setNight(null);
      setDone(false);
      setPriceHistory([]);
      setEventFeed([]);
      setStreaming(true);

      const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
      const socket = new WebSocket(
        `${proto}//${window.location.host}${API}/simulations/${sim.id}/stream`
      );
      socket.onmessage = (e) => {
        const msg: StreamMessage = JSON.parse(e.data);
        if (msg.type === "tick") {
          setLastTick(msg);
          setNight(null);
          const absT = msg.dayOffset + msg.ts;
          setPriceHistory((prev) => [
            ...prev,
            { absT, t: msg.ts, mid: msg.mid, bid: msg.bestBid, ask: msg.bestAsk, day: msg.day },
          ]);
          if (msg.events?.length) {
            setEventFeed((prev) => [...prev.slice(-92), ...msg.events]);
          }
        } else if (msg.type === "night") {
          setNight(msg);
        } else if (msg.type === "complete") {
          setDone(true);
          setStreaming(false);
        }
      };
      socket.onclose = () => setStreaming(false);
      wsRef.current = socket;
    },
    []
  );

  const handleDelete = useCallback(
    async (id: string) => {
      await fetch(`${API}/simulations/${id}`, { method: "DELETE" });
      setSims((prev) => prev.filter((s) => s.id !== id));
      if (activeSim?.id === id) {
        setActiveSim(null);
        setLastTick(null);
        setNight(null);
        setDone(false);
        setPriceHistory([]);
        setEventFeed([]);
        if (wsRef.current) wsRef.current.close();
      }
    },
    [activeSim]
  );

  const handleStop = useCallback(() => {
    if (wsRef.current) wsRef.current.close();
    wsRef.current = null;
    setStreaming(false);
  }, []);

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
          {activeSim && (lastTick || night || done) ? (
            <SimulationView
              sim={activeSim}
              tick={lastTick}
              night={night}
              done={done}
              streaming={streaming}
              priceHistory={priceHistory}
              eventFeed={eventFeed}
              onStop={handleStop}
            />
          ) : activeSim && streaming ? (
            <div className="placeholder">Connecting to stream...</div>
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
