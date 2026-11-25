"use strict";

const assert = require("node:assert/strict");
const { readFile } = require("node:fs/promises");
const path = require("node:path");
const { chromium } = require("playwright");
const { checkGatewayLayout } = require("./gateway_browser_layout.cjs");

const timeout = 10000;
const measurementLimit = 5;
const apiRoot = "/api/v1";

function waitText(page, selector, text) {
  return page.waitForFunction(({ selector, text }) => document.querySelector(selector)?.textContent.includes(text),
    { selector, text }, { timeout });
}

function responseFor(page, endpoint, method = "GET") {
  return page.waitForResponse((response) => new URL(response.url()).pathname === `${apiRoot}/${endpoint}` &&
    response.request().method() === method, { timeout });
}

async function clickAndRead(page, selector, endpoint, method, expectedStatus) {
  const [response] = await Promise.all([responseFor(page, endpoint, method), page.locator(selector).click()]);
  assert.equal(response.status(), expectedStatus, `HTTP status for ${method} ${endpoint}`);
  if (response.ok()) return response.json();
}

function observeSockets(page, origin) {
  const connections = [];
  page.on("websocket", (socket) => {
    const address = new URL(socket.url());
    if (address.host !== origin.host || address.pathname !== `${apiRoot}/stream`) return;
    const connection = {
      plainWebSocket: address.protocol === "ws:" && address.search === "",
      events: [],
      closed: false,
    };
    connections.push(connection);
    socket.on("framereceived", ({ payload }) => {
      try {
        connection.events.push(JSON.parse(payload));
      } catch {
        connection.invalidEvent = true;
      }
    });
    socket.on("close", () => { connection.closed = true; });
  });
  return connections;
}

async function waitFor(label, condition) {
  const end = Date.now() + timeout;
  while (Date.now() < end) {
    const found = await condition();
    if (found) return found;
    await new Promise((resolve) => setTimeout(resolve, 60));
  }
  throw new Error(`Timed out waiting for ${label}`);
}

async function waitStream(page, connections, index) {
  await waitText(page, "#stream-state", "WS 已连接");
  const connection = await waitFor("WS snapshot", () => connections[index]?.events.length && connections[index]);
  assert(connection.plainWebSocket, "browser used direct same-origin WS");
  const [snapshot] = connection.events;
  assert.equal(snapshot.kind, "status");
  assert.equal(snapshot.snapshot, true);
  assert.equal(snapshot.linkConnected, true);
  return connection;
}

function sampleUtc(micros) {
  const time = BigInt(micros);
  return new Date(Number(time / 1000n)).toISOString().replace("Z", `${String(time % 1000n).padStart(3, "0")}Z`);
}

async function verifySensorPanel(page, events, online) {
  const fields = [
    ["temperature", "temperatureC", 2, " °C"], ["humidity", "humidityPercent", 2, " % RH"],
    ["pressure", "pressureHpa", 2, " hPa"], ["distance", "lidarDistanceM", 2, " m"],
    ["latitude", "latitudeDeg", 6, " °"], ["longitude", "longitudeDeg", 6, " °"],
    ["altitude", "heightM", 2, " m"],
  ];
  const sensors = await waitFor("sensor readings and time from the same SkyCore frame", async () => {
    const display = await page.evaluate((ids) => ({
      time: document.querySelector("#reading-time").dateTime,
      values: ids.map((id) => document.getElementById(`reading-${id}`).textContent),
    }), fields.map(([id]) => id));
    const frame = events.find((event) => event.kind === "sensors" &&
      event.sensors && sampleUtc(event.sensors.hostTimeUs) === display.time);
    if (!frame || !fields.every(([, key]) => online ? Number.isFinite(frame.sensors[key]) : frame.sensors[key] === null)) return false;
    for (const [i, [, key, decimals, unit, prefix = ""]] of fields.entries()) {
      const value = frame.sensors[key];
      assert.equal(display.values[i], prefix + (value === null ? "—" : value.toFixed(decimals) + unit), key);
    }
    return frame.sensors;
  });
  console.log("Browser sensor readings match SkyCore", JSON.stringify(sensors));
}

