// HeapLens visualization model: pure functions, no DOM.
//
// The runtime does a collection in one go and returns
//     before  - a snapshot of the whole heap before the command
//     events  - the structured GC event stream the collector emitted
//     after   - a snapshot after the command
// The UI replays the events one at a time on top of a copy of `before`, so the
// learner can watch the collector think. The collector never knows about the UI.
(function (root) {
  'use strict';

  const clone = (o) => JSON.parse(JSON.stringify(o));
  const hex = (a) => '0x' + Number(a).toString(16).toUpperCase().padStart(4, '0');

  // ----------------------------------------------------------------- state
  function indexObjects(st) {
    st.objs = {};
    for (const o of st.snap.objects) st.objs[o.id] = o;
  }

  function makeState(snapshot) {
    const st = {
      snap: clone(snapshot),
      objs: {},
      objState: {},      // id -> text label during a replay (UNVISITED, DISCOVERED, ...)
      phase: '',
      focus: { ids: [], edge: null, root: null },
      barrier: null,     // {src, dst, remembered} for the write-barrier panel
      lastRefUpdate: null,
      cycleIds: [],
      replaying: false,
    };
    indexObjects(st);
    return st;
  }

  function objName(st, id) {
    const o = st.objs[id];
    if (!o) return '#' + id;
    return o.label ? `${o.label} (#${id})` : `Object #${id}`;
  }

  // What an object is called when no collection is being replayed.
  function idleLabel(st, id) {
    const o = st.objs[id];
    if (!o) return '';
    return o.live ? 'REACHABLE' : 'UNREACHABLE';
  }

  function stateLabel(st, id) {
    return st.objState[id] || idleLabel(st, id);
  }

  function regionOfAddr(snap, addr) {
    return snap.regions.find((r) => addr >= r.start && addr < r.end) || null;
  }

  // ------------------------------------------------------------- phases
  const PHASES = {
    rc: ['GC START', 'ZERO-COUNT SWEEP', 'CYCLE DIAGNOSIS'],
    marksweep: ['GC START', 'ROOT SCAN', 'MARK', 'SWEEP', 'FREE LIST'],
    compact: ['GC START', 'ROOT SCAN', 'MARK', 'RECLAIM', 'COMPUTE ADDRESSES', 'UPDATE REFERENCES', 'MOVE OBJECTS', 'FREE SPACE'],
    copying: ['GC START', 'ROOT EVACUATION', 'SCAN TO-SPACE', 'DISCARD FROM-SPACE'],
    minor: ['GC START', 'ROOT SCAN', 'REMEMBERED SET SCAN', 'COPY SURVIVORS', 'RECLAIM YOUNG', 'REMEMBERED SET UPDATE'],
    major: ['GC START', 'ROOT SCAN', 'MARK', 'SWEEP OLD', 'EVACUATE YOUNG', 'UPDATE REFERENCES', 'REMEMBERED SET REBUILD'],
  };

  // One plain-English sentence per phase, shown as the "right now" narration.
  const PHASE_INFO = {
    'GC START': 'The program is paused (stop-the-world) so nothing changes while the collector works.',
    'ROOT SCAN': 'Looking at every root (your variables). Whatever a root points to is alive.',
    'MARK': 'Following references from live objects to find everything reachable. Gray = found, black = fully scanned.',
    'SWEEP': 'Walking the whole heap: objects that were never reached are garbage, so their memory is freed.',
    'FREE LIST': 'Recording which memory ranges are free again.',
    'RECLAIM': 'Throwing away the unreachable objects (compaction will overwrite their space).',
    'COMPUTE ADDRESSES': 'Deciding where each survivor will live after compaction - its forwarding address.',
    'UPDATE REFERENCES': 'Objects are about to move, so every pointer to them is rewritten to the new address.',
    'MOVE OBJECTS': 'Sliding the survivors down to their new addresses.',
    'FREE SPACE': 'All free memory is now one contiguous block.',
    'ROOT EVACUATION': 'Copying the objects your variables point to into the empty TO-SPACE.',
    'SCAN TO-SPACE': 'Copying everything the copied objects point to, until nothing is left to copy.',
    'DISCARD FROM-SPACE': 'Anything left behind was never reached - it is garbage, and the whole old space is reused.',
    'REMEMBERED SET SCAN': 'Old objects that point to young ones act as extra roots (the write barrier recorded them).',
    'COPY SURVIVORS': 'Copying young survivors to the next space; objects that are old enough are promoted to Old.',
    'RECLAIM YOUNG': 'Young objects that were not copied are garbage; Eden is emptied in one step.',
    'REMEMBERED SET UPDATE': 'Updating the list of old-to-young pointers after objects moved.',
    'SWEEP OLD': 'Freeing unreachable objects in the old generation.',
    'EVACUATE YOUNG': 'Moving live young objects out of Eden / the survivor space.',
    'REMEMBERED SET REBUILD': 'Recomputing which old objects point into the young generation.',
    'ZERO-COUNT SWEEP': 'Freeing objects whose reference count is 0.',
    'CYCLE DIAGNOSIS': 'Checking for objects that are counted but unreachable (reference-count leaks).',
    'ESCALATE': 'The old generation may be too full, so a full GC runs instead.',
  };

  function phasesFor(algo, events) {
    if (algo === 'generational') {
      const major = (events || []).some((e) => e.type === 'MajorGCStarted');
      return major ? PHASES.major : PHASES.minor;
    }
    return PHASES[algo] || [];
  }

  // ------------------------------------------------------------ safepoints
  const SAFEPOINT_CHAIN = ['RUNNING', 'GC_REQUESTED', 'SAFEPOINT', 'STOPPED', 'GC', 'RESUMED'];

  // ------------------------------------------------------------ events
  function eventTitle(ev) {
    return ev.type.replace(/([a-z])([A-Z])/g, '$1 $2').toUpperCase();
  }

  const GC_START = { GCStarted: 1, MinorGCStarted: 1, MajorGCStarted: 1 };
  const GC_END = { GCCompleted: 1, MinorGCCompleted: 1, MajorGCCompleted: 1 };

  // Applies one event to the replay state and returns a list of human readable
  // changes ("WHAT CHANGED"). ctx.after is the final snapshot (used for cycles).
  function applyEvent(st, ev, ctx) {
    const changes = [];
    const S = st.snap;
    const o = ev.id ? st.objs[ev.id] : null;
    st.focus = { ids: [], edge: null, root: null };
    if (ev.phase) st.phase = ev.phase;

    const setState = (id, label) => {
      const prev = stateLabel(st, id);
      st.objState[id] = label;
      if (prev !== label) changes.push(`${objName(st, id)}: ${prev} → ${label}`);
    };

    switch (ev.type) {
      case 'GCStarted':
      case 'MinorGCStarted':
      case 'MajorGCStarted':
        st.replaying = true;
        if (S.safepoint && S.safepoint.state === 'STOPPED') S.safepoint.state = 'GC';
        if (S.algo !== 'rc') {
          for (const id of Object.keys(st.objs)) {
            const ob = st.objs[id];
            st.objState[id] = ev.type === 'MinorGCStarted' && ob.regionKind === 'Old' ? 'NOT TRACED' : 'UNVISITED';
          }
          changes.push(ev.type === 'MinorGCStarted'
            ? 'Young objects start UNVISITED (white); old objects are not traced.'
            : 'Every object starts UNVISITED (white): nothing has been shown to be reachable yet.');
        }
        break;

      case 'RootDiscovered':
        st.focus.root = ev.id2;
        st.focus.ids = [ev.id];
        break;

      case 'ObjectDiscovered':
        st.focus.ids = [ev.id];
        setState(ev.id, 'DISCOVERED');
        break;

      case 'ObjectMarked':
        st.focus.ids = [ev.id];
        break;

      case 'ObjectScanned':
        st.focus.ids = [ev.id];
        setState(ev.id, 'SCANNED');
        break;

      case 'ReferenceTraversed':
        st.focus.edge = [ev.id, ev.id2];
        st.focus.ids = [ev.id, ev.id2];
        break;

      case 'MarkingCompleted':
        for (const id of Object.keys(st.objs)) {
          const cur = st.objState[id];
          if (cur === 'DISCOVERED' || cur === 'SCANNED') st.objState[id] = 'LIVE';
        }
        changes.push(`${ev.value} object(s) are now LIVE; everything still UNVISITED is garbage.`);
        break;

      case 'ObjectUnreachable':
        st.focus.ids = [ev.id];
        setState(ev.id, 'UNREACHABLE');
        break;

      case 'ObjectReclaimed':
        st.focus.ids = [ev.id];
        if (o) {
          setState(ev.id, 'RECLAIMED');
          o.reclaimed = true;
          changes.push(`${objName(st, ev.id)}: ${o.size} bytes at ${hex(ev.addr)} become FREE`);
        }
        break;

      case 'ForwardingAssigned':
        if (o) {
          o.fwd = ev.addr2;
          st.focus.ids = [ev.id];
          setState(ev.id, ev.addr === ev.addr2 ? 'STAYS' : 'WILL MOVE');
          changes.push(`${objName(st, ev.id)}: forwarding address ${hex(ev.addr2)} stored in its header`);
        }
        break;

      case 'ObjectMoved':
        if (o) {
          st.focus.ids = [ev.id];
          changes.push(`${objName(st, ev.id)}: address ${hex(ev.addr)} → ${hex(ev.addr2)}`);
          o.addr = ev.addr2;
          delete o.fwd;
          const r = regionOfAddr(S, ev.addr2);
          if (r) { o.region = r.name; o.regionKind = r.kind; }
          // dead objects whose space was just overwritten disappear
          for (const id of Object.keys(st.objs)) {
            const g = st.objs[id];
            if (g.reclaimed && g.addr < ev.addr2 + o.size && ev.addr2 < g.addr + g.size) delete st.objs[id];
          }
          st.objState[ev.id] = 'MOVED';
        }
        break;

      case 'ReferenceUpdated': {
        st.focus.ids = [ev.id, ev.id2].filter(Boolean);
        let where;
        if (ev.id) {
          const h = st.objs[ev.id];
          if (h && h.refs[ev.value]) h.refs[ev.value].addr = ev.addr2;
          st.focus.edge = [ev.id, ev.id2];
          where = `${objName(st, ev.id)}.refs[${ev.value}]`;
        } else {
          const r = S.roots.find((x) => x.id === ev.value);
          if (r) r.addr = ev.addr2;
          st.focus.root = ev.value;
          where = `root '${r ? r.name : ev.value}'`;
        }
        st.lastRefUpdate = { where, target: ev.id2, from: ev.addr, to: ev.addr2 };
        changes.push(`${where} → ${objName(st, ev.id2)}: old reference ${hex(ev.addr)} → new reference ${hex(ev.addr2)}`);
        break;
      }

      case 'SpacesSwapped':
        if (S.algo === 'copying') {
          for (const r of S.regions) {
            if (r.start === ev.addr) { r.name = 'FROM-SPACE'; r.kind = 'FromSpace'; }
            else if (r.start === ev.addr2) { r.name = 'TO-SPACE'; r.kind = 'ToSpace'; }
          }
          changes.push('FROM-SPACE and TO-SPACE swapped roles.');
        } else {
          const r = S.regions.find((x) => x.start === ev.addr2);
          if (r) changes.push(`${r.name} now holds the survivors.`);
        }
        break;

      case 'ObjectAged':
        if (o) {
          o.age = ev.value;
          st.focus.ids = [ev.id];
          setState(ev.id, 'SURVIVED');
          changes.push(`${objName(st, ev.id)}: age → ${ev.value}`);
        }
        break;

      case 'ObjectPromoted':
        if (o) {
          o.age = ev.value;
          st.focus.ids = [ev.id];
          setState(ev.id, 'PROMOTED');
          changes.push(`${objName(st, ev.id)}: generation → Old`);
        }
        break;

      case 'WriteBarrierTriggered':
        st.barrier = { src: ev.id, dst: ev.id2, remembered: false };
        st.focus.edge = [ev.id, ev.id2];
        st.focus.ids = [ev.id, ev.id2];
        break;

      case 'CardDirtied':
        if (!S.cards.dirty.includes(ev.value)) S.cards.dirty.push(ev.value);
        changes.push(`card #${ev.value} → DIRTY`);
        break;

      case 'RememberedSetAdded':
        if (!S.remembered.includes(ev.id)) S.remembered.push(ev.id);
        if (st.barrier && st.barrier.src === ev.id) st.barrier.remembered = true;
        st.focus.ids = [ev.id];
        changes.push(`${objName(st, ev.id)} added to the remembered set`);
        break;

      case 'RememberedSetRemoved':
        S.remembered = S.remembered.filter((x) => x !== ev.id);
        changes.push(`${objName(st, ev.id)} removed from the remembered set`);
        break;

      case 'CardCleaned':
        S.cards.dirty = [];
        changes.push('all cards → CLEAN');
        break;

      case 'RefCountChanged':
        if (o) {
          const before = o.refCount;
          o.refCount = ev.value;
          st.focus.ids = [ev.id];
          changes.push(`${objName(st, ev.id)}: refCount ${before} → ${ev.value}`);
        }
        break;

      case 'CycleDetected': {
        const cyc = ((ctx && ctx.after && ctx.after.cycles) || []).find((c) => c.includes(ev.id)) || [ev.id];
        st.cycleIds = cyc;
        st.focus.ids = cyc;
        for (const id of cyc) setState(id, 'UNREACHABLE CYCLE');
        break;
      }

      case 'SafepointRequested':
        if (S.safepoint) S.safepoint.state = 'GC_REQUESTED';
        break;
      case 'ThreadReachedSafepoint':
        if (S.safepoint) {
          const t = S.safepoint.threads.find((x) => x.index === ev.id);
          if (t) t.state = 'AT_SAFEPOINT';
          if (S.safepoint.threads.every((x) => x.state === 'AT_SAFEPOINT')) S.safepoint.state = 'SAFEPOINT';
        }
        break;
      case 'AllThreadsStopped':
        if (S.safepoint) S.safepoint.state = 'STOPPED';
        break;
      case 'ThreadsResumed':
        if (S.safepoint) {
          S.safepoint.state = 'RESUMED';
          for (const t of S.safepoint.threads) t.state = 'RUNNING';
        }
        break;

      default:
        break;
    }
    return changes;
  }

  // ---------------------------------------------------------------- heap view
  // Rows for the heap view: for each region, its blocks in address order with
  // gaps filled by FREE blocks.
  function heapRows(st) {
    const rows = [];
    for (const r of st.snap.regions) {
      const objs = Object.values(st.objs)
        .filter((o) => o.addr >= r.start && o.addr < r.end)
        .sort((a, b) => a.addr - b.addr || (a.reclaimed ? -1 : 1));
      const blocks = [];
      let cursor = r.start;
      for (const o of objs) {
        if (o.addr > cursor) blocks.push({ kind: 'free', addr: cursor, size: o.addr - cursor });
        blocks.push({ kind: o.reclaimed ? 'reclaimed' : 'object', addr: o.addr, size: o.size, id: o.id });
        cursor = Math.max(cursor, o.addr + o.size);
      }
      if (cursor < r.end) blocks.push({ kind: 'free', addr: cursor, size: r.end - cursor });
      // statistics of the region as drawn right now (reclaimed blocks count as free)
      let used = 0, freeBytes = 0, freeRuns = 0, largest = 0, run = 0;
      for (const b of blocks) {
        if (b.kind === 'object') { used += b.size; if (run) { freeRuns++; largest = Math.max(largest, run); run = 0; } }
        else { freeBytes += b.size; run += b.size; }
      }
      if (run) { freeRuns++; largest = Math.max(largest, run); }
      const stats = { used, free: freeBytes, freeRuns, largest, fragmentation: freeBytes ? Math.round((1 - largest / freeBytes) * 100) : 0 };
      rows.push({ region: r, blocks, stats });
    }
    return rows;
  }

  // ----------------------------------------------------------- graph layout
  // Deterministic layered layout: roots on top, reachable objects by BFS depth,
  // garbage in a separate band underneath.
  function layoutGraph(snap) {
    const objs = snap.objects;
    const byId = {};
    for (const o of objs) byId[o.id] = o;
    const out = (o) => o.refs.filter((r) => r.target && byId[r.target]).map((r) => r.target);

    const level = {};
    let queue = [];
    for (const r of snap.roots) {
      if (r.target && byId[r.target] && level[r.target] === undefined) { level[r.target] = 0; queue.push(r.target); }
    }
    while (queue.length) {
      const next = [];
      for (const id of queue)
        for (const t of out(byId[id]))
          if (level[t] === undefined) { level[t] = level[id] + 1; next.push(t); }
      queue = next;
    }
    const reachable = Object.keys(level).map(Number);
    const maxReach = reachable.length ? Math.max(...reachable.map((id) => level[id])) : -1;

    // garbage band
    const garbage = objs.filter((o) => level[o.id] === undefined).map((o) => o.id).sort((a, b) => a - b);
    const gLevel = {};
    const incoming = {};
    for (const id of garbage) incoming[id] = 0;
    for (const id of garbage) for (const t of out(byId[id])) if (t !== id && incoming[t] !== undefined) incoming[t]++;
    const sources = garbage.filter((id) => incoming[id] === 0);
    const bfsFrom = (start) => {
      let q = [start];
      gLevel[start] = 0;
      while (q.length) {
        const nx = [];
        for (const id of q)
          for (const t of out(byId[id]))
            if (gLevel[t] === undefined && level[t] === undefined) { gLevel[t] = gLevel[id] + 1; nx.push(t); }
        q = nx;
      }
    };
    for (const s of sources) if (gLevel[s] === undefined) bfsFrom(s);
    for (const id of garbage) if (gLevel[id] === undefined) bfsFrom(id);   // pure cycles

    const ROW = 84, TOP = 36;
    const rows = [];                                    // [{y, ids}]
    for (let l = 0; l <= maxReach; l++) rows.push({ y: TOP + 56 + l * ROW, ids: reachable.filter((id) => level[id] === l).sort((a, b) => a - b) });
    const garbageTop = TOP + 56 + (maxReach + 1) * ROW + (garbage.length ? 28 : 0);
    const maxG = garbage.length ? Math.max(...garbage.map((id) => gLevel[id])) : -1;
    for (let l = 0; l <= maxG; l++) rows.push({ y: garbageTop + l * ROW, ids: garbage.filter((id) => gLevel[id] === l).sort((a, b) => a - b), garbage: true });

    const widest = Math.max(snap.roots.length, 4, ...rows.map((r) => r.ids.length));
    const width = Math.max(560, widest * 96 + 40);
    const pos = {};
    for (const row of rows) {
      const n = row.ids.length;
      row.ids.forEach((id, i) => { pos[id] = { x: ((i + 1) * width) / (n + 1), y: row.y }; });
    }
    const rootPos = {};
    snap.roots.forEach((r, i) => { rootPos[r.id] = { x: ((i + 1) * width) / (snap.roots.length + 1), y: TOP }; });
    const height = (rows.length ? rows[rows.length - 1].y : TOP) + 60;
    return { pos, rootPos, width, height, garbageTop: garbage.length ? garbageTop - 40 : null };
  }

  const api = { clone, hex, makeState, applyEvent, objName, stateLabel, idleLabel, heapRows, layoutGraph, phasesFor,
                eventTitle, regionOfAddr, PHASE_INFO, SAFEPOINT_CHAIN, GC_START, GC_END };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.HeapLens = api;
})(typeof window !== 'undefined' ? window : globalThis);
