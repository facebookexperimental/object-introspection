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
#include <map>
#include <set>
#include <string>
#include <vector>

extern "C" {
#include <drgn.h>
}

constexpr int oidMagicId = 0x01DE8;

struct ContainerInfo;

struct RootInfo {
  std::string varName;
  struct drgn_qualified_type type;
};

struct ClassMember {
  std::string typeName;
  std::string varName;
};

struct DrgnClassMemberInfo {
  struct drgn_type *type;
  std::string member_name;
  uint64_t bit_offset;
  uint64_t bit_field_size;
  bool isStubbed;
};

struct TypeHierarchy {
  std::map<struct drgn_type *, std::vector<DrgnClassMemberInfo>>
      classMembersMap;
  std::map<struct drgn_type *,
           std::pair<ContainerInfo, std::vector<struct drgn_qualified_type>>>
      containerTypeMap;
  std::map<struct drgn_type *, struct drgn_type *> typedefMap;
  std::map<std::string, size_t> sizeMap;
  std::set<struct drgn_type *> knownDummyTypeList;
  std::map<struct drgn_type *, struct drgn_type *> pointerToTypeMap;
  std::set<struct drgn_type *> thriftIssetStructTypes;
};
