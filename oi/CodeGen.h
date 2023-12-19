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
#include <list>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "ContainerInfo.h"
#include "OICodeGen.h"
#include "type_graph/TypeGraph.h"

struct drgn_type;
namespace oi::detail {
class SymbolService;
}
namespace oi::detail::type_graph {
class Class;
class Member;
}  // namespace oi::detail::type_graph

namespace oi::detail {

class CodeGen {
 public:
  CodeGen(const OICodeGen::Config& config);
  CodeGen(const OICodeGen::Config& config, SymbolService& symbols)
      : config_(config), symbols_(&symbols) {
  }

  struct ExactName {
    std::string name;
  };
  struct HashedComponent {
    std::string name;
  };
  using RootFunctionName = std::variant<ExactName, HashedComponent>;

  /*
   * Helper function to perform all the steps required for code generation for a
   * single drgn_type.
   */
  bool codegenFromDrgn(struct drgn_type* drgnType, std::string& code);
  bool codegenFromDrgn(struct drgn_type* drgnType,
                       std::string linkageName,
                       std::string& code);
  void exportDrgnTypes(TypeHierarchy& th,
                       std::list<drgn_type>& drgnTypes,
                       drgn_type** rootType) const;

  bool registerContainers();
  void registerContainer(std::unique_ptr<ContainerInfo> containerInfo);
  void registerContainer(const std::filesystem::path& path);
  void addDrgnRoot(struct drgn_type* drgnType,
                   type_graph::TypeGraph& typeGraph);
  void transform(type_graph::TypeGraph& typeGraph);
  void generate(type_graph::TypeGraph& typeGraph,
                std::string& code,
                RootFunctionName rootName);

 private:
  type_graph::TypeGraph typeGraph_;
  const OICodeGen::Config& config_;
  SymbolService* symbols_ = nullptr;
  std::vector<std::unique_ptr<ContainerInfo>> containerInfos_;
  std::unordered_set<const ContainerInfo*> definedContainers_;
  std::unordered_map<const type_graph::Class*, const type_graph::Member*>
      thriftIssetMembers_;

  bool codegenFromDrgn(struct drgn_type* drgnType,
                       std::string& code,
                       RootFunctionName name);

  void genDefsThrift(const type_graph::TypeGraph& typeGraph, std::string& code);
  void addGetSizeFuncDefs(const type_graph::TypeGraph& typeGraph,
                          std::string& code);
  void getClassSizeFuncDef(const type_graph::Class& c, std::string& code);
  void getClassSizeFuncConcrete(std::string_view funcName,
                                const type_graph::Class& c,
                                std::string& code) const;
  void addTypeHandlers(const type_graph::TypeGraph& typeGraph,
                       std::string& code);

  void genClassTypeHandler(const type_graph::Class& c, std::string& code);
  void genClassStaticType(const type_graph::Class& c, std::string& code);
  void genClassTraversalFunction(const type_graph::Class& c, std::string& code);
  void genClassTreeBuilderInstructions(const type_graph::Class& c,
                                       std::string& code);
};

}  // namespace oi::detail
