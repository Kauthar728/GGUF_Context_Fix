/* Continuum front-end — vanilla JS, no build step. */
"use strict";

const $  = (s, r = document) => r.querySelector(s);
const $$ = (s, r = document) => [...r.querySelectorAll(s)];
const esc = (s) => String(s == null ? "" : s)
  .replace(/&/g, "&amp;").replace(/</g, "&lt;").replace(/>/g, "&gt;");

async function api(path, opts = {}) {
  const res = await fetch(path, opts);
  const ct = res.headers.get("content-type") || "";
  const body = ct.includes("application/json") ? await res.json() : await res.text();
  if (!res.ok) throw new Error((body && body.detail) || res.statusText || "request failed");
  return body;
}
const jpost = (p, obj) => api(p, {
  method: "POST", headers: { "Content-Type": "application/json" },
  body: JSON.stringify(obj || {}),
});

let STATE = { view: "dashboard", concepts: [], activeConcept: null, chats: [], graph: null, polling: null };

/* ---------------- toast / modal ---------------- */
let toastT;
function toast(msg) {
  const t = $("#toast"); t.textContent = msg; t.hidden = false;
  clearTimeout(toastT); toastT = setTimeout(() => (t.hidden = true), 2600);
}
function confirmModal(title, body) {
  return new Promise((resolve) => {
    $("#modal-title").textContent = title;
    $("#modal-body").textContent = body;
    const m = $("#modal"); m.hidden = false;
    const done = (v) => { m.hidden = true; $("#modal-ok").onclick = null; $("#modal-cancel").onclick = null; resolve(v); };
    $("#modal-ok").onclick = () => done(true);
    $("#modal-cancel").onclick = () => done(false);
  });
}

/* ---------------- navigation ---------------- */
function show(view) {
  STATE.view = view;
  $$(".view").forEach((v) => v.classList.toggle("active", v.dataset.view === view));
  $$(".nav-item").forEach((n) => n.classList.toggle("active", n.dataset.view === view));
  if (view === "concepts") loadConcepts();
  if (view === "chats") loadChats();
  if (view === "graph") loadGraph();
  if (view === "model") loadModel();
  if (view === "events") loadEvents();
  if (view === "dashboard") refreshStatus();
}

/* ---------------- theme / accent ---------------- */
function applyTheme(theme, accent) {
  if (theme) document.documentElement.dataset.theme = theme;
  if (accent) document.documentElement.dataset.accent = accent;
  $$("[data-set-theme]").forEach((b) => b.classList.toggle("active", b.dataset.setTheme === document.documentElement.dataset.theme));
  $$("[data-set-accent]").forEach((b) => b.classList.toggle("active", b.dataset.setAccent === document.documentElement.dataset.accent));
}
async function saveSettings(obj) { try { await jpost("/api/settings", obj); } catch (e) {} }

/* ---------------- status / dashboard ---------------- */
async function refreshStatus() {
  let s;
  try { s = await api("/api/status"); } catch (e) { return; }
  const st = s.stats || {};
  const set = (id, v) => { const el = $(id); if (el) el.textContent = v ?? 0; };
  set("#d-chats", st.chat); set("#d-turns", st.turn); set("#d-terms", st.term);
  set("#d-meanings", st.meaning); set("#d-evidence", st.evidence); set("#d-rel", st.term_relationship);
  set("#ps-chats", st.chat); set("#ps-turns", st.turn); set("#ps-terms", st.term);
  set("#ps-meanings", st.meaning); set("#ps-rel", st.term_relationship); set("#ps-emb", st.term_embedding);
  $("#status-db").textContent = s.db_path || "";
  $("#set-dbpath").textContent = s.db_path || "—";
  const dp = $("#deps-pill");
  if (s.deps_available) { dp.textContent = "embeddings: ready"; dp.className = "pill ok"; }
  else { dp.textContent = "embeddings: off"; dp.className = "pill off"; }
  // job
  const j = s.job || {};
  const sj = $("#status-job");
  if (j.running) sj.innerHTML = `<span class="spin"></span>${esc(j.kind)} running…`;
  else sj.textContent = j.error ? `error: ${j.error}` : (j.message || "idle");
}