async function verifyDeviceTable(page) {
  const names = await page.locator("#device-rows tr td:first-child").allTextContents();
  for (const name of ["EPSILON", "PTB", "HC2A", "激光雷达", "FPGA"]) assert(names.includes(name), name);
  const dataDevices = ["EPSILON", "PTB", "HC2A", "激光雷达", "TCP 波形", "FPGA", "BMP390", "SHT45"];
  assert(names.every((name) => dataDevices.includes(name)), `unexpected device in ${names}`);
}

async function command(page, selector, skyCommandId) {
  const accepted = await clickAndRead(page, selector, "commands", "POST", 202);
  assert.equal(accepted.status, "accepted");
  assert.equal(accepted.skyCommandId, skyCommandId);
  assert.equal(await page.locator("#command-id").textContent(), accepted.id);
  await waitText(page, "#command-accepted", "网关已受理");
  await waitText(page, "#command-result", "SkyCore 已应答");
  const receipt = await clickAndRead(page, "#command-check", `commands/${accepted.id}`, "GET", 200);
  assert.equal(receipt.id, accepted.id);
  assert.equal(receipt.skyCommandId, skyCommandId);
  assert.equal(receipt.status, "acknowledged");
  assert.equal(receipt.errorCode, 0);
  assert(receipt.sequence > 0 && receipt.completedAt);
  await waitText(page, "#command-result", `序号 ${receipt.sequence}`);
  console.log(`Browser command ${skyCommandId} ${accepted.id} SkyCore ACK ${receipt.sequence}`);
  return receipt;
}

function csvFields(line) {
  // This fixture reads one line per feature. Commas and escaped quotes can occur inside quoted fields.
  return [...line.matchAll(/(?:^|,)(?:"((?:[^"]|"")*)"|([^,]*))/g)]
    .map((match) => match[1] === undefined ? match[2] : match[1].replaceAll('""', '"'));
}

async function verifySession(sessionRoot, sessionName, observed) {
  assert.match(sessionName, /^[\w.-]+$/);
  assert(![".", ".."].includes(sessionName));
  const directory = path.join(sessionRoot, sessionName);
  const manifest = JSON.parse((await readFile(path.join(directory, "session.json"), "utf8")).replace(/^\uFEFF/, ""));
  assert.equal(manifest.state, "complete");
  assert.equal(manifest.session_name, sessionName);
  assert.equal(manifest.recording_origin, "sky");
  assert(Number(manifest.counts.waveform_frames) >= observed.length);
  const csv = (await readFile(path.join(directory, "sensors", "waveform_features.csv"), "utf8")).replace(/^\uFEFF/, "");
  const [header, ...lines] = csv.trimEnd().split(/\r?\n/);
  const keys = csvFields(header);
  const features = new Map();
  for (const line of lines) {
    const fields = csvFields(line);
    assert.equal(fields.length, keys.length);
    const row = Object.fromEntries(keys.map((key, index) => [key, fields[index]]));
    if (row.cycle_id) features.set(`${row.host_time_us}/${row.channel_id}/${row.cycle_id}`, row);
  }
  assert.equal(lines.length, Number(manifest.counts.waveform_feature_rows));
  for (const measurement of observed) {
    const key = `${measurement.hostTimeUs}/${measurement.channel}/${measurement.cycleId}`;
    const row = features.get(key);
    assert(row, `WS feature found by timestamp, channel, and cycle ${key}`);
    assert.equal(Number(row.quality_flags), measurement.qualityFlags);
    const recorded = Number(row.peak);
    assert(Math.abs(recorded - measurement.value) <= Math.max(1e-7, 1e-5 * Math.abs(recorded)), `peak matches for ${key}`);
  }
  console.log(`${sessionName} ${observed.length} WS peaks matched SkyCore, ${manifest.counts.waveform_frames} waveform frames`);
}

