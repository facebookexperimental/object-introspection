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
#include <unordered_set>

#include "ContainerInfo.h"
#include "OICodeGen.h"

struct drgn_type;

class SymbolService;

namespace type_graph {
class TypeGraph;
}  // namespace type_graph

class CodeGen {
 public:
  CodeGen(OICodeGen::Config& config, SymbolService& symbols)
      : config_(config), symbols_(symbols) {
  }

  /*
   * Helper function to perform all the steps required for code generation for a
   * single drgn_type.
   */
  bool codegenFromDrgn(struct drgn_type* drgnType, std::string& code);

  void registerContainer(const std::filesystem::path& path);
  void addDrgnRoot(struct drgn_type* drgnType,
                   type_graph::TypeGraph& typeGraph);
  void transform(type_graph::TypeGraph& typeGraph);
  void generate(type_graph::TypeGraph& typeGraph,
                std::string& code,
                struct drgn_type*
                    drgnType /* TODO: this argument should not be required */
  );

 private:
  OICodeGen::Config config_;
  SymbolService& symbols_;
  std::vector<ContainerInfo> containerInfos_;
  std::unordered_set<const ContainerInfo*> definedContainers_;
};
