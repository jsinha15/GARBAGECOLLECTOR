// The code playground: a tiny editor on the left, live program state on the right.
// It "compiles" the program (script.js), then executes it statement by statement
// by sending ordinary runtime commands to the server (the same ones the buttons use).
(function () {
  'use strict';
  const A = window.MiniApp, S = window.MiniScript, M = window.HeapLens, EXAMPLES = window.MiniExamples;
  const $ = (id) => document.getElementById(id);
  const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

  function el(tag, props, ...kids) {
    const e = document.createElement(tag);
    for (const k in props || {}) {
      const v = props[k];
      if (v === null || v === undefined || v === false) continue;
      if (k === 'class') e.className = v;
      else if (k === 'text') e.textContent = v;
      else if (k.startsWith('on')) e.addEventListener(k.slice(2), v);
      else e.setAttribute(k, v);
    }
    for (const c of kids.flat()) if (c !== null && c !== undefined && c !== false) e.append(c.nodeType ? c : document.createTextNode(String(c)));
    return e;
  }

  const P = {
    compiled: null,     // result of S.compile
    pc: 0,              // index of the next statement in compiled.flat
    begun: false,       // runtime reset for this run?
    auto: false,        // Play mode
    instant: false,     // Run instantly
    waiting: null,      // continuation while a GC replay is in progress
    runtimeError: null, // {line,msg}
    timer: null,
  };

  // --------------------------------------------------------------- editor
  const LH = 20;
  function lineCount() { return $('code').value.split('\n').length; }

  function renderEditor() {
    const n = lineCount();
    const errLines = new Set((P.compiled ? P.compiled.errors : []).map((e) => e.line));
    if (P.runtimeError) errLines.add(P.runtimeError.line);
    const cur = currentLine();
    const g = $('gutter');
    g.replaceChildren();
    for (let i = 1; i <= n; i++) g.append(el('div', { class: 'gl' + (errLines.has(i) ? ' err' : '') + (i === cur ? ' cur' : ''), text: String(i) }));
    const ta = $('code');
    ta.style.height = Math.max(300, n * LH + 18) + 'px';
    g.style.height = ta.style.height;
    const hl = $('hl');
    if (cur) { hl.hidden = false; hl.style.top = (cur - 1) * LH + 8 + 'px'; hl.classList.toggle('bad', !!P.runtimeError); }
    else hl.hidden = true;
  }

  function currentLine() {
    if (P.runtimeError) return P.runtimeError.line;
    if (!P.compiled || !P.compiled.ok) return 0;
    if (P.waiting && P.lastLine) return P.lastLine;
    if (P.pc >= P.compiled.flat.length) return 0;
    return P.begun || P.pc > 0 ? P.compiled.flat[P.pc].line : 0;
  }

  function doCompile() {
    P.compiled = S.compile($('code').value);
    const box = $('compile');
    box.replaceChildren();
    if (P.compiled.ok) {
      box.className = 'compile ok';
      const n = P.compiled.flat.length;
      box.append(`✓ compiled: ${n} statement${n === 1 ? '' : 's'}` + (Object.keys(P.compiled.types).length
        ? ' · types: ' + Object.entries(P.compiled.types).map(([t, f]) => `${t}(${f.join(', ')})`).join(', ') : ''));
    } else {
      box.className = 'compile bad';
      for (const e of P.compiled.errors.slice(0, 6)) box.append(el('div', { text: `✗ line ${e.line}: ${e.msg}` }));
    }
    renderEditor();
    refreshButtons();
  }

  function onEdit() {
    stopAuto();
    try { localStorage.setItem('minigc-code', $('code').value); localStorage.setItem('minigc-example', $('example').value); } catch (e) { /* storage may be blocked */ }
    if (P.pc > 0 || P.begun || P.runtimeError) {
      P.pc = 0; P.begun = false; P.runtimeError = null; P.waiting = null;
      showNext('The code changed, so the program will start again from line 1 (the heap is reset on the next Step).');
    }
    doCompile();
    showNext();
  }

  // --------------------------------------------------------------- helpers
  function refreshButtons() {
    const ok = P.compiled && P.compiled.ok;
    const finished = ok && P.pc >= P.compiled.flat.length && !A.busy() && !P.waiting;
    const replaying = A.busy();
    $('p-step').disabled = !ok || (finished && !replaying) || !!P.runtimeError && !replaying;
    $('p-play').disabled = !ok || finished || !!P.runtimeError || P.auto;
    $('p-run').disabled = !ok || finished || !!P.runtimeError || P.auto;
    $('p-stop').disabled = !P.auto;
    $('p-step').textContent = replaying ? 'Next GC step ▶' : 'Step ▶';
    // loaders: the button of whatever is currently running shows a spinner
    $('p-step').classList.toggle('loading', !!(P.exec || P.spin) && !P.auto && !P.instant);
    $('p-play').classList.toggle('loading', P.auto);
    $('p-play').textContent = P.auto ? 'Playing…' : 'Play';
    $('p-run').classList.toggle('loading', P.instant);
    $('p-run').textContent = P.instant ? 'Running…' : 'Skip to end';
    $('p-stop').textContent = P.auto ? 'Pause' : 'Pause';
  }

  function showDone(on) {
    $('done').hidden = !on;
    const sel = $('example');
    $('d-next').hidden = !(sel.selectedIndex > 0 && sel.selectedIndex < sel.options.length - 1);
  }

  function showNext(msg) {
    const n = $('next');
    showDone(false);
    n.classList.remove('dim');
    if (msg) { n.textContent = msg; return; }
    if (!P.compiled || !P.compiled.ok) { n.classList.add('dim'); n.textContent = 'Fix the compile errors above first.'; return; }
    if (P.runtimeError) { n.textContent = `Runtime error on line ${P.runtimeError.line}: ${P.runtimeError.msg}`; return; }
    if (P.pc >= P.compiled.flat.length) { n.textContent = 'Nothing left to run. Edit the code, or use the buttons below.'; showDone(true); return; }
    const st = P.compiled.flat[P.pc];
    n.replaceChildren(el('b', { text: `Next: line ${st.line}` + (st.iteration ? ` (loop pass ${st.iteration})` : '') + '  ' }), el('code', { text: st.text }), el('div', { class: 'dim', text: S.describe(st) }));
  }

  function freshLabel(v) {
    const used = new Set(A.app.snap.objects.map((o) => o.label));
    if (!used.has(v)) return v;
    let k = 2;
    while (used.has(`${v}_${k}`)) k++;
    return `${v}_${k}`;
  }

  // ---------------------------------------------------------------- execute
  const NOTE_TYPES = ['WriteBarrierTriggered', 'RememberedSetAdded', 'CardDirtied', 'RefCountChanged', 'ObjectReclaimed', 'CycleDetected', 'ObjectPromoted'];

  async function execute(st, animate) {
    const changes = [];
    const snap = () => A.app.snap;
    const send = async (cmd, replay) => {
      const res = await A.run(cmd, { replay: !!replay, autoplay: !!(replay && P.auto), force: true, quiet: P.instant });
      if (!res) throw new Error('the runtime is busy');
      if (!res.ok) throw new Error(res.message);
      if (!replay || !res.replay) for (const e of res.events) if (NOTE_TYPES.includes(e.type)) changes.push(e.text);
      return res;
    };
    const rootOf = (v) => snap().roots.find((r) => r.name === v);
    const objById = (id) => snap().objects.find((o) => o.id === id);
    const objByLabel = (l) => snap().objects.find((o) => o.label === l);
    const labelOfVar = (v) => { const r = rootOf(v); const o = r && r.target ? objById(r.target) : null; return o ? o.label : null; };
    const needVar = (v) => {
      const l = labelOfVar(v);
      if (!l) throw new Error(`variable '${v}' is null (it points to no object), so it cannot be used here.`);
      return l;
    };
    const setVar = async (v, label) => {
      const r = rootOf(v);
      if (r && label && r.target === (objByLabel(label) || {}).id) return;   // already points there
      if (r) await send('root rm ' + v);
      if (label) await send(`root add ${v} ${label} 0`);
    };
    const slotOf = (label, field) => {
      const o = objByLabel(label);
      const fields = P.compiled.types[o.type];
      const slot = fields ? fields.indexOf(field) : -1;
      if (slot < 0) throw new Error(`object ${label} (type ${o.type}) has no field '${field}'.`);
      return slot;
    };
    const done = (extra) => ({ ok: true, changes: changes.concat(extra || []) });

    switch (st.kind) {
      case 'set': {
        const res = await send(`set ${st.key} ${st.value}`);
        return done([res.message]);
      }
      case 'use': {
        const res = await send('algo ' + st.algo);
        return done([res.message]);
      }
      case 'new': {
        const label = freshLabel(st.var);
        const res = await send(`new ${st.type} ${st.fields.length} ${st.data} ${label}`, animate);
        const finish = async () => {
          await setVar(st.var, label);
          return done([res.message, `variable ${st.var} (a GC root) → ${label}`]);
        };
        return A.busy() ? { waiting: true, finish } : finish();
      }
      case 'clear': {
        if (rootOf(st.var)) await send('root rm ' + st.var);
        return done([`variable ${st.var} → null`]);
      }
      case 'copy': {
        const l = needVar(st.src);
        await setVar(st.var, l);
        return done([`variable ${st.var} → ${l} (same object as ${st.src})`]);
      }
      case 'load': {
        const holder = needVar(st.src);
        const slot = slotOf(holder, st.field);
        const o = objByLabel(holder);
        const t = o.refs[slot] ? o.refs[slot].target : 0;
        const target = t ? objById(t) : null;
        if (target) await setVar(st.var, target.label);
        else if (rootOf(st.var)) await send('root rm ' + st.var);
        return done([target ? `variable ${st.var} → ${target.label}  (read from ${st.src}.${st.field})` : `${st.src}.${st.field} is null, so ${st.var} → null`]);
      }
      case 'store': {
        const holder = needVar(st.var);
        const slot = slotOf(holder, st.field);
        if (st.value === null) await send(`unref ${holder} ${slot}`);
        else await send(`ref ${holder} ${slot} ${needVar(st.value)}`);
        return done([st.value === null ? `${st.var}.${st.field} = null` : `${st.var}.${st.field} → ${labelOfVar(st.value)}`]);
      }
      case 'gc': {
        const res = await send('gc' + (st.mode ? ' ' + st.mode : ''), animate);
        if (A.busy()) return { waiting: true, finish: async () => done([res.message]) };
        return done([res.message]);
      }
      default:
        throw new Error('unknown statement');
    }
  }

  async function begin() {
    await A.run('reset', { force: true, replay: false, quiet: P.instant });
    P.begun = true;
  }

  // Executes ONE statement. Returns 'done' | 'waiting' | 'error' | 'end'.
  async function runStatement(animate) {
    if (P.exec) return 'busy';            // ignore a second click while a statement is still executing
    P.exec = true;
    refreshButtons();
    const t0 = Date.now();
    try { return await runStatementInner(animate); } finally {
      P.exec = false;                       // the re-entrancy guard is released immediately...
      P.spin = true;                        // ...but the spinner stays visible a moment so it is noticed
      setTimeout(() => { P.spin = false; refreshButtons(); }, Math.max(0, 300 - (Date.now() - t0)));
      refreshButtons();
    }
  }

  async function runStatementInner(animate) {
    if (!P.compiled || !P.compiled.ok) return 'error';
    if (P.pc >= P.compiled.flat.length) return 'end';
    if (!P.begun) await begin();
    const st = P.compiled.flat[P.pc];
    P.lastLine = st.line;
    renderEditor();
    let r;
    try {
      r = await execute(st, animate);
    } catch (e) {
      fail(st, e.message);
      return 'error';
    }
    if (r.waiting) {
      P.waiting = { st, finish: r.finish };
      refreshButtons();
      return 'waiting';
    }
    complete(st, r, true);
    return 'done';
  }

  function complete(st, r, explain) {
    P.pc++;
    P.waiting = null;
    if (explain && !P.instant) {
      A.setExplain({ title: `LINE ${st.line}  ·  ${st.text}`, why: S.describe(st), changes: r.changes });
    }
    renderEditor();
    showNext();
    refreshButtons();
    if (P.pc >= P.compiled.flat.length && !P.instant) A.toast('Program finished', 'ok');
  }

  function fail(st, msg) {
    stopAuto();
    P.runtimeError = { line: st.line, msg };
    P.waiting = null;
    A.setExplain({ title: `RUNTIME ERROR · line ${st.line}`, why: msg, changes: [] });
    A.toast(`Line ${st.line}: ${msg}`, 'err');
    const box = $('compile');
    box.className = 'compile bad';
    box.replaceChildren(`✗ runtime error on line ${st.line}: ${msg}`);
    renderEditor();
    showNext();
    refreshButtons();
  }

  // called by app.js when a GC replay finishes
  async function onReplayDone() {
    const w = P.waiting;
    if (!w) { refreshButtons(); return; }
    try {
      await w.finish();
      P.waiting = null;
      P.pc++;
      renderEditor(); showNext(); refreshButtons();
      A.renderAll();
    } catch (e) { fail(w.st, e.message); return; }
    if (P.auto) autoNext();
  }

  // ------------------------------------------------------------- buttons
  function stopAuto() {
    P.auto = false;
    if (P.timer) clearTimeout(P.timer);
    P.timer = null;
    refreshButtons();
  }

  async function onStep() {
    if (A.busy()) { P.auto = false; A.stopPlaying(); A.stepOnce(); refreshButtons(); return; }
    P.instant = false;
    P.auto = false;
    await runStatement(true);
    refreshButtons();
  }

  async function autoNext() {
    if (!P.auto) return;
    P.timer = setTimeout(async () => {
      if (!P.auto) return;
      const r = await runStatement(true);
      if (r === 'done' || r === 'busy') autoNext();
      else if (r === 'end' || r === 'error') stopAuto();
      // 'waiting': onReplayDone continues the loop
    }, Math.max(150, A.delay() * 0.6));
  }

  async function onPlay() {
    P.instant = false;
    P.auto = true;
    refreshButtons();
    const r = await runStatement(true);
    if (r === 'done') autoNext();
    else if (r === 'end' || r === 'error') stopAuto();
  }

  async function onRunInstant() {
    P.instant = true;
    P.auto = false;
    refreshButtons();
    while (P.pc < P.compiled.flat.length) {
      const r = await runStatement(false);
      if (r !== 'done') break;
    }
    P.instant = false;
    A.renderAll();
    if (!P.runtimeError) {
      A.setExplain({ title: 'PROGRAM FINISHED', why: 'The whole program ran instantly. Look at the Variables and the Heap for the end result. Press Restart and use Step to watch it happen line by line, including every garbage collection.', changes: [] });
    }
    refreshButtons();
  }

  async function onRestart() {
    stopAuto();
    P.pc = 0; P.begun = false; P.runtimeError = null; P.waiting = null; P.lastLine = 0;
    await begin();
    P.begun = false;       // reset again on first Step (keeps the heap empty either way)
    A.setExplain({ title: 'RESTARTED', why: 'The heap is empty again. Press Step to run line 1.', changes: [] });
    doCompile();
    showNext();
    A.renderAll();
  }

  // ------------------------------------------------------------ variables panel
  function stateClass(label) { return 'st-' + label.toLowerCase().replace(/[^a-z]+/g, '-'); }

  function renderVars() {
    const box = $('vars');
    const st = A.app.st;
    if (!box || !st) return;
    box.replaceChildren();
    const snap = st.snap;
    const objName = (id) => (st.objs[id] ? (st.objs[id].label || '#' + id) : '#' + id);

    // ---- variables (roots)
    if (!snap.roots.length) {
      box.append(el('div', { class: 'dim empty', text: 'No variables yet. A variable (GC root) appears here as soon as your program creates one: a = new Node' }));
    } else {
      const t = el('table', { class: 'vt' }, el('thead', {}, el('tr', {}, ['variable', 'points to', 'address', 'status'].map((h) => el('th', { text: h })))));
      const tb = el('tbody');
      for (const r of snap.roots) {
        const o = st.objs[r.target];
        const label = o ? M.stateLabel(st, o.id) : '—';
        tb.append(el('tr', { class: 'clk', onclick: () => o && A.select(o.id) },
          el('td', {}, el('b', { text: r.name }), el('span', { class: 'dim', text: r.kind === 'stack' ? '  (stack, thread ' + r.thread + ')' : '  (global)' })),
          el('td', { text: o ? `${o.label || 'Object'} (#${o.id})` : 'null' }),
          el('td', { class: 'mono', text: o ? M.hex(o.addr) : '' }),
          el('td', {}, o ? el('span', { class: 'chip2 ' + stateClass(label), text: label }) : '')));
      }
      t.append(tb);
      box.append(t);
    }

    // ---- objects
    const objs = Object.values(st.objs).sort((a, b) => a.id - b.id);
    if (objs.length) {
      const holders = {};
      for (const r of snap.roots) if (r.target) (holders[r.target] = holders[r.target] || []).push(r.name);
      const t = el('table', { class: 'vt objs' }, el('thead', {}, el('tr', {}, ['object', 'where', 'fields', 'held by variables', 'status'].map((h) => el('th', { text: h })))));
      const tb = el('tbody');
      for (const o of objs) {
        const fields = (P.compiled && P.compiled.types[o.type]) || o.refs.map((_, i) => '[' + i + ']');
        const label = M.stateLabel(st, o.id);
        const extra = snap.algo === 'rc' ? ` · refCount ${o.refCount}` : snap.algo === 'generational' ? ` · age ${o.age}` : '';
        const region = st.snap.regions.find((r) => o.addr >= r.start && o.addr < r.end);
        tb.append(el('tr', { class: 'clk' + (A.app.selected === o.id ? ' sel' : '') + (o.reclaimed ? ' gone' : ''), onclick: () => A.select(o.id) },
          el('td', {}, el('b', { text: o.label || '#' + o.id }), el('span', { class: 'dim', text: ` #${o.id} ${o.type}` })),
          el('td', { class: 'mono', text: `${M.hex(o.addr)} ${region ? region.name : ''}${extra}` }),
          el('td', {}, o.refs.length ? o.refs.map((r, i) => el('div', { class: 'fld' }, el('span', { class: 'dim', text: fields[i] + ' ' }), r.target ? '→ ' + objName(r.target) : '= null')) : el('span', { class: 'dim', text: '(no fields)' })),
          el('td', { text: (holders[o.id] || []).join(', ') || '—' }),
          el('td', {}, el('span', { class: 'chip2 ' + stateClass(label), text: label }),
            !o.reclaimed && !o.live && !st.replaying ? el('div', { class: 'dim small', text: 'no variable can reach it' }) : null)));
      }
      t.append(tb);
      box.append(t);
    }
  }

  // ------------------------------------------------------------------ init
  function loadExample(id) {
    const ex = EXAMPLES.find((e) => e.id === id);
    if (!ex) return;
    stopAuto();
    $('code').value = ex.code;
    try { localStorage.setItem('minigc-code', ex.code); localStorage.setItem('minigc-example', ex.id); } catch (e) { /* ignore */ }
    $('lesson').classList.remove('dim');
    $('lesson').replaceChildren(el('b', { text: 'Idea: ' }), ex.lesson);
    P.pc = 0; P.begun = false; P.runtimeError = null; P.waiting = null; P.lastLine = 0;
    doCompile();
    showNext();
    A.run('reset', { force: true, replay: false }).then(() => {
      A.setExplain({ title: 'EXAMPLE LOADED', why: ex.lesson + ' Press Step to run the first line.', changes: [] });
      A.renderAll();
    });
  }

  function setView(name) {
    for (const b of document.querySelectorAll('.vtab')) b.classList.toggle('active', b.dataset.view === name);
    for (const v of document.querySelectorAll('.view')) v.hidden = v.id !== 'view-' + name;
  }

  function setGuide(show) {
    $('guide').hidden = !show;
    try { localStorage.setItem('minigc-guide', show ? '1' : '0'); } catch (e) { /* storage may be blocked */ }
  }

  function init() {
    $('example').append(...EXAMPLES.map((e) => el('option', { value: e.id, text: e.title })));
    $('example').onchange = () => { if ($('example').value) loadExample($('example').value); };
    $('code').addEventListener('input', onEdit);
    $('code').addEventListener('scroll', () => { /* editor grows with its content; page scrolls instead */ });
    $('code').addEventListener('keydown', (e) => {
      if (e.key === 'Tab') {               // insert two spaces instead of leaving the editor
        e.preventDefault();
        const ta = e.target, s = ta.selectionStart;
        ta.value = ta.value.slice(0, s) + '  ' + ta.value.slice(ta.selectionEnd);
        ta.selectionStart = ta.selectionEnd = s + 2;
        onEdit();
      } else if (e.key === 'Enter' && (e.ctrlKey || e.metaKey)) { e.preventDefault(); onStep(); }
    });
    $('p-step').onclick = onStep;
    $('p-play').onclick = onPlay;
    $('p-run').onclick = onRunInstant;
    $('p-stop').onclick = stopAuto;
    $('p-reset').onclick = onRestart;
    for (const b of document.querySelectorAll('.vtab')) b.onclick = () => setView(b.dataset.view);
    $('b-help').onclick = () => setGuide($('guide').hidden);
    $('b-guide-x').onclick = () => setGuide(false);
    let seen = false;
    try { seen = localStorage.getItem('minigc-guide') === '0'; } catch (e) { /* ignore */ }
    $('guide').hidden = seen;
    let saved = null, savedEx = 'basic';
    try { saved = localStorage.getItem('minigc-code'); savedEx = localStorage.getItem('minigc-example') || 'basic'; } catch (e) { /* ignore */ }
    loadExample(savedEx && EXAMPLES.some((x) => x.id === savedEx) ? savedEx : 'basic');
    const base = EXAMPLES.find((x) => x.id === (EXAMPLES.some((e) => e.id === savedEx) ? savedEx : 'basic'));
    if (saved && saved.trim() && base && saved !== base.code) { $('code').value = saved; doCompile(); showNext(); A.toast('Restored the program you were editing', 'info'); }
    $('example').value = EXAMPLES.some((x) => x.id === savedEx) ? savedEx : 'basic';
    $('d-again').onclick = onRestart;
    $('d-next').onclick = () => {
      const sel = $('example');
      if (sel.selectedIndex < sel.options.length - 1) { sel.selectedIndex += 1; loadExample(sel.value); }
    };
  }

  A.afterRender = () => { renderVars(); refreshButtons(); };
  A.onReplayDone = onReplayDone;
  A.onReady = init;
  window.MiniPlayground = { P, doCompile, runStatement, loadExample };
})();