async function verify(rawOrigin, sessionRoot) {
  const listener = new URL(rawOrigin);
  assert.equal(listener.protocol, "http:");
  assert.equal(listener.hostname, "127.0.0.1");
  const origin = new URL(listener);
  // Chromium treats loopback names as secure contexts. Resolve an ordinary
  // test hostname to the same local listener to exercise LAN-style HTTP.
  origin.hostname = "vaporview-gateway.test";
  const browser = await chromium.launch({
    executablePath: process.env.VAPORVIEW_TEST_CHROMIUM || undefined,
    args: ["--no-proxy-server", "--host-resolver-rules=MAP vaporview-gateway.test 127.0.0.1"],
  });
  try {
    const page = await browser.newPage({ viewport: { width: 1260, height: 980 } });
    page.setDefaultTimeout(timeout);
    const connections = observeSockets(page, origin);
    const [response, statusResponse] = await Promise.all([
      page.goto(origin.href), responseFor(page, "status"),
    ]);
    assert.equal(response?.status(), 200);
    assert.equal(statusResponse.status(), 200);
    assert.equal(await page.evaluate(() => window.isSecureContext), false);
    assert.equal(await page.evaluate(() => typeof crypto.randomUUID), "undefined");
    const status = await statusResponse.json();
    assert.equal(status.deviceId, "simulated-ground");
    assert.equal(status.linkConnected, true);
    await waitText(page, "h1", "地面网关");
    await waitText(page, "#gateway-state", "可达");
    await waitText(page, "#link-state", "已连接");

    assert(await page.locator("#home-view").isVisible());
    assert(await page.locator("#home-view #peak-trend").isVisible(), "peak trend is on the initial dashboard");
    assert(await page.locator("#home-view #measurement-details summary").isVisible());
    const colors = await page.evaluate(() => {
      const sample = document.createElement("span");
      document.querySelector("#home-controls").append(sample);
      try {
        return ["connected", "disconnected", "connecting", "warning", "error", "disabled"].map(state => {
          displayDeviceState(sample, state);
          return getComputedStyle(sample).color;
        });
      } finally { sample.remove(); }
    });
    assert.equal(new Set(colors).size, colors.length, "connection, warning and fault states have distinct colors");
    await page.locator('nav a[href="#gateway"]').click();
    await waitText(page, "#out-state", "未启用");
    await page.locator('nav a[href="#home"]').click();
    const controls = await waitFor("production desktop controls", async () => {
      const data = await page.evaluate(async () => (await fetch("/api/v1/operations")).json());
      return data.controls.some(row => row.enabled && row.connected) && data.controls;
    });
    for (const key of ["epsilon", "ptb", "hc2a", "lidar", "wave"]) {
      assert(controls.some(row => row.key === key), `desktop control available: ${key}`);
      const selector = `#home-controls [data-key="${key}"]`;
      const row = page.locator(selector);
      for (const [commandId, state] of [[22, "未连接"], [21, "已连接"]]) {
        const accepted = await clickAndRead(page, selector + " button", "commands", "POST", 202);
        assert.equal(accepted.targetDevice, key);
        assert.equal(accepted.skyCommandId, commandId);
        await waitText(page, "#device-feedback", "已确认设备状态");
        if (commandId === 22) await row.getByText(state, {exact: true}).waitFor();
        else await row.locator('[data-state="connected"], [data-state="warning"]').waitFor();
      }
      console.log("Desktop device disconnected and reconnected:", key);
    }
    await page.locator('nav a[href="#gateway"]').click();
    await command(page, "#command-status", 10);
    const first = await waitStream(page, connections, 0);
    await waitText(page, "#telemetry-state", "已收到");
    await waitText(page, "#recording-state", "停止");
    await page.locator("#disconnect-stream").click();
    await waitText(page, "#stream-state", "已断开");
    await waitFor("closing first WS connection", () => first.closed);

    await command(page, "#command-start", 1);
    await page.locator("#connect-stream").click();
    const second = await waitStream(page, connections, 1);
    assert.equal(second.events[0].streamId, first.events[0].streamId);
    await waitText(page, "#recording-state", "记录中");
    const sessionName = await page.locator("#session-name").textContent();
    assert(sessionName && sessionName !== "—");
    const observed = await waitFor("FPGA measurements during recording", () => {
      const events = second.events.filter((event) => event.kind === "measurement" && event.source === "fpga-cycle-feature" && event.valid);
      return events.length >= measurementLimit && events.slice(0, measurementLimit);
    });
    await page.locator('nav a[href="#home"]').click();
    await verifySensorPanel(page, second.events, true);
    await page.waitForFunction(() => [...document.querySelectorAll("#fpga-plots canvas")].some(c => Number(c.dataset.points) > 0));
    assert(second.events.some(e => e.kind === "waveform" && e.source === "fpga" && e.points.length));
    if (process.env.VAPORVIEW_TEST_BROWSER_SCREENSHOT)
      await page.screenshot({ path: process.env.VAPORVIEW_TEST_BROWSER_SCREENSHOT, fullPage: true });
    await page.locator('nav a[href="#gateway"]').click();
    await verifyDeviceTable(page);
    assert(!second.invalidEvent);
    for (let i = 1; i < second.events.length; ++i) {
      assert.equal(second.events[i].streamId, second.events[0].streamId);
      assert(BigInt(second.events[i].sequence) > BigInt(second.events[i - 1].sequence));
    }
    for (const [channel, wavelength] of [[48, "1368 nm"], [49, "816 nm"]]) {
      const track = `fpga-cycle-feature/${channel}`;
      await waitText(page, `#trend-channel option[value="${track}"]`, wavelength);
      await page.locator("#trend-channel").selectOption(track);
      await waitFor(`rendered peak and sample time match channel ${channel}`, async () => {
        const reading = await page.evaluate(() => ({
          value: document.querySelector("#trend-latest-value").textContent,
          quality: document.querySelector("#trend-latest-quality").textContent,
          time: document.querySelector("#trend-sample-time").dateTime,
        }));
        return second.events.some((event) => {
          if (event.source !== "fpga-cycle-feature" || event.channel !== channel || !event.valid) return false;
          const sampled = sampleUtc(event.hostTimeUs);
          return reading.time === sampled && reading.value === String(event.value) && reading.quality.includes(`质量位 ${event.qualityFlags}`);
        });
      });
    }
    assert.equal(await page.locator("#measurement-details").evaluate((details) => details.open), false);
    await page.locator("#measurement-details summary").click();
    await page.locator("#measurement-rows tr td:nth-child(7)").first().waitFor();
    const rendered = await page.locator("#measurement-rows tr").evaluateAll(
      (rows) => rows.map((row) => [...row.cells].map((cell) => cell.textContent)),
    );
    assert(rendered.length <= 50);
    const event = second.events.find((entry) => entry.kind === "measurement" && entry.sequence === rendered[0][0]);
    assert(event, "rendered sequence came from the browser's WS connection");
    assert.equal(rendered[0][3], String(event.channel));
    assert.equal(rendered[0][4], event.valid ? `${event.value} ${event.unit}` : "无有效峰值");
    assert.match(rendered[0][5], /\.\d{6}Z$/);
    assert.equal(rendered[0][6], String(event.qualityFlags));
    const latest = await clickAndRead(page, "#refresh-latest", "measurements/latest", "GET", 200);
    assert.equal(latest.linkConnected, true);
    assert.equal(latest.measurement?.kind, "measurement");
    await waitText(page, "#latest-measurement", `通道 ${latest.measurement.channel}`);

    await command(page, "#command-stop", 3);
    await waitText(page, "#recording-state", "停止");
    await clickAndRead(page, "#refresh-status", "status", "GET", 200);
    await verifySession(sessionRoot, sessionName, observed);
    const fpga = page.locator("#device-rows tr").filter({ has: page.getByRole("cell", { name: "FPGA", exact: true }) });
    for (const [selector, commandId, state, label] of [
      ["#command-fpga-disconnect", 22, 1, "未连接"],
      ["#command-fpga-connect", 21, 3, "已连接"],
    ]) {
      const firstEvent = second.events.length;
      await command(page, selector, commandId);
      await waitFor(`FPGA telemetry state ${state}`, () => second.events.slice(firstEvent).some((event) =>
        event.kind === "status" && event.devices?.some((device) => device.device === 8 && device.state === state)));
      await fpga.getByRole("cell", { name: label, exact: true }).waitFor();
      await verifySensorPanel(page, second.events, state === 3);
    }
    const afterReconnect = second.events.length;
    await waitFor("fresh FPGA peak after reconnection", () => second.events.slice(afterReconnect).some((event) =>
      event.kind === "measurement" && event.source === "fpga-cycle-feature" && event.valid));
    await page.setViewportSize({ width: 390, height: 844 });
    assert(await page.evaluate(() => document.documentElement.scrollWidth <= innerWidth), "phone has no page-level horizontal overflow");
    await page.setViewportSize({ width: 1260, height: 980 });
    await page.locator("#disconnect-stream").click();
    await waitFor("closing second WS connection", () => second.closed);
    await checkGatewayLayout(browser, origin.href);
  } finally {
    await browser.close();
  }
}

verify(...process.argv.slice(2)).catch((error) => {
  console.error(error.stack ?? error);
  process.exitCode = 1;
});
