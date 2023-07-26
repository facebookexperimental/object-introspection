/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */
#pragma once

#include <vector>

#include "Types.h"

namespace oi::detail::type_graph {

/*
 * NodeTracker
 *
 * Helper class for visitors. Efficiently tracks whether or not a graph node has
 * been seen before, to avoid infinite looping on cycles.
 */
class NodeTracker {
 public:
  NodeTracker() = default;
  NodeTracker(size_t size) : visited_(size) {
  }

  /*
   * visit
   *
   * Marks a given node as visited.
   * Returns true if this node has already been visited, false otherwise.
   */
  bool visit(const Type& type) {
    auto id = type.id();
    if (id < 0)
      return false;
    if (visited_.size() <= static_cast<size_t>(id))
      visited_.resize(id + 1);
    bool result = visited_[id];
    visited_[id] = true;
    return result;
  }

  /*
   * reset
   *
   * Clears the contents of this NodeTracker and marks every node as unvisited.
   */
  void reset() {
    std::fill(visited_.begin(), visited_.end(), false);
  }

  void resize(size_t size) {
    visited_.resize(size);
  }

 private:
  std::vector<bool> visited_;
};

}  // namespace oi::detail::type_graph
