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

#include <filesystem>
#include <functional>
#include <string>

#include "ContainerInfo.h"
#include "OICodeGen.h"
#include "SymbolService.h"

struct drgn_type;

namespace type_graph {
class Class;
class Container;
class Type;
class TypeGraph;
}  // namespace type_graph

class CodeGen {
 public:
  CodeGen(type_graph::TypeGraph& typeGraph,
          OICodeGen::Config& config,
          SymbolService& symbols)
      : typeGraph_(typeGraph), config_(config), symbols_(symbols) {
  }

  bool generate(drgn_type* drgnType, std::string& code);
  void loadConfig(const std::set<std::filesystem::path>& containerConfigPaths);

 private:
  void registerContainer(const std::filesystem::path& path);

  type_graph::TypeGraph& typeGraph_;
  OICodeGen::Config config_;
  std::vector<ContainerInfo> containerInfos_;
  SymbolService& symbols_;
};
