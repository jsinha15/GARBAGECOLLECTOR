#include "minigc/commands.h"

#include <cctype>
#include <exception>
#include <sstream>

#include "minigc/graph.h"
#include "minigc/visualization.h"

namespace minigc {

namespace {

std::vector<std::string> tokenize(const std::string& line) {
  std::vector<std::string> out;
  std::istringstream in(line);
  std::string t;
  while (in >> t) out.push_back(t);
  return out;
}

bool parseUInt(const std::string& s, uint32_t& out) {
  if (s.empty()) return false;
  for (char c : s)
    if (!std::isdigit(static_cast<unsigned char>(c))) return false;
  out = static_cast<uint32_t>(std::stoul(s));
  return true;
}

CommandResult fail(const std::string& msg) {
  CommandResult r;
  r.ok = false;
  r.message = msg;
  return r;
}

CommandResult good(const std::string& msg) {
  CommandResult r;
  r.message = msg;
  return r;
}

}  // namespace

// ---------------------------------------------------------------------- demos
const std::vector<DemoInfo>& Session::demos() {
  static const std::vector<DemoInfo> d = {
      {1, "Basic garbage", "marksweep",
       "Root -> A -> B and C -> D. The root of C was removed, so C and D are unreachable.",
       "Press 'Trigger GC' (or run `gc`) and step through it: C and D are reclaimed because no root reaches them.",
       {"new Node 2 0 A", "new Node 2 0 B", "new Node 2 0 C", "new Node 2 0 D", "root add rootA A",
        "root add rootC C", "ref A 0 B", "ref C 0 D", "root rm rootC"}},
      {2, "Cycle", "rc",
       "A -> B and B -> A, with one external root on A that has just been removed. Look at the reference counts: both are still 1.",
       "Run `gc`: reference counting cannot reclaim the cycle. Then run `demo 2 marksweep` and `gc` to watch a tracing GC reclaim it.",
       {"new Node 1 0 A", "new Node 1 0 B", "root add ext A", "ref A 0 B", "ref B 0 A", "root rm ext"}},
      {3, "Compaction", "marksweep",
       "A, x, B, y, C were allocated side by side; x and y died and were swept, leaving FREE holes. Mark-Compact is now selected.",
       "Press 'Trigger GC': B and C slide down next to A, their old and new addresses are shown, and every reference to them is rewritten.",
       {"new Node 1 0 A", "new Node 1 0 x", "new Node 1 0 B", "new Node 1 0 y", "new Node 1 0 C", "root add rA A",
        "root add rB B", "root add rC C", "ref A 0 B", "ref B 0 C", "gc", "algo compact"}},
      {4, "Generational promotion", "generational",
       "A is a long-lived object (rooted); junk1 and junk2 die immediately. All three start in Eden with age 0.",
       "Press 'Minor GC' repeatedly: junk dies young, A moves Eden -> Survivor A -> Survivor B and is promoted to Old once its age reaches the threshold.",
       {"new Node 2 0 A", "root add rA A", "new Node 2 0 junk1", "new Node 2 0 junk2"}},
      {5, "Remembered set", "generational",
       "O was promoted to the Old generation. Then a brand-new young object Y was stored into O. Nothing else references Y.",
       "Look at the write barrier / remembered set panel, then press 'Minor GC': Y survives only because O is in the remembered set.",
       {"new Node 2 0 O", "root add rO O", "gc minor", "gc minor", "gc minor", "new Node 2 0 Y", "ref O 0 Y"}},
      {6, "Copying GC", "copying",
       "A, x, B, y, C live in FROM-SPACE; x and y are garbage. A -> B -> C.",
       "Press 'Trigger GC': A, B, C are copied into TO-SPACE (contiguous), forwarding pointers fix the references, the spaces swap.",
       {"new Node 1 0 A", "new Node 1 0 x", "new Node 1 0 B", "new Node 1 0 y", "new Node 1 0 C", "root add rA A",
        "ref A 0 B", "ref B 0 C"}},
  };
  return d;
}

std::string Session::demosJson() {
  std::ostringstream o;
  o << "[";
  bool first = true;
  for (const DemoInfo& d : demos()) {
    if (!first) o << ",";
    first = false;
    o << "{\"number\":" << d.number << ",\"title\":\"" << jsonEscape(d.title) << "\",\"algo\":\"" << d.algo
      << "\",\"description\":\"" << jsonEscape(d.description) << "\",\"next\":\"" << jsonEscape(d.next) << "\"}";
  }
  o << "]";
  return o.str();
}

std::string Session::help() {
  return
      "Commands:\n"
      "  algo <rc|marksweep|compact|copying|generational>   select the collector\n"
      "  new [Type] [refs=2] [dataBytes=0] [label]          allocate an object\n"
      "  ref <src> <slot> <dst>                             src.refs[slot] = dst   (objects by label, #id or id)\n"
      "  unref <src> <slot>                                 src.refs[slot] = null\n"
      "  root add <name> <obj> [thread]                     register a global (or stack) root\n"
      "  root rm <name>                                     remove a root\n"
      "  gc [minor|major]                                   collect garbage\n"
      "  demo <1-6> [algo]                                  load a predefined scenario\n"
      "  threads <n>                                        number of simulated application threads\n"
      "  set <promotion|cards|heap|autogc|safepoints> <v>   change configuration\n"
      "  heap | status | graph | stats | events [n]         inspect the runtime\n"
      "  inspect <obj> | why <obj>                          object details / reachability reason\n"
      "  reset                                              wipe the heap and start over\n";
}

// ------------------------------------------------------------------- session
Session::Session(const Config& cfg) : rt_(cfg) {}

std::string Session::stateJson() { return snapshotJson(rt_); }

CommandResult Session::run(const std::string& line) {
  if (rt_.events.size() > 20000) rt_.events.clearHistory();
  size_t start = rt_.events.size();
  uint32_t epoch = rt_.events.epoch();
  std::vector<std::string> tok = tokenize(line);
  CommandResult res;
  if (tok.empty() || tok[0][0] == '#') {
    res.message = "";
    res.eventStart = res.eventEnd = start;
    return res;
  }
  try {
    res = exec(tok);
  } catch (const std::exception& e) {
    res = fail(std::string("Error: ") + e.what());
  }
  if (rt_.events.epoch() != epoch) start = 0;  // a reset cleared the history
  res.eventStart = start;
  res.eventEnd = rt_.events.size();
  bool ranGc = false;
  for (size_t i = res.eventStart; i < res.eventEnd; ++i) {
    EventType t = rt_.events.history()[i].type;
    if (t == EventType::GCStarted || t == EventType::MinorGCStarted || t == EventType::MajorGCStarted) ranGc = true;
  }
  res.replay = ranGc && tok[0] != "demo";
  return res;
}

std::string Session::runJson(const std::string& line) {
  std::string before = snapshotJson(rt_);
  CommandResult r = run(line);
  std::ostringstream o;
  o << "{\"ok\":" << (r.ok ? "true" : "false") << ",\"command\":\"" << jsonEscape(line) << "\",\"message\":\""
    << jsonEscape(r.message) << "\",\"replay\":" << (r.replay ? "true" : "false") << ",\"before\":" << before
    << ",\"events\":" << eventsToJson(rt_.events.history(), r.eventStart, r.eventEnd)
    << ",\"after\":" << snapshotJson(rt_) << "}";
  return o.str();
}

CommandResult Session::runDemo(int number, const std::string& algoOverride) {
  for (const DemoInfo& d : demos()) {
    if (d.number != number) continue;
    Algo a;
    if (!parseAlgo(algoOverride.empty() ? d.algo : algoOverride, a)) return fail("Unknown algorithm '" + algoOverride + "'");
    Config c = rt_.config;
    c.algo = a;
    rt_.reconfigure(c);
    for (const std::string& line : d.script) {
      CommandResult r = run(line);
      if (!r.ok) return fail("Demo setup failed at '" + line + "': " + r.message);
    }
    return good("DEMO " + std::to_string(d.number) + " - " + d.title + " [" + algoName(a) + "]\n" + d.description +
                "\n=> " + d.next);
  }
  return fail("No such demo (choose 1-6).");
}

CommandResult Session::exec(const std::vector<std::string>& t) {
  const std::string& cmd = t[0];

  if (cmd == "help") return good(help());

  if (cmd == "algo") {
    Algo a;
    if (t.size() < 2 || !parseAlgo(t[1], a)) return fail("usage: algo <rc|marksweep|compact|copying|generational>");
    bool kept = rt_.setAlgorithm(a);
    return good(std::string("Collector: ") + algoName(a) + (kept ? " (heap contents preserved)" : " (heap reset: this algorithm uses a different memory layout)"));
  }

  if (cmd == "reset") {
    rt_.reset();
    return good("Runtime reset.");
  }

  if (cmd == "new") {
    std::string type = t.size() > 1 ? t[1] : "Node";
    uint32_t refs = 2, data = 0;
    if (t.size() > 2 && !parseUInt(t[2], refs)) return fail("refs must be a number");
    if (t.size() > 3 && !parseUInt(t[3], data)) return fail("dataBytes must be a number");
    std::string label = t.size() > 4 ? t[4] : "";
    if (refs > kMaxRefs) return fail("at most 16 reference slots");
    if (!label.empty() && rt_.resolve(label) != 0) return fail("label '" + label + "' is already in use");
    ObjId id = rt_.allocate(type, refs, data, label);
    if (id == 0) return fail("Out of memory: cannot allocate " + std::to_string(objectSize(refs, data)) + " bytes.");
    return good("Allocated " + rt_.name(id) + " at " + hex(rt_.addrOf(id)) + " in " + rt_.spaceName(rt_.addrOf(id)) + ".");
  }

  if (cmd == "ref" || cmd == "unref") {
    bool isRef = cmd == "ref";
    if (t.size() < (isRef ? 4u : 3u)) return fail(isRef ? "usage: ref <src> <slot> <dst>" : "usage: unref <src> <slot>");
    ObjId src = rt_.resolve(t[1]);
    uint32_t slot;
    if (!src) return fail("unknown object '" + t[1] + "'");
    if (!parseUInt(t[2], slot)) return fail("slot must be a number");
    ObjId dst = 0;
    if (isRef) {
      dst = rt_.resolve(t[3]);
      if (!dst) return fail("unknown object '" + t[3] + "'");
    }
    if (!rt_.setRef(src, slot, dst)) return fail(rt_.name(src) + " has no reference slot " + std::to_string(slot));
    return good(isRef ? rt_.name(src) + ".refs[" + t[2] + "] = " + rt_.name(dst) : rt_.name(src) + ".refs[" + t[2] + "] = null");
  }

  if (cmd == "root") {
    if (t.size() >= 4 && t[1] == "add") {
      ObjId id = rt_.resolve(t[3]);
      if (!id) return fail("unknown object '" + t[3] + "'");
      uint32_t thread = 0;
      int th = -1;
      if (t.size() > 4) {
        if (!parseUInt(t[4], thread)) return fail("thread must be a number");
        th = static_cast<int>(thread);
      }
      if (!rt_.addRoot(t[2], id, th)) return fail("root name '" + t[2] + "' already exists");
      return good("Root '" + t[2] + "' -> " + rt_.name(id));
    }
    if (t.size() >= 3 && (t[1] == "rm" || t[1] == "remove")) {
      if (!rt_.removeRoot(t[2])) return fail("no root named '" + t[2] + "'");
      return good("Root '" + t[2] + "' removed.");
    }
    return fail("usage: root add <name> <obj> [thread] | root rm <name>");
  }

  if (cmd == "gc") {
    CollectKind k = CollectKind::Auto;
    if (t.size() > 1) {
      if (t[1] == "minor") k = CollectKind::Minor;
      else if (t[1] == "major" || t[1] == "full") k = CollectKind::Major;
      else return fail("usage: gc [minor|major]");
    }
    size_t from = rt_.events.size();
    rt_.collect(k);
    std::string msg = "GC finished.";
    const auto& h = rt_.events.history();
    for (size_t i = h.size(); i > from; --i) {
      EventType ty = h[i - 1].type;
      if (ty == EventType::GCCompleted || ty == EventType::MinorGCCompleted || ty == EventType::MajorGCCompleted) {
        msg = h[i - 1].text;
        break;
      }
    }
    return good(msg);
  }

  if (cmd == "demo") {
    uint32_t n;
    if (t.size() < 2 || !parseUInt(t[1], n)) return fail("usage: demo <1-6> [algo]");
    return runDemo(static_cast<int>(n), t.size() > 2 ? t[2] : "");
  }

  if (cmd == "threads") {
    uint32_t n;
    if (t.size() < 2 || !parseUInt(t[1], n) || n > 16) return fail("usage: threads <0-16>");
    rt_.config.threads = n;
    rt_.safepoints.configure(n);
    return good(std::to_string(n) + " simulated application thread(s).");
  }

  if (cmd == "set") {
    if (t.size() < 3) return fail("usage: set <promotion|cards|heap|autogc|safepoints> <value>");
    const std::string& key = t[1];
    const std::string& v = t[2];
    uint32_t n = 0;
    bool on = v == "on" || v == "true" || v == "cards" || v == "1";
    if (key == "promotion") {
      if (!parseUInt(v, n) || n > 15) return fail("promotion threshold must be 0-15");
      rt_.config.promotionThreshold = n;
      return good("Promotion threshold = " + v);
    }
    if (key == "cards") {
      rt_.config.useCardTable = on;
      return good(on ? "Minor GC will scan DIRTY CARDS." : "Minor GC will scan the REMEMBERED SET.");
    }
    if (key == "autogc") { rt_.config.autoGC = on; return good(std::string("Auto GC on allocation failure: ") + (on ? "on" : "off")); }
    if (key == "safepoints") { rt_.config.useSafepoints = on; return good(std::string("Safepoint protocol: ") + (on ? "on" : "off")); }
    if (key == "heap") {
      if (!parseUInt(v, n) || n < 512 || n > 65536 || n % 64 != 0) return fail("heap size must be a multiple of 64 between 512 and 65536");
      Config c = rt_.config;
      c.heapBytes = n;
      rt_.reconfigure(c);
      return good("Heap resized to " + v + " bytes (runtime reset).");
    }
    return fail("unknown setting '" + key + "'");
  }

  if (cmd == "heap") return good(asciiHeap(rt_));

  if (cmd == "status" || cmd == "stats") {
    std::ostringstream o;
    o << "Collector: " << algoName(rt_.config.algo) << "\n" << asciiHeap(rt_);
    RegionStats s = rt_.heap.stats();
    o << "Heap: " << s.used << "/" << s.size << " bytes used, " << s.objects << " object(s), " << s.freeBlocks
      << " free block(s), largest free " << s.largestFree << " B, fragmentation "
      << static_cast<int>(s.fragmentation * 100 + 0.5) << "%\n";
    for (const auto& kv : rt_.metrics.toMap()) o << "  " << kv.first << " = " << kv.second << "\n";
    return good(o.str());
  }

  if (cmd == "graph") {
    ObjectGraph g(rt_);
    std::ostringstream o;
    std::set<ObjId> live = g.reachable();
    for (const Root& r : rt_.roots.all()) {
      ObjId id = r.addr ? rt_.heap.mem().header(r.addr)->id : 0;
      o << "ROOT '" << r.name << "' -> " << (id ? rt_.name(id) : "null") << "\n";
    }
    for (ObjId id : g.nodes()) {
      o << (live.count(id) ? "LIVE        " : "UNREACHABLE ") << rt_.name(id);
      for (ObjId t : g.edgesOf(id)) o << " -> " << rt_.name(t);
      o << "\n";
    }
    return good(o.str());
  }

  if (cmd == "inspect" || cmd == "why") {
    if (t.size() < 2) return fail("usage: " + cmd + " <obj>");
    ObjId id = rt_.resolve(t[1]);
    if (!id) return fail("unknown object '" + t[1] + "'");
    if (cmd == "inspect") return good(describeObject(rt_, id));
    ObjectGraph g(rt_);
    return good(rt_.name(id) + ": " + (g.reachable().count(id) ? "LIVE. " : "UNREACHABLE. ") + g.explain(id));
  }

  if (cmd == "events") {
    uint32_t n = 20;
    if (t.size() > 1) parseUInt(t[1], n);
    const auto& h = rt_.events.history();
    std::ostringstream o;
    for (size_t i = h.size() > n ? h.size() - n : 0; i < h.size(); ++i) o << describeEvent(h[i]) << "\n";
    return good(o.str());
  }

  return fail("unknown command '" + cmd + "' (try `help`)");
}

}  // namespace minigc
