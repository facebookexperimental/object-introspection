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
#include "PassManager.h"

#include <glog/logging.h>

#include <sstream>

#include "Printer.h"
#include "TypeGraph.h"

template <typename T>
using ref = std::reference_wrapper<T>;

namespace type_graph {

void Pass::run(TypeGraph& typeGraph) {
  fn_(typeGraph);
}

void PassManager::addPass(Pass p) {
  passes_.push_back(std::move(p));
}

namespace {
void print(const TypeGraph& typeGraph) {
  if (!VLOG_IS_ON(1))
    return;

  // TODO: Long strings will be truncated by glog. Find another way to do this
  std::stringstream out;
  Printer printer{out};
  for (const auto& type : typeGraph.rootTypes()) {
    printer.print(type);
  }

  LOG(INFO) << "\n" << out.str();
}
}  // namespace

const std::string separator = "----------------";

void PassManager::run(TypeGraph& typeGraph) {
  VLOG(1) << separator;
  VLOG(1) << "Parsed Type Graph:";
  VLOG(1) << separator;
  print(typeGraph);
  VLOG(1) << separator;

  for (size_t i = 0; i < passes_.size(); i++) {
    auto& pass = passes_[i];
    LOG(INFO) << "Running pass (" << i + 1 << "/" << passes_.size()
              << "): " << pass.name();
    pass.run(typeGraph);
    VLOG(1) << separator;
    print(typeGraph);
    VLOG(1) << separator;
  }
}

}  // namespace type_graph