function startPolling() {
  if (STATE.polling) return;
  STATE.polling = setInterval(async () => {
    let j; try { j = await api("/api/job"); } catch (e) { return; }
    const sj = $("#status-job");
    if (j.running) { sj.innerHTML = `<span class="spin"></span>${esc(j.kind)} running…`; }
    else {
      clearInterval(STATE.polling); STATE.polling = null;
      sj.textContent = j.error ? `error: ${j.error}` : (j.message || "idle");
      if (j.error) toast(`${j.kind} failed: ${j.error}`);
      else if (j.kind) toast(`${j.kind} complete`);
      refreshStatus();
      if (STATE.view === "concepts") loadConcepts();
      if (STATE.view === "model") loadModel();
      if (STATE.view === "graph") loadGraph();
    }
  }, 900);
}

/* ---------------- ingest ---------------- */
async function uploadFiles(fileList) {
  if (!fileList || !fileList.length) return;
  const fd = new FormData();
  [...fileList].forEach((f) => fd.append("files", f));
  try {
    const r = await api("/api/ingest/files", { method: "POST", body: fd });
    toast(`Imported ${r.imported}, skipped ${r.skipped}`);
    refreshStatus();
  } catch (e) { toast("Import failed: " + e.message); }
}

/* ---------------- concepts ---------------- */
async function loadConcepts() {
  const q = $("#concept-filter").value.trim();
  let r; try { r = await api("/api/concepts?limit=300" + (q ? "&q=" + encodeURIComponent(q) : "")); }
  catch (e) { return; }
  STATE.concepts = r.concepts || [];
  const list = $("#concept-list");
  if (!STATE.concepts.length) { list.innerHTML = `<p class="muted" style="padding:10px">No concepts yet. Ingest chats and run extraction.</p>`; return; }
  list.innerHTML = STATE.concepts.map((c) => `
    <div class="litem" data-id="${c.term_id}">
      <div class="t">${esc(c.term)}</div>
      <div class="s">${c.occurrences}× · ${c.confidence != null ? "conf " + Number(c.confidence).toFixed(2) : "no meaning yet"}</div>
    </div>`).join("");
  $$("#concept-list .litem").forEach((el) => el.onclick = () => openConcept(el.dataset.id, el));
}

async function openConcept(termId, el) {
  $$("#concept-list .litem").forEach((n) => n.classList.toggle("active", n === el));
  let p; try { p = await api("/api/concept/" + termId); } catch (e) { return; }
  STATE.activeConcept = p;
  $("#concept-detail").innerHTML = renderPacket(p);
  wirePacket("#concept-detail");
}

function renderPacket(p) {
  if (!p || !p.found) {
    const cands = (p && p.candidates || []).map((c) => `<span class="chip" data-term="${esc(c.term)}">${esc(c.term)}</span>`).join("");
    return `<h3>No authoritative concept found</h3><p class="muted">${esc((p && p.message) || "")}</p>
      ${cands ? `<p class="muted">Closest concepts:</p><div class="chips">${cands}</div>` : ""}`;
  }
  const conf = p.confidence != null ? Math.round(p.confidence * 100) : null;
  const related = (p.related || []).map((r) =>
    `<span class="chip" data-term="${esc(r.term)}">${esc(r.term)} <span class="muted">· ${esc(r.type)}</span></span>`).join("");
  const evidence = (p.evidence || []).map((e) =>
    `<div class="evi"><b>[${esc(e.title || e.source || "chat")}]</b> ${esc(e.evidence_text)}</div>`).join("");
  const evo = (p.evolution || []);
  const evoHtml = evo.length > 1
    ? `<div class="kv"><div class="k">Evolution</div><div>${evo.map((e) => `v${e.version}: ${esc((e.meaning || "n/a").slice(0, 80))}`).join(" → ")}</div></div>` : "";
  return `
    <h3>${esc(p.term)} <span class="badge">${esc(p.match)} match</span></h3>
    <div class="kv"><div class="k">Occurrences</div><div>${p.occurrences}</div></div>
    ${p.meaning ? `<div class="kv"><div class="k">Meaning</div><div>${esc(p.meaning)}</div></div>` : ""}
    ${p.understanding ? `<div class="kv"><div class="k">Understanding</div><div>${esc(p.understanding)}</div></div>` : ""}
    ${conf != null ? `<div class="kv"><div class="k">Confidence</div><div style="flex:1">${conf}% <span class="muted sm">(evidence-based, not absolute)</span><div class="conf"><i style="width:${conf}%"></i></div></div></div>` : ""}
    ${evoHtml}
    ${related ? `<div class="kv"><div class="k">Related</div><div class="chips">${related}</div></div>` : ""}
    ${evidence ? `<h3 style="margin-top:16px">Evidence (${p.evidence.length})</h3>${evidence}` : ""}`;
}

