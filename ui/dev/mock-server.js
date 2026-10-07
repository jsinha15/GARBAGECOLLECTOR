// UI DEVELOPMENT MOCK - not part of HeapLens itself.
//
// A tiny Node re-implementation of the runtime's JSON API for ONE algorithm
// (mark-sweep), so the browser UI can be developed and tested without building
// the C++ binary.  The real thing is `minigc --serve`.
//
//   node ui/dev/mock-server.js [port]      then open http://127.0.0.1:8081/
'use strict';
const http = require('http');
const fs = require('fs');
const path = require('path');

const BASE = 0x1000, SIZE = 2048;

class Mock {
  constructor() { this.reset(); }
  reset() {
    this.objs = new Map(); this.roots = []; this.nextId = 1; this.nextRoot = 1; this.seq = 1; this.events = [];
    this.stats = { allocations: 0, allocatedBytes: 0, collections: 0, reclaimedObjects: 0, reclaimedBytes: 0 };
    this.phase = '';
  }
  ev(type, o) { const e = Object.assign({ seq: this.seq++, type, id: 0, id2: 0, addr: 0, addr2: 0, value: 0, phase: this.phase, text: '' }, o); this.events.push(e); return e; }
  name(id) { const o = this.objs.get(id); return o.label + ' (#' + id + ')'; }
  sorted() { return [...this.objs.values()].sort((a, b) => a.addr - b.addr); }
  freeBlocks() {
    const out = []; let cur = BASE;
    for (const o of this.sorted()) { if (o.addr > cur) out.push({ addr: cur, size: o.addr - cur, region: 'HEAP' }); cur = o.addr + o.size; }
    if (cur < BASE + SIZE) out.push({ addr: cur, size: BASE + SIZE - cur, region: 'HEAP' });
    return out;
  }
  alloc(refs, label, type) {
    const size = Math.ceil((24 + 4 * refs) / 8) * 8;
    const fb = this.freeBlocks().find((b) => b.size >= size);
    if (!fb) return null;
    const id = this.nextId++;
    if (!label) label = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ'.split('').find((c) => ![...this.objs.values()].some((o) => o.label === c)) || 'N' + id;
    this.objs.set(id, { id, label, type: type || 'Node', addr: fb.addr, size, refs: new Array(refs).fill(0), color: 0 });
    this.stats.allocations++; this.stats.allocatedBytes += size;
    this.ev('ObjectAllocated', { id, addr: fb.addr, value: size, text: `Allocated ${this.name(id)} [Node, ${size} bytes] at 0x${fb.addr.toString(16)} in HEAP.` });
    return id;
  }
  resolve(t) { if (!t) return 0; const n = t.replace('#', ''); if (/^\d+$/.test(n)) return this.objs.has(+n) ? +n : 0; for (const o of this.objs.values()) if (o.label === t) return o.id; return 0; }
  gc() {
    this.stats.collections++;
    const live = new Set(), gray = [];
    this.phase = 'GC START';
    this.ev('SafepointRequested', { value: 3, phase: '', text: 'GC REQUESTED: the collector raises the safepoint flag.' });
    [0, 1, 2].forEach((t) => this.ev('ThreadReachedSafepoint', { id: t, value: 1 + 2 * t, phase: '', text: `Thread ${t} polled the safepoint flag and parked.` }));
    this.ev('AllThreadsStopped', { phase: '', text: 'STOPPED: every application thread is parked at a safepoint.' });
    this.ev('GCStarted', { text: 'MARK-SWEEP GC started. The world is stopped.' });
    this.phase = 'ROOT SCAN';
    this.ev('PhaseStarted', { text: 'ROOT SCAN: tracing starts from the GC roots.' });
    const shade = (id, why) => { this.objs.get(id).color = 1; gray.push(id); live.add(id); this.ev('ObjectDiscovered', { id, addr: this.objs.get(id).addr, text: why }); this.ev('ObjectMarked', { id, text: this.name(id) + ' is marked (GRAY).' }); };
    for (const r of this.roots) {
      this.ev('RootDiscovered', { id: r.target, id2: r.id, addr: this.objs.get(r.target).addr, text: `${this.name(r.target)} is reachable because it is directly referenced by GC root '${r.name}' (root #${r.id}).` });
      if (!live.has(r.target)) shade(r.target, `${this.name(r.target)} is discovered through root '${r.name}'.`);
    }
    this.phase = 'MARK';
    this.ev('PhaseStarted', { text: 'MARK: repeatedly take a GRAY object, scan its references, and turn it BLACK.' });
    while (gray.length) {
      const id = gray.shift(), o = this.objs.get(id);
      this.ev('ObjectScanned', { id, addr: o.addr, value: o.refs.length, text: `Scanning ${this.name(id)}.` });
      o.color = 2;
      o.refs.forEach((t, slot) => {
        if (!t) return;
        this.ev('ReferenceTraversed', { id, id2: t, value: slot, text: `${this.name(id)} --> ${this.name(t)}` });
        if (!live.has(t)) shade(t, `${this.name(t)} is reachable because ${this.name(id)} references it.`);
      });
    }
    this.ev('MarkingCompleted', { value: live.size, text: `MARKING COMPLETE: ${live.size} object(s) are LIVE.` });
    const dead = this.sorted().filter((o) => !live.has(o.id));
    dead.forEach((o) => this.ev('ObjectUnreachable', { id: o.id, addr: o.addr, text: `${this.name(o.id)} is UNREACHABLE: no path exists from any GC root to it.` }));
    this.phase = 'SWEEP';
    this.ev('PhaseStarted', { text: 'SWEEP: unmarked objects are returned to the free list.' });
    let bytes = 0;
    for (const o of dead) {
      this.ev('ObjectReclaimed', { id: o.id, addr: o.addr, value: o.size, text: `${this.name(o.id)} reclaimed: it was UNREACHABLE. It was not removed because it was old - age is irrelevant.` });
      bytes += o.size; this.objs.delete(o.id);
    }
    for (const o of this.objs.values()) o.color = 0;
    this.stats.reclaimedObjects += dead.length; this.stats.reclaimedBytes += bytes;
    this.phase = 'FREE LIST';
    this.ev('FreeListRebuilt', { value: this.freeBlocks().length, text: 'Free list rebuilt; adjacent free blocks coalesced.' });
    this.ev('GCCompleted', { value: dead.length, text: `MARK-SWEEP complete: ${this.objs.size} object(s) survive, ${dead.length} reclaimed (${bytes} bytes).` });
    this.phase = '';
    this.ev('ThreadsResumed', { value: 3, text: 'RESUMED: application threads continue.' });
  }
  // ---- JSON
  reach() {
    const parent = new Map(), q = [];
    for (const r of this.roots) if (!parent.has(r.target)) { parent.set(r.target, 0); q.push([r.target, r.name]); }
    const via = new Map(q.map(([id, n]) => [id, n]));
    while (q.length) { const [id] = q.shift(); for (const t of this.objs.get(id).refs) if (t && !parent.has(t)) { parent.set(t, id); q.push([t, null]); } }
    return { parent, via };
  }
  why(id) {
    const { parent, via } = this.reach();
    if (!parent.has(id)) return `No path exists from any GC root to ${this.name(id)}.`;
    const chain = []; for (let c = id; c; c = parent.get(c)) chain.unshift(c);
    return `Root '${via.get(chain[0])}'` + chain.map((c) => ' -> ' + this.name(c)).join('');
  }
  snapshot() {
    const { parent } = this.reach();
    const used = this.sorted().reduce((a, o) => a + o.size, 0);
    const free = this.freeBlocks();
    const largest = Math.max(0, ...free.map((b) => b.size));
    const frag = SIZE - used ? Math.round((1 - largest / (SIZE - used)) * 100) : 0;
    return {
      algo: 'marksweep', algoName: 'Mark-Sweep (UI mock)',
      config: { heapBase: BASE, heapBytes: SIZE, promotionThreshold: 2, cardBytes: 128, useCardTable: false, autoGC: true, threads: 3 },
      regions: [{ kind: 'Main', name: 'HEAP', start: BASE, end: BASE + SIZE, alloc: 'free-list', top: BASE + SIZE, used, free: SIZE - used, objects: this.objs.size, freeBlocks: free.length, largestFree: largest, fragmentation: frag }],
      freeBlocks: free,
      objects: this.sorted().sort((a, b) => a.id - b.id).map((o) => ({
        id: o.id, label: o.label, type: o.type, addr: o.addr, size: o.size, region: 'HEAP', regionKind: 'Main', age: 0, color: o.color, refCount: 0,
        numRefs: o.refs.length, dataBytes: 0, live: parent.has(o.id), why: this.why(o.id),
        refs: o.refs.map((t, slot) => ({ slot, target: t, addr: t ? this.objs.get(t).addr : 0 })),
      })),
      roots: this.roots.map((r) => ({ id: r.id, name: r.name, kind: 'global', thread: -1, target: r.target, addr: this.objs.get(r.target).addr })),
      remembered: [], cards: { start: 0, cardBytes: 128, count: 0, dirty: [] },
      safepoint: { state: 'RUNNING', threads: [0, 1, 2].map((i) => ({ index: i, state: 'RUNNING', ticksToPoll: 1 + 2 * i })) },
      cycles: [],
      stats: Object.assign({ minorCollections: 0, majorCollections: 0, objectsMoved: 0, objectsPromoted: 0, referencesUpdated: 0, barrierHits: 0, rememberedSetAdds: 0, safepointStops: this.stats.collections,
        lastMarked: 0, lastReclaimedObjects: 0, lastReclaimedBytes: 0, lastMoved: 0, lastPromoted: 0, heapSize: SIZE, heapUsed: used, heapFree: SIZE - used, liveObjects: this.objs.size, freeBlocks: free.length, largestFree: largest, fragmentation: frag }, this.stats),
    };
  }
  run(line) {
    const before = this.snapshot();
    const start = this.events.length;
    const t = line.trim().split(/\s+/);
    let ok = true, message = '', replay = false;
    const fail = (m) => { ok = false; message = m; };
    switch (t[0]) {
      case 'new': { const id = this.alloc(+(t[2] ?? 2), t[4], t[1]); id ? (message = 'Allocated ' + this.name(id)) : fail('Out of memory'); break; }
      case 'ref': case 'unref': {
        const s = this.resolve(t[1]), d = t[0] === 'ref' ? this.resolve(t[3]) : 0;
        if (!s || (t[0] === 'ref' && !d) || +t[2] >= this.objs.get(s).refs.length) { fail('bad reference command'); break; }
        this.objs.get(s).refs[+t[2]] = d; this.ev('ReferenceStored', { id: s, id2: d, text: `${this.name(s)}.refs[${t[2]}] = ${d ? this.name(d) : 'null'}` }); message = 'ok'; break;
      }
      case 'root': {
        if (t[1] === 'add') { const id = this.resolve(t[3]); if (!id) { fail('unknown object'); break; } this.roots.push({ id: this.nextRoot++, name: t[2], target: id }); message = `Root '${t[2]}' -> ${this.name(id)}`; }
        else { this.roots = this.roots.filter((r) => r.name !== t[2]); message = `Root '${t[2]}' removed.`; }
        break;
      }
      case 'gc': this.gc(); replay = true; message = 'Mark-sweep done.'; break;
      case 'algo': if (t[1] !== 'marksweep') fail('the UI mock server only implements marksweep'); else message = 'Collector: Mark-Sweep'; break;
      case 'reset': this.reset(); message = 'Runtime reset.'; break;
      case 'demo': {
        this.reset();
        ['A', 'B', 'C', 'D'].forEach((l) => this.alloc(2, l));
        this.roots.push({ id: this.nextRoot++, name: 'rootA', target: 1 });
        this.objs.get(1).refs[0] = 2; this.objs.get(3).refs[0] = 4;
        message = 'DEMO 1 - Basic garbage (mock). Root -> A -> B and C -> D. Press Trigger GC.'; break;
      }
      case 'help': message = 'mock server: new, ref, unref, root add|rm, gc, reset, demo 1'; break;
      case '': break;
      default: fail('mock server does not implement: ' + t[0]);
    }
    return { ok, command: line, message, replay, before, events: this.events.slice(start), after: this.snapshot() };
  }
}

