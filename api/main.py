import asyncio
import logging
import re
import shutil
import sys
import time
import uuid
from pathlib import Path
from typing import Dict, List, Optional

from contextlib import asynccontextmanager

from fastapi import FastAPI, HTTPException, WebSocket, WebSocketDisconnect
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel, field_validator

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "notebooks"))
from book_replay import _MiniBook
from qrsdp_reader import load_manifest, read_day, read_header

logger = logging.getLogger("qrsdp_api")

REPO_ROOT = Path(__file__).resolve().parent.parent
RUN_BIN = REPO_ROOT / "build" / "qrsdp_run"
OUTPUT_DIR = REPO_ROOT / "output" / "api_sims"

SYMBOL_RE = re.compile(r"^[A-Z0-9]{1,8}$")
MAX_DAYS = 252
MAX_SECONDS = 86400
MAX_SPEED = 10000.0
MAX_P0 = 100000.0

EVENT_NAMES = {
    0: "ADD BID", 1: "ADD ASK", 2: "CANCEL BID",
    3: "CANCEL ASK", 4: "EXEC BUY", 5: "EXEC SELL",
}

PRESETS = {
    "simple_high_exec": {
        "label": "Simple — High Execution",
        "model": "simple",
        "base_L": 20.0, "base_C": 0.5, "base_M": 40.0,
        "imbalance_sens": 1.0, "cancel_sens": 1.0,
        "epsilon_exec": 1.0, "spread_sens": 0.4,
    },
    "simple_default": {
        "label": "Simple — Default",
        "model": "simple",
        "base_L": 20.0, "base_C": 0.5, "base_M": 15.0,
        "imbalance_sens": 1.0, "cancel_sens": 1.0,
        "epsilon_exec": 0.5, "spread_sens": 0.4,
    },
    "simple_high_cancel": {
        "label": "Simple — High Cancel",
        "model": "simple",
        "base_L": 20.0, "base_C": 1.5, "base_M": 15.0,
        "imbalance_sens": 1.0, "cancel_sens": 2.0,
        "epsilon_exec": 0.5, "spread_sens": 0.4,
    },
    "simple_no_spread_fb": {
        "label": "Simple — No Spread Feedback",
        "model": "simple",
        "base_L": 20.0, "base_C": 0.5, "base_M": 15.0,
        "imbalance_sens": 1.0, "cancel_sens": 1.0,
        "epsilon_exec": 0.5, "spread_sens": 0.0,
    },
    "hlr_default": {
        "label": "HLR 2014 — Queue-Reactive",
        "model": "hlr",
        "base_L": 20.0, "base_C": 0.5, "base_M": 15.0,
        "imbalance_sens": 1.0, "cancel_sens": 1.0,
        "epsilon_exec": 0.5, "spread_sens": 0.4,
    },
}

@asynccontextmanager
async def lifespan(application: FastAPI):
    if not RUN_BIN.exists():
        logger.error("C++ binary not found at %s — build the project first", RUN_BIN)
    OUTPUT_DIR.mkdir(parents=True, exist_ok=True)
    yield


app = FastAPI(title="QRSDP Simulation API", lifespan=lifespan)
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_methods=["*"],
    allow_headers=["*"],
)


