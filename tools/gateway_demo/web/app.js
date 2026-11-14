"use strict";

(() => {
  const $ = id => document.getElementById(id);
  const commandNames = {10: "读取状态", 1: "启动记录", 3: "停止记录"};
  const checkNames = {
    local_demo_environment: "本机环境",
    isolated_runtime: "隔离缓存",
    completed_session_fixture: "测试 Session",
    simulated_skycore: "模拟 SkyCore",
    shared_skycore_connection: "复用现有连接",
    verification_completed: "验证完成",
    single_sky_socket: "单条 SkyCore 连接",
    local_updates_continue: "本地持续更新",
    command_acknowledged: "命令 ACK",
    session_synced: "Session 同步",
    offline_retry_recovery: "断网恢复"
  };
  const signatures = new Map();
  let currentState = null;
  let connection = null;
  let tokenVisible = false;
  let verifying = false;
  let sendingCommand = false;
  let changingNetwork = false;
  let pollInProgress = false;
  let mutationVersion = 0;
  let currentPayload = "";

  const text = (id, value) => { $(id).textContent = String(value); };
  const node = (tag, className, value) => {
    const element = document.createElement(tag);
    if (className) element.className = className;
    if (value !== undefined) element.textContent = String(value);
    return element;
  };
  const count = value => Number.isFinite(Number(value)) ? Math.max(0, Number(value)) : 0;
  const number = value => count(value).toLocaleString("zh-CN");
  const bytes = value => {
    const size = count(value);
    if (size < 1024) return `${number(size)} B`;
    if (size < 1024 * 1024) return `${(size / 1024).toFixed(1)} KiB`;
    return `${(size / (1024 * 1024)).toFixed(2)} MiB`;
  };
  const time = value => {
    if (value === null || value === undefined || value === "") return "—";
    const date = new Date(typeof value === "number" && value < 1e12 ? value * 1000 : value);
    return Number.isNaN(date.getTime()) ? String(value) : date.toLocaleTimeString("zh-CN", {hour12: false});
  };
  const displayValue = value => value === undefined || value === null ? "—" : String(value);
  const statusLabels = {
    queued: ["等待 Gateway", "waiting"], pending: ["等待处理", "waiting"],
    accepted: ["已接收", "running"], dispatched: ["已转发", "running"],
    sent: ["已下发", "running"], running: ["执行中", "running"],
    success: ["执行成功", "success"], succeeded: ["执行成功", "success"],
    completed: ["已完成", "success"], acknowledged: ["已确认", "success"], ok: ["执行成功", "success"],
    expired: ["已过期", "failure"], rejected: ["已拒绝", "failure"],
    denied: ["无权限", "failure"], failed: ["执行失败", "failure"],
    timeout: ["等待超时", "failure"], timed_out: ["等待超时", "failure"],
    duplicate: ["重复命令", "neutral"], unknown: ["结果未知", "neutral"]
  };
  Object.assign(statusLabels, {
    disconnected: ["连接已断开", "failure"], "not-sent": ["未发送", "failure"],
    "configuration-changed": ["配置已变更", "failure"], "not-allowed": ["无权限", "failure"],
    "link-closed": ["设备未连接", "failure"], "invalid-command": ["命令无效", "failure"],
    forbidden: ["未获本地授权", "failure"], "wrong-device": ["设备不匹配", "failure"],
    busy: ["命令队列已满", "failure"], "invalid-payload": ["载荷无效", "failure"]
  });
  const statusBadge = status => {
    const [label, kind] = statusLabels[status] || [displayValue(status), "neutral"];
    return node("span", `badge ${kind}`, label);
  };
  const setMessage = (id, message, error = false) => {
    $(id).textContent = message;
    $(id).classList.toggle("error", error);
  };
  const emptyRow = (body, columns, message) => {
    const tr = node("tr");
    const td = node("td", "empty-cell", message);
    td.colSpan = columns;
    tr.append(td);
    body.replaceChildren(tr);
  };
  const updateWhenChanged = (name, value, render) => {
    const signature = JSON.stringify(value);
    if (signatures.get(name) !== signature) {
      render(value);
      signatures.set(name, signature);
    }
  };

  async function request(path, body) {
    const controller = new AbortController();
    const timeout = window.setTimeout(() => controller.abort(), 12000);
    try {
      const response = await fetch(`/demo/v1/${path}`, {
        method: body === undefined ? "GET" : "POST",
        headers: body === undefined ? {Accept: "application/json"} : {Accept: "application/json", "Content-Type": "application/json"},
        body: body === undefined ? undefined : JSON.stringify(body),
        signal: controller.signal,
        cache: "no-store",
        credentials: "same-origin"
      });
      const raw = await response.text();
      let data;
      try { data = raw ? JSON.parse(raw) : {}; }
      catch { throw new Error(`服务器返回了无效 JSON（HTTP ${response.status}）`); }
      if (!response.ok) throw new Error(data.error || data.detail || `请求失败（HTTP ${response.status}）`);
      return data;
    } catch (error) {
      if (error.name === "AbortError") throw new Error("服务器响应超时，请检查演示服务是否仍在运行。");
      throw error;
    } finally { window.clearTimeout(timeout); }
  }

  function updateControls() {
    const reachable = currentState !== null && $("api-error").hidden;
    const running = verifying || currentState?.verification?.state === "running";
    $("verify-button").disabled = !reachable || running;
    text("verify-button-label", running ? "验证正在运行…" : currentState?.verification?.state === "idle" || !currentState?.verification
      ? "运行真实 Gateway 验证" : "重新运行链路验证");
    $("network-toggle").disabled = !reachable || changingNetwork || running;
    for (const button of document.querySelectorAll("[data-command]"))
      button.disabled = !reachable || sendingCommand || !currentState?.online || running;
  }

  function renderVerification(value) {
    const verification = value || {state: "idle", checks: []};
    const labels = {idle: ["尚未运行", "neutral"], running: ["正在验证", "running"], passed: ["验证通过", "success"], failed: ["验证失败", "failure"]};
    const [label, kind] = labels[verification.state] || [displayValue(verification.state), "neutral"];
    $("verification-badge").className = `badge ${kind}`;
    text("verification-badge", label);
    text("verification-detail", verification.detail || (verification.state === "idle"
      ? "点击下方按钮开始。验证会产生实际遥测和测试 Session，无需连接真实设备。" : "等待验证程序报告进展…"));
    $("verification-detail").classList.toggle("failed", verification.state === "failed");
    const checks = Array.isArray(verification.checks) ? verification.checks : [];
    if (!checks.length) {
      $("verification-checks").replaceChildren(node("li", "check-empty", verification.state === "running"
        ? "验证程序正在运行，等待服务器确认各项检查。" : "运行后将在这里显示检查结果。"));
      return;
    }
    $("verification-checks").replaceChildren(...checks.map(check => {
      const item = node("li");
      const passed = check.passed === true;
      const failed = check.passed === false;
      const marker = node("span", `check-marker${passed ? " passed" : failed ? " failed" : ""}`, passed ? "✓" : failed ? "×" : "·");
      marker.setAttribute("aria-label", passed ? "通过" : failed ? "失败" : "等待");
      const copy = node("div", "check-copy");
      copy.append(node("strong", "", checkNames[check.name] || check.name || "检查项目"));
      if (check.detail) copy.append(node("p", "", check.detail));
      item.append(marker, copy);
      return item;
    }));
  }

  function renderCommands(commands) {
    const body = $("commands-body");
    if (!commands.length) { emptyRow(body, 3, "尚未下发命令"); return; }
    body.replaceChildren(...commands.slice(0, 30).map(command => {
      const row = node("tr");
      const name = node("td", "", commandNames[command.commandId] || `命令 ${displayValue(command.commandId)}`);
      name.title = `ID: ${displayValue(command.id)}\n有效期至: ${displayValue(command.expiresAt)}`;
      const status = node("td");
      const result = command.result;
      const resultStatus = result && typeof result === "object" ? result.status : undefined;
      status.append(statusBadge(resultStatus || command.status));
      const description = result === undefined || result === null ? "等待执行回执" : typeof result === "string" ? result
        : result.detail || result.message || JSON.stringify(result);
      const resultCell = node("td", "", description);
      resultCell.title = description;
      resultCell.style.maxWidth = "190px";
      resultCell.style.overflow = "hidden";
      resultCell.style.textOverflow = "ellipsis";
      row.append(name, status, resultCell);
      return row;
    }));
  }

  function payloadBytes(base64) {
    try { return atob(base64 || "").length; }
    catch { return null; }
  }

  function renderTelemetry(batches) {
    const frames = [];
    for (const batch of batches) {
      for (const frame of Array.isArray(batch.frames) ? [...batch.frames].reverse() : [])
        frames.push({frame, batchId: batch.id, receivedAt: batch.receivedAt});
    }
    const recent = frames.slice(0, 30);
    text("telemetry-count", `${number(recent.length)} 条最近记录`);
    if (!recent.length) {
      const row = node("tr");
      const cell = node("td", "empty-cell tall-empty");
      cell.colSpan = 5;
      cell.append(node("span", "empty-icon", "↗"), node("strong", "", "等待第一帧遥测"),
        node("span", "", "运行链路验证，或将 VaporView GUI 连接到本机服务器。"));
      row.append(cell);
      $("telemetry-body").replaceChildren(row);
      return;
    }
    $("telemetry-body").replaceChildren(...recent.map(({frame, batchId, receivedAt}) => {
      const row = node("tr");
      const type = node("td");
      type.append(node("span", "type-pill", `Type ${displayValue(frame.type)}`));
      type.title = `消息 ID: ${displayValue(batchId)}\n接收时间: ${displayValue(receivedAt)}\nflags: ${displayValue(frame.flags)}`;
      const size = payloadBytes(frame.payloadBase64);
      const action = node("td");
      const view = node("button", "table-action", "查看数据");
      view.type = "button";
      view.addEventListener("click", () => {
        currentPayload = frame.payloadBase64 || "";
        text("payload-meta", `类型 ${displayValue(frame.type)} · 序号 ${displayValue(frame.sequence)} · ${size === null ? "载荷编码无效" : `${number(size)} 字节`} · 采集时间 ${displayValue(frame.timeUs)} µs`);
        text("payload-data", currentPayload || "（空载荷）");
        setMessage("payload-message", "");
        $("copy-payload").disabled = !currentPayload;
        $("payload-dialog").showModal();
      });
      action.append(view);
      row.append(type, node("td", "code", displayValue(frame.sequence)), node("td", "code", displayValue(frame.timeUs)), node("td", "mono", size === null ? "无效 Base64" : bytes(size)), action);
      return row;
    }));
  }

  function renderSessions(sessions) {
    if (!sessions.length) {
      const empty = node("div", "section-empty");
      empty.append(node("span", "empty-icon", "▤"), node("strong", "", "还没有同步任务"),
        node("span", "", "验证程序会同步一个测试 Session。真实 Session 需先结束记录，再从 GUI 手动同步。"));
      $("sessions-list").replaceChildren(empty);
      return;
    }
    $("sessions-list").replaceChildren(...sessions.slice(0, 20).map(session => {
      const files = Array.isArray(session.files) ? session.files : [];
      const totalSize = files.reduce((total, file) => total + count(file.size), 0);
      const receivedFiles = files.filter(file => count(file.received) >= count(file.size)).length;
      const received = count(session.bytes);
      const progress = session.complete ? 100 : totalSize ? Math.min(99, Math.floor(received / totalSize * 100)) : 0;
      const item = node("article", "session-item");
      const header = node("div", "session-header");
      header.append(node("span", "session-name", session.sessionId || "未命名 Session"), node("span", `badge ${session.complete ? "success" : "running"}`, session.complete ? "同步完成" : "正在同步"));
      const details = node("div", "session-details");
      details.append(node("span", "", `${receivedFiles} / ${files.length} 个文件 · ${bytes(received)}${totalSize ? ` / ${bytes(totalSize)}` : ""}`), node("span", "mono", `${progress}%`));
      const track = node("div", "progress-track");
      track.setAttribute("role", "progressbar");
      track.setAttribute("aria-label", `Session ${session.sessionId || ""} 同步进度`);
      track.setAttribute("aria-valuemin", "0");
      track.setAttribute("aria-valuemax", "100");
      track.setAttribute("aria-valuenow", String(progress));
      const fill = node("div", `progress-fill${session.complete ? " complete" : ""}`);
      fill.style.width = `${progress}%`;
      track.append(fill);
      item.append(header, details, track);
      if (files.length) {
        const disclosure = node("details", "session-files");
        disclosure.append(node("summary", "", "查看文件"));
        const list = node("ul");
        for (const file of files) {
          const row = node("li");
          const done = count(file.received) >= count(file.size);
          row.append(node("span", "file-name", file.path || "未命名文件"), node("span", done ? "file-complete mono" : "mono", `${done ? "✓ " : ""}${bytes(file.received)} / ${bytes(file.size)}`));
          list.append(row);
        }
        disclosure.append(list);
        item.append(disclosure);
      }
      return item;
    }));
  }

  function renderEvents(events) {
    const recent = events.slice(-50).reverse();
    text("events-count", `${number(recent.length)} 条最近事件`);
    if (!recent.length) { emptyRow($("events-body"), 5, "暂无请求记录"); return; }
    $("events-body").replaceChildren(...recent.map(event => {
      const row = node("tr");
      const code = count(event.status);
      const status = node("td");
      status.append(node("span", `http-code${code >= 500 ? " error" : code >= 400 ? " warning" : ""}`, code || "—"));
      row.append(node("td", "mono", time(event.time)), node("td", "http-method", displayValue(event.method)), node("td", "code", event.path || "/"), status, node("td", "", displayValue(event.detail)));
      return row;
    }));
  }

  function renderState(state) {
    currentState = state;
    const stats = state.stats || {};
    text("metric-frames", number(stats.frames));
    text("metric-batches", `${number(stats.telemetryBatches)} 批已接收遥测`);
    text("metric-results", number(stats.commandResults));
    text("metric-sessions", number(stats.sessionsComplete));
    text("metric-session-bytes", `${bytes(stats.sessionBytes)} 已接收文件数据`);
    text("metric-duplicates", number(stats.duplicates));
    text("metric-rejections", number(stats.rejectedRequests));
    $("network-dot").className = `status-dot${state.online ? "" : " offline"}`;
    text("network-label", state.online ? "模拟互联网 · 在线" : "模拟互联网 · 已断开");
    $("network-toggle").setAttribute("aria-checked", String(Boolean(state.online)));
    $("network-toggle").title = state.online ? "切断模拟互联网，观察 Gateway 缓存与重试" : "恢复模拟互联网，观察 Gateway 自动补传";
    updateWhenChanged("verification", state.verification || null, renderVerification);
    updateWhenChanged("commands", state.commands || [], renderCommands);
    updateWhenChanged("telemetry", state.telemetry || [], renderTelemetry);
    updateWhenChanged("sessions", state.sessions || [], renderSessions);
    updateWhenChanged("events", state.events || [], renderEvents);
    updateControls();
  }

  async function poll() {
    if (pollInProgress) return;
    pollInProgress = true;
    const version = mutationVersion;
    try {
      const state = await request("state");
      if (version !== mutationVersion) return;
      if (!state || typeof state.online !== "boolean" || !state.stats) throw new Error("服务器状态格式不完整，无法确认验证结果。");
      $("api-error").hidden = true;
      $("server-dot").className = "status-dot";
      text("refresh-label", `验证台已连接 · ${new Date().toLocaleTimeString("zh-CN", {hour12: false})} 更新`);
      renderState(state);
    } catch (error) {
      if (version !== mutationVersion) return;
      $("api-error").textContent = `无法更新验证台：${error.message}  将自动重试。已显示的数值为上次成功读取的结果。`;
      $("api-error").hidden = false;
      $("server-dot").className = "status-dot error";
      text("refresh-label", "验证台连接中断");
      updateControls();
    } finally { pollInProgress = false; }
  }

  $("verify-button").addEventListener("click", async () => {
    if (verifying || currentState?.verification?.state === "running") return;
    verifying = true;
    mutationVersion += 1;
    updateControls();
    text("verification-detail", "正在请求启动真实 Gateway 验证程序…");
    $("verification-detail").classList.remove("failed");
    try {
      const verification = await request("verify", {});
      if (verification.state !== "running") throw new Error("服务器未确认验证程序已启动。");
      if (currentState) currentState.verification = verification;
      updateWhenChanged("verification", verification, renderVerification);
      await poll();
    } catch (error) {
      text("verification-detail", `无法启动验证：${error.message}`);
      $("verification-detail").classList.add("failed");
      $("verification-badge").className = "badge failure";
      text("verification-badge", "启动失败");
    } finally {
      verifying = false;
      updateControls();
    }
  });

  $("network-toggle").addEventListener("click", async () => {
    if (!currentState || changingNetwork) return;
    changingNetwork = true;
    mutationVersion += 1;
    updateControls();
    try {
      const response = await request("network", {online: !currentState.online});
      if (typeof response.online !== "boolean") throw new Error("服务器未确认网络状态。");
      await poll();
    } catch (error) {
      $("api-error").textContent = `无法切换模拟互联网：${error.message}`;
      $("api-error").hidden = false;
    } finally { changingNetwork = false; updateControls(); }
  });

  for (const button of document.querySelectorAll("[data-command]")) {
    button.addEventListener("click", async () => {
      if (sendingCommand) return;
      sendingCommand = true;
      mutationVersion += 1;
      updateControls();
      const commandId = Number(button.dataset.command);
      setMessage("command-message", `正在下发${commandNames[commandId]}命令…`);
      try {
        const response = await request("commands", {commandId, ttlSeconds: 60});
        if (!response.command?.id) throw new Error("服务器未返回命令 ID。");
        setMessage("command-message", `${commandNames[commandId]}已排队，等待 Gateway 轮询。ID: ${response.command.id}`);
        await poll();
      } catch (error) { setMessage("command-message", `命令未下发：${error.message}`, true); }
      finally { sendingCommand = false; updateControls(); }
    });
  }

  async function copy(value, messageId) {
    if (!value) { setMessage(messageId, "暂无可复制的内容。", true); return; }
    try {
      if (!navigator.clipboard?.writeText) throw new Error("浏览器不支持自动复制，请选中字段手动复制。");
      await navigator.clipboard.writeText(value);
      setMessage(messageId, "已复制到剪贴板。");
    } catch (error) { setMessage(messageId, error.message || "复制失败，请手动复制。", true); }
  }

  async function loadConnection() {
    try {
      connection = await request("connection");
      if (!connection.url || !connection.deviceId) throw new Error("服务器未提供完整的连接配置。");
      $("connection-url").value = connection.url;
      $("connection-device").value = connection.deviceId;
      $("copy-url").disabled = false;
      $("copy-device").disabled = false;
      $("reveal-token").disabled = !connection.token;
    } catch (error) { setMessage("connection-message", `无法读取连接配置：${error.message}。刷新页面可重试。`, true); }
  }

  $("copy-url").addEventListener("click", () => copy(connection?.url, "connection-message"));
  $("copy-device").addEventListener("click", () => copy(connection?.deviceId, "connection-message"));
  $("reveal-token").addEventListener("click", () => {
    tokenVisible = !tokenVisible;
    $("connection-token").type = tokenVisible ? "text" : "password";
    $("connection-token").value = tokenVisible ? connection?.token || "" : "";
    $("copy-token").disabled = !tokenVisible;
    $("reveal-token").setAttribute("aria-expanded", String(tokenVisible));
    text("reveal-token", tokenVisible ? "隐藏" : "显示");
    setMessage("connection-message", tokenVisible ? "访问令牌仅供本地连接，请勿包含在截图或共享记录中。" : "");
  });
  $("copy-token").addEventListener("click", () => copy(tokenVisible ? connection?.token : "", "connection-message"));
  $("close-payload").addEventListener("click", () => $("payload-dialog").close());
  $("copy-payload").addEventListener("click", () => copy(currentPayload, "payload-message"));
  $("payload-dialog").addEventListener("click", event => {
    if (event.target !== $("payload-dialog")) return;
    const rect = $("payload-dialog").getBoundingClientRect();
    if (event.clientX < rect.left || event.clientX > rect.right || event.clientY < rect.top || event.clientY > rect.bottom)
      $("payload-dialog").close();
  });

  poll();
  loadConnection();
  window.setInterval(() => { if (!document.hidden) poll(); }, 1000);
  document.addEventListener("visibilitychange", () => { if (!document.hidden) poll(); });
})();
