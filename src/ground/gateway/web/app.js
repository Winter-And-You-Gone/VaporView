"use strict";

const element = (id) => document.getElementById(id);
const commandButtons = document.querySelectorAll("[data-command]");
const measurementRows = element("measurement-rows");
const trendChannel = element("trend-channel");
const trendCanvas = element("peak-trend");
const trendDurationUs = 60_000_000n;
const trendChannels = new Map();
const deviceNames = { 1: "EPSILON", 2: "PTB", 3: "HC2A", 4: "激光雷达", 5: "TCP 波形", 8: "FPGA", 9: "BMP390", 10: "SHT45" };
const deviceStates = ["已禁用", "未连接", "连接中", "已连接", "故障", "重连中"];
const apiErrors = {
  "invalid-command": "命令参数格式无效",
  "expired-or-expiry-out-of-range": "命令期限无效，请核对地面机与浏览器时钟",
  "forbidden-origin": "请求来源不匹配，请直接打开地面网关页面",
  forbidden: "该命令不受地面网关支持",
  "command-not-found": "网关没有此命令的回执",
};
const requestErrors = {
  400: "网关拒绝了请求参数，请核对地面机时钟",
  403: "地面网关拒绝此请求",
  404: "网关未找到该资源或命令",
  409: "命令 ID 与已有命令冲突",
  429: "网关当前请求过多",
  503: "网关服务暂不可用",
};
const receiptLabels = {
  acknowledged: "SkyCore 已应答",
  rejected: "SkyCore 已拒绝",
  timeout: "等待 SkyCore 回执超时",
  disconnected: "机地链路已断开",
  busy: "网关繁忙",
  expired: "命令已过期",
  "not-sent": "命令未发送",
  "device-busy": "设备繁忙",
  "calibration-required": "需要在地面端准备定标参数",
};

let statusRequest = 0;
let latestRequest = 0;
let statusRevision = 0;
let sensorRevision = 0;
let stream = null;
let currentCommand = null;
let linkConnected = false;
let fpgaConnected = false;
let latestTrendTime = null;
let trendPaint = 0;

function show(id, text, tone = "") {
  const target = element(id);
  target.textContent = text;
  target.dataset.tone = tone;
}

function label(value) {
  return typeof value === "string" || typeof value === "number" ? String(value) : "—";
}

function syncButtons() {
  element("connect-stream").disabled = stream !== null;
  element("disconnect-stream").disabled = stream === null;
  element("command-check").disabled = !currentCommand || currentCommand.checking || currentCommand.sending;
  for (const button of commandButtons) {
    button.disabled = !linkConnected || Boolean(currentCommand?.sending || currentCommand?.pending);
  }
}

async function request(path, { method = "GET", body } = {}) {
  const headers = body ? { "Content-Type": "application/json" } : undefined;
  const deadline = AbortSignal.timeout(15000);
  let response;
  try {
    response = await fetch(path, {
      method, headers, body: body ? JSON.stringify(body) : undefined,
      signal: deadline, cache: "no-store", redirect: "error",
    });
  } catch {
    show("gateway-state", "不可达", "error");
    if (!stream || stream.socket?.readyState !== WebSocket.OPEN) {
      linkConnected = false;
      show("link-state", "未知");
      syncButtons();
    }
    throw new Error(deadline.aborted ? "HTTP 请求超时" : "HTTP 无响应，请检查网络和监听地址");
  }
  show("gateway-state", "可达", "good");
  const payload = await response.json().catch(() => null);
  if (response.ok && payload !== null) return payload;
  if (response.ok) throw new Error("网关返回了无效 JSON");
  const message = Object.hasOwn(apiErrors, payload?.error) ? apiErrors[payload.error] : requestErrors[response.status];
  throw new Error(`HTTP ${response.status} · ${message ?? "请求失败"}`);
}

function placeholder(body, count, text) {
  const row = document.createElement("tr");
  const cell = row.insertCell();
  cell.colSpan = count;
  cell.textContent = text;
  body.replaceChildren(row);
}

