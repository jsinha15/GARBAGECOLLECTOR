// HeapLens browser UI: a thin visualization layer over the runtime's event stream.
(function () {
  'use strict';
  const M = window.HeapLens;
  const $ = (id) => document.getElementById(id);

  const app = {
    snap: null,       // latest committed snapshot
    st: null,         // what is on screen (a replay state)
    layout: null,
    selected: null,
    replay: null,     // {events, idx, before, after, done, timer, playing, command, changes}
    explain: null,    // {title, why, changes[]}
    lastMessage: '',
    demos: [],
    log: [],
  };

  // ------------------------------------------------------------ DOM helpers
  function el(tag, props, ...kids) {
    const e = document.createElement(tag);
    for (const k in props || {}) {
      const v = props[k];
      if (v === null || v === undefined || v === false) continue;
      if (k === 'class') e.className = v;
      else if (k === 'text') e.textContent = v;
      else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else if (k === 'style') e.style.cssText = v;
      else e.setAttribute(k, v === true ? '' : v);
    }
    for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false) e.append(c.nodeType ? c : document.createTextNode(String(c)));
    return e;
  }
  const SVG = 'http://www.w3.org/2000/svg';
  function sv(tag, props, ...kids) {
    const e = document.createElementNS(SVG, tag);
    for (const k in props || {}) {
      const v = props[k];
      if (v === null || v === undefined) continue;
      if (k === 'text') e.textContent = v;
      else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else e.setAttribute(k, v);
    }
    for (const c of kids.flat()) if (c) e.append(c);
    return e;
  }

  // ------------------------------------------------------------------- API
  async function api(path) {
    const r = await fetch(path, { cache: 'no-store' });
    if (!r.ok) throw new Error(`${r.status} ${await r.text()}`);
    return r.json();
  }

  function busy() { return !!(app.replay && !app.replay.done); }

  // ---- loaders and toasts
  let pending = 0, busyAt = 0;
  function busyOn(text) {
    pending++;
    busyAt = Date.now();
    $('busy-text').textContent = text;
    $('busy').hidden = false;
    document.body.classList.add('working');
  }
  function busyOff() {
    pending = Math.max(0, pending - 1);
    if (pending > 0) return;
    // keep the spinner visible for a moment so quick operations are still noticed
    setTimeout(() => { if (pending === 0) { $('busy').hidden = true; document.body.classList.remove('working'); } }, Math.max(0, 450 - (Date.now() - busyAt)));
  }
  function toast(msg, kind) {
    const t = el('div', { class: 'toast ' + (kind || 'info'), text: msg, role: 'status' });
    $('toasts').append(t);
    setTimeout(() => { t.classList.add('out'); setTimeout(() => t.remove(), 300); }, kind === 'err' ? 7000 : 2600);
  }

  async function run(cmd, opts) {
    opts = opts || {};
    if (busy() && !opts.force) { say('Finish (or skip) the current GC replay first.', true); return null; }
    stopPlaying();
    let res;
    busyOn(/^gc\b/.test(cmd) ? 'Running the garbage collector…' : /^(reset|demo|algo|set)\b/.test(cmd) ? 'Setting up the runtime…' : 'Running on the runtime…');
    try {
      res = await api('/api/cmd?c=' + encodeURIComponent(cmd));
    } catch (e) {
      say('Cannot reach the HeapLens server: ' + e.message, true);
      toast('Cannot reach the HeapLens server. Is it still running?', 'err');
      $('conn').textContent = 'disconnected';
      $('conn').className = 'pill bad';
      busyOff();
      return null;
    }
    busyOff();
    if (!res.ok && !opts.quiet) toast(res.message, 'err');
    log('> ' + cmd, 'cmd');
    if (res.message) log(res.message, res.ok ? 'out' : 'err');
    app.lastMessage = res.message;
    app.snap = res.after;
    if (/^(reset|demo|algo|set)\b/.test(cmd)) app.lastBarrier = null;
    for (const e of res.events || []) if (e.type === 'WriteBarrierTriggered') app.lastBarrier = { src: e.id, dst: e.id2, remembered: true };
    if (res.replay && opts.replay !== false) {
      startReplay(res, !!opts.autoplay, cmd);
    } else {
      app.replay = null;
      showSnapshot(res.after);
      if (!res.ok) app.explain = { title: 'COMMAND FAILED', why: res.message, changes: [] };
      else if (res.message) app.explain = { title: 'LAST COMMAND', why: res.message, changes: [] };
      if (!opts.quiet) renderEvents(res.events, -1);
    }
    if (!opts.quiet) renderAll();
    return res;
  }

  function showSnapshot(snap) {
    app.snap = snap;
    app.st = M.makeState(snap);
    app.layout = M.layoutGraph(snap);
    if (app.lastBarrier && app.st.objs[app.lastBarrier.src] && app.st.objs[app.lastBarrier.dst]) app.st.barrier = Object.assign({}, app.lastBarrier);
    if (app.selected && !app.st.objs[app.selected]) app.selected = null;
  }

  // ----------------------------------------------------------------- replay
  function startReplay(res, autoplay, cmd) {
    app.replay = { events: res.events, idx: 0, before: res.before, after: res.after, done: false, timer: null, playing: false, command: cmd };
    app.st = M.makeState(res.before);
    app.st.replaying = true;
    app.layout = M.layoutGraph(res.before);
    app.explain = {
      title: 'BEFORE GC',
      why: 'The heap exactly as it was when the collection was requested. ' + (res.events.length ? res.events.length + ' events are queued; press Step to execute the collector one logical step at a time.' : ''),
      changes: [],
    };
    renderEvents(res.events, -1);
    if (autoplay) play();
  }

  function stepOnce() {
    const rp = app.replay;
    if (!rp || rp.done) return;
    if (rp.idx >= rp.events.length) { finishReplay(); return; }
    let ev = rp.events[rp.idx++];
    let changes = M.applyEvent(app.st, ev, { after: rp.after });
    let text = ev.text;
    // ObjectDiscovered and ObjectMarked are one logical step
    if (ev.type === 'ObjectDiscovered' && rp.idx < rp.events.length) {
      const nx = rp.events[rp.idx];
      if (nx.type === 'ObjectMarked' && nx.id === ev.id) {
        rp.idx++;
        changes = changes.concat(M.applyEvent(app.st, nx, { after: rp.after }));
        app.st.objState[ev.id] = 'DISCOVERED';
        text += ' ' + nx.text;
      }
    }
    app.explain = { title: M.eventTitle(ev) + (ev.phase ? '   ·   ' + ev.phase : ''), why: text, changes, ev };
    if (ev.type === 'WriteBarrierTriggered') app.explain.barrier = true;
    renderAll();
    renderEvents(rp.events, rp.idx - 1);
    if (rp.idx >= rp.events.length) { stopPlaying(); finishReplay(true); }
  }

  function finishReplay(keepExplain) {
    const rp = app.replay;
    if (!rp) return;
    stopPlaying();
    rp.done = true;
    rp.idx = rp.events.length;
    const last = app.explain;
    const keepSel = app.selected;
    app.st = M.makeState(rp.after);
    app.layout = M.layoutGraph(rp.after);
    app.selected = keepSel && app.st.objs[keepSel] ? keepSel : null;
    app.snap = rp.after;
    app.explain = {
      title: 'GC COMPLETE · FINAL HEAP',
      why: (keepExplain && last && last.why ? last.why + ' ' : '') + 'This is the heap after the collection: compare it with the BEFORE state (Replay).',
      changes: [],
    };
    renderAll();
    renderEvents(rp.events, rp.events.length - 1);
    if (api_.onReplayDone) api_.onReplayDone();
  }

  function play() {
    const rp = app.replay;
    if (!rp || rp.done) return;
    rp.playing = true;
    const tick = () => {
      if (!app.replay || app.replay !== rp || rp.done || !rp.playing) return;
      stepOnce();
      if (!rp.done && rp.playing) rp.timer = setTimeout(tick, delay());
    };
    rp.timer = setTimeout(tick, 150);
    renderControls();
  }

  function stopPlaying() {
    const rp = app.replay;
    if (rp) { rp.playing = false; if (rp.timer) clearTimeout(rp.timer); rp.timer = null; }
  }

  function delay() { return 1100 - Number($('speed').value) * 100; }

  function restartReplay() {
    const rp = app.replay;
    if (!rp) return;
    stopPlaying();
    rp.idx = 0;
    rp.done = false;
    app.st = M.makeState(rp.before);
    app.st.replaying = true;
    app.layout = M.layoutGraph(rp.before);
    app.explain = { title: 'BEFORE GC', why: 'Replaying from the start.', changes: [] };
    renderAll();
    renderEvents(rp.events, -1);
  }

  // ------------------------------------------------------------------- log
  function log(text, cls) {
    app.log.push({ text, cls });
    if (app.log.length > 200) app.log.shift();
    const c = $('console');
    if (!c) return;
    c.append(el('div', { class: 'log-' + (cls || 'out'), text }));
    c.scrollTop = c.scrollHeight;
  }
  function say(text, isErr) {
    app.explain = { title: isErr ? 'NOTICE' : 'INFO', why: text, changes: [] };
    renderExplain();
  }

  // ---------------------------------------------------------------- render
  function renderAll() {
    renderControls();
    renderHeap();
    renderGraph();
    renderTimeline();
    renderSafepoint();
    renderExplain();
    renderInspector();
    renderAlgoPanel();
    renderStats();
    renderLegend();
    renderNarrator();
    if (api_.afterRender) api_.afterRender();
  }

  // plain-English "right now" sentence under the timeline
  function renderNarrator() {
    const n = $('narrator');
    const rp = app.replay;
    $('progress').hidden = !rp;
    if (rp && !rp.done) {
      const ph = app.st.phase;
      n.textContent = (M.PHASE_INFO[ph] ? ph + ' - ' + M.PHASE_INFO[ph] : 'The collector is starting. The program is paused while it works.');
    } else if (rp && rp.done) {
      n.textContent = 'The collection is finished and the program continues. Compare the heap with the BEFORE state using Replay.';
    } else {
      n.textContent = 'No collection is running. Garbage stays in memory until a GC runs (gc(), or automatically when memory fills up).';
    }
  }

  function renderControls() {
    const rp = app.replay;
    const replaying = busy();
    $('heapsize').disabled = replaying;
    $('b-step').disabled = !replaying;
    $('b-continue').disabled = !replaying || (rp && rp.playing);
    $('b-finish').disabled = !replaying;
    $('b-restart').disabled = !rp;
    $('replaybar').hidden = !rp;
    $('rb-spin').hidden = !(rp && rp.playing);
    $('pbar').style.width = rp ? Math.round(100 * (rp.done ? rp.events.length : rp.idx) / Math.max(1, rp.events.length)) + '%' : '0%';
    $('b-continue').textContent = rp && rp.playing ? 'Playing…' : 'Auto ▶▶';
    const algo = app.snap ? app.snap.algo : '';
    $('algo').value = algo;
    $('heapsize').value = String(app.snap.config.heapBytes);
    $('stepinfo').textContent = rp
      ? (rp.done ? `finished (${rp.events.length} events)` : `step ${rp.idx} of ${rp.events.length}`)
      : '';
  }

  function objLabel(id) { return app.snap ? M.objName({ objs: indexById(app.snap) }, id) : '#' + id; }
  function indexById(snap) { const m = {}; for (const o of snap.objects) m[o.id] = o; return m; }

  // ---- heap view
  const ABBR = { LIVE: 'LIVE', REACHABLE: 'REACH', UNREACHABLE: 'DEAD', DISCOVERED: 'DISC', SCANNED: 'SCAN', RECLAIMED: 'FREED',
    UNVISITED: 'WHITE', MOVED: 'MOVED', 'WILL MOVE': 'MOVE?', STAYS: 'STAY', SURVIVED: 'SURV', PROMOTED: 'PROMO',
    'NOT TRACED': 'OLD', 'UNREACHABLE CYCLE': 'CYCLE' };
  function stateClass(label) { return 'st-' + label.toLowerCase().replace(/[^a-z]+/g, '-'); }

  function renderHeap() {
    const st = app.st;
    const box = $('heap');
    box.replaceChildren();
    const algo = st.snap.algo;
    const rows = M.heapRows(st);
    $('heap-sub').textContent = `${st.snap.algoName} · ${st.snap.config.heapBytes} bytes from ${M.hex(st.snap.config.heapBase)}`;
    let lastGroup = '';
    for (const row of rows) {
      const r = row.region;
      if (algo === 'generational') {
        const group = r.kind === 'Old' ? 'OLD GENERATION' : 'YOUNG GENERATION';
        if (group !== lastGroup) { box.append(el('div', { class: 'group', text: group })); lastGroup = group; }
      }
      const head = el('div', { class: 'region-head' },
        el('strong', { text: r.name }),
        el('span', { class: 'dim', text: `  ${M.hex(r.start)}–${M.hex(r.end)} · ${r.alloc} allocator · used ${row.stats.used}/${r.end - r.start} B · ${row.stats.freeRuns} free block(s) · fragmentation ${row.stats.fragmentation}%` }));
      const bar = el('div', { class: 'bar' });
      for (const b of row.blocks) {
        if (b.kind === 'free') {
          bar.append(el('div', { class: 'block free', style: `flex:${b.size} 1 0`, title: `FREE ${b.size} bytes at ${M.hex(b.addr)}` },
            el('span', { class: 'bl', text: 'FREE' }), el('span', { class: 'bs', text: b.size + 'B' })));
          continue;
        }
        const o = st.objs[b.id];
        const label = M.stateLabel(st, b.id);
        const classes = ['block', b.kind === 'reclaimed' ? 'reclaimed' : 'obj', stateClass(label)];
        if (app.selected === b.id) classes.push('selected');
        if (st.focus.ids.includes(b.id)) classes.push('focus');
        const extra = algo === 'rc' ? `rc${o.refCount}` : algo === 'generational' ? `age ${o.age}` : '';
        bar.append(el('div', { class: classes.join(' '), style: `flex:${b.size} 1 0`, onclick: () => select(b.id),
          title: `${o.label || 'Object'} #${b.id} · ${M.hex(o.addr)} · ${o.size} B · ${label}${o.fwd ? ' · forwarding ' + M.hex(o.fwd) : ''}` },
          el('span', { class: 'bl', text: o.label || '#' + b.id }),
          el('span', { class: 'bs', text: ABBR[label] || label }),
          extra ? el('span', { class: 'bx', text: extra }) : null));
      }
      box.append(el('div', { class: 'region' }, head, bar));
    }
  }

  // ---- graph view
  function renderGraph() {
    const st = app.st, L = app.layout;
    const box = $('graph');
    box.replaceChildren();
    const svg = sv('svg', { viewBox: `0 0 ${L.width} ${L.height}`, class: 'graphsvg', role: 'img', 'aria-label': 'object graph' });
    svg.append(sv('defs', {},
      sv('marker', { id: 'arr', viewBox: '0 0 10 10', refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse' }, sv('path', { d: 'M0,0 L10,5 L0,10 z', class: 'arrowhead' })),
      sv('marker', { id: 'arr-hot', viewBox: '0 0 10 10', refX: 9, refY: 5, markerWidth: 7, markerHeight: 7, orient: 'auto-start-reverse' }, sv('path', { d: 'M0,0 L10,5 L0,10 z', class: 'arrowhead hot' }))));
    if (L.garbageTop !== null && L.garbageTop !== undefined) {
      svg.append(sv('line', { x1: 0, x2: L.width, y1: L.garbageTop, y2: L.garbageTop, class: 'divider' }));
      svg.append(sv('text', { x: 8, y: L.garbageTop + 14, class: 'divider-label', text: 'NOT REACHABLE FROM ANY ROOT (before GC)' }));
    }
    const NODE_W = 74, NODE_H = 46;
    // a curved arrow from (sx,sy) to (ex,ey); `curve` bends it sideways so A->B and B->A do not overlap
    const edge = (sx, sy, ex, ey, hot, slot, curve) => {
      const len = Math.max(1, Math.hypot(ex - sx, ey - sy));
      const nx = -(ey - sy) / len, ny = (ex - sx) / len;
      const cx = (sx + ex) / 2 + nx * curve, cy = (sy + ey) / 2 + ny * curve;
      const g = sv('g', {});
      g.append(sv('path', { d: `M${sx},${sy} Q${cx},${cy} ${ex},${ey}`, class: 'edge' + (hot ? ' hot' : ''), 'marker-end': hot ? 'url(#arr-hot)' : 'url(#arr)' }));
      if (slot !== null) g.append(sv('text', { x: cx, y: cy - 3, class: 'edge-slot', text: slot }));
      return g;
    };
    // endpoints on the borders of two node boxes
    const between = (a, b) => {
      if (Math.abs(b.y - a.y) > 8) {
        const d = b.y > a.y ? 1 : -1;
        return [a.x, a.y + d * NODE_H / 2, b.x, b.y - d * NODE_H / 2];
      }
      const d = b.x > a.x ? 1 : -1;
      return [a.x + d * NODE_W / 2, a.y, b.x - d * NODE_W / 2, b.y];
    };
    const objs = st.snap.objects.filter((o) => st.objs[o.id]).map((o) => st.objs[o.id]);
    const rootPos = L.rootPos;
    // edges
    for (const r of st.snap.roots) {
      const rp = rootPos[r.id];
      const tp = L.pos[r.target];
      if (!rp || !tp) continue;
      const hot = st.focus.root === r.id;
      svg.append(edge(rp.x, rp.y + 12, tp.x, tp.y - NODE_H / 2, hot, null, 0));
    }
    for (const o of objs) {
      const a = L.pos[o.id];
      if (!a) continue;
      for (const r of o.refs) {
        if (!r.target) continue;
        const b = L.pos[r.target];
        if (!b || !st.objs[r.target]) continue;
        const hot = st.focus.edge && st.focus.edge[0] === o.id && st.focus.edge[1] === r.target;
        const self = o.id === r.target;
        if (self) {
          svg.append(sv('path', { d: `M${a.x + 20},${a.y - 22} c 26,-34 56,-6 22,20`, class: 'edge' + (hot ? ' hot' : ''), 'marker-end': hot ? 'url(#arr-hot)' : 'url(#arr)' }));
        } else {
          const back = b.y < a.y || (b.y === a.y && b.x < a.x);
          svg.append(edge(...between(a, b), hot, r.slot, back ? 26 : 10));
        }
      }
    }
    // roots
    for (const r of st.snap.roots) {
      const p = rootPos[r.id];
      if (!p) continue;
      const hot = st.focus.root === r.id;
      svg.append(sv('g', { class: 'rootnode' + (hot ? ' focus' : '') },
        sv('rect', { x: p.x - 38, y: p.y - 12, width: 76, height: 24, rx: 4 }),
        sv('text', { x: p.x, y: p.y + 4, 'text-anchor': 'middle', text: `ROOT ${r.name}` })));
    }
    // objects
    for (const o of objs) {
      const p = L.pos[o.id];
      if (!p) continue;
      const label = M.stateLabel(st, o.id);
      const cls = ['node', stateClass(label)];
      if (app.selected === o.id) cls.push('selected');
      if (st.focus.ids.includes(o.id)) cls.push('focus');
      if (o.reclaimed) cls.push('reclaimed');
      const extra = st.snap.algo === 'rc' ? ` · rc=${o.refCount}` : st.snap.algo === 'generational' ? ` · age ${o.age}` : '';
      svg.append(sv('g', { class: cls.join(' '), onclick: () => select(o.id) },
        sv('title', { text: `${o.label || 'Object'} #${o.id} at ${M.hex(o.addr)} — ${label}` }),
        sv('rect', { x: p.x - NODE_W / 2, y: p.y - NODE_H / 2, width: NODE_W, height: NODE_H, rx: 8 }),
        sv('text', { x: p.x, y: p.y - 5, 'text-anchor': 'middle', class: 'nl', text: o.label || '#' + o.id }),
        sv('text', { x: p.x, y: p.y + 8, 'text-anchor': 'middle', class: 'ns', text: `#${o.id}${extra}` }),
        sv('text', { x: p.x, y: p.y + 19, 'text-anchor': 'middle', class: 'nt', text: label })));
    }
    box.append(svg);
  }

  // ---- timeline + safepoint strip
  function renderTimeline() {
    const rp = app.replay;
    const t = $('timeline');
    t.replaceChildren();
    if (!app.snap) return;
    const phases = M.phasesFor(app.snap.algo, rp ? rp.events : []);
    const cur = app.st.phase;
    const ci = phases.indexOf(cur);
    phases.forEach((p, i) => {
      const cls = 'phase' + (rp && !rp.done && p === cur ? ' current' : '') + (rp && (rp.done || (ci >= 0 && i < ci)) ? ' done' : '');
      t.append(el('div', { class: cls, text: p }));
      if (i < phases.length - 1) t.append(el('span', { class: 'arrow', text: '→' }));
    });
    if (!phases.length) t.append(el('span', { class: 'dim', text: 'no collector selected' }));
  }

  function renderSafepoint() {
    const sp = app.st.snap.safepoint;
    const box = $('safepoint');
    box.replaceChildren();
    const chain = el('div', { class: 'chain' });
    M.SAFEPOINT_CHAIN.forEach((s, i) => {
      chain.append(el('span', { class: 'phase small' + (sp.state === s ? ' current' : ''), text: s.replace('_', ' ') }));
      if (i < M.SAFEPOINT_CHAIN.length - 1) chain.append(el('span', { class: 'arrow', text: '→' }));
    });
    box.append(el('div', { class: 'dim', text: 'Stop-the-world coordination (simulated application threads):' }), chain);
    for (const t of sp.threads) {
      const stopped = t.state === 'AT_SAFEPOINT';
      box.append(el('div', { class: 'thread' },
        el('span', { class: 'tname', text: 'Thread ' + t.index }),
        el('span', { class: 'lane' + (stopped ? ' stopped' : '') }, el('span', { class: 'dot' })),
        el('span', { class: 'tstate', text: stopped ? '● STOPPED at safepoint' : 'running…' })));
    }
  }

  // ---- explanation panel
  function renderExplain() {
    const body = $('explain-body');
    const x = app.explain;
    body.replaceChildren();
    body.classList.toggle('dim', !x);
    if (!x) { body.append('Create some objects, give a few of them roots, then press Step GC.'); return; }
    body.append(el('div', { class: 'ex-title', text: x.title }));
    body.append(el('div', { class: 'ex-label', text: 'WHY' }), el('div', { class: 'ex-why', text: x.why }));
    if (x.changes && x.changes.length) {
      body.append(el('div', { class: 'ex-label', text: 'WHAT CHANGED' }), el('ul', { class: 'ex-changes' }, x.changes.map((c) => el('li', { text: c }))));
    }
    if (app.selected && app.st.lastRefUpdate && app.st.lastRefUpdate.target === app.selected) {
      const u = app.st.lastRefUpdate;
      body.append(el('div', { class: 'ex-label', text: 'REFERENCE REWRITE' }),
        el('div', { class: 'mono', text: `${u.where}\n  old reference → ${M.hex(u.from)}\n  new reference → ${M.hex(u.to)}` }));
    }
  }

  // ---- inspector
  function renderInspector() {
    const box = $('inspector');
    box.replaceChildren();
    box.classList.remove('dim');
    const st = app.st;
    const o = app.selected ? st.objs[app.selected] : null;
    $('inspcard').hidden = !o;
    if (!o) return;
    const label = M.stateLabel(st, o.id);
    const kv = (k, v, cls) => el('div', { class: 'kv' }, el('span', { class: 'k', text: k }), el('span', { class: 'v ' + (cls || ''), text: v }));
    const region = st.snap.regions.find((r) => o.addr >= r.start && o.addr < r.end);
    box.append(
      el('div', { class: 'insp-title', text: `${o.label || 'Object'}  #${o.id}` }),
      kv('Type', o.type), kv('Address', M.hex(o.addr) + (o.fwd ? `  → ${M.hex(o.fwd)} (forwarding)` : '')),
      kv('Size', `${o.size} bytes (${o.numRefs} ref slot(s), ${o.dataBytes} data)`),
      kv('Generation / space', region ? region.name : '?'),
      kv('Age', String(o.age)), kv('Reference count', String(o.refCount)),
      kv('GC state', label, stateClass(label)),
      el('div', { class: 'k', text: 'References' }),
      o.refs.filter((r) => r.target).length
        ? el('ul', {}, o.refs.filter((r) => r.target).map((r) => el('li', { class: 'link', onclick: () => select(r.target), text: `[${r.slot}] → ${M.objName(st, r.target)} @ ${M.hex(r.addr)}` })))
        : el('div', { class: 'dim', text: '(none)' }),
      el('div', { class: 'k', text: 'Referenced by' }));
    const by = [];
    for (const x of Object.values(st.objs)) for (const r of x.refs) if (r.target === o.id) by.push(el('li', { class: 'link', onclick: () => select(x.id), text: `${M.objName(st, x.id)} [slot ${r.slot}]` }));
    for (const r of st.snap.roots) if (r.target === o.id) by.push(el('li', { text: `ROOT '${r.name}' (root #${r.id})` }));
    box.append(by.length ? el('ul', {}, by) : el('div', { class: 'dim', text: '(nothing)' }));
    box.append(el('div', { class: 'k', text: 'Why is it ' + (o.live ? 'live?' : 'garbage?') }), el('div', { class: 'why ' + (o.live ? 'yes' : 'no'), text: o.live ? o.why : o.why }));
    if (o.reclaimed) box.append(el('div', { class: 'why no', text: 'This object has been RECLAIMED: its bytes are free again.' }));
  }

  // ---- algorithm specific panel
  function renderAlgoPanel() {
    const box = $('algo-panel');
    box.replaceChildren();
    const st = app.st, S = st.snap;
    $('algo-title').textContent = S.algoName + ' state';
    if (S.algo === 'rc') {
      box.append(el('div', { class: 'dim', text: 'Every object carries a count of references to it. Counts are updated on every store; at 0 the object is freed at once.' }));
      const rows = Object.values(st.objs).filter((o) => !o.reclaimed).sort((a, b) => a.id - b.id);
      box.append(el('div', { class: 'rc-list' }, rows.map((o) => el('span', { class: 'chip ' + (o.live ? '' : 'bad'), text: `${o.label || '#' + o.id}: refCount = ${o.refCount}` }))));
      const cycles = (S.cycles || []).filter((c) => c.every((id) => st.objs[id]));
      if (cycles.length) {
        box.append(el('div', { class: 'alert' },
          el('strong', { text: 'UNREACHABLE CYCLE: ' + cycles.map((c) => c.map((id) => M.objName(st, id)).join(' ⇄ ')).join('; ') }),
          el('div', { text: 'Reference counting cannot reclaim this cycle because each object still has an incoming reference from another unreachable object. No count ever reaches 0. A tracing collector (Mark-Sweep) would reclaim it - try “demo 2 marksweep”.' })));
      }
    } else if (S.algo === 'generational') {
      renderGenerational(box, st, S);
    } else if (S.algo === 'copying') {
      box.append(el('div', { class: 'dim', text: 'New objects are bump-allocated in FROM-SPACE. A collection copies the live objects into TO-SPACE (leaving forwarding pointers), then the two spaces swap roles. Garbage is never touched, and survivors end up packed together - there is no fragmentation.' }));
    } else if (S.algo === 'compact') {
      box.append(el('div', { class: 'dim', text: 'Mark → compute forwarding addresses → update references → slide objects. Live objects keep their order and the free space becomes one block. Old and new addresses are shown in the inspector and explanation.' }));
    } else {
      box.append(el('div', { class: 'dim', text: 'Mark from the roots (WHITE → GRAY → BLACK), then sweep every WHITE object back to the free list. Live objects never move, so free space can fragment.' }));
    }
  }

  function renderGenerational(box, st, S) {
    box.append(el('div', { class: 'dim', text: `Promotion threshold: an object is promoted to Old when it survives a minor GC with age ≥ ${S.config.promotionThreshold}. Minor GC scans the ${S.config.useCardTable ? 'DIRTY CARDS' : 'REMEMBERED SET'} for old→young pointers.` }));
    const nm = (id) => M.objName(st, id);
    const b = st.barrier;
    const flow = el('div', { class: 'barrier' });
    if (b && st.objs[b.src] && st.objs[b.dst]) {
      flow.append(el('div', { class: 'bx1', text: `WRITE   Old ${nm(b.src)}  ──►  Young ${nm(b.dst)}` }),
        el('div', { class: 'bx2', text: '↓ WRITE BARRIER (runs on every pointer store)' }),
        el('div', { class: 'bx3', text: `REMEMBERED SET ← ${nm(b.src)}` + (b.remembered ? '' : '   (recording…)') }),
        el('div', { class: 'dim', text: 'Why: a minor GC does not trace the old generation. Without this record, ' + nm(b.dst) + ' would look unreachable and be freed while Old ' + nm(b.src) + ' still points at it.' }));
    } else {
      flow.append(el('div', { class: 'dim', text: 'Store a young object into an old one (ref <old> 0 <young>) to see the write barrier fire.' }));
    }
    box.append(flow);
    box.append(el('div', { class: 'k', text: 'Remembered set (old objects that point into the young generation)' }),
      S.remembered.length ? el('div', {}, S.remembered.map((id) => el('span', { class: 'chip', text: nm(id) }))) : el('span', { class: 'dim', text: '(empty)' }));
    const c = S.cards;
    box.append(el('div', { class: 'k', text: `Card table (${c.cardBytes} bytes per card over the Old generation)` }));
    const cells = el('div', { class: 'cards' });
    for (let i = 0; i < c.count; i++) cells.append(el('span', { class: 'card' + (c.dirty.includes(i) ? ' dirty' : ''), title: `card #${i} ${c.dirty.includes(i) ? 'DIRTY' : 'clean'}`, text: c.dirty.includes(i) ? 'D' : '·' }));
    box.append(cells);
  }

  // ---- statistics
  function renderStats() {
    const box = $('stats');
    box.replaceChildren();
    const s = app.snap.stats;
    const frag = s.fragmentation;
    const rows = [
      ['Heap used', `${s.heapUsed} / ${s.heapSize} B`], ['Objects', s.liveObjects], ['Free blocks', s.freeBlocks],
      ['Largest free block', s.largestFree + ' B'], ['Fragmentation', frag + '%'],
      ['Allocations', s.allocations], ['Collections', `${s.collections} (${s.minorCollections} minor / ${s.majorCollections} major)`],
      ['Reclaimed (total)', `${s.reclaimedObjects} objects / ${s.reclaimedBytes} B`],
      ['Last GC', `marked ${s.lastMarked} · reclaimed ${s.lastReclaimedObjects} · moved ${s.lastMoved} · promoted ${s.lastPromoted}`],
      ['Refs rewritten', s.referencesUpdated], ['Write barrier hits', s.barrierHits], ['Safepoint stops', s.safepointStops],
    ];
    box.append(el('div', { class: 'stats' }, rows.map(([k, v]) => el('div', { class: 'kv' }, el('span', { class: 'k', text: k }), el('span', { class: 'v', text: String(v) })))));
    box.append(el('div', { class: 'fragbar', title: 'fragmentation' }, el('span', { style: `width:${Math.min(100, frag)}%` })));
  }

  function renderLegend() {
    const lg = $('legend');
    if (lg.childElementCount) return;
    const items = [['LIVE', 'reachable (marked)'], ['DISCOVERED', 'found, waiting in the worklist (gray)'], ['SCANNED', 'its references were followed (black)'],
      ['UNVISITED', 'not reached yet (white)'], ['UNREACHABLE', 'no path from any root'], ['RECLAIMED', 'memory returned to the heap'],
      ['MOVED', 'relocated to a new address'], ['PROMOTED', 'moved to the old generation']];
    for (const [k, d] of items) lg.append(el('span', { class: 'lg ' + stateClass(k), title: d, text: k }));
  }

  // ---- event list
  function renderEvents(events, current) {
    const box = $('events');
    box.replaceChildren();
    if (!events || !events.length) { box.append(el('div', { class: 'dim', text: 'No events yet. Run a GC.' })); return; }
    events.forEach((ev, i) => {
      box.append(el('div', { class: 'ev' + (i === current ? ' current' : '') + (current >= 0 && i > current ? ' future' : ''), id: 'ev' + i },
        el('span', { class: 'evn', text: '#' + ev.seq }), el('span', { class: 'evt', text: ev.type }), el('span', { class: 'evx', text: ev.text })));
    });
    const c = current >= 0 ? $('ev' + current) : null;
    if (c) box.scrollTop = Math.max(0, c.offsetTop - box.clientHeight / 2);
  }

  function renderVarsHook() { if (api_.afterRender) api_.afterRender(); }

  function select(id) {
    app.selected = id;
    renderHeap(); renderGraph(); renderInspector(); renderExplain(); renderVarsHook();
  }

  // ------------------------------------------------------------- bind UI
  function bind() {
    $('insp-x').onclick = () => { app.selected = null; renderHeap(); renderGraph(); renderInspector(); renderVarsHook(); };
    $('b-step').onclick = () => { stopPlaying(); stepOnce(); renderControls(); };
    $('b-continue').onclick = () => play();
    $('b-finish').onclick = () => finishReplay(false);
    $('b-restart').onclick = restartReplay;
    $('heapsize').onchange = () => run('set heap ' + $('heapsize').value);
    document.addEventListener('keydown', (e) => {
      if (e.target && /INPUT|SELECT|TEXTAREA/.test(e.target.tagName)) return;
      if ((e.key === 'n' || e.key === 'ArrowRight') && busy()) { stopPlaying(); stepOnce(); renderControls(); }
    });
  }

  // ------------------------------------------------------------------ init
  async function init() {
    bind();
    try {
      const snap = await api('/api/state');
      $('conn').textContent = 'connected';
      $('conn').className = 'pill ok';
      showSnapshot(snap);
      renderAll();
      renderEvents([], -1);
      log('HeapLens ready.', 'out');
      if (api_.onReady) api_.onReady();
    } catch (e) {
      $('conn').textContent = 'server not reachable';
      $('conn').className = 'pill bad';
      $('explain-body').textContent = 'Start the backend first:  minigc --serve   then open http://127.0.0.1:8080/';
    }
  }
  function setExplain(x) { app.explain = x; renderExplain(); }
  function selectObject(id) { select(id); }

  // small API for playground.js (the code editor)
  const api_ = window.MiniApp = { app, run, stepOnce, busy, play, stopPlaying, finishReplay, renderAll, setExplain, log, say,
    select: selectObject, delay, toast, afterRender: null, onReplayDone: null, onReady: null };

  init();
})();
