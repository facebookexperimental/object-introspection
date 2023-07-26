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

#include <functional>
#include <string>
#include <utility>
#include <vector>

namespace type_graph {

class TypeGraph;

/*
 * Pass
 *
 * TODO
 */
class Pass {
  using PassFn = std::function<void(TypeGraph& typeGraph)>;

 public:
  Pass(std::string name, PassFn fn) : name_(std::move(name)), fn_(fn) {
  }
  void run(TypeGraph& typeGraph);
  std::string& name() {
    return name_;
  };

 private:
  std::string name_;
  PassFn fn_;
};

/*
 * PassManager
 *
 * TODO
 */
class PassManager {
 public:
  void addPass(Pass p);
  void run(TypeGraph& typeGraph);

 private:
  std::vector<Pass> passes_;
};

}  // namespace type_graph