function appendCells(row, values) {
  for (const value of values) row.insertCell().textContent = label(value);
}

function renderLink(status) {
  show("device-id", label(status.deviceId));
  linkConnected = status.linkConnected === true;
  show("link-state", linkConnected ? "已连接" : "未连接", linkConnected ? "good" : "");
  scheduleTrend();
  syncButtons();
}

function renderStatus(status, updateReadings = true) {
  statusRevision += 1;
  renderLink(status);
  if (updateReadings) renderSensors(linkConnected ? status.sensors : null);
  const hasStatus = status.telemetryAvailable === true;
  show("telemetry-state", hasStatus ? "已收到" : "暂无状态", hasStatus ? "good" : "");
  show("recording-state", hasStatus ? (["停止", "记录中", "暂停"][status.recordingState] ?? label(status.recordingState)) : "—");
  show("session-name", hasStatus ? label(status.sessionName || null) : "—");
  fpgaConnected = hasStatus && Array.isArray(status.devices) &&
    status.devices.some((device) => device.device === 8 && device.state === 3);
  if (!linkConnected) dashboard.clear();
  else if (hasStatus && !fpgaConnected) dashboard.clearFpga();
  const devices = element("device-rows");
  const visibleDevices = hasStatus && Array.isArray(status.devices)
    ? status.devices.filter((device) => Object.hasOwn(deviceNames, device.device)) : [];
  if (visibleDevices.length === 0) {
    placeholder(devices, 5, "暂无设备状态");
    return;
  }
  devices.replaceChildren();
  for (const device of visibleDevices) {
    const row = devices.insertRow();
    appendCells(row, [deviceNames[device.device],
      deviceStates[device.state] ?? `状态 ${label(device.state)}`, device.receivedCount, device.errorCount, device.errorCode]);
    row.cells[1].dataset.tone = device.state === 3 ? "good" : device.state === 4 ? "error" : "";
  }
}

function renderSensors(sensors) {
  sensorRevision += 1;
  const readings = [
    ["temperature", sensors?.temperatureC, 2, "°C"],
    ["humidity", sensors?.humidityPercent, 2, "% RH"],
    ["pressure", sensors?.pressureHpa, 2, "hPa"],
    ["latitude", sensors?.latitudeDeg, 6, "°"],
    ["longitude", sensors?.longitudeDeg, 6, "°"],
    ["altitude", sensors?.heightM, 2, "m"],
    ["ecef-x", sensors?.ecefX, 2, "m"],
    ["ecef-y", sensors?.ecefY, 2, "m"],
    ["ecef-z", sensors?.ecefZ, 2, "m"],
    ["satellites", sensors?.satellites, 0, "颗"],
    ["hacc", sensors?.horizontalAccuracyM, 2, "m"],
    ["vacc", sensors?.verticalAccuracyM, 2, "m"],
    ["distance", sensors?.lidarDistanceM, 2, "m"],
  ];
  for (const [id, value, precision, unit, prefix = ""] of readings) {
    const reading = Number.isFinite(value) ? `${value.toFixed(precision)} ${unit}` : "—";
    show(`reading-${id}`, `${prefix}${reading}`);
  }
  const time = element("reading-time");
  const stamp = utcTime(sensors?.hostTimeUs);
  time.dateTime = stamp === "—" ? "" : stamp;
  time.textContent = stamp !== "—" ? `采样 ${stamp}` : linkConnected ? "等待测量" : "机地数传未连接";
}

function parseMicros(value) {
  return typeof value === "string" && /^\d{1,20}$/.test(value) ? BigInt(value) : null;
}

function utcTime(micros) {
  const time = parseMicros(micros);
  if (time === null) return "—";
  const date = new Date(Number(time / 1000n));
  return Number.isNaN(date.getTime()) ? "—" : date.toISOString().replace(/Z$/, `${String(time % 1000n).padStart(3, "0")}Z`);
}

