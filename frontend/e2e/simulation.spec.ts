import { test, expect } from "@playwright/test";

test.describe("Simulation workflow", () => {
  test.beforeEach(async ({ request }) => {
    const res = await request.get("/api/simulations");
    const sims = await res.json();
    for (const s of sims) {
      await request.delete(`/api/simulations/${s.id}`);
    }
  });

  test("create simulation, stream data, see price chart", async ({ page }) => {
    await page.goto("/");
    await expect(page.locator(".app-header h1")).toBeVisible();

    await page.fill('input[type="text"]', "E2E");
    await page.fill('input[type="number"][min="1"][max="100000"]', "100");

    const durationSelect = page.locator("select").nth(0);
    await durationSelect.selectOption("3600");
    const daysSelect = page.locator("select").nth(1);
    await daysSelect.selectOption("1");

    await page.click('button:has-text("Create Simulation")');

    await expect(page.locator(".sim-symbol")).toContainText("E2E", { timeout: 30_000 });
    await expect(page.locator(".sim-status")).toContainText("ready", { ignoreCase: true });

    await page.click('button:has-text("Replay")');

    await expect(page.locator(".sim-view-header h2")).toContainText("E2E", { timeout: 10_000 });

    await expect(page.locator(".stat-value").first()).toBeVisible({ timeout: 15_000 });

    const bidText = await page.locator(".bid-stat .stat-value").textContent();
    expect(bidText).toMatch(/\$\d+\.\d+/);

    await expect(page.locator(".recharts-wrapper")).toBeVisible({ timeout: 10_000 });
  });

  test("empty symbol disables submit button", async ({ page }) => {
    await page.goto("/");

    await page.fill('input[type="text"]', "");

    await expect(page.locator('button:has-text("Create Simulation")')).toBeDisabled();
  });

  test("duplicate symbol shows error", async ({ page, request }) => {
    await request.post("/api/simulations", {
      data: { symbol: "DUP", seconds: 60, days: 1, seed: 1, p0: 100 },
    });

    await page.goto("/");
    await page.fill('input[type="text"]', "DUP");
    await page.click('button:has-text("Create Simulation")');

    await expect(page.locator(".form-error")).toBeVisible({ timeout: 30_000 });
    await expect(page.locator(".form-error")).toContainText("already exists");
  });
});
