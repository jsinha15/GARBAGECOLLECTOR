// Demo programs for the playground. Each one is a short story about ONE idea.
(function (root) {
  'use strict';

  const EXAMPLES = [
    {
      id: 'basic', title: '1. Basic garbage (Mark-Sweep)', algo: 'marksweep',
      lesson: 'An object is garbage when no variable can reach it - not because it is old.',
      code: `// Root -> a -> b      and      c -> d
use marksweep
a = new Node(next, other)   // a is a variable = a GC root
b = new Node
c = new Node
d = new Node
a.next = b                  // a -> b
c.next = d                  // c -> d

// Drop the variables c and d. Nothing can reach C or D any more.
c = null
d = null

gc()   // watch: C and D are discovered UNREACHABLE and swept
`,
    },
    {
      id: 'cycle', title: '2. Cycle: Reference Counting vs tracing', algo: 'rc',
      lesson: 'Reference counting cannot free a cycle. Change "use rc" to "use marksweep" and run again.',
      code: `// Try it with reference counting first.
use rc
a = new Node(next)
b = new Node(next)
a.next = b
b.next = a          // a cycle: a -> b -> a
a = null            // drop both variables...
b = null            // ...each object is still referenced by the other (count = 1)

gc()                // RC: nothing is freed - UNREACHABLE CYCLE (a leak)

// Now edit line 2 to:  use marksweep   and press Run again:
// a tracing collector finds that no root reaches them, and frees both.
`,
    },
    {
      id: 'list', title: '3. Linked list: drop the head', algo: 'marksweep',
      lesson: 'Dropping one variable can make a whole chain of objects garbage.',
      code: `use marksweep
head = new Node(next)
cur = head
repeat 5 {                  // build head -> n -> n -> n -> n -> n
  n = new Node
  cur.next = n
  cur = n
}
cur = null
n = null                    // only 'head' is left holding the list

gc()                        // everything is still reachable through head

head = null                 // now the WHOLE list is unreachable
gc()                        // ...and is reclaimed in one sweep
`,
    },
    {
      id: 'shared', title: '4. Shared object (a small tree)', algo: 'marksweep',
      lesson: 'An object stays alive as long as ANY path from a root reaches it.',
      code: `use marksweep
top = new Tree(left, right)
l = new Tree
r = new Tree
shared = new Tree
top.left = l
top.right = r
l.left = shared             // both l and r point to 'shared'
r.left = shared
l = null
r = null
shared = null               // only the tree itself holds everything

gc()                        // all 4 survive

top.left = null             // l is cut off...
gc()                        // ...l dies, but 'shared' survives: r still reaches it
`,
    },
    {
      id: 'compact', title: '5. Compaction (Mark-Sweep, then Mark-Compact)', algo: 'marksweep',
      lesson: 'Sweeping leaves holes. Compaction slides survivors together and must rewrite every reference to them.',
      code: `use marksweep
a = new Node(next)
x = new Node
b = new Node
y = new Node
c = new Node
a.next = b                  // a -> b -> c
b.next = c
x = null
y = null
gc()                        // sweep: x and y become FREE holes between a, b and c

use compact                 // same heap, different collector
gc()                        // b and c slide down; look at old -> new addresses
`,
    },
    {
      id: 'copying', title: '6. Copying collector', algo: 'copying',
      lesson: 'Live objects are copied into an empty space; garbage is never touched; no fragmentation.',
      code: `use copying
a = new Node(next)
x = new Node
b = new Node
y = new Node
c = new Node
a.next = b
b.next = c
x = null
y = null

gc()    // a, b, c are copied from FROM-SPACE to TO-SPACE, then the spaces swap
gc()    // ...and back again
`,
    },
    {
      id: 'promotion', title: '7. Generational: aging and promotion', algo: 'generational',
      lesson: 'Eden -> Survivor A -> Survivor B -> Old. Most objects die young.',
      code: `use generational
keeper = new Node(next)     // long-lived
junk = new Node             // short-lived
junk = null

gc(minor)   // junk dies in Eden. keeper -> Survivor A (age 1)
gc(minor)   // keeper -> Survivor B (age 2)
gc(minor)   // age reached the threshold (2): keeper is PROMOTED to Old
gc(minor)   // old objects are not touched by minor GCs
`,
    },
    {
      id: 'barrier', title: '8. Write barrier & remembered set', algo: 'generational',
      lesson: 'A minor GC only looks at young objects. Old objects that point at them must be remembered.',
      code: `use generational
old = new Node(next)
gc(minor)
gc(minor)
gc(minor)                   // 'old' is promoted to the Old generation

young = new Node            // a brand-new young object
old.next = young            // OLD -> YOUNG store: the WRITE BARRIER fires
young = null                // now ONLY the old object references it

gc(minor)                   // young survives: the remembered set makes 'old' an extra root
`,
    },
    {
      id: 'churn', title: '9. Allocation churn (automatic GC)', algo: 'generational',
      lesson: 'When Eden fills up, a minor GC runs automatically; short-lived objects cost almost nothing.',
      code: `use generational
keep = new Node(next)
repeat 60 {
  tmp = new Node            // every iteration creates one object...
  keep.next = tmp           // ...that replaces the previous one (the old one is garbage)
}
// No gc() call anywhere: Eden filled up and minor GCs ran on their own.
// Look at the Statistics panel: collections, reclaimed objects.
`,
    },
    {
      id: 'overflow', title: '10. Tuning: survivor overflow', algo: 'generational',
      lesson: 'A survivor space only holds 8 objects. Live objects that do not fit are promoted early. Try "set promotion 0" or "set cards on".',
      code: `use generational
set promotion 2            // tuning knobs live in code too:  set cards on  |  set heap 4096
head = new Node(next)
cur = head
repeat 12 {                // a chain of 13 live objects, all reachable from 'head'
  n = new Node
  cur.next = n
  cur = n
}
cur = null
n = null

gc(minor)                  // 13 live objects, but Survivor A holds 8: the rest go straight to Old
`,
    },
    {
      id: 'sandbox', title: '11. Empty sandbox', algo: 'marksweep',
      lesson: 'Write your own program. Fields are declared the first time you use a type: new Node(left, right).',
      code: `use marksweep
a = new Node(next, other)
b = new Node
a.next = b
gc()
`,
    },
  ];

  if (typeof module !== 'undefined' && module.exports) module.exports = { EXAMPLES };
  else root.MiniExamples = EXAMPLES;
})(typeof window !== 'undefined' ? window : globalThis);