function peak(measurement) {
  if (measurement.valid !== true || typeof measurement.value !== "number") return "无有效峰值";
  return `${measurement.value} ${label(measurement.unit)}`;
}

function scheduleTrend() {
  if (!trendPaint) trendPaint = requestAnimationFrame(drawTrend);
}

function resetTrend() {
  trendChannels.clear();
  latestTrendTime = null;
  trendChannel.replaceChildren(new Option("等待测量通道"));
  trendChannel.disabled = true;
  scheduleTrend();
}

function trendChannelLabel(source, channel) {
  if (source !== "fpga-cycle-feature") return `TCP 波形 · 通道 ${channel}`;
  const hex = `0x${channel.toString(16).padStart(2, "0")}`;
  if (channel === 0x31) return `FPGA · 816 nm（${hex}）`;
  if (channel === 0x30) return `FPGA · 1368 nm（${hex}）`;
  return `FPGA · 通道 ${hex}`;
}

function addTrendPoint(message) {
  const time = parseMicros(message.hostTimeUs ?? message.timeUs);
  if (time === null || time < trendDurationUs || utcTime(String(time)) === "—" ||
      !Number.isInteger(message.channel) || message.channel < 0 || message.channel > 65535 ||
      !["fpga-cycle-feature", "waveform-feature"].includes(message.source) || message.unit !== "native") return;

  if (latestTrendTime === null || time > latestTrendTime) latestTrendTime = time;
  if (time < latestTrendTime - trendDurationUs) return;

  const key = `${message.source}/${message.channel}`;
  if (!trendChannels.has(key)) {
    if (trendChannels.size >= 32) return;
    if (trendChannel.disabled) trendChannel.replaceChildren();
    trendChannel.add(new Option(trendChannelLabel(message.source, message.channel), key));
    trendChannels.set(key, []);
    trendChannel.disabled = false;
  }
  const points = trendChannels.get(key);
  const value = message.valid === true && Number.isFinite(message.value) ? message.value : null;
  points.push({ time, value, quality: message.qualityFlags });
  if (points.length > 1200) points.shift();
  scheduleTrend();
}

function renderTrendReading(points) {
  const latest = points.reduce((newest, point) => newest === null || point.time >= newest.time ? point : newest, null);
  const timestamp = element("trend-sample-time");
  if (latest === null) {
    timestamp.removeAttribute("datetime");
    timestamp.textContent = "—";
    show("trend-latest-value", "—");
    const reason = !stream ? "订阅未连接" : linkConnected ? "等待当前通道测量" : "等待机地数传连接";
    show("trend-latest-quality", reason);
    return;
  }
  const quality = `质量位 ${label(latest.quality)}`;
  timestamp.dateTime = utcTime(String(latest.time));
  timestamp.textContent = timestamp.dateTime.slice(11, 23);
  const offline = !stream || !linkConnected || (trendChannel.value.startsWith("fpga-cycle-feature/") && !fpgaConnected);
  const previous = offline ? " · 断开前样本" : "";
  show("trend-latest-value", latest.value === null ? "无有效峰值" : label(latest.value));
  if (latest.value === null) show("trend-latest-quality", `样本无效 · ${quality}${previous}`, "error");
  else if (latest.quality === 0) show("trend-latest-quality", `有效 · ${quality}${previous}`, "good");
  else show("trend-latest-quality", `有质量标记 · ${quality}${previous}`, "warning");
}

