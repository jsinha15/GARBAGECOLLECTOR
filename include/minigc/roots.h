// GC roots: the references that live OUTSIDE the heap (globals, thread stacks).
// Everything reachable from a root is live; everything else is garbage.
#pragma once
#include <string>
#include <vector>

#include "minigc/common.h"

namespace minigc {

enum class RootKind { Global, Stack };

struct Root {
  uint32_t id = 0;        // deterministic root number (1, 2, 3, ...), never reused
  std::string name;       // user visible name
  RootKind kind = RootKind::Global;
  int thread = -1;        // owning simulated thread for stack roots
  Addr addr = kNull;      // the reference itself; updated by moving collectors
};

class RootManager {
 public:
  // Returns the new root number, or 0 if a root with that name already exists.
  uint32_t add(const std::string& name, RootKind kind, int thread, Addr addr) {
    if (find(name)) return 0;
    Root r;
    r.id = nextId_++;
    r.name = name;
    r.kind = kind;
    r.thread = thread;
    r.addr = addr;
    roots_.push_back(r);
    return r.id;
  }

  bool remove(const std::string& name, Root* removed = nullptr) {
    for (size_t i = 0; i < roots_.size(); ++i) {
      if (roots_[i].name == name) {
        if (removed) *removed = roots_[i];
        roots_.erase(roots_.begin() + static_cast<std::ptrdiff_t>(i));
        return true;
      }
    }
    return false;
  }

  Root* find(const std::string& name) {
    for (auto& r : roots_)
      if (r.name == name) return &r;
    return nullptr;
  }

  std::vector<Root>& all() { return roots_; }
  const std::vector<Root>& all() const { return roots_; }
  void clear() { roots_.clear(); nextId_ = 1; }

 private:
  std::vector<Root> roots_;
  uint32_t nextId_ = 1;
};

}  // namespace minigc
