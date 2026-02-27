import { defineConfig } from "@playwright/test";

export default defineConfig({
  testDir: "./e2e",
  timeout: 60_000,
  retries: 0,
  use: {
    baseURL: "http://localhost:5173",
    headless: true,
  },
  projects: [
    { name: "chromium", use: { browserName: "chromium" } },
  ],
  webServer: [
    {
      command: "cd /workspace && source notebooks/venv/bin/activate && python -m uvicorn api.main:app --host 0.0.0.0 --port 8000",
      port: 8000,
      reuseExistingServer: true,
      timeout: 15_000,
    },
    {
      command: "npx vite --host 0.0.0.0 --port 5173",
      port: 5173,
      reuseExistingServer: true,
      timeout: 15_000,
    },
  ],
});
