// HeapLens playground language: a tiny compiler (pure functions, no DOM).
//
//   use generational            // pick the collector (optional, anywhere)
//   a = new Node(next, other)   // allocate; `a` is a local variable = a GC root
//   b = new Node                // reuse the Node layout (default fields: next, other)
//   a.next = b                  // store a reference (runs the write barrier / refcounting)
//   c = a.next                  // load a reference into a variable
//   b = null                    // drop a variable (removes the root)
//   a.next = null               // clear a field
//   gc()   gc(minor)   gc(major)
//   repeat 5 {                  // loops are unrolled; handy for creating garbage
//     tmp = new Node
//   }
//
// compile(source) -> { ok, errors:[{line,msg}], flat:[statement], types:{Type:[fields]} }
(function (root) {
  'use strict';

  const KEYWORDS = new Set(['new', 'null', 'gc', 'use', 'repeat', 'set']);
  const ALGOS = {
    rc: 'rc', refcount: 'rc', marksweep: 'marksweep', ms: 'marksweep', compact: 'compact', markcompact: 'compact',
    copying: 'copying', copy: 'copying', generational: 'generational', gen: 'generational',
  };
  const ALGO_NAMES = { rc: 'Reference Counting', marksweep: 'Mark-Sweep', compact: 'Mark-Compact', copying: 'Copying', generational: 'Generational' };
  const IDENT = '[A-Za-z_][A-Za-z0-9_]*';
  const MAX_FLAT = 3000;

  function stripComment(line) {
    let s = line;
    const i = s.indexOf('//'); if (i >= 0) s = s.slice(0, i);
    const j = s.indexOf('#'); if (j >= 0) s = s.slice(0, j);
    return s.trim();
  }

  function compile(source) {
    const errors = [];
    const types = {};                       // Type -> [field names]
    const defined = new Set();              // variables assigned so far (static, straight-line)
    const varType = {};                     // variable -> Type when statically known
    const err = (line, msg) => errors.push({ line, msg });

    const root = { kind: 'block', items: [] };
    const stack = [root];
    const lines = String(source).split('\n');

    const checkVar = (name, line) => {
      if (KEYWORDS.has(name)) { err(line, `'${name}' is a keyword and cannot be used as a variable name.`); return false; }
      return true;
    };
    const useVar = (name, line) => {
      if (!checkVar(name, line)) return false;
      if (!defined.has(name)) { err(line, `Variable '${name}' is used before it was given a value (try: ${name} = new Node).`); return false; }
      return true;
    };

    lines.forEach((raw, idx) => {
      const line = idx + 1;
      const s = stripComment(raw);
      if (!s) return;
      const top = stack[stack.length - 1];
      let m;

      if ((m = s.match(new RegExp(`^repeat\\s+(\\d+)\\s*\\{$`)))) {
        const n = Number(m[1]);
        if (n < 1 || n > 500) err(line, 'repeat count must be between 1 and 500.');
        const blk = { kind: 'repeat', line, n: Math.max(1, Math.min(500, n)), items: [], text: s };
        top.items.push(blk);
        stack.push(blk);
      } else if (s === '}') {
        if (stack.length === 1) err(line, "Unexpected '}' (there is no open 'repeat' block).");
        else stack.pop();
      } else if ((m = s.match(new RegExp(`^use\\s+(${IDENT})$`)))) {
        const key = ALGOS[m[1].toLowerCase()];
        if (!key) err(line, `Unknown collector '${m[1]}'. Choose one of: rc, marksweep, compact, copying, generational.`);
        else top.items.push({ kind: 'use', line, text: s, algo: key });
      } else if ((m = s.match(/^set\s+(promotion|cards|heap)\s+(\w+)$/))) {
        const [, key, val] = m;
        let bad = null;
        if (key === 'promotion' && !(/^\d+$/.test(val) && Number(val) <= 15)) bad = 'promotion must be a number from 0 to 15 (survivals before promotion).';
        if (key === 'cards' && val !== 'on' && val !== 'off') bad = "cards must be 'on' or 'off'.";
        if (key === 'heap' && !(/^\d+$/.test(val) && Number(val) >= 512 && Number(val) <= 65536 && Number(val) % 64 === 0)) bad = 'heap must be a multiple of 64 between 512 and 65536 bytes.';
        if (bad) err(line, bad);
        else top.items.push({ kind: 'set', line, text: s, key, value: val });
      } else if ((m = s.match(new RegExp(`^(${IDENT})\\s*=\\s*new\\s+(${IDENT})\\s*(?:\\(([^)]*)\\))?\\s*(?:\\[(\\d+)\\])?$`)))) {
        const [, v, type, fieldList, data] = m;
        if (!checkVar(v, line)) return;
        let fields;
        if (fieldList !== undefined && fieldList.trim() !== '') {
          fields = fieldList.split(',').map((x) => x.trim());
          const bad = fields.find((f) => !new RegExp(`^${IDENT}$`).test(f));
          if (bad !== undefined) { err(line, `'${bad}' is not a valid field name.`); return; }
          if (new Set(fields).size !== fields.length) { err(line, 'Field names must be different from each other.'); return; }
          if (fields.length > 16) { err(line, 'At most 16 fields per object.'); return; }
        } else if (fieldList !== undefined) {
          fields = [];                                    // new Leaf()  -> no reference fields
        } else {
          fields = types[type] || ['next', 'other'];      // new Node   -> reuse layout
        }
        if (types[type] && JSON.stringify(types[type]) !== JSON.stringify(fields)) {
          err(line, `Type ${type} was already defined with fields (${types[type].join(', ')}); use the same fields or a different type name.`);
          return;
        }
        types[type] = fields;
        defined.add(v);
        varType[v] = type;
        top.items.push({ kind: 'new', line, text: s, var: v, type, fields, data: data ? Number(data) : 0 });
      } else if ((m = s.match(new RegExp(`^(${IDENT})\\s*=\\s*null$`)))) {
        if (!checkVar(m[1], line)) return;
        defined.add(m[1]);
        delete varType[m[1]];
        top.items.push({ kind: 'clear', line, text: s, var: m[1] });
      } else if ((m = s.match(new RegExp(`^(${IDENT})\\s*=\\s*(${IDENT})\\.(${IDENT})$`)))) {
        const [, v, src, f] = m;
        if (!checkVar(v, line) || !useVar(src, line)) return;
        if (varType[src] && !types[varType[src]].includes(f)) { err(line, `Type ${varType[src]} has no field '${f}' (fields: ${types[varType[src]].join(', ') || 'none'}).`); return; }
        defined.add(v);
        delete varType[v];
        top.items.push({ kind: 'load', line, text: s, var: v, src, field: f });
      } else if ((m = s.match(new RegExp(`^(${IDENT})\\s*=\\s*(${IDENT})$`)))) {
        const [, v, src] = m;
        if (!checkVar(v, line) || !useVar(src, line)) return;
        defined.add(v);
        if (varType[src]) varType[v] = varType[src]; else delete varType[v];
        top.items.push({ kind: 'copy', line, text: s, var: v, src });
      } else if ((m = s.match(new RegExp(`^(${IDENT})\\.(${IDENT})\\s*=\\s*(null|${IDENT})$`)))) {
        const [, v, f, val] = m;
        if (!useVar(v, line)) return;
        if (varType[v] && !types[varType[v]].includes(f)) { err(line, `Type ${varType[v]} has no field '${f}' (fields: ${types[varType[v]].join(', ') || 'none'}).`); return; }
        if (val !== 'null' && !useVar(val, line)) return;
        top.items.push({ kind: 'store', line, text: s, var: v, field: f, value: val === 'null' ? null : val });
      } else if ((m = s.match(/^gc\s*\(\s*(minor|major)?\s*\)\s*;?$/))) {
        top.items.push({ kind: 'gc', line, text: s, mode: m[1] || '' });
      } else {
        err(line, `I do not understand this line: "${s}". Examples: a = new Node  |  a.next = b  |  b = null  |  gc()`);
      }
    });
    if (stack.length > 1) err(stack[stack.length - 1].line, "This 'repeat' block is never closed with '}'.");

    // unroll loops
    const flat = [];
    const expand = (items, iteration) => {
      for (const it of items) {
        if (flat.length > MAX_FLAT) return;
        if (it.kind === 'repeat') for (let k = 1; k <= it.n; k++) expand(it.items, k);
        else flat.push(Object.assign({}, it, { iteration }));
      }
    };
    expand(root.items, 0);
    if (flat.length > MAX_FLAT) err(1, `The program unrolls to more than ${MAX_FLAT} statements.`);

    errors.sort((a, b) => a.line - b.line);
    return { ok: errors.length === 0, errors, flat, types, lineCount: lines.length };
  }

  // Plain-English description of a statement BEFORE it runs.
  function describe(st) {
    switch (st.kind) {
      case 'set': return st.key === 'heap' ? `Resize the heap to ${st.value} bytes. This resets the runtime, so put it at the top of the program.`
        : st.key === 'cards' ? `Make minor collections scan the card table (${st.value}) instead of the remembered set.`
        : `Promote an object to the old generation after it survives ${st.value} minor collections.`;
      case 'use': return `Switch the garbage collector to ${ALGO_NAMES[st.algo]}.`;
      case 'new': return `Allocate a new ${st.type} object on the heap${st.fields.length ? ' with reference fields (' + st.fields.join(', ') + ')' : ''}, and make the variable '${st.var}' point to it. A variable is a GC root.`;
      case 'clear': return `Make the variable '${st.var}' point to nothing. Its old object loses one root.`;
      case 'copy': return `Make '${st.var}' point to the same object as '${st.src}' (a second root for that object).`;
      case 'load': return `Read the reference stored in ${st.src}.${st.field} and make '${st.var}' point to that object.`;
      case 'store': return st.value === null
        ? `Set field ${st.var}.${st.field} to null (deletes that reference).`
        : `Store a reference to '${st.value}' in field ${st.var}.${st.field}. The runtime's store hook runs (reference counting / write barrier).`;
      case 'gc': return st.mode === 'minor' ? 'Run a MINOR collection (young generation only).'
        : st.mode === 'major' ? 'Run a MAJOR (full) collection of the whole heap.' : 'Run a garbage collection now.';
      default: return '';
    }
  }

  const api = { compile, describe, ALGOS, ALGO_NAMES };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.MiniScript = api;
})(typeof window !== 'undefined' ? window : globalThis);