function drawTrend() {
  trendPaint = 0;
  const width = trendCanvas.clientWidth;
  const height = trendCanvas.clientHeight;
  if (!width || !height) return;
  const scale = window.devicePixelRatio || 1;
  const pixelWidth = Math.round(width * scale);
  const pixelHeight = Math.round(height * scale);
  if (trendCanvas.width !== pixelWidth) trendCanvas.width = pixelWidth;
  if (trendCanvas.height !== pixelHeight) trendCanvas.height = pixelHeight;
  const context = trendCanvas.getContext("2d");
  context.setTransform(scale, 0, 0, scale, 0, 0);
  context.clearRect(0, 0, width, height);
  context.font = "11px Consolas, monospace";

  const end = latestTrendTime;
  const start = end === null ? null : end - trendDurationUs;
  const points = (trendChannels.get(trendChannel.value) ?? []).filter((point) => point.time >= start);
  renderTrendReading(points);
  const valid = points.filter((point) => point.value !== null);
  const marked = valid.filter((point) => point.quality !== 0).length;
  const invalid = points.length - valid.length;
  const summary = element("trend-summary");
  summary.dataset.points = String(valid.length);
  summary.dataset.qualityMarked = String(marked);
  summary.dataset.invalid = String(invalid);
  summary.textContent = points.length
    ? `${valid.length} 个峰值 · 其中 ${marked} 个有质量标记 · ${invalid} 个无有效峰值`
    : "等待当前通道的峰值";
  const windowLabel = element("trend-window");
  const startLabel = start === null ? "" : utcTime(String(start));
  const endLabel = end === null ? "" : utcTime(String(end));
  windowLabel.textContent = end === null
    ? "最近 60 秒 · UTC" : `${startLabel.slice(11, 19)}–${endLabel.slice(11, 19)} UTC · 60 秒`;
  windowLabel.title = end === null ? "" : `${startLabel} → ${endLabel}`;

  const low = valid.length ? Math.min(...valid.map((point) => point.value)) : 0;
  const high = valid.length ? Math.max(...valid.map((point) => point.value)) : 1;
  const margin = high === low ? Math.max(Math.abs(low) * .05, 1e-9) : (high - low) * .08;
  const minimum = low - margin;
  const span = high + margin - minimum;
  const left = 69, right = width - 19, top = 24, bottom = height - 32;
  const x = (time) => left + Number(time - start) / Number(trendDurationUs) * (right - left);
  const y = (value) => bottom - (value - minimum) / span * (bottom - top);
  context.fillStyle = "#3c6572";
  context.fillText("native", 9, 15);
  context.strokeStyle = "#dbe9ed";
  context.beginPath();
  for (let grid = 0; grid <= 4; grid++) {
    const row = top + grid * (bottom - top) / 4;
    context.moveTo(left, row);
    context.lineTo(right, row);
    if (valid.length) context.fillText(Number((minimum + span * (1 - grid / 4)).toPrecision(4)).toString(), 8, row + 4, left - 15);
  }
  for (let tick = 0; tick <= 4; tick++) {
    const column = left + tick * (right - left) / 4;
    context.moveTo(column, top);
    context.lineTo(column, bottom);
    if (start !== null && (width >= 440 || tick % 2 === 0)) {
      const time = utcTime(String(start + BigInt(tick) * (trendDurationUs / 4n))).slice(11, 19);
      context.fillText(time, Math.min(column - 25, width - 56), height - 13);
    }
  }
  context.stroke();
  // Mark each observation at its own timestamp without interpolating missing samples.
  for (const point of points) {
    const column = x(point.time);
    if (point.value === null) {
      context.strokeStyle = "#a64e42";
      context.beginPath();
      context.moveTo(column, bottom + 3);
      context.lineTo(column, bottom + 9);
      context.stroke();
      continue;
    }
    const row = y(point.value);
    if (point.quality === 0) {
      context.fillStyle = "#007d81";
      context.beginPath();
      context.arc(column, row, 2, 0, 2 * Math.PI);
      context.fill();
    } else {
      context.fillStyle = "#a65a0c";
      context.fillRect(column - 2, row - 2, 5, 5);
    }
  }
}

async function refreshStatus() {
  const requestId = ++statusRequest;
  const revision = statusRevision;
  const readingsRevision = sensorRevision;
  try {
    const status = await request("/api/v1/status");
    if (requestId !== statusRequest) return;
    if (revision === statusRevision) renderStatus(status, readingsRevision === sensorRevision);
    show("api-feedback", "已查询网关状态", "good");
  } catch (error) {
    if (requestId === statusRequest) show("api-feedback", error.message, "error");
  }
}

