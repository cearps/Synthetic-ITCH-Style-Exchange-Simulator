import asyncio
import subprocess
import sys
import time
import uuid
from pathlib import Path
from typing import Dict, List, Optional

from fastapi import FastAPI, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "notebooks"))
from book_replay import _MiniBook
from qrsdp_reader import read_day, read_header

REPO_ROOT = Path(__file__).resolve().parent.parent
CLI_BIN = REPO_ROOT / "build" / "qrsdp_cli"
OUTPUT_DIR = REPO_ROOT / "output" / "api_sims"

app = FastAPI(title="QRSDP Simulation API")
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class SimulationCreate(BaseModel):
    symbol: str
    seconds: int = 3600
    seed: int = 42
    speed: float = 100.0


class SimulationInfo(BaseModel):
    id: str
    symbol: str
    seconds: int
    seed: int
    speed: float
    status: str
    total_events: int = 0


_simulations: Dict[str, dict] = {}
_active_streams: Dict[str, bool] = {}


@app.post("/api/simulations", response_model=SimulationInfo)
async def create_simulation(cfg: SimulationCreate):
    sim_id = uuid.uuid4().hex[:8]
    out_dir = OUTPUT_DIR / sim_id
    out_dir.mkdir(parents=True, exist_ok=True)
    out_file = out_dir / f"{cfg.symbol}.qrsdp"

    result = subprocess.run(
        [str(CLI_BIN), str(cfg.seed), str(cfg.seconds), str(out_file)],
        capture_output=True,
        text=True,
        timeout=120,
    )
    if result.returncode != 0:
        _simulations[sim_id] = {
            "id": sim_id,
            "symbol": cfg.symbol,
            "seconds": cfg.seconds,
            "seed": cfg.seed,
            "speed": cfg.speed,
            "status": "error",
            "total_events": 0,
            "file": None,
        }
        return SimulationInfo(**{k: v for k, v in _simulations[sim_id].items() if k != "file"})

    header = read_header(str(out_file))
    events = read_day(str(out_file))

    _simulations[sim_id] = {
        "id": sim_id,
        "symbol": cfg.symbol,
        "seconds": cfg.seconds,
        "seed": cfg.seed,
        "speed": cfg.speed,
        "status": "ready",
        "total_events": len(events),
        "file": str(out_file),
        "header": header,
    }
    return SimulationInfo(**{k: v for k, v in _simulations[sim_id].items() if k not in ("file", "header")})


@app.get("/api/simulations", response_model=List[SimulationInfo])
async def list_simulations():
    return [
        SimulationInfo(**{k: v for k, v in s.items() if k not in ("file", "header")})
        for s in _simulations.values()
    ]


@app.get("/api/simulations/{sim_id}", response_model=SimulationInfo)
async def get_simulation(sim_id: str):
    s = _simulations.get(sim_id)
    if not s:
        return SimulationInfo(id=sim_id, symbol="", seconds=0, seed=0, speed=0, status="not_found")
    return SimulationInfo(**{k: v for k, v in s.items() if k not in ("file", "header")})


@app.delete("/api/simulations/{sim_id}")
async def delete_simulation(sim_id: str):
    _active_streams.pop(sim_id, None)
    s = _simulations.pop(sim_id, None)
    if s and s.get("file"):
        p = Path(s["file"])
        if p.exists():
            p.unlink()
        if p.parent.exists():
            try:
                p.parent.rmdir()
            except OSError:
                pass
    return {"deleted": sim_id}


@app.websocket("/api/simulations/{sim_id}/stream")
async def stream_simulation(websocket: WebSocket, sim_id: str):
    await websocket.accept()
    sim = _simulations.get(sim_id)
    if not sim or sim["status"] != "ready":
        await websocket.send_json({"type": "error", "msg": "simulation not found or not ready"})
        await websocket.close()
        return

    _active_streams[sim_id] = True

    try:
        header = sim["header"]
        events = read_day(sim["file"])
        n = len(events)

        book = _MiniBook(
            p0_ticks=header["p0_ticks"],
            levels_per_side=header["levels_per_side"],
            initial_spread_ticks=header["initial_spread_ticks"],
            initial_depth=header["initial_depth"],
        )

        speed = sim["speed"]
        ts_arr = events["ts_ns"]
        types = events["type"]
        prices = events["price_ticks"]
        qtys = events["qty"]

        t0_sim = int(ts_arr[0])
        wall_t0 = time.monotonic()
        batch_size = max(1, n // 500)

        price_history: list = []

        for i in range(n):
            if not _active_streams.get(sim_id, False):
                break

            book.apply(int(types[i]), int(prices[i]), int(qtys[i]))

            if i % batch_size == 0 or i == n - 1:
                sim_elapsed_s = (int(ts_arr[i]) - t0_sim) / 1e9
                target_wall = wall_t0 + sim_elapsed_s / speed
                now = time.monotonic()
                if target_wall > now:
                    await asyncio.sleep(target_wall - now)

                mid = (book.best_bid + book.best_ask) / 2.0
                ts_s = int(ts_arr[i]) / 1e9
                price_history.append({"t": round(ts_s, 3), "mid": round(mid / 100, 4),
                                      "bid": round(book.best_bid / 100, 4),
                                      "ask": round(book.best_ask / 100, 4)})

                bids = [{"price": round(book.bids[k].price / 100, 4), "depth": book.bids[k].depth}
                        for k in range(book.num_levels)]
                asks = [{"price": round(book.asks[k].price / 100, 4), "depth": book.asks[k].depth}
                        for k in range(book.num_levels)]

                msg = {
                    "type": "update",
                    "idx": i,
                    "total": n,
                    "ts": round(ts_s, 3),
                    "mid": round(mid / 100, 4),
                    "bestBid": round(book.best_bid / 100, 4),
                    "bestAsk": round(book.best_ask / 100, 4),
                    "spread": round((book.best_ask - book.best_bid) / 100, 4),
                    "bids": bids,
                    "asks": asks,
                    "priceHistory": price_history[-60:],
                }
                try:
                    await websocket.send_json(msg)
                except Exception:
                    break

        await websocket.send_json({"type": "complete", "totalEvents": n})
    except WebSocketDisconnect:
        pass
    finally:
        _active_streams.pop(sim_id, None)


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
