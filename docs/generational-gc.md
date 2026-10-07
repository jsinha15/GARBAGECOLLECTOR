# Generational GC

Source: `src/core/gc_generational.cpp`.

## 1. Concept
**Weak generational hypothesis: most objects die young.** So split the heap by age and collect the young part often and cheaply, the old
part rarely.

```
YOUNG GENERATION                                              OLD GENERATION
+------- EDEN -------+-- SURVIVOR A --+-- SURVIVOR B --+      +----------- OLD ----------+
 new objects           holds survivors   (the other one                                    
                       of the last GC     is empty "to")
```
Default 2 KB heap: Eden 768 B, each survivor 256 B, Old 768 B.

## 2. Why it exists
A full trace costs time proportional to everything live. A **minor GC** only looks at the young generation, which is small and mostly
garbage, so the pause is short and the work per reclaimed byte is low.

## 3. Data structures
* Four regions (Eden/Survivors are bump-allocated, Old is a free list).
* `age` in the object header = minor GCs survived; `promotionThreshold` (default 2, `set promotion N`).
* `fromSurv_` = the survivor space holding the survivors of the last GC; the other is the empty "to" space. They swap each minor GC,
  so the first copy goes Eden → **Survivor A**, the next Survivor A → **Survivor B**, and so on.
* Remembered set / card table (see [write-barriers.md](write-barriers.md), [remembered-sets.md](remembered-sets.md)).
* Objects bigger than a survivor space (256 B) are **pretenured**: allocated directly in Old, since they could never be copied.

## 4. Algorithm

### Minor GC
```
roots:        every root pointing into Eden / from-survivor → evacuate
old→young:    every remembered old object: scan its fields; evacuate young targets      (extra roots!)
closure:      follow references out of everything copied (queue)
evacuate(x):  forwarded? reuse.  else if x is in the from-survivor and age >= threshold → PROMOTE to Old (age kept)
              else copy to the to-survivor with age+1    (to-survivor full → promoted early)
reclaim:      anything in Eden/from-survivor with no forwarding pointer is garbage → ObjectReclaimed; reset both spaces
maintain:     swap survivor roles; drop remembered entries that no longer point young; remember promoted objects that still do
```
Lifecycle with threshold 2: **Eden (age 0) → Survivor A (1) → Survivor B (2) → Old** (promoted on the third minor GC).

*Promotion guarantee:* if Old might not have room for the survivors, the minor GC escalates to a major GC first.

### Major (full) GC
```
mark the WHOLE heap from the roots (same tracer as Mark-Sweep) → sweep Old (free list) → evacuate live young objects
→ update every root and reference field → reset young spaces → rebuild remembered set from scratch
```
Minor GCs never free old garbage (and an unreachable old object still keeps its young targets alive — "floating garbage"); only the
major GC does.

## 5. Example (Demo 4)
```
Object #1 (A)   Age 0  EDEN
minor GC #1  →  Age 1  SURVIVOR A
minor GC #2  →  Age 2  SURVIVOR B
minor GC #3  →  Age 2  OLD            "A survived and is PROMOTED because its age (2) reached the promotion threshold (2)"
```

## 6. Visualization
Heap view groups `YOUNG GENERATION` and `OLD GENERATION`; blocks show `age N`; `ObjectMoved` makes a block jump region, `ObjectAged`
changes the badge, `ObjectPromoted` turns it purple with the explanation above.

## 7. Tests
`generational.layout_has_eden_two_survivors_and_old`, `generational.minor_gc_reclaims_young_garbage`,
`generational.survivors_age_and_are_promoted`, `generational.promotion_threshold_is_configurable`,
`generational.minor_gc_updates_references_between_young_objects`, `generational.both_survivor_spaces_alternate_roles`,
`generational.survivor_overflow_promotes_early`, `generational.eden_exhaustion_triggers_minor_gc_automatically`,
`generational.major_gc_*`, `graph.generational_*`, `session.demo4_generational_promotion`.

## 8. How to run it
```
./build/minigc --demo 4 --trace
minigc> gc            # minor
minigc> gc major
minigc> set promotion 1
```
