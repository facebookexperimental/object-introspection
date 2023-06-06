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

#include <unordered_map>
#include <vector>

#include "TypeGraph.h"
#include "Types.h"

struct drgn_type;
struct drgn_type_template_parameter;
struct drgn_error;

struct ContainerInfo;

namespace type_graph {

class DrgnParser {
 public:
  DrgnParser(TypeGraph& typeGraph,
             const std::vector<ContainerInfo>& containers,
             bool chaseRawPointers)
      : typeGraph_(typeGraph),
        containers_(containers),
        chaseRawPointers_(chaseRawPointers) {
  }
  Type* parse(struct drgn_type* root);

 private:
  Type* enumerateType(struct drgn_type* type);
  Container* enumerateContainer(struct drgn_type* type);
  Type* enumerateClass(struct drgn_type* type);
  Enum* enumerateEnum(struct drgn_type* type);
  Typedef* enumerateTypedef(struct drgn_type* type);
  Type* enumeratePointer(struct drgn_type* type);
  Array* enumerateArray(struct drgn_type* type);
  Primitive* enumeratePrimitive(struct drgn_type* type);

  void enumerateTemplateParam(drgn_type_template_parameter* tparams,
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

  // Store a mapping of drgn types to type graph nodes for deduplication during
  // parsing. This stops us getting caught in cycles.
  std::unordered_map<struct drgn_type*, Type*> drgn_types_;

  template <typename T, typename... Args>
  T* make_type(struct drgn_type* type, Args&&... args) {
    auto type_unique_ptr = std::make_unique<T>(std::forward<Args>(args)...);
    auto type_raw_ptr = type_unique_ptr.get();
    typeGraph_.add(std::move(type_unique_ptr));
    drgn_types_.insert({type, type_raw_ptr});
    return type_raw_ptr;
  }
  bool chasePointer() const;

  TypeGraph& typeGraph_;
  const std::vector<ContainerInfo>& containers_;
  int depth_;
  bool chaseRawPointers_;
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

}  // namespace type_graph
