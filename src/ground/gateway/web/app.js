"use strict";

const element = (id) => document.getElementById(id);
const commandButtons = document.querySelectorAll("[data-command]");
const measurementRows = element("measurement-rows");
const requestErrors = {
  400: "网关拒绝了请求参数，请核对地面机时钟",
  403: "地面网关未授权此操作",
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
let stream = null;
let currentCommand = null;

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
  for (const button of commandButtons) button.disabled = Boolean(currentCommand?.sending || currentCommand?.pending);
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
    throw new Error(deadline.aborted ? "HTTP 请求超时" : "HTTP 无响应，请检查网络和监听地址");
  }
  show("gateway-state", "可达", "good");
  if (!response.ok) throw new Error(`HTTP ${response.status} · ${requestErrors[response.status] ?? "请求失败"}`);
  try {
    return await response.json();
  } catch {
    throw new Error("网关返回了无效 JSON");
  }
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
  const connected = status.linkConnected === true;
  show("link-state", connected ? "已连接" : "未连接", connected ? "good" : "");
}

function renderStatus(status) {
  statusRevision += 1;
  renderLink(status);
  const hasStatus = status.telemetryAvailable === true;
  show("telemetry-state", hasStatus ? "已收到" : "暂无状态", hasStatus ? "good" : "");
  show("recording-state", hasStatus ? (["停止", "记录中", "暂停"][status.recordingState] ?? label(status.recordingState)) : "—");
  show("session-name", hasStatus ? label(status.sessionName || null) : "—");
  const devices = element("device-rows");
  if (!hasStatus || !Array.isArray(status.devices) || status.devices.length === 0) {
    placeholder(devices, 4, "暂无设备状态");
    return;
  }
  devices.replaceChildren();
  for (const device of status.devices) appendCells(devices.insertRow(), [device.device, device.state, device.errorCode, device.receivedCount]);
}

function utcTime(micros) {
  if (typeof micros !== "string" || !/^\d{1,20}$/.test(micros)) return "—";
  const time = BigInt(micros);
  const date = new Date(Number(time / 1000n));
  return Number.isNaN(date.getTime()) ? "—" : date.toISOString().replace(/Z$/, `${String(time % 1000n).padStart(3, "0")}Z`);
}

function peak(measurement) {
  if (measurement.valid !== true || typeof measurement.value !== "number") return "无有效峰值";
  return `${measurement.value} ${label(measurement.unit)}`;
}

async function refreshStatus() {
  const requestId = ++statusRequest;
  const revision = statusRevision;
  try {
    const status = await request("/api/v1/status");
    if (requestId !== statusRequest) return;
    if (revision === statusRevision) renderStatus(status);
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
      ? `通道 ${label(measurement.channel)} · ${peak(measurement)} · ${utcTime(measurement.timeUs)} · 质量位 ${label(measurement.qualityFlags)}`
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
  show("stream-state", text, tone);
  syncButtons();
}

function connectStream() {
  if (stream) return;
  const active = { socket: null, timeout: null, hasMeasurement: false };
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
    });
    active.socket.addEventListener("message", (event) => {
      if (stream !== active) return;
      try {
        const message = JSON.parse(event.data);
        if (message.kind !== "status" && message.kind !== "measurement") return;
        show("stream-event", `${message.kind === "status" ? "状态" : "测量"} #${label(message.sequence)}`);
        show("stream-dropped", label(message.ingressDropped));
        if (message.kind === "status") {
          renderStatus(message);
          return;
        }
        if (!active.hasMeasurement) measurementRows.replaceChildren();
        active.hasMeasurement = true;
        appendCells(measurementRows.insertRow(0), [message.sequence, message.frameSequence, message.source,
          message.channel, peak(message), utcTime(message.hostTimeUs ?? message.timeUs), message.qualityFlags]);
        if (measurementRows.rows.length > 50) measurementRows.deleteRow(-1);
      } catch {
        disconnectStream("WS 事件格式无效，可重新连接", "error");
      }
    });
    active.socket.addEventListener("error", () => {
      if (stream === active) disconnectStream("WS 连接失败，请检查网络和网关订阅状态", "error");
    });
    active.socket.addEventListener("close", () => {
      if (stream === active) disconnectStream("WS 已关闭，可重新连接");
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

async function sendCommand(commandId) {
  const random = crypto.getRandomValues(new Uint8Array(16));
  const id = Array.from(random, (byte) => byte.toString(16).padStart(2, "0")).join("");
  const command = { id, sending: true, pending: false, checking: false, pollUntil: Date.now() + 35000 };
  clearTimeout(currentCommand?.timer);
  currentCommand = command;
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
element("refresh-status").addEventListener("click", refreshStatus);
element("refresh-latest").addEventListener("click", refreshLatest);
element("connect-stream").addEventListener("click", connectStream);
element("disconnect-stream").addEventListener("click", () => disconnectStream());
element("command-check").addEventListener("click", checkCommand);
for (const button of commandButtons) button.addEventListener("click", () => sendCommand(Number(button.dataset.command)));

syncButtons();
void refreshStatus();
