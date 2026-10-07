// Tests for ui/model.js (the pure replay/layout logic).  Run:  node ui/dev/test-model.js
'use strict';
const assert = require('assert');
const M = require('../model.js');
const { Mock } = require('./mock-server.js');

let failed = 0;
function test(name, fn) {
  try { fn(); console.log('ok   ' + name); } catch (e) { failed++; console.log('FAIL ' + name + '\n     ' + e.message); }
}

function demo1() {
  const m = new Mock();
  ['A', 'B', 'C', 'D'].forEach((l) => m.alloc(2, l));
  m.run('root add rootA A'); m.run('ref A 0 B'); m.run('ref C 0 D');
  return m;
}

test('replay walks the paper example: A,B,C live; D,E-style garbage reclaimed', () => {
  const m = demo1();
  const res = m.run('gc');
  const st = M.makeState(res.before);
  const seen = [];
  for (const ev of res.events) { M.applyEvent(st, ev, { after: res.after }); seen.push(ev.type); }
  assert.strictEqual(M.stateLabel(st, 1), 'LIVE');
  assert.strictEqual(M.stateLabel(st, 2), 'LIVE');
  assert.strictEqual(M.stateLabel(st, 3), 'RECLAIMED');
  assert.strictEqual(M.stateLabel(st, 4), 'RECLAIMED');
  assert.ok(seen.indexOf('MarkingCompleted') < seen.indexOf('ObjectReclaimed'));
  assert.strictEqual(st.snap.safepoint.state, 'RESUMED');
});

test('intermediate states follow white -> gray -> black -> live', () => {
  const m = demo1();
  const res = m.run('gc');
  const st = M.makeState(res.before);
  const trace = [];
  for (const ev of res.events) {
    M.applyEvent(st, ev, { after: res.after });
    trace.push(M.stateLabel(st, 2));
  }
  const uniq = trace.filter((s, i) => s !== trace[i - 1]);
  assert.ok(uniq.includes('UNVISITED') && uniq.includes('DISCOVERED') && uniq.includes('SCANNED') && uniq.includes('LIVE'));
  assert.ok(uniq.indexOf('DISCOVERED') < uniq.indexOf('SCANNED') && uniq.indexOf('SCANNED') < uniq.indexOf('LIVE'));
});