async function refreshLatest() {
  const requestId = ++latestRequest;
  const revision = statusRevision;
  try {
    const latest = await request("/api/v1/measurements/latest");
    if (requestId !== latestRequest) return;
    if (revision === statusRevision) {
      if (latest.linkConnected === false) renderStatus(latest);
      else renderLink(latest);
    }
    const measurement = latest.measurement;
    const summary = measurement
      ? `通道 ${label(measurement.channel)} · ${peak(measurement)} · ${utcTime(measurement.hostTimeUs ?? measurement.timeUs)} · 质量位 ${label(measurement.qualityFlags)}`
      : "暂无测量";
    show("latest-measurement", `HTTP 最新测量 · ${summary}`);
    show("api-feedback", "已查询最新测量", "good");
  } catch (error) {
    if (requestId === latestRequest) show("api-feedback", error.message, "error");
  }
}

function disconnectStream(text = "已断开，可重新连接", tone = "") {
  const active = stream;
  stream = null;
  if (active) {
    clearTimeout(active.timeout);
    active.socket?.close();
  }
  dashboard.clear();
  renderSensors(null);
  show("stream-state", text, tone);
  scheduleTrend();
  syncButtons();
}

function connectStream() {
  if (stream) return;
  const active = { socket: null, timeout: null, hasMeasurement: false };
  resetTrend();
  stream = active;
  show("stream-state", "正在连接 WS");
  show("stream-event", "—");
  show("stream-dropped", "—");
  placeholder(measurementRows, 7, "等待实时测量");
  syncButtons();
  active.timeout = setTimeout(() => {
    if (stream === active) disconnectStream("WS 连接超时，可重新连接", "error");
  }, 12000);
  try {
    const endpoint = new URL("/api/v1/stream", location.origin);
    endpoint.protocol = "ws:";
    active.socket = new WebSocket(endpoint);
    active.socket.addEventListener("open", () => {
      if (stream !== active) return;
      clearTimeout(active.timeout);
      show("stream-state", "WS 已连接", "good");
      show("gateway-state", "可达", "good");
    });
    active.socket.addEventListener("message", (event) => {
      if (stream !== active) return;
      try {
        const message = JSON.parse(event.data);
        if (message.kind === "waveform") { dashboard.waveform(message); return; }
        if (message.kind === "internalEnvironment") { dashboard.internal(message); return; }
        const eventNames = { status: "状态", measurement: "峰值", sensors: "传感器" };
        if (!Object.hasOwn(eventNames, message.kind)) return;
        show("stream-event", `${eventNames[message.kind]} #${label(message.sequence)}`);
        show("stream-dropped", label(message.ingressDropped));
        if (message.kind === "status") {
          if (message.linkConnected === false && active.hasMeasurement) {
            active.hasMeasurement = false;
            resetTrend();
            placeholder(measurementRows, 7, "机地数传已断开");
            show("latest-measurement", "HTTP 最新测量 · 暂无测量");
          }
          renderStatus(message);
          return;
        }
        if (message.kind === "sensors") {
          renderSensors(message.sensors);
          return;
        }
        if (!active.hasMeasurement) measurementRows.replaceChildren();
        active.hasMeasurement = true;
        addTrendPoint(message);
        appendCells(measurementRows.insertRow(0), [message.sequence, message.frameSequence, message.source,
          message.channel, peak(message), utcTime(message.hostTimeUs ?? message.timeUs), message.qualityFlags]);
        if (measurementRows.rows.length > 50) measurementRows.deleteRow(-1);
      } catch {
        disconnectStream("WS 事件格式无效，可重新连接", "error");
      }
    });
    active.socket.addEventListener("error", () => {
      if (stream !== active) return;
      disconnectStream("WS 连接失败，可重新连接", "error");
      void refreshStatus();
    });
    active.socket.addEventListener("close", () => {
      if (stream !== active) return;
      disconnectStream("WS 已关闭，可重新连接");
      void refreshStatus();
    });
  } catch {
    if (stream === active) disconnectStream("无法启动 WS 连接", "error");
  }
}