function wirePacket(scope) {
  $$(scope + " .chip[data-term]").forEach((c) => c.onclick = () => {
    show("search"); $("#search-q").value = c.dataset.term; doSearch();
  });
}

/* ---------------- search ---------------- */
async function doSearch() {
  const q = $("#search-q").value.trim();
  if (!q) return;
  $("#search-result").innerHTML = `<p class="muted"><span class="spin"></span> Resolving…</p>`;
  try {
    const p = await api("/api/search?q=" + encodeURIComponent(q));
    $("#search-result").innerHTML = renderPacket(p);
    wirePacket("#search-result");
    const b = await api("/api/briefing?q=" + encodeURIComponent(q));
    $("#briefing-text").textContent = b;
  } catch (e) {
    $("#search-result").innerHTML = `<p class="muted">Search failed: ${esc(e.message)}</p>`;
  }
}

/* ---------------- chats ---------------- */
async function loadChats() {
  let r; try { r = await api("/api/chats"); } catch (e) { return; }
  STATE.chats = r.chats || [];
  const list = $("#chat-list");
  if (!STATE.chats.length) { list.innerHTML = `<p class="muted" style="padding:10px">No chats ingested yet.</p>`; return; }
  list.innerHTML = STATE.chats.map((c) => `
    <div class="litem" data-id="${c.chat_id}">
      <div class="t">${esc(c.title || "Untitled")}</div>
      <div class="s">${c.turns} turns · ${esc(c.source || "")}</div>
    </div>`).join("");
  $$("#chat-list .litem").forEach((el) => el.onclick = () => openChat(el.dataset.id, el));
}
async function openChat(id, el) {
  $$("#chat-list .litem").forEach((n) => n.classList.toggle("active", n === el));
  let r; try { r = await api("/api/chat/" + id); } catch (e) { return; }
  const turns = (r.turns || []).map((t) =>
    `<div class="evi"><b>${esc(t.speaker)}</b><br>${esc(t.message)}</div>`).join("");
  $("#chat-detail").innerHTML = `<h3>${esc(r.chat.title || "Untitled")}</h3>
    <p class="muted sm">${esc(r.chat.file_path || r.chat.source || "")}</p>${turns}`;
}

/* ---------------- events ---------------- */
async function loadEvents() {
  let r; try { r = await api("/api/events"); } catch (e) { return; }
  const list = $("#event-list");
  if (!r.events || !r.events.length) { list.innerHTML = `<p class="muted" style="padding:10px">No events yet.</p>`; return; }
  list.innerHTML = r.events.map((e) =>
    `<div class="litem"><div class="t">${esc(e.kind)}</div><div class="s">${esc(e.ts)} · ${esc(e.detail || "")}</div></div>`).join("");
}