test('applyEvent reports what changed', () => {
  const m = demo1();
  const res = m.run('gc');
  const st = M.makeState(res.before);
  const all = [];
  for (const ev of res.events) all.push(...M.applyEvent(st, ev, { after: res.after }));
  assert.ok(all.some((c) => /C \(#3\): UNREACHABLE → RECLAIMED/.test(c)), all.join('\n'));
});

test('idle labels distinguish reachable and unreachable objects', () => {
  const st = M.makeState(demo1().snapshot());
  assert.strictEqual(M.stateLabel(st, 1), 'REACHABLE');
  assert.strictEqual(M.stateLabel(st, 3), 'UNREACHABLE');
  assert.ok(st.objs[3].why.startsWith('No path exists'));
  assert.ok(st.objs[2].why.includes("Root 'rootA'"));
});

test('moving an object updates its address and drops overlapped ghosts', () => {
  const snap = demo1().snapshot();
  const st = M.makeState(snap);
  const b = st.objs[2];
  const ghost = st.objs[1];
  M.applyEvent(st, { type: 'ObjectReclaimed', id: 1, addr: ghost.addr, value: ghost.size, text: '' }, {});
  assert.ok(st.objs[1].reclaimed);
  M.applyEvent(st, { type: 'ObjectMoved', id: 2, addr: b.addr, addr2: ghost.addr, value: b.size, text: '' }, {});
  assert.strictEqual(st.objs[2].addr, ghost.addr);
  assert.strictEqual(st.objs[1], undefined);
});

test('reference updates rewrite fields and roots in the replay model', () => {
  const st = M.makeState(demo1().snapshot());
  const target = st.objs[2];
  M.applyEvent(st, { type: 'ReferenceUpdated', id: 1, id2: 2, addr: target.addr, addr2: 0x3000, value: 0, text: '' }, {});
  assert.strictEqual(st.objs[1].refs[0].addr, 0x3000);
  assert.deepStrictEqual(st.lastRefUpdate.to, 0x3000);
  M.applyEvent(st, { type: 'ReferenceUpdated', id: 0, id2: 1, addr: 0x1000, addr2: 0x2000, value: st.snap.roots[0].id, text: '' }, {});
  assert.strictEqual(st.snap.roots[0].addr, 0x2000);
});

test('generational events: age, promotion, barrier, remembered set, cards', () => {
  const snap = demo1().snapshot();
  snap.algo = 'generational';
  snap.cards = { start: 0x1400, cardBytes: 128, count: 6, dirty: [] };
  const st = M.makeState(snap);
  M.applyEvent(st, { type: 'ObjectAged', id: 1, addr: 0, addr2: 0, value: 1, text: '' }, {});
  assert.strictEqual(st.objs[1].age, 1);
  M.applyEvent(st, { type: 'ObjectPromoted', id: 1, value: 2, text: '' }, {});
  assert.strictEqual(M.stateLabel(st, 1), 'PROMOTED');
  M.applyEvent(st, { type: 'WriteBarrierTriggered', id: 1, id2: 2, text: '' }, {});
  assert.deepStrictEqual(st.barrier, { src: 1, dst: 2, remembered: false });
  M.applyEvent(st, { type: 'CardDirtied', id: 1, value: 3, text: '' }, {});
  M.applyEvent(st, { type: 'RememberedSetAdded', id: 1, text: '' }, {});
  assert.deepStrictEqual(st.snap.remembered, [1]);
  assert.deepStrictEqual(st.snap.cards.dirty, [3]);
  assert.strictEqual(st.barrier.remembered, true);
});

test('heap rows fill gaps with FREE blocks and report fragmentation', () => {
  const st = M.makeState(demo1().snapshot());
  let rows = M.heapRows(st);
  assert.strictEqual(rows.length, 1);
  assert.strictEqual(rows[0].blocks.filter((b) => b.kind === 'object').length, 4);
  assert.strictEqual(rows[0].stats.used, 128);
  assert.strictEqual(rows[0].stats.freeRuns, 1);
  delete st.objs[2];                              // punch a hole
  rows = M.heapRows(st);
  assert.strictEqual(rows[0].stats.freeRuns, 2);
  assert.ok(rows[0].stats.fragmentation > 0);
  const total = rows[0].blocks.reduce((a, b) => a + b.size, 0);
  assert.strictEqual(total, 2048);
});

test('graph layout: reachable on top, garbage in a separate band, deterministic', () => {
  const snap = demo1().snapshot();
  const a = M.layoutGraph(snap), b = M.layoutGraph(M.clone(snap));
  assert.deepStrictEqual(a, b);
  assert.ok(a.pos[1].y < a.pos[2].y);             // A above B
  assert.ok(a.pos[3].y > a.pos[2].y);             // garbage C below the reachable part
  assert.ok(a.pos[4].y > a.pos[3].y);
  assert.ok(a.garbageTop !== null);
  for (const id of [1, 2, 3, 4]) assert.ok(a.pos[id].x > 0 && a.pos[id].x < a.width);
});

test('timeline phases per algorithm', () => {
  assert.deepStrictEqual(M.phasesFor('marksweep').slice(0, 3), ['GC START', 'ROOT SCAN', 'MARK']);
  assert.ok(M.phasesFor('compact').includes('UPDATE REFERENCES'));
  assert.ok(M.phasesFor('generational', [{ type: 'MajorGCStarted' }]).includes('SWEEP OLD'));
  assert.ok(M.phasesFor('generational', []).includes('REMEMBERED SET SCAN'));
});

console.log(failed ? `\n${failed} FAILED` : '\nall UI model tests passed');
process.exit(failed ? 1 : 0);
