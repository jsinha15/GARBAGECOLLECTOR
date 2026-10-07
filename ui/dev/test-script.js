// Tests for the playground language compiler.  Run:  node ui/dev/test-script.js
'use strict';
const assert = require('assert');
const S = require('../script.js');
const { EXAMPLES } = require('../examples.js');

let failed = 0;
function test(name, fn) {
  try { fn(); console.log('ok   ' + name); } catch (e) { failed++; console.log('FAIL ' + name + '\n     ' + e.message); }
}

test('every shipped example compiles without errors', () => {
  for (const ex of EXAMPLES) {
    const r = S.compile(ex.code);
    assert.ok(r.ok, `${ex.id}: ${JSON.stringify(r.errors)}`);
    assert.ok(r.flat.length > 0, ex.id);
  }
});

test('statements are parsed into the right kinds', () => {
  const r = S.compile(`use rc
a = new Node(next, other)
b = new Node
a.next = b
c = a.next
d = b
b = null
a.next = null
gc()
gc(minor)
gc(major)`);
  assert.ok(r.ok, JSON.stringify(r.errors));
  assert.deepStrictEqual(r.flat.map((s) => s.kind), ['use', 'new', 'new', 'store', 'load', 'copy', 'clear', 'store', 'gc', 'gc', 'gc']);
  assert.deepStrictEqual(r.types.Node, ['next', 'other']);
  assert.strictEqual(r.flat[1].fields.length, 2);
  assert.strictEqual(r.flat[9].mode, 'minor');
});

test('repeat blocks are unrolled and keep their source line', () => {
  const r = S.compile(`x = new Node
repeat 3 {
  y = new Node
  x.next = y
}`);
  assert.ok(r.ok);
  assert.strictEqual(r.flat.length, 7);
  assert.deepStrictEqual(r.flat.map((s) => s.line), [1, 3, 4, 3, 4, 3, 4]);
  assert.deepStrictEqual(r.flat.map((s) => s.iteration), [0, 1, 1, 2, 2, 3, 3]);
});

test('errors carry line numbers and helpful messages', () => {
  let r = S.compile('a = new Node\nb.next = a');
  assert.ok(!r.ok);
  assert.strictEqual(r.errors[0].line, 2);
  assert.ok(/before it was given a value/.test(r.errors[0].msg));

  r = S.compile('a = new Node\na.bogus = a');
  assert.ok(/no field 'bogus'/.test(r.errors[0].msg));

  r = S.compile('a = new Node(x)\nb = new Node(y)');
  assert.ok(/already defined/.test(r.errors[0].msg));

  r = S.compile('use banana');
  assert.ok(/Unknown collector/.test(r.errors[0].msg));

  r = S.compile('repeat 2 {\n a = new Node');
  assert.ok(/never closed/.test(r.errors[0].msg));

  r = S.compile('}');
  assert.ok(/Unexpected/.test(r.errors[0].msg));

  r = S.compile('new = new Node');
  assert.ok(/keyword/.test(r.errors[0].msg));

  r = S.compile('hello world');
  assert.ok(/do not understand/.test(r.errors[0].msg));
});

test('comments and blank lines are ignored; # also starts a comment', () => {
  const r = S.compile('// hi\n\n# there\na = new Node // trailing\n');
  assert.ok(r.ok);
  assert.strictEqual(r.flat.length, 1);
});

test('describe() gives plain English for every kind', () => {
  const r = S.compile('use marksweep\na = new Node\nb = new Node\na.next = b\nc = a.next\nd = a\nd = null\na.next = null\ngc()');
  for (const st of r.flat) assert.ok(S.describe(st).length > 10, st.kind);
});


test('set statements are validated', () => {
  const r = S.compile('set promotion 3\nset cards on\nset heap 4096');
  assert.ok(r.ok, JSON.stringify(r.errors));
  assert.deepStrictEqual(r.flat.map((x) => x.kind + ':' + x.key + '=' + x.value), ['set:promotion=3', 'set:cards=on', 'set:heap=4096']);
  assert.ok(/0 to 15/.test(S.compile('set promotion 99').errors[0].msg));
  assert.ok(/on. or .off/.test(S.compile('set cards maybe').errors[0].msg));
  assert.ok(/multiple of 64/.test(S.compile('set heap 1000').errors[0].msg));
  assert.ok(/keyword/.test(S.compile('set = new Node').errors[0].msg));
});

console.log(failed ? `\n${failed} FAILED` : '\nall script tests passed');
process.exit(failed ? 1 : 0);
