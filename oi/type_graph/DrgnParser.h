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

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "TypeGraph.h"
#include "Types.h"

struct drgn_type;
struct drgn_type_template_parameter;
struct drgn_error;

struct ContainerInfo;

namespace type_graph {
class Array;
class Container;
class Enum;
class Member;
class Primitive;
class TemplateParam;
class Type;
class TypeGraph;
class Typedef;
struct Function;
struct Parent;
}  // namespace type_graph

namespace type_graph {

/*
 * DrgnParser
 *
 * Reads debug information from a drgn_type to build a type graph.
 * Returns a reference to the Type node corresponding to the given drgn_type.
 *
 * DrgnParser tries to balance two philosophies:
 * - Simplicity: containing minimal logic other than reading info from drgn
 * - Performance: reading no more info from drgn than necessary
 *
 * For the most part we try to move logic out of DrgnParser and have later
 * passes clean up the type graph, e.g. flattening parents. However, we do
 * incorporate some extra logic here when it would allow us to read less DWARF
 * information, e.g. matching containers in DrngParser means we don't read
 * details about container internals.
 */
class DrgnParser {
 public:
  DrgnParser(TypeGraph& typeGraph,
             const std::vector<ContainerInfo>& containers,
             bool chaseRawPointers)
      : typeGraph_(typeGraph),
        containers_(containers),
        chaseRawPointers_(chaseRawPointers) {
  }
  Type& parse(drgn_type* root);

 private:
  Type& enumerateType(drgn_type* type);
  Container* enumerateContainer(drgn_type* type, const std::string& fqName);
  Type& enumerateClass(drgn_type* type);
  Enum& enumerateEnum(drgn_type* type);
  Typedef& enumerateTypedef(drgn_type* type);
  Type& enumeratePointer(drgn_type* type);
  Array& enumerateArray(drgn_type* type);
  Primitive& enumeratePrimitive(drgn_type* type);

  void enumerateTemplateParam(drgn_type_template_parameter* tparams,
                              size_t i,
                              std::vector<TemplateParam>& params);
  void enumerateClassTemplateParams(drgn_type* type,
                                    std::vector<TemplateParam>& params);
  void enumerateClassParents(drgn_type* type, std::vector<Parent>& parents);
  void enumerateClassMembers(drgn_type* type, std::vector<Member>& members);
  void enumerateClassFunctions(drgn_type* type,
                               std::vector<Function>& functions);

  template <typename T, typename... Args>
  T& makeType(drgn_type* drgnType, Args&&... args) {
    auto& newType = typeGraph_.makeType<T>(std::forward<Args>(args)...);
    drgn_types_.insert({drgnType, newType});
    return newType;
  }
  bool chasePointer() const;

  // Store a mapping of drgn types to type graph nodes for deduplication during
  // parsing. This stops us getting caught in cycles.
  std::unordered_map<drgn_type*, std::reference_wrapper<Type>> drgn_types_;

  TypeGraph& typeGraph_;
  const std::vector<ContainerInfo>& containers_;
  int depth_;
  bool chaseRawPointers_;
};

class DrgnParserError : public std::runtime_error {
 public:
  DrgnParserError(const std::string& msg) : std::runtime_error{msg} {
  }
  DrgnParserError(const std::string& msg, drgn_error* err);

  ~DrgnParserError();

 private:
  drgn_error* err_ = nullptr;
};

}  // namespace type_graph
