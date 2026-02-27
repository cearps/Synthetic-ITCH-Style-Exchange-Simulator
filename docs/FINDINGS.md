# Security & Reliability Audit Findings

## Stack Summary

| Layer | Technology |
|---|---|
| Frontend | React 18 + TypeScript + Vite + Recharts |
| Backend | Python FastAPI (async) |
| Data | In-memory dict (no DB), C++ subprocess for simulation |
| Auth | None (local dev tool) |
| Tests (before) | C++ Google Test only; **zero** API or frontend tests |
| Tests (after) | 14 API integration tests, 3 Playwright E2E tests |

## Top Risks Found & Fixed

### P0 — Security

| # | Issue | Fix |
|---|---|---|
| 1 | **Symbol not validated** — free string used in subprocess args and as directory name. Path traversal possible (e.g. `../../etc`). | Added `^[A-Z0-9]{1,8}$` regex validator on Pydantic model. |
| 2 | **No input bounds** — seconds, days, speed, p0 all unbounded. speed=0 causes divide-by-zero; days=99999 causes disk/time exhaustion. | Added Pydantic `field_validator` for all numeric fields with documented bounds. |
| 3 | **subprocess.run blocks event loop** — synchronous blocking call in async endpoint. Large simulations block all other requests. | Replaced with `asyncio.create_subprocess_exec` + `asyncio.wait_for` with 600s timeout. |
| 4 | **subprocess stderr leaked to client** — internal file paths and error details exposed in HTTP 500 response. | Logged server-side only; client receives generic error message. |
| 5 | **`_pub()` denylist** — used field exclusion instead of inclusion. New internal fields would silently leak to API responses. | Replaced with `SIM_PUBLIC_FIELDS` allowlist. |
| 6 | **shutil.rmtree unguarded** — `run_dir` path from in-memory dict passed directly to rmtree. | Added check: `OUTPUT_DIR in p.parents` before deletion. |

### P1 — Reliability

| # | Issue | Fix |
|---|---|---|
| 7 | **Frontend: uncaught fetch errors** — `refreshSims` did `await res.json()` without checking `res.ok`. | Added try/catch, check `res.ok`. |
| 8 | **Frontend: WebSocket JSON.parse unguarded** — malformed message would crash the app. | Wrapped in try/catch. |
| 9 | **Frontend: no `onerror` handler on WebSocket** — connection failures left stale `streaming` state. | Added `onerror` handler, clear wsRef on close. |
| 10 | **Frontend: delete doesn't check response** — optimistic UI update even on server error. | Check `res.ok` before removing from list. |
| 11 | **Frontend: presets fetch error swallowed** — empty dropdown with no user feedback. | Show error message to user on failure. |
| 12 | **Backend: `on_event` deprecated** — FastAPI deprecation warning on startup. | Replaced with `lifespan` context manager. |

### P2 — Hygiene

| # | Issue | Status |
|---|---|---|
| 13 | Unused `Preset` import in App.tsx | Fixed |
| 14 | Price rounding inconsistency (2 vs 4 decimals) | Fixed: all prices use 4 decimal rounding |
| 15 | CORS wildcard `allow_origins=["*"]` | Documented; acceptable for local dev tool |
| 16 | In-memory state lost on restart | Documented; acceptable for simulation tool (not persistent data) |

## How to Run Tests

```bash
# C++ unit tests (127 tests)
./build/tests

# API integration tests (14 tests)
cd /workspace
source notebooks/venv/bin/activate
python -m pytest api/test_api.py -v

# E2E tests (3 tests) — starts servers automatically
cd /workspace/frontend
npx playwright test

# TypeScript type check
cd /workspace/frontend
npx tsc --noEmit
```

## Commit Summary

| # | Commit | Changes |
|---|---|---|
| 1 | `fix(api): input validation, async subprocess, sanitized error responses` | Backend security: validators, async subprocess, allowlist, guarded rmtree |
| 2 | `fix(frontend): error handling, WebSocket robustness, remove unused import` | Frontend: try/catch on all fetches, WebSocket onerror, guarded JSON parse |
| 3 | `test(api): 14 integration tests + fix on_event deprecation` | API tests: validation, CRUD, duplicate, 404, response shape. Lifespan fix. |
| 4 | `test(e2e): 3 Playwright E2E tests for core simulation workflow` | E2E: happy path, validation, duplicate error |
| 5 | `docs: audit findings, test instructions` | This document |
