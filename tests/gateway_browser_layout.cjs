"use strict";

const assert = require("node:assert/strict");

// Hold the markup at each extreme so both viewports see the same transition,
// independent of which telemetry frame arrives while Chromium measures it.
async function checkGatewayLayout(browser, origin) {
  const page = await browser.newPage();
  await page.route("**/app.js", (route) => route.fulfill({ contentType: "text/javascript", body: "" }));
  try {
    for (const width of [1260, 390]) {
      await page.setViewportSize({ width, height: 900 });
      await page.goto(origin);
      const shifts = await page.evaluate(async () => {
        await document.fonts.ready;
        document.querySelector("#measurement-details").open = true;
        const anchors = [".workspace-grid", "#peak-trend", "#command-check", "#device-title", "#measurement-details", ".stream-table"];
        const positions = () => anchors.map((selector) => document.querySelector(selector).getBoundingClientRect().top);
        const before = positions();
        const beforeHeight = document.documentElement.scrollHeight;
        const fill = (id, text) => { document.getElementById(id).textContent = text; };
        fill("session-name", "session_2026-11-20_03-29-59_simulated_ground");
        fill("device-id", "ground-unit-".repeat(9));
        fill("command-id", "0a".repeat(32));
        fill("command-result", "SkyCore 已拒绝 · 请在地面端检查设备连接和定标文件。".repeat(6));
        fill("trend-window", "03:29:59–03:30:59 UTC · 60 秒");
        fill("trend-latest-value", "0.20514437364975271");
        fill("trend-latest-quality", "样本无效 · 质量位 4294967295 · 断开前样本");
        fill("latest-measurement", "通道 49 · 0.20514437364975271 native · 2026-11-20T03:29:59.123456Z · 质量位 4294967295");
        for (const reading of document.querySelectorAll(".reading-value")) reading.textContent = "−123.456789";
        for (const [id, count] of [["device-rows", 8], ["measurement-rows", 50]]) {
          const table = document.getElementById(id);
          const cols = table.closest("table").tHead.rows[0].cells.length;
          table.replaceChildren();
          for (let row = 0; row < count; row++) {
            const entry = table.insertRow();
            for (let col = 0; col < cols; col++) entry.insertCell().textContent = "123456789";
          }
        }
        const after = positions();
        return Object.fromEntries([...anchors.map((name, i) => [name, after[i] - before[i]]),
          ["document height", document.documentElement.scrollHeight - beforeHeight]]);
      });
      console.log(`Gateway layout ${width}px: ${JSON.stringify(shifts)}`);
      for (const [anchor, delta] of Object.entries(shifts)) assert(Math.abs(delta) <= 1, `${width}px: ${anchor} shifted by ${delta}px`);
    }
  } finally {
    await page.close();
  }
}

module.exports = { checkGatewayLayout };
