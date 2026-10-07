// Object graph + reachability analysis (Phase 4). This is the "ground truth"
// used by tests and by the UI to explain WHY an object is live or garbage;
// the collectors do their own tracing and must agree with it.
#pragma once
#include <map>
#include <set>
#include <string>
#include <vector>

#include "minigc/runtime.h"

namespace minigc {

class ObjectGraph {
 public:
  explicit ObjectGraph(Runtime& rt);

  const std::vector<ObjId>& nodes() const { return nodes_; }
  const std::vector<ObjId>& edgesOf(ObjId id) const;

  std::vector<ObjId> dfs(ObjId start) const;  // preorder, follows slots in order
  std::vector<ObjId> bfs(ObjId start) const;

  std::set<ObjId> reachable() const;          // reachable from any root
  std::vector<ObjId> unreachable() const;     // everything else, ascending id

  // Shortest path root -> ... -> target. Fills `rootName` and `path` (object ids
  // starting with the object the root points at). False if unreachable.
  bool pathTo(ObjId target, std::string& rootName, std::vector<ObjId>& path) const;
  std::string explain(ObjId id) const;        // "Root 'r' -> A -> B" or "No path exists ..."

  // Groups of unreachable objects that keep each other alive (strongly
  // connected components with a cycle). Used by the reference-counting demo.
  std::vector<std::vector<ObjId>> unreachableCycles() const;

 private:
  Runtime& rt_;
  std::vector<ObjId> nodes_;
  std::map<ObjId, std::vector<ObjId>> edges_;
  std::vector<ObjId> none_;
};

}  // namespace minigc