const mock = new Mock();
const UI = path.join(__dirname, '..');
const TYPES = { '.html': 'text/html', '.js': 'application/javascript', '.css': 'text/css' };
if (require.main === module) {
  const port = +(process.argv[2] || 8081);
  http.createServer((req, res) => {
    const u = new URL(req.url, 'http://x');
    const send = (code, type, body) => { res.writeHead(code, { 'Content-Type': type, 'Cache-Control': 'no-store' }); res.end(body); };
    if (u.pathname === '/api/state') return send(200, 'application/json', JSON.stringify(mock.snapshot()));
    if (u.pathname === '/api/demos') return send(200, 'application/json', JSON.stringify([{ number: 1, title: 'Basic garbage', algo: 'marksweep', description: 'Root -> A -> B and C -> D (C unrooted).', next: 'Press Trigger GC.' }]));
    if (u.pathname === '/api/cmd') return send(200, 'application/json', JSON.stringify(mock.run(u.searchParams.get('c') || '')));
    const f = path.join(UI, u.pathname === '/' ? 'index.html' : u.pathname);
    if (!f.startsWith(UI) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) return send(404, 'text/plain', 'not found');
    send(200, TYPES[path.extname(f)] || 'application/octet-stream', fs.readFileSync(f));
  }).listen(port, '127.0.0.1', () => console.log(`UI mock server: http://127.0.0.1:${port}/`));
}
module.exports = { Mock };