/* ---------------- model control ---------------- */
async function loadModel() {
  let r; try { r = await api("/api/model/status"); } catch (e) { return; }
  const st = r.status || {};
  $("#m-deps").textContent = st.deps_available ? "ready" : "off";
  $("#m-active").textContent = st.active_model ? st.active_model.version_name : "base";
  $("#m-embedded").textContent = st.embedded_terms || 0;
  let pairs = 0; try { const s = await api("/api/status"); pairs = (s.stats || {}).training_pair || 0; } catch (e) {}
  $("#m-pairs").textContent = pairs;
  const vl = $("#version-list");
  const vs = r.versions || [];
  vl.innerHTML = vs.length ? vs.map((v) => `
    <div class="litem ${v.is_active ? "active" : ""}" data-vid="${v.version_id}">
      <div class="t">${esc(v.version_name)} ${v.is_active ? '<span class="badge">active</span>' : ""}</div>
      <div class="s">${esc(v.base_model || "")} · ${v.pair_count || 0} pairs · ${esc((v.training_date || "").slice(0, 16))}</div>
      ${v.is_active ? "" : `<button class="btn sm ghost" style="margin-top:6px" data-activate="${v.version_id}">Activate</button>`}
    </div>`).join("") : `<p class="muted">No trained versions yet.</p>`;
  $$("#version-list [data-activate]").forEach((b) => b.onclick = async (ev) => {
    ev.stopPropagation();
    try { await jpost("/api/model/activate", { version_id: +b.dataset.activate }); toast("Model activated"); loadModel(); }
    catch (e) { toast("Activate failed: " + e.message); }
  });
  const noDeps = !st.deps_available;
  $("#btn-embed").disabled = noDeps; $("#btn-train").disabled = noDeps;
  const banner = $("#model-banner");
  if (noDeps) {
    banner.hidden = false; banner.className = "banner err";
    banner.innerHTML = "Embedding dependencies not installed. Run <code>pip install sentence-transformers</code> to enable training &amp; semantic search. Everything else works without it.";
  } else { banner.hidden = true; }
}

/* ---------------- graph (canvas force-ish layout) ---------------- */
async function loadGraph() {
  let g; try { g = await api("/api/graph"); } catch (e) { return; }
  STATE.graph = g;
  drawGraph(g);
}
function drawGraph(g) {
  const canvas = $("#graph-canvas");
  const dpr = window.devicePixelRatio || 1;
  const W = canvas.clientWidth || 800, H = 560;
  canvas.width = W * dpr; canvas.height = H * dpr;
  const ctx = canvas.getContext("2d"); ctx.scale(dpr, dpr);
  const nodes = (g.nodes || []).map((n, i) => ({ ...n, x: 0, y: 0, vx: 0, vy: 0 }));
  if (!nodes.length) { ctx.fillStyle = getCss("--muted"); ctx.font = "14px sans-serif"; ctx.fillText("No graph yet. Ingest chats and run extraction.", 20, 30); return; }
  const idx = new Map(nodes.map((n, i) => [n.id, i]));
  // init on a circle
  nodes.forEach((n, i) => {
    const a = (i / nodes.length) * Math.PI * 2;
    n.x = W / 2 + Math.cos(a) * Math.min(W, H) * 0.32;
    n.y = H / 2 + Math.sin(a) * Math.min(W, H) * 0.32;
  });
  const edges = (g.edges || []).map((e) => ({ s: idx.get(e.source), t: idx.get(e.target), w: e.strength || 1 }))
    .filter((e) => e.s != null && e.t != null);
  // simple force simulation
  for (let it = 0; it < 220; it++) {
    for (let i = 0; i < nodes.length; i++) for (let j = i + 1; j < nodes.length; j++) {
      const a = nodes[i], b = nodes[j];
      let dx = a.x - b.x, dy = a.y - b.y, d2 = dx * dx + dy * dy + 0.01;
      const f = 2600 / d2; const d = Math.sqrt(d2);
      a.vx += (dx / d) * f; a.vy += (dy / d) * f; b.vx -= (dx / d) * f; b.vy -= (dy / d) * f;
    }
    edges.forEach((e) => {
      const a = nodes[e.s], b = nodes[e.t];
      let dx = b.x - a.x, dy = b.y - a.y, d = Math.sqrt(dx * dx + dy * dy) + 0.01;
      const f = (d - 110) * 0.02;
      a.vx += (dx / d) * f; a.vy += (dy / d) * f; b.vx -= (dx / d) * f; b.vy -= (dy / d) * f;
    });
    nodes.forEach((n) => {
      n.x += n.vx * 0.5; n.y += n.vy * 0.5; n.vx *= 0.82; n.vy *= 0.82;
      n.x = Math.max(40, Math.min(W - 40, n.x)); n.y = Math.max(30, Math.min(H - 30, n.y));
    });
  }
  ctx.clearRect(0, 0, W, H);
  const accent = getCss("--accent"), accent2 = getCss("--accent-2"), line = getCss("--line-2"), text = getCss("--text");
  ctx.strokeStyle = line; ctx.lineWidth = 1;
  edges.forEach((e) => { const a = nodes[e.s], b = nodes[e.t]; ctx.beginPath(); ctx.moveTo(a.x, a.y); ctx.lineTo(b.x, b.y); ctx.stroke(); });
  const maxSize = Math.max(...nodes.map((n) => n.size || 1), 1);
  nodes.forEach((n) => {
    const r = 6 + 16 * Math.sqrt((n.size || 1) / maxSize);
    const grad = ctx.createLinearGradient(n.x - r, n.y - r, n.x + r, n.y + r);
    grad.addColorStop(0, accent2); grad.addColorStop(1, accent);
    ctx.beginPath(); ctx.arc(n.x, n.y, r, 0, Math.PI * 2); ctx.fillStyle = grad; ctx.fill();
    ctx.fillStyle = text; ctx.font = "12px -apple-system, sans-serif"; ctx.textAlign = "center";
    ctx.fillText(n.label, n.x, n.y + r + 13);
  });
  STATE._graphNodes = nodes;
  canvas.onclick = (ev) => {
    const rect = canvas.getBoundingClientRect();
    const mx = ev.clientX - rect.left, my = ev.clientY - rect.top;
    const hit = nodes.find((n) => Math.hypot(n.x - mx, n.y - my) < 22);
    if (hit) { show("search"); $("#search-q").value = hit.label; doSearch(); }
  };
}
function getCss(v) { return getComputedStyle(document.documentElement).getPropertyValue(v).trim() || "#888"; }