class SimulationCreate(BaseModel):
    symbol: str
    seconds: int = 23400
    days: int = 1
    seed: int = 42
    p0: float = 100.0
    preset: str = "simple_high_exec"
    model: Optional[str] = None
    base_M: Optional[float] = None
    epsilon_exec: Optional[float] = None
    base_L: Optional[float] = None
    base_C: Optional[float] = None
    imbalance_sens: Optional[float] = None
    cancel_sens: Optional[float] = None
    spread_sens: Optional[float] = None

    @field_validator("symbol")
    @classmethod
    def validate_symbol(cls, v: str) -> str:
        v = v.strip().upper()
        if not SYMBOL_RE.match(v):
            raise ValueError("Symbol must be 1-8 alphanumeric characters")
        return v

    @field_validator("seconds")
    @classmethod
    def validate_seconds(cls, v: int) -> int:
        if not (60 <= v <= MAX_SECONDS):
            raise ValueError(f"seconds must be between 60 and {MAX_SECONDS}")
        return v

    @field_validator("days")
    @classmethod
    def validate_days(cls, v: int) -> int:
        if not (1 <= v <= MAX_DAYS):
            raise ValueError(f"days must be between 1 and {MAX_DAYS}")
        return v

    @field_validator("seed")
    @classmethod
    def validate_seed(cls, v: int) -> int:
        if not (1 <= v <= 2**63 - 1):
            raise ValueError("seed must be a positive integer")
        return v

    @field_validator("p0")
    @classmethod
    def validate_p0(cls, v: float) -> float:
        if not (0.01 <= v <= MAX_P0):
            raise ValueError(f"p0 must be between 0.01 and {MAX_P0}")
        return v

    @field_validator("preset")
    @classmethod
    def validate_preset(cls, v: str) -> str:
        if v not in PRESETS:
            raise ValueError(f"Unknown preset: {v}. Valid: {list(PRESETS.keys())}")
        return v


class SimulationInfo(BaseModel):
    id: str
    symbol: str
    seconds: int
    days: int
    seed: int
    p0: float
    status: str
    total_events: int = 0
    preset: str = ""


class DeleteResponse(BaseModel):
    deleted: str


_simulations: Dict[str, dict] = {}
_active_streams: Dict[str, bool] = {}

SIM_PUBLIC_FIELDS = {"id", "symbol", "seconds", "days", "seed", "p0", "status", "total_events", "preset"}


def _pub(s: dict) -> dict:
    return {k: v for k, v in s.items() if k in SIM_PUBLIC_FIELDS}


@app.get("/api/presets")
async def get_presets():
    return PRESETS


@app.post("/api/simulations", response_model=SimulationInfo)
async def create_simulation(cfg: SimulationCreate):
    symbol = cfg.symbol
    for s in _simulations.values():
        if s["symbol"] == symbol and s["status"] == "ready":
            raise HTTPException(409, f"Symbol '{symbol}' already exists. Delete it first or choose a different name.")

    if not RUN_BIN.exists():
        raise HTTPException(503, "Simulation engine not available — binary not built")

    preset = PRESETS[cfg.preset]
    model = cfg.model or preset["model"]
    if model not in ("simple", "hlr"):
        raise HTTPException(400, "model must be 'simple' or 'hlr'")

    base_L = cfg.base_L if cfg.base_L is not None else preset["base_L"]
    base_C = cfg.base_C if cfg.base_C is not None else preset["base_C"]
    base_M = cfg.base_M if cfg.base_M is not None else preset["base_M"]
    imb = cfg.imbalance_sens if cfg.imbalance_sens is not None else preset["imbalance_sens"]
    canc = cfg.cancel_sens if cfg.cancel_sens is not None else preset["cancel_sens"]
    eps = cfg.epsilon_exec if cfg.epsilon_exec is not None else preset["epsilon_exec"]
    sprd = cfg.spread_sens if cfg.spread_sens is not None else preset["spread_sens"]

    p0_ticks = int(cfg.p0 * 100)

    sim_id = uuid.uuid4().hex[:12]
    out_dir = OUTPUT_DIR / sim_id

    cmd = [
        str(RUN_BIN),
        "--seed", str(cfg.seed),
        "--days", str(cfg.days),
        "--seconds", str(cfg.seconds),
        "--output", str(out_dir),
        "--securities", f"{symbol}:{p0_ticks}",
        "--model", model,
        "--base-L", str(base_L),
        "--base-C", str(base_C),
        "--base-M", str(base_M),
        "--imbalance-sens", str(imb),
        "--cancel-sens", str(canc),
        "--epsilon-exec", str(eps),
        "--spread-sens", str(sprd),
    ]

    try:
        proc = await asyncio.create_subprocess_exec(
            *cmd,
            stdout=asyncio.subprocess.PIPE,
            stderr=asyncio.subprocess.PIPE,
        )
        _, stderr_bytes = await asyncio.wait_for(proc.communicate(), timeout=600)
        if proc.returncode != 0:
            logger.error("Simulation subprocess failed: %s", stderr_bytes.decode(errors="replace")[:500])
            raise HTTPException(500, "Simulation engine returned an error")
    except asyncio.TimeoutError:
        proc.kill()
        raise HTTPException(504, "Simulation timed out")

    manifest = load_manifest(out_dir)
    total = 0
    header_sample = None
    for sec in manifest.get("securities", []):
        for sess in sec["sessions"]:
            fpath = out_dir / sess["file"]
            if fpath.exists():
                evts = read_day(str(fpath))
                total += len(evts)
                if header_sample is None:
                    header_sample = read_header(str(fpath))

    _simulations[sim_id] = {
        "id": sim_id, "symbol": symbol, "seconds": cfg.seconds, "days": cfg.days,
        "seed": cfg.seed, "p0": cfg.p0, "status": "ready",
        "total_events": total, "preset": cfg.preset,
        "run_dir": str(out_dir), "header_sample": header_sample,
    }
    return SimulationInfo(**_pub(_simulations[sim_id]))


