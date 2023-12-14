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

#include <memory>
#include <unordered_map>
#include <vector>

#include "TypeGraph.h"
#include "Types.h"

struct drgn_type;
struct drgn_type_template_parameter;
struct drgn_error;

struct ContainerInfo;

namespace oi::detail::type_graph {

struct DrgnParserOptions {
  bool chaseRawPointers = false;
  bool readEnumValues = false;
};

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
 * passes clean up the type graph, e.g. flattening parents and identifying
 * containers.
 */
class DrgnParser {
 public:
  DrgnParser(TypeGraph& typeGraph, DrgnParserOptions options)
      : typeGraph_(typeGraph), options_(options) {
  }
  Type& parse(struct drgn_type* root);

 private:
  Type& enumerateType(struct drgn_type* type);
  Class& enumerateClass(struct drgn_type* type);
  Enum& enumerateEnum(struct drgn_type* type);
  Typedef& enumerateTypedef(struct drgn_type* type);
  Type& enumeratePointer(struct drgn_type* type);
  Array& enumerateArray(struct drgn_type* type);
  Primitive& enumeratePrimitive(struct drgn_type* type);

  void enumerateTemplateParam(struct drgn_type* type,
                              drgn_type_template_parameter* tparams,
                              size_t i,
                              std::vector<TemplateParam>& params);
  void enumerateClassTemplateParams(struct drgn_type* type,
                                    std::vector<TemplateParam>& params);
  void enumerateClassParents(struct drgn_type* type,
                             std::vector<Parent>& parents);
  void enumerateClassMembers(struct drgn_type* type,
                             std::vector<Member>& members);
  void enumerateClassFunctions(struct drgn_type* type,
                               std::vector<Function>& functions);

  template <typename T, typename... Args>
  T& makeType(struct drgn_type* drgnType, Args&&... args) {
    auto& newType = typeGraph_.makeType<T>(std::forward<Args>(args)...);
    drgn_types_.insert_or_assign(drgnType, newType);
    return newType;
  }
  bool chasePointer() const;

  // Store a mapping of drgn types to type graph nodes for deduplication during
  // parsing. This stops us getting caught in cycles.
  std::unordered_map<struct drgn_type*, std::reference_wrapper<Type>>
      drgn_types_;

  TypeGraph& typeGraph_;
  int depth_;
  DrgnParserOptions options_;
};

class DrgnParserError : public std::runtime_error {
 public:
  DrgnParserError(const std::string& msg) : std::runtime_error{msg} {
  }
  DrgnParserError(const std::string& msg, struct drgn_error* err);

  ~DrgnParserError();

 private:
  struct drgn_error* err_ = nullptr;
};

}  // namespace oi::detail::type_graph
