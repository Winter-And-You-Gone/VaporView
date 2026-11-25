"use strict";

// Dashboard frames use the existing SkyCore preview sample order and stride.
const dashboard = (() => {
  const byId = (id) => document.getElementById(id);
  const waves = new Map();
  const fpgaChannels = [32, 33, 49, 48];
  const colors = ["#286bc3", "#b65314", "#008277", "#864ba6"];
  const source = byId("wave-source");
  const tcp = byId("tcp-channel");
  const internalTimers = new Map();
  const maxAge = 4000;
  let chosenSource = false;
  let animation = 0;
  function paintLater() {
    if (!animation) animation = requestAnimationFrame(paint);
  }
  function setSource() {
    byId("tcp-plots").hidden = source.value !== "tcp";
    byId("fpga-plots").hidden = source.value !== "fpga";
    paintLater();
  }
  source.addEventListener("change", () => { chosenSource = true; setSource(); });
  tcp.addEventListener("change", paintLater);
  function navigate() {
    for (const a of document.querySelectorAll("nav a")) {
      if (a.hash === (location.hash || "#home")) a.setAttribute("aria-current", "location");
      else a.removeAttribute("aria-current");
    }
    paintLater();
  }
  window.addEventListener("hashchange", navigate);
  navigate();

  function waveform(frame) {
    const key = frame.source + "/" + frame.channel;
    if (frame.source === "tcp") {
      if (!waves.has(key) && !Array.from(tcp.options).some(option => option.value === String(frame.channel))) {
        if (tcp.disabled) tcp.replaceChildren();
        if (tcp.options.length >= 32) return;
        tcp.add(new Option("通道 " + frame.channel, String(frame.channel)));
        tcp.disabled = false;
      }
    } else if (frame.source !== "fpga" || !fpgaChannels.includes(frame.channel)) return;
    if (!chosenSource && waves.size === 0) { source.value = frame.source; setSource(); }
    waves.set(key, { frame, received: performance.now() });
    paintLater();
  }

  function series(frame) {
    if (frame.source === "tcp" || frame.channel < 48) return [["采样值", frame.points]];
    const hasH = frame.points.some(p => p.harmonics);
    const keys = hasH ? ["h1", "h2"] : ["i1", "q1", "i2", "q2"];
    return keys.map(key => [key.toUpperCase(), frame.points.map(p => p[hasH ? "harmonics" : "iq"] ? p[key] : null)]);
  }

  function plot(id, key) {
    const canvas = byId("wave-" + id);
    if (!canvas.clientWidth) return;
    const w = canvas.clientWidth, h = canvas.clientHeight, scale = window.devicePixelRatio || 1;
    const ctx = canvas.getContext("2d");
    if (canvas.width !== Math.round(w * scale)) canvas.width = Math.round(w * scale);
    if (canvas.height !== Math.round(h * scale)) canvas.height = Math.round(h * scale);
    ctx.setTransform(scale, 0, 0, scale, 0, 0);
    ctx.clearRect(0, 0, w, h);
    const left = 55, right = w - 12, top = 12, bottom = h - 22;
    ctx.lineWidth = 1;
    ctx.strokeStyle = "#dae2eb";
    ctx.beginPath();
    for (let i = 0; i <= 4; ++i) {
      const y = top + (bottom - top) * i / 4, x = left + (right - left) * i / 4;
      ctx.moveTo(left, y); ctx.lineTo(right, y);
      ctx.moveTo(x, top); ctx.lineTo(x, bottom);
    }
    ctx.stroke();
    const entry = waves.get(key);
    const frame = entry?.frame;
    const curves = frame?.valid && frame.points.length ? series(frame) : [];
    const valid = curves.flatMap(([, values]) => values.filter(Number.isFinite));
    const legend = byId("legend-" + id);
    legend.replaceChildren();
    const info = byId("info-" + id);
    canvas.dataset.points = String(valid.length);
    if (!valid.length) {
      info.textContent = frame ? "当前预览帧无有效波形" : "等待预览帧";
      return;
    }
    let lo = Math.min(...valid), hi = Math.max(...valid);
    if (hi === lo) { lo -= 1; hi += 1; }
    ctx.font = "11px Consolas, monospace";
    ctx.fillStyle = "#53677b";
    ctx.fillText(Number(hi.toPrecision(4)), 2, top + 4, left - 5);
    ctx.fillText(Number(lo.toPrecision(4)), 2, bottom, left - 5);
    if (Number.isFinite(frame.xStart) && Number.isFinite(frame.xStep)) {
      ctx.fillText(Number(frame.xStart.toPrecision(5)), left, h - 5);
      ctx.textAlign = "right";
      ctx.fillText(Number((frame.xStart + (frame.points.length - 1) * frame.xStep).toPrecision(6)), right, h - 5);
      ctx.textAlign = "left";
    }
    curves.forEach(([name, values], c) => {
      const label = document.createElement("span");
      label.textContent = name;
      label.style.color = colors[c];
      legend.append(label);
      ctx.strokeStyle = colors[c];
      ctx.lineWidth = 1.6;
      ctx.beginPath();
      let connected = false;
      values.forEach((v, i) => {
        if (!Number.isFinite(v)) { connected = false; return; }
        const x = left + i / Math.max(1, values.length - 1) * (right - left);
        const y = bottom - (v - lo) / (hi - lo) * (bottom - top);
        if (connected) ctx.lineTo(x, y);
        else { ctx.moveTo(x, y); ctx.lineTo(x + .6, y); }
        connected = true;
      });
      ctx.stroke();
    });
    const cycle = frame.source === "fpga" ? "周期 " + frame.cycleId + " · " : "";
    info.textContent = cycle + frame.points.length + " 个预览点";
  }
  function paint() {
    animation = 0;
    if (source.value === "fpga") fpgaChannels.forEach(id => plot(id, "fpga/" + id));
    else plot("tcp", "tcp/" + tcp.value);
  }
  const resize = new ResizeObserver(paintLater);
  document.querySelectorAll("figure canvas").forEach(canvas => resize.observe(canvas));
  setInterval(() => {
    for (const [key, entry] of waves) {
      if (performance.now() - entry.received > maxAge) { waves.delete(key); paintLater(); }
    }
  }, 500);

  function internal(frame) {
    const bmp = frame.device === "BMP390";
    const fields = bmp ? [["pressure", frame.pressureHpa, "hPa"]] :
      [["temperature", frame.temperatureC, "°C"], ["humidity", frame.humidityPercent, "% RH"]];
    for (const [id, value, unit] of fields) {
      byId("reading-internal-" + id).textContent = Number.isFinite(value) ? value.toFixed(2) + " " + unit : "—";
    }
    clearTimeout(internalTimers.get(frame.device));
    if (fields.some(([, value]) => Number.isFinite(value))) {
      internalTimers.set(frame.device, setTimeout(() => internal({ device: frame.device }), maxAge));
    }
  }
  function clear() {
    waves.clear();
    tcp.replaceChildren(new Option("等待通道"));
    tcp.disabled = true;
    internal({device:"BMP390"});
    internal({device:"SHT45"});
    paintLater();
  }
  function clearFpga() {
    fpgaChannels.forEach(id => waves.delete("fpga/" + id));
    paintLater();
  }
  return { waveform, internal, clear, clearFpga };
})();
