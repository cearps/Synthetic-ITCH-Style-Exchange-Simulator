import { useState, useCallback, useRef } from "react";
import type { Simulation, TickUpdate, NightUpdate, PricePoint, OrderEvent, StreamMessage } from "./types";
import CreateSimulation from "./components/CreateSimulation";
import SimulationList from "./components/SimulationList";
import SimulationView from "./components/SimulationView";

const API = "/api";
const DEFAULT_REPLAY_SPEED = 500;

export default function App() {
  const [sims, setSims] = useState<Simulation[]>([]);
  const [activeSim, setActiveSim] = useState<Simulation | null>(null);
  const [lastTick, setLastTick] = useState<TickUpdate | null>(null);
  const [night, setNight] = useState<NightUpdate | null>(null);
  const [done, setDone] = useState(false);
  const [streaming, setStreaming] = useState(false);
  const [paused, setPaused] = useState(false);
  const [replaySpeed, setReplaySpeed] = useState(DEFAULT_REPLAY_SPEED);
  const [priceHistory, setPriceHistory] = useState<PricePoint[]>([]);
  const [eventFeed, setEventFeed] = useState<OrderEvent[]>([]);
  const wsRef = useRef<WebSocket | null>(null);

  const refreshSims = useCallback(async () => {
    try {
      const res = await fetch(`${API}/simulations`);
      if (!res.ok) return;
      setSims(await res.json());
    } catch {
      // network error on initial load
    }
  }, []);

  const handleCreate = useCallback(
    async (body: Record<string, unknown>) => {
      const res = await fetch(`${API}/simulations`, {
        method: "POST",
        headers: { "Content-Type": "application/json" },
        body: JSON.stringify(body),
      });
      if (!res.ok) {
        const err = await res.json().catch(() => ({ detail: `Server error (${res.status})` }));
        throw new Error(err.detail || `Request failed (${res.status})`);
      }
      const sim: Simulation = await res.json();
      setSims((prev) => [...prev, sim]);
      return sim;
    },
    []
  );

  const sendControl = useCallback((msg: Record<string, unknown>) => {
    const ws = wsRef.current;
    if (ws && ws.readyState === WebSocket.OPEN) {
      ws.send(JSON.stringify(msg));
    }
  }, []);

  const handleSetSpeed = useCallback((speed: number) => {
    sendControl({ type: "set_speed", speed });
  }, [sendControl]);

  const handlePause = useCallback(() => {
    sendControl({ type: "pause" });
  }, [sendControl]);

  const handleResume = useCallback(() => {
    sendControl({ type: "resume" });
  }, [sendControl]);

  const handleReplay = useCallback(
    (sim: Simulation) => {
      if (wsRef.current) wsRef.current.close();
      setActiveSim(sim);
      setLastTick(null);
      setNight(null);
      setDone(false);
      setPaused(false);
      setReplaySpeed(DEFAULT_REPLAY_SPEED);
      setPriceHistory([]);
      setEventFeed([]);
      setStreaming(true);

      const proto = window.location.protocol === "https:" ? "wss:" : "ws:";
      const socket = new WebSocket(
        `${proto}//${window.location.host}${API}/simulations/${sim.id}/stream?speed=${DEFAULT_REPLAY_SPEED}`
      );

      socket.onmessage = (e) => {
        let msg: StreamMessage;
        try {
          msg = JSON.parse(e.data);
        } catch {
          return;
        }

        if (msg.type === "tick") {
          setLastTick(msg);
          setNight(null);
          setReplaySpeed(msg.speed);
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
        } else if (msg.type === "playback_init") {
          setReplaySpeed(msg.speed);
          setPaused(msg.paused);
        } else if (msg.type === "speed_changed") {
          setReplaySpeed(msg.speed);
        } else if (msg.type === "paused") {
          setPaused(true);
        } else if (msg.type === "resumed") {
          setPaused(false);
        }
      };

      socket.onerror = () => setStreaming(false);
      socket.onclose = () => {
        setStreaming(false);
        wsRef.current = null;
      };
      wsRef.current = socket;
    },
    []
  );

  const handleDelete = useCallback(
    async (id: string) => {
      try {
        const res = await fetch(`${API}/simulations/${id}`, { method: "DELETE" });
        if (!res.ok && res.status !== 404) return;
      } catch {
        return;
      }
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
            onReplay={handleReplay}
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
              paused={paused}
              replaySpeed={replaySpeed}
              priceHistory={priceHistory}
              eventFeed={eventFeed}
              onSetSpeed={handleSetSpeed}
              onPause={handlePause}
              onResume={handleResume}
              onStop={handleStop}
            />
          ) : activeSim && streaming ? (
            <div className="placeholder">Connecting to replay...</div>
          ) : (
            <div className="placeholder">
              <p>Create a simulation and click Replay to view real-time data</p>
            </div>
          )}
        </main>
      </div>
    </div>
  );
}