@app.get("/api/simulations", response_model=List[SimulationInfo])
async def list_simulations():
    return [SimulationInfo(**_pub(s)) for s in _simulations.values()]


@app.get("/api/simulations/{sim_id}", response_model=SimulationInfo)
async def get_simulation(sim_id: str):
    s = _simulations.get(sim_id)
    if not s:
        raise HTTPException(404, "Not found")
    return SimulationInfo(**_pub(s))


@app.delete("/api/simulations/{sim_id}", response_model=DeleteResponse)
async def delete_simulation(sim_id: str):
    _active_streams.pop(sim_id, None)
    s = _simulations.pop(sim_id, None)
    if not s:
        raise HTTPException(404, "Not found")
    run_dir = s.get("run_dir")
    if run_dir:
        p = Path(run_dir)
        if p.exists() and OUTPUT_DIR in p.parents:
            shutil.rmtree(p, ignore_errors=True)
    return DeleteResponse(deleted=sim_id)


class _PlaybackState:
    """Mutable playback state shared between stream and control listener."""

    def __init__(self, speed: float):
        self.speed = speed
        self.paused = False
        self._resume_event = asyncio.Event()
        self._resume_event.set()

    def set_speed(self, s: float) -> None:
        self.speed = max(1.0, min(s, MAX_SPEED))

    def pause(self) -> None:
        self.paused = True
        self._resume_event.clear()

    def resume(self) -> None:
        self.paused = False
        self._resume_event.set()

    async def wait_if_paused(self) -> None:
        await self._resume_event.wait()