/* ---------------- actions (menus + buttons) ---------------- */
async function runAction(action) {
  switch (action) {
    case "ingest-sample":
      try { const r = await jpost("/api/ingest/sample", {}); toast(`Loaded sample: ${r.imported} chats`); refreshStatus(); }
      catch (e) { toast("Sample load failed: " + e.message); } break;
    case "open-files": $("#file-input").click(); show("ingest"); break;
    case "open-folder": show("ingest"); $("#folder-path").focus(); break;
    case "run-extract":
      try { await jpost("/api/extract", {}); toast("Extraction started"); startPolling(); }
      catch (e) { toast("Extract failed: " + e.message); } break;
    case "generate-pairs":
      try { const r = await jpost("/api/model/generate-pairs", {}); toast(`Generated ${r.pairs} pairs`); loadModel(); }
      catch (e) { toast("Failed: " + e.message); } break;
    case "export-db":
      try { const s = await api("/api/status"); await confirmModal("Database location", s.db_path); } catch (e) {} break;
    case "reset": {
      const ok = await confirmModal("Reset database", "This deletes all ingested chats, concepts and training data. Continue?");
      if (ok) { try { await jpost("/api/reset", {}); toast("Database reset"); refreshStatus(); loadConcepts(); } catch (e) { toast("Reset failed"); } }
      break;
    }
    case "goto-settings": show("settings"); break;
    case "view-dashboard": show("dashboard"); break;
    case "view-concepts": show("concepts"); break;
    case "view-search": show("search"); break;
    case "view-graph": show("graph"); break;
    case "view-chats": show("chats"); break;
    case "view-model": show("model"); break;
    case "view-events": show("events"); break;
    case "view-help": show("help"); break;
    case "toggle-theme": {
      const t = document.documentElement.dataset.theme === "dark" ? "light" : "dark";
      applyTheme(t, null); saveSettings({ theme: t }); break;
    }
    case "accent-violet": case "accent-teal": case "accent-amber": case "accent-rose": {
      const a = action.split("-")[1]; applyTheme(null, a); saveSettings({ accent: a }); break;
    }
    case "about":
      await confirmModal("Continuum 1.0",
        "A local knowledge & meaning engine. Your chats become an evidence-backed knowledge surface; a small embedding model finds concepts, a larger model reasons on top. Everything runs on your machine."); break;
  }
}

