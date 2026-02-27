"""API integration tests for the QRSDP simulation backend."""

import pytest
from httpx import ASGITransport, AsyncClient

from api.main import app, _simulations, OUTPUT_DIR, RUN_BIN


@pytest.fixture(autouse=True)
def clean_state():
    _simulations.clear()
    yield
    _simulations.clear()


@pytest.fixture
async def client():
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as c:
        yield c


# --- Presets ---

@pytest.mark.anyio
async def test_presets_returns_all(client):
    r = await client.get("/api/presets")
    assert r.status_code == 200
    data = r.json()
    assert "simple_high_exec" in data
    assert "hlr_default" in data
    assert data["simple_high_exec"]["model"] == "simple"


# --- Validation ---

@pytest.mark.anyio
async def test_create_rejects_empty_symbol(client):
    r = await client.post("/api/simulations", json={"symbol": ""})
    assert r.status_code == 422


@pytest.mark.anyio
async def test_create_rejects_invalid_symbol(client):
    r = await client.post("/api/simulations", json={"symbol": "../etc"})
    assert r.status_code == 422


@pytest.mark.anyio
async def test_create_rejects_symbol_with_spaces(client):
    r = await client.post("/api/simulations", json={"symbol": "AA BB"})
    assert r.status_code == 422


@pytest.mark.anyio
async def test_create_rejects_negative_days(client):
    r = await client.post("/api/simulations", json={"symbol": "AAPL", "days": -1})
    assert r.status_code == 422


@pytest.mark.anyio
async def test_create_rejects_zero_p0(client):
    r = await client.post("/api/simulations", json={"symbol": "AAPL", "p0": 0})
    assert r.status_code == 422


@pytest.mark.anyio
async def test_create_rejects_unknown_preset(client):
    r = await client.post("/api/simulations", json={"symbol": "AAPL", "preset": "does_not_exist"})
    assert r.status_code == 422


@pytest.mark.anyio
async def test_create_rejects_invalid_model(client):
    r = await client.post("/api/simulations", json={"symbol": "AAPL", "model": "evil"})
    assert r.status_code == 400


# --- CRUD (requires C++ binary) ---

@pytest.mark.anyio
@pytest.mark.skipif(not RUN_BIN.exists(), reason="C++ binary not built")
async def test_create_list_delete_flow(client):
    r = await client.post("/api/simulations", json={
        "symbol": "TEST",
        "seconds": 60,
        "days": 1,
        "seed": 1,
        "p0": 100,
    })
    assert r.status_code == 200
    sim = r.json()
    assert sim["symbol"] == "TEST"
    assert sim["status"] == "ready"
    assert sim["total_events"] > 0
    sim_id = sim["id"]

    r = await client.get("/api/simulations")
    assert r.status_code == 200
    sims = r.json()
    assert any(s["id"] == sim_id for s in sims)

    r = await client.get(f"/api/simulations/{sim_id}")
    assert r.status_code == 200
    assert r.json()["id"] == sim_id

    r = await client.delete(f"/api/simulations/{sim_id}")
    assert r.status_code == 200
    assert r.json()["deleted"] == sim_id

    r = await client.get(f"/api/simulations/{sim_id}")
    assert r.status_code == 404


@pytest.mark.anyio
@pytest.mark.skipif(not RUN_BIN.exists(), reason="C++ binary not built")
async def test_duplicate_symbol_rejected(client):
    r = await client.post("/api/simulations", json={
        "symbol": "DUP", "seconds": 60, "days": 1, "seed": 1, "p0": 100,
    })
    assert r.status_code == 200

    r = await client.post("/api/simulations", json={
        "symbol": "DUP", "seconds": 60, "days": 1, "seed": 2, "p0": 100,
    })
    assert r.status_code == 409


@pytest.mark.anyio
async def test_delete_nonexistent_returns_404(client):
    r = await client.delete("/api/simulations/does_not_exist")
    assert r.status_code == 404


@pytest.mark.anyio
async def test_get_nonexistent_returns_404(client):
    r = await client.get("/api/simulations/does_not_exist")
    assert r.status_code == 404


# --- Response shape ---

@pytest.mark.anyio
@pytest.mark.skipif(not RUN_BIN.exists(), reason="C++ binary not built")
async def test_simulation_response_has_no_internal_fields(client):
    r = await client.post("/api/simulations", json={
        "symbol": "SHAPE", "seconds": 60, "days": 1, "seed": 1, "p0": 100,
    })
    sim = r.json()
    assert "run_dir" not in sim
    assert "header_sample" not in sim
    expected_keys = {"id", "symbol", "seconds", "days", "seed", "p0", "status", "total_events", "preset"}
    assert set(sim.keys()) == expected_keys
