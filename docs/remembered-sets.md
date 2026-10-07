# Remembered sets and the card table

Source: `include/minigc/barrier.h`, `GenerationalCollector::minor` (steps "REMEMBERED SET SCAN" and "REMEMBERED SET UPDATE").

## 1. Concept
The **remembered set** is the list of old objects that (may) point into the young generation. During a minor GC they are treated as
additional roots. The **card table** is a coarser alternative: the old generation is divided into *cards* (default 128 bytes); a store
marks the card containing the holder as **dirty**; a minor GC scans the objects in dirty cards.

## 2. Why it exists
It is the data structure that makes the write barrier useful: a precise (remembered set) or approximate (cards) answer to "which old
objects must I look at?". Cards are cheaper to update (one byte write) but cost more to scan; real VMs use them for that reason.

## 3. Data structures
* `std::set<ObjId>` for the remembered set (object-granular).
* `CardTable`: `vector<uint8_t>` over the Old region; `indexOf(addr) = (addr - oldStart) / cardBytes`. Simplification: the card of the
  object's *start* is dirtied.
* `Config::useCardTable` (`set cards on|off`) chooses which one a minor GC scans; both are always maintained so you can compare them.

## 4. Algorithm
```
minor GC, after roots are scanned:
   holders = remembered set (or: old objects whose card is dirty)
   for each holder: for each field pointing into the collection set: evacuate target, rewrite the field
after the GC:
   for each remembered object: if it no longer points into the young generation → RememberedSetRemoved
   for each object promoted this GC that still points young → RememberedSetAdded
   clear all cards, re-dirty the cards of the remembered objects
major GC: clear everything and rebuild from the live old objects
```
Entries disappear when the young target is promoted (it is no longer young) or when the reference is cleared.

## 5. Example
```
Old O → Y (young).  Minor GC #1: Y copied to Survivor B, O stays remembered.
Minor GC #2: Y → Survivor A.  Minor GC #3: Y promoted to Old → O no longer points young → removed from the remembered set.
```

## 6. Visualization
Algorithm panel: remembered-set chips and a card grid (`·` clean, `D` dirty). Switch **card table** on in *Runtime* controls to make the
GC scan cards instead.

## 7. Tests
`barrier.remembered_set_keeps_young_object_alive_through_minor_gc`, `barrier.entry_is_dropped_once_the_young_target_is_promoted`,
`barrier.entry_is_dropped_when_the_reference_is_cleared`, `barrier.promoted_object_pointing_to_young_object_becomes_remembered`,
`barrier.card_table_mode_finds_the_same_roots`, `barrier.card_index_is_derived_from_the_address`, `session.demo5_remembered_set`.

## 8. How to run it
`./build/minigc --demo 5 --trace` then `gc minor`; try `set cards on` first to use the card table.