/* ---------------- wiring ---------------- */
function wire() {
  // menus open/close
  $$(".menu").forEach((m) => {
    m.querySelector(".menu-btn").onclick = (e) => {
      e.stopPropagation();
      const open = m.classList.contains("open");
      $$(".menu").forEach((x) => x.classList.remove("open"));
      if (!open) m.classList.add("open");
    };
  });
  document.addEventListener("click", () => $$(".menu").forEach((m) => m.classList.remove("open")));

  // any element with data-action
  $$("[data-action]").forEach((el) => el.addEventListener("click", () => runAction(el.dataset.action)));
  $$("[data-view-jump]").forEach((el) => el.addEventListener("click", () => show(el.dataset.viewJump)));

  // sidebar nav
  $$(".nav-item[data-view]").forEach((n) => n.onclick = () => show(n.dataset.view));

  // theme toggle button
  $("#theme-toggle").onclick = () => runAction("toggle-theme");

  // ingest
  $("#choose-files").onclick = () => $("#file-input").click();
  $("#file-input").onchange = (e) => uploadFiles(e.target.files);
  const dz = $("#dropzone");
  ["dragenter", "dragover"].forEach((ev) => dz.addEventListener(ev, (e) => { e.preventDefault(); dz.classList.add("drag"); }));
  ["dragleave", "drop"].forEach((ev) => dz.addEventListener(ev, (e) => { e.preventDefault(); dz.classList.remove("drag"); }));
  dz.addEventListener("drop", (e) => uploadFiles(e.dataTransfer.files));
  $("#folder-import").onclick = async () => {
    const path = $("#folder-path").value.trim(); if (!path) return;
    try { const r = await jpost("/api/ingest/folder", { path }); toast(`Imported ${r.imported}/${r.files} files`); refreshStatus(); }
    catch (e) { toast("Import failed: " + e.message); }
  };
  $("#paste-import").onclick = async () => {
    const text = $("#paste-text").value.trim(); if (!text) { toast("Nothing to ingest"); return; }
    try { await jpost("/api/ingest/paste", { text, title: $("#paste-title").value.trim() || "Pasted chat" }); toast("Pasted chat ingested"); $("#paste-text").value = ""; refreshStatus(); }
    catch (e) { toast("Failed: " + e.message); }
  };

  // concepts
  $("#concept-filter").oninput = debounce(loadConcepts, 220);

  // search
  $("#search-go").onclick = doSearch;
  $("#search-q").addEventListener("keydown", (e) => { if (e.key === "Enter") doSearch(); });
  $("#copy-briefing").onclick = () => {
    const t = $("#briefing-text").textContent; navigator.clipboard?.writeText(t); toast("Briefing copied");
  };

  // model
  $("#btn-genpairs").onclick = () => runAction("generate-pairs");
  $("#btn-embed").onclick = async () => { try { await jpost("/api/model/embed", {}); toast("Embedding started"); startPolling(); } catch (e) { toast("Failed: " + e.message); } };
  $("#btn-train").onclick = async () => {
    const epochs = +$("#train-epochs").value || 1;
    try { await jpost("/api/model/train", { epochs }); toast("Training started"); startPolling(); }
    catch (e) { toast("Train failed: " + e.message); }
  };

  // settings
  $$("[data-set-theme]").forEach((b) => b.onclick = () => { applyTheme(b.dataset.setTheme, null); saveSettings({ theme: b.dataset.setTheme }); });
  $$("[data-set-accent]").forEach((b) => b.onclick = () => { applyTheme(null, b.dataset.setAccent); saveSettings({ accent: b.dataset.setAccent }); });
  $("#set-minocc").onchange = () => saveSettings({ min_occurrences: $("#set-minocc").value });
}
function debounce(fn, ms) { let t; return (...a) => { clearTimeout(t); t = setTimeout(() => fn(...a), ms); }; }

/* ---------------- boot ---------------- */
async function boot() {
  wire();
  try {
    const s = await api("/api/settings");
    applyTheme(s.theme, s.accent);
    $("#set-minocc").value = s.min_occurrences || "2";
  } catch (e) { applyTheme("dark", "violet"); }
  await refreshStatus();
  // if a job is already running, resume polling
  try { const j = await api("/api/job"); if (j.running) startPolling(); } catch (e) {}
}
boot();
