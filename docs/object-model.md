# Managed object model

Source: `include/minigc/object.h`, `Runtime::allocate`.

## 1. Concept
Every managed object is a **header** followed by **reference fields** followed by **payload bytes**.

```
+-----------------------+  <- address
| ObjectHeader (24 B)   |
+-----------------------+
| Addr refs[numRefs]    |  4 bytes each, 0 = null
+-----------------------+
| payload (opaque)      |
+-----------------------+  total size rounded up to a multiple of 8
```

## 2. Why it exists
A collector must be able to answer, **from the raw bytes alone**: *how big is this object? which of its words are
references? has it been marked/moved? how old is it?* The header carries exactly that.

## 3. Data structures
```cpp
struct ObjectHeader {
  uint32_t id;        // 0 => this block is FREE
  uint32_t size;      // total bytes, header included
  uint32_t forward;   // forwarding address while moving (0 = none)
  uint32_t refCount;  // reference counting only
  uint16_t type;      // index into the runtime's type table ("Node", ...)
  uint8_t  color;     // white / gray / black
  uint8_t  age;       // minor GCs survived
  uint8_t  numRefs;   // reference slots after the header
  uint8_t  flags, pad...
};
```
*Generation* is derived from the address (which region the object lives in), so it can never disagree with reality.
The runtime also keeps an **object table** `id → current address` (updated whenever an object moves) and debug labels (`A`, `B`, …).

Size examples: `Node` with 2 refs = 24 + 8 = **32 B**; `Blob` with 0 refs and 10 data bytes = 24 + 10 → **40 B**.

## 4. Algorithm — allocation
```
size = alignUp(24 + 4·numRefs + dataBytes, 8)
addr = collector.allocate(size)          // which region? the collector decides
if fail and autoGC: collect(); retry     // (generational: minor, then major)
zero the block; write header; table[id] = addr; emit ObjectAllocated
```
Ids are sequential from 1 and restart on `reset` (deterministic demos).

## 5. Example
```
Object #42   Type: Node   Size: 32 bytes   Generation: Young (EDEN)   Age: 2
References:  -> Object #18   -> Object #93
```
This is what the **Inspector** shows; try `inspect A` on the CLI.

## 6. Visualization
Selecting a block or node fills the inspector (id, type, address, size, generation, age, GC state, references, referenced-by,
reachability reason).

## 7. Tests
`objects.header_layout`, `objects.size_includes_payload_and_alignment`, `objects.ids_are_deterministic_and_sequential`,
`objects.references_store_target_addresses`, `objects.labels_resolve`, `objects.out_of_memory_without_auto_gc`.

## 8. How to run it
```
minigc> new Blob 0 10 B       # type, refs, data bytes, label
minigc> inspect B
```