@app.websocket("/api/simulations/{sim_id}/stream")
async def stream_simulation(websocket: WebSocket, sim_id: str):
    await websocket.accept()
    sim = _simulations.get(sim_id)
    if not sim or sim["status"] != "ready":
        await websocket.send_json({"type": "error", "msg": "Simulation not found or not ready"})
        await websocket.close(code=4004)
        return

    speed_param = websocket.query_params.get("speed")
    initial_speed = float(speed_param) if speed_param else 500.0
    initial_speed = max(1.0, min(initial_speed, MAX_SPEED))

    pb = _PlaybackState(initial_speed)
    _active_streams[sim_id] = True

    async def _listen_controls():
        try:
            while True:
                raw = await websocket.receive_text()
                try:
                    ctrl = __import__("json").loads(raw)
                except (ValueError, TypeError):
                    continue
                ctype = ctrl.get("type")
                if ctype == "set_speed" and isinstance(ctrl.get("speed"), (int, float)):
                    pb.set_speed(float(ctrl["speed"]))
                    await websocket.send_json({"type": "speed_changed", "speed": pb.speed})
                elif ctype == "pause":
                    pb.pause()
                    await websocket.send_json({"type": "paused"})
                elif ctype == "resume":
                    pb.resume()
                    await websocket.send_json({"type": "resumed"})
        except (WebSocketDisconnect, Exception):
            pass

    control_task = asyncio.create_task(_listen_controls())

    run_dir = Path(sim["run_dir"])
    header_sample = sim["header_sample"]
    tick_div = header_sample.get("tick_size", 100) if header_sample else 100

    try:
        await websocket.send_json({"type": "playback_init", "speed": pb.speed, "paused": False})

        manifest = load_manifest(run_dir)
        sec = manifest["securities"][0]
        sessions = sec["sessions"]
        total_days = len(sessions)
        grand_total = sim["total_events"]
        events_so_far = 0

        for day_idx, sess in enumerate(sessions):
            if not _active_streams.get(sim_id, False):
                break

            fpath = run_dir / sess["file"]
            hdr = read_header(str(fpath))
            events = read_day(str(fpath))
            n = len(events)

            book = _MiniBook(
                p0_ticks=hdr["p0_ticks"],
                levels_per_side=hdr["levels_per_side"],
                initial_spread_ticks=hdr["initial_spread_ticks"],
                initial_depth=hdr["initial_depth"],
            )

            ts_arr = events["ts_ns"]
            types = events["type"]
            prices = events["price_ticks"]
            qtys = events["qty"]

            prev_ts_ns = int(ts_arr[0])
            batch = max(1, n // 400)
            recent_events: list = []

            for i in range(n):
                if not _active_streams.get(sim_id, False):
                    break

                await pb.wait_if_paused()

                etype = int(types[i])
                eprice = int(prices[i])
                eqty = int(qtys[i])
                book.apply(etype, eprice, eqty)

                recent_events.append({
                    "type": EVENT_NAMES.get(etype, "?"),
                    "price": round(eprice / tick_div, 4),
                    "qty": eqty,
                })
                if len(recent_events) > 50:
                    recent_events = recent_events[-50:]

                if i % batch == 0 or i == n - 1:
                    cur_ts_ns = int(ts_arr[i])
                    if i > 0:
                        sim_gap_s = (cur_ts_ns - prev_ts_ns) / 1e9
                        wall_gap = sim_gap_s / pb.speed
                        if wall_gap > 0:
                            await asyncio.sleep(min(wall_gap, 2.0))
                    prev_ts_ns = cur_ts_ns

                    mid = (book.best_bid + book.best_ask) / 2.0
                    ts_s = cur_ts_ns / 1e9
                    global_idx = events_so_far + i

                    bids = [{"price": round(book.bids[k].price / tick_div, 4),
                             "depth": book.bids[k].depth}
                            for k in range(book.num_levels)]
                    asks = [{"price": round(book.asks[k].price / tick_div, 4),
                             "depth": book.asks[k].depth}
                            for k in range(book.num_levels)]

                    msg = {
                        "type": "tick",
                        "idx": global_idx,
                        "total": grand_total,
                        "day": day_idx + 1,
                        "totalDays": total_days,
                        "date": sess["date"],
                        "ts": round(ts_s, 3),
                        "dayOffset": day_idx * 86400,
                        "mid": round(mid / tick_div, 4),
                        "bestBid": round(book.best_bid / tick_div, 4),
                        "bestAsk": round(book.best_ask / tick_div, 4),
                        "spread": round((book.best_ask - book.best_bid) / tick_div, 4),
                        "bids": bids,
                        "asks": asks,
                        "events": recent_events[-8:],
                        "speed": pb.speed,
                    }
                    try:
                        await websocket.send_json(msg)
                    except Exception:
                        return
                    recent_events.clear()

            events_so_far += n

            if day_idx < total_days - 1 and _active_streams.get(sim_id, False):
                next_date = sessions[day_idx + 1]["date"]
                close_price = round(book.best_bid / tick_div, 4)
                try:
                    await websocket.send_json({
                        "type": "night",
                        "day": day_idx + 1,
                        "totalDays": total_days,
                        "date": sess["date"],
                        "nextDate": next_date,
                        "close": close_price,
                    })
                except Exception:
                    return
                await asyncio.sleep(2.5)

        if _active_streams.get(sim_id, False):
            await websocket.send_json({"type": "complete", "totalEvents": grand_total, "totalDays": total_days})
    except WebSocketDisconnect:
        pass
    except Exception:
        logger.exception("WebSocket stream error for sim %s", sim_id)
    finally:
        control_task.cancel()
        _active_streams.pop(sim_id, None)


if __name__ == "__main__":
    import uvicorn
    uvicorn.run(app, host="0.0.0.0", port=8000)
