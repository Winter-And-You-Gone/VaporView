"use strict";

const assert = require("node:assert/strict");
const { createHash, randomBytes, X509Certificate } = require("node:crypto");
const { readFile } = require("node:fs/promises");
const path = require("node:path");
const { chromium } = require("playwright");

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
      encrypted: address.protocol === "wss:",
      usedTicket: Boolean(address.searchParams.get("ticket")),
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

async function connectStream(page, connections) {
  const previousCount = connections.length;
  const [ticketResponse] = await Promise.all([
    responseFor(page, "stream-ticket", "POST"), page.locator("#connect-stream").click(),
  ]);
  assert.equal(ticketResponse.status(), 200);
  await waitText(page, "#stream-state", "WSS 已连接");
  const connection = await waitFor("WSS snapshot", () => connections[previousCount]?.events.length && connections[previousCount]);
  assert(connection.encrypted && connection.usedTicket, "browser used same-origin WSS with a ticket");
  const [snapshot] = connection.events;
  assert.equal(snapshot.kind, "status");
  assert.equal(snapshot.snapshot, true);
  assert.equal(snapshot.linkConnected, true);
  return connection;
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
    assert(row, `WSS feature found by timestamp, channel, and cycle ${key}`);
    assert.equal(Number(row.quality_flags), measurement.qualityFlags);
    const recorded = Number(row.peak);
    assert(Math.abs(recorded - measurement.value) <= Math.max(1e-7, 1e-5 * Math.abs(recorded)), `peak matches for ${key}`);
  }
  console.log(`${sessionName} ${observed.length} WSS peaks matched SkyCore, ${manifest.counts.waveform_frames} waveform frames`);
}

async function verify(rawOrigin, certificate, sessionRoot) {
  const token = process.env.VAPORVIEW_GATEWAY_TOKEN;
  assert(token, "C++ runner supplies VAPORVIEW_GATEWAY_TOKEN");
  const origin = new URL(rawOrigin);
  assert.equal(origin.protocol, "https:");
  assert.equal(origin.hostname, "127.0.0.1");
  const cert = new X509Certificate(await readFile(certificate));
  const spki = cert.publicKey.export({ type: "spki", format: "der" });
  const pin = createHash("sha256").update(spki).digest("base64");
  const browser = await chromium.launch({
    executablePath: process.env.VAPORVIEW_TEST_CHROMIUM || undefined,
    args: [`--ignore-certificate-errors-spki-list=${pin}`],
  });
  try {
    const page = await browser.newPage({ viewport: { width: 1260, height: 980 } });
    page.setDefaultTimeout(timeout);
    const connections = observeSockets(page, origin);
    const response = await page.goto(origin.href);
    assert.equal(response?.status(), 200);
    await waitText(page, "h1", "地面网关");

    await page.locator("#token").fill(randomBytes(32).toString("hex"));
    await clickAndRead(page, "#apply-token", "status", "GET", 401);
    await waitText(page, "#api-feedback", "入站令牌无效");
    await waitText(page, "#gateway-state", "认证失败");

    await page.locator("#token").fill(token);
    const status = await clickAndRead(page, "#apply-token", "status", "GET", 200);
    assert.equal(status.deviceId, "simulated-ground");
    assert.equal(status.linkConnected, true);
    assert.equal(await page.locator("#token").inputValue(), "");
    await waitText(page, "#gateway-state", "可达");
    await waitText(page, "#link-state", "已连接");

    await command(page, "#command-status", 10);
    const first = await connectStream(page, connections);
    await waitText(page, "#telemetry-state", "已收到");
    await waitText(page, "#recording-state", "停止");
    await page.locator("#disconnect-stream").click();
    await waitText(page, "#stream-state", "已断开");
    await waitFor("closing first WSS connection", () => first.closed);

    await command(page, "#command-start", 1);
    const second = await connectStream(page, connections);
    assert.equal(second.events[0].streamId, first.events[0].streamId);
    await waitText(page, "#recording-state", "记录中");
    const sessionName = await page.locator("#session-name").textContent();
    assert(sessionName && sessionName !== "—");
    const observed = await waitFor("FPGA measurements during recording", () => {
      const events = second.events.filter((event) => event.kind === "measurement" && event.source === "fpga-cycle-feature" && event.valid);
      return events.length >= measurementLimit && events.slice(0, measurementLimit);
    });
    assert(!second.invalidEvent);
    for (let i = 1; i < second.events.length; ++i) {
      assert.equal(second.events[i].streamId, second.events[0].streamId);
      assert(BigInt(second.events[i].sequence) > BigInt(second.events[i - 1].sequence));
    }
    await page.locator("#measurement-rows tr td:nth-child(7)").first().waitFor();
    const rendered = await page.locator("#measurement-rows tr").evaluateAll(
      (rows) => rows.map((row) => [...row.cells].map((cell) => cell.textContent)),
    );
    assert(rendered.length <= 50);
    const event = second.events.find((entry) => entry.kind === "measurement" && entry.sequence === rendered[0][0]);
    assert(event, "rendered sequence came from the browser's WSS connection");
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
    await page.locator("#disconnect-stream").click();
    await waitFor("closing second WSS connection", () => second.closed);
    if (process.env.VAPORVIEW_TEST_BROWSER_SCREENSHOT) {
      await page.screenshot({ path: process.env.VAPORVIEW_TEST_BROWSER_SCREENSHOT, fullPage: true });
    }
  } finally {
    await browser.close();
  }
}

verify(...process.argv.slice(2)).catch((error) => {
  const token = process.env.VAPORVIEW_GATEWAY_TOKEN;
  const safeMessage = String(error.stack ?? error).replace(/([?&]ticket=)[^\s&'"\])]+/gi, "$1[redacted]");
  console.error(token ? safeMessage.replaceAll(token, "[redacted]") : safeMessage);
  process.exitCode = 1;
});