function renderReceipt(command, receipt) {
  command.pending = receipt.status === "accepted";
  show("command-accepted", "网关已受理", "good");
  if (command.pending) {
    show("command-result", "等待 SkyCore 回执");
    return;
  }
  const status = Object.hasOwn(receiptLabels, receipt.status) ? receiptLabels[receipt.status] : label(receipt.status);
  const parts = [status];
  if (typeof receipt.detail === "string" && receipt.detail && receipt.detail !== status) parts.push(receipt.detail);
  if (receipt.sequence !== undefined) parts.push(`序号 ${label(receipt.sequence)}`);
  if (receipt.errorCode !== undefined) parts.push(`错误码 ${label(receipt.errorCode)}`);
  if (receipt.result !== undefined) parts.push(`结果 ${label(receipt.result)}`);
  if (receipt.completedAt) parts.push(label(receipt.completedAt));
  show("command-result", parts.join(" · "), receipt.status === "acknowledged" ? "good" : "error");
  show("command-session", label(receipt.sessionId));
  show("command-feedback", "已收到最终回执");
  void refreshStatus();
}

async function checkCommand() {
  const command = currentCommand;
  if (!command || command.checking) return;
  clearTimeout(command.timer);
  command.checking = true;
  syncButtons();
  try {
    const receipt = await request(`/api/v1/commands/${command.id}`);
    if (currentCommand !== command) return;
    renderReceipt(command, receipt);
    if (command.pending && Date.now() < command.pollUntil) {
      command.timer = setTimeout(checkCommand, 650);
    } else if (command.pending) {
      command.pending = false;
      show("command-feedback", "尚未收到最终回执。请查询当前命令及录制状态", "error");
    }
  } catch (error) {
    if (currentCommand !== command) return;
    command.pending = false;
    show("command-feedback", error.message, "error");
  } finally {
    if (currentCommand === command) {
      command.checking = false;
      syncButtons();
    }
  }
}

async function sendCommand(commandId, operation) {
  const random = crypto.getRandomValues(new Uint8Array(16));
  const id = Array.from(random, (byte) => byte.toString(16).padStart(2, "0")).join("");
  const command = { id, sending: true, pending: false, checking: false, pollUntil: Date.now() + 35000 };
  clearTimeout(currentCommand?.timer);
  currentCommand = command;
  show("command-operation", operation);
  show("command-id", command.id);
  show("command-accepted", "等待受理");
  show("command-result", "—");
  show("command-session", "—");
  show("command-feedback", "正在下发命令");
  syncButtons();
  try {
    const receipt = await request("/api/v1/commands", { method: "POST", body: {
      id: command.id, commandId, expiresAt: new Date(Date.now() + 30000).toISOString(),
    } });
    if (currentCommand !== command) return;
    renderReceipt(command, receipt);
    if (command.pending) {
      show("command-feedback", "网关已受理，正在查询最终回执");
      command.timer = setTimeout(checkCommand, 650);
    }
  } catch (error) {
    if (currentCommand !== command) return;
    show("command-accepted", "受理未确认");
    show("command-feedback", error.message, "error");
  } finally {
    if (currentCommand === command) {
      command.sending = false;
      syncButtons();
    }
  }
}

show("gateway-origin", location.host);
trendChannel.addEventListener("change", scheduleTrend);
new ResizeObserver(scheduleTrend).observe(trendCanvas);
element("refresh-status").addEventListener("click", refreshStatus);
element("refresh-latest").addEventListener("click", refreshLatest);
element("connect-stream").addEventListener("click", connectStream);
element("disconnect-stream").addEventListener("click", () => disconnectStream());
element("command-check").addEventListener("click", checkCommand);
for (const button of commandButtons) {
  button.addEventListener("click", () => sendCommand(Number(button.dataset.command), button.textContent));
}

syncButtons();
void refreshStatus();
connectStream();
