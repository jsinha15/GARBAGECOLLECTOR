#include "minigc/graph.h"

#include <algorithm>
#include <deque>

namespace minigc {

ObjectGraph::ObjectGraph(Runtime& rt) : rt_(rt) {
  for (const auto& kv : rt.objects()) {  // std::map => ascending id => deterministic
    ObjId id = kv.first;
    nodes_.push_back(id);
    std::vector<ObjId>& out = edges_[id];
    ObjectHeader* h = rt.heap.mem().header(kv.second);
    Addr* refs = rt.heap.mem().refs(kv.second);
    for (uint32_t i = 0; i < h->numRefs; ++i) {
      if (refs[i] == kNull) continue;
      out.push_back(rt.heap.mem().header(refs[i])->id);
    }
  }
}

const std::vector<ObjId>& ObjectGraph::edgesOf(ObjId id) const {
  auto it = edges_.find(id);
  return it == edges_.end() ? none_ : it->second;
}

std::vector<ObjId> ObjectGraph::dfs(ObjId start) const {
  std::vector<ObjId> order;
  std::set<ObjId> seen;
  std::vector<ObjId> stack{start};
  while (!stack.empty()) {
    ObjId cur = stack.back();
    stack.pop_back();
    if (!seen.insert(cur).second) continue;
    order.push_back(cur);
    const auto& out = edgesOf(cur);
    for (auto it = out.rbegin(); it != out.rend(); ++it) stack.push_back(*it);  // so slot 0 is visited first
  }
  return order;
}

std::vector<ObjId> ObjectGraph::bfs(ObjId start) const {
  std::vector<ObjId> order;
  std::set<ObjId> seen{start};
  std::deque<ObjId> q{start};
  while (!q.empty()) {
    ObjId cur = q.front();
    q.pop_front();
    order.push_back(cur);
    for (ObjId n : edgesOf(cur))
      if (seen.insert(n).second) q.push_back(n);
  }
  return order;
}

std::set<ObjId> ObjectGraph::reachable() const {
  std::set<ObjId> seen;
  std::deque<ObjId> q;
  for (const Root& r : rt_.roots.all()) {
    if (r.addr == kNull) continue;
    ObjId id = rt_.heap.mem().header(r.addr)->id;
    if (seen.insert(id).second) q.push_back(id);
  }
  while (!q.empty()) {
    ObjId cur = q.front();
    q.pop_front();
    for (ObjId n : edgesOf(cur))
      if (seen.insert(n).second) q.push_back(n);
  }
  return seen;
}

std::vector<ObjId> ObjectGraph::unreachable() const {
  std::set<ObjId> live = reachable();
  std::vector<ObjId> out;
  for (ObjId id : nodes_)
    if (!live.count(id)) out.push_back(id);
  return out;
}

bool ObjectGraph::pathTo(ObjId target, std::string& rootName, std::vector<ObjId>& path) const {
  std::map<ObjId, ObjId> parent;       // child -> parent (0 = hanging directly off a root)
  std::map<ObjId, std::string> viaRoot;
  std::deque<ObjId> q;
  for (const Root& r : rt_.roots.all()) {
    if (r.addr == kNull) continue;
    ObjId id = rt_.heap.mem().header(r.addr)->id;
    if (parent.count(id)) continue;
    parent[id] = 0;
    viaRoot[id] = r.name;
    q.push_back(id);
  }
  while (!q.empty()) {
    ObjId cur = q.front();
    q.pop_front();
    if (cur == target) break;
    for (ObjId n : edgesOf(cur)) {
      if (parent.count(n)) continue;
      parent[n] = cur;
      q.push_back(n);
    }
  }
  if (!parent.count(target)) return false;
  path.clear();
  for (ObjId cur = target; cur != 0; cur = parent[cur]) {
    path.push_back(cur);
    if (parent[cur] == 0) rootName = viaRoot[cur];
  }
  std::reverse(path.begin(), path.end());
  return true;
}

std::string ObjectGraph::explain(ObjId id) const {
  std::string rootName;
  std::vector<ObjId> path;
  if (!pathTo(id, rootName, path))
    return "No path exists from any GC root to " + rt_.name(id) + ".";
  std::string s = "Root '" + rootName + "'";
  for (ObjId p : path) s += " -> " + rt_.name(p);
  return s;
}

// Tarjan's strongly connected components over the unreachable subgraph.
std::vector<std::vector<ObjId>> ObjectGraph::unreachableCycles() const {
  std::set<ObjId> garbage;
  for (ObjId id : unreachable()) garbage.insert(id);

  std::map<ObjId, int> index, low;
  std::set<ObjId> onStack;
  std::vector<ObjId> stack;
  std::vector<std::vector<ObjId>> result;
  int counter = 0;

  std::vector<std::pair<ObjId, size_t>> work;  // iterative Tarjan: (node, next edge to visit)
  for (ObjId root : garbage) {
    if (index.count(root)) continue;
    work.push_back({root, 0});
    while (!work.empty()) {
      ObjId v = work.back().first;
      size_t& ei = work.back().second;
      if (ei == 0 && !index.count(v)) {
        index[v] = low[v] = counter++;
        stack.push_back(v);
        onStack.insert(v);
      }
      const auto& out = edgesOf(v);
      bool descended = false;
      while (ei < out.size()) {
        ObjId w = out[ei++];
        if (!garbage.count(w)) continue;
        if (!index.count(w)) {
          work.push_back({w, 0});
          descended = true;
          break;
        }
        if (onStack.count(w)) low[v] = std::min(low[v], index[w]);
      }
      if (descended) continue;
      if (low[v] == index[v]) {
        std::vector<ObjId> comp;
        ObjId w;
        do {
          w = stack.back();
          stack.pop_back();
          onStack.erase(w);
          comp.push_back(w);
        } while (w != v);
        bool selfLoop = comp.size() == 1 &&
                        std::find(out.begin(), out.end(), comp[0]) != out.end();
        if (comp.size() > 1 || selfLoop) {
          std::sort(comp.begin(), comp.end());
          result.push_back(comp);
        }
      }
      work.pop_back();
      if (!work.empty()) {
        ObjId parent = work.back().first;
        low[parent] = std::min(low[parent], low[v]);
      }
    }
  }
  std::sort(result.begin(), result.end());
  return result;
}

}  // namespace minigc
