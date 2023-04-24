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
#include <memory>
#include <msgpack/sbuffer_decl.hpp>
#include <optional>
#include <set>
#include <string>
#include <unordered_set>
#include <vector>

#include "oi/Common.h"
#include "oi/Features.h"

// The rocksdb includes are extremely heavy and bloat compile times,
// so we just forward-declare `DB` to avoid making other compile units
// pay the cost of including the relevant headers.
namespace rocksdb {
class DB;
}

// Forward declared, comes from PaddingInfo.h
struct PaddingInfo;

class TreeBuilder {
 public:
  struct Config {
    // Don't set default values for the config so the user gets
    // an "unitialized field" warning if he missed any.
    ObjectIntrospection::FeatureSet features;
    bool logAllStructs;
    bool dumpDataSegment;
    std::optional<std::string> jsonPath;
  };

  TreeBuilder(Config);
  ~TreeBuilder();

  void build(const std::vector<uint64_t>&,
             const std::string&,
             struct drgn_type*,
             const TypeHierarchy&);
  void dumpJson();
  void setPaddedStructs(std::map<std::string, PaddingInfo>* paddedStructs);
  bool emptyOutput() const;

 private:
  typedef uint64_t Version;
  typedef uint64_t NodeID;
  struct DBHeader;
  struct Node;
  struct Variable;

  const TypeHierarchy* th = nullptr;
  const std::vector<uint64_t>* oidData = nullptr;
  std::map<std::string, PaddingInfo>* paddedStructs = nullptr;

  /*
   * The RocksDB output needs versioning so they are imported correctly in
   * Scuba. Version 1 had no concept of versioning and no header.
   * We currently are at version 2.1:
   *  - Introduce the Error ID at index 1023, but don't output it
   * Changelog v2:
   *  - Introduce the DBHeader at index 0
   *  - Introduce the versioning
   *  - Handle multiple root_ids, to import multiple objects in Scuba
   */
  static constexpr Version VERSION = 2;
  static constexpr NodeID ROOT_NODE_ID = 0;
  static constexpr NodeID ERROR_NODE_ID = 1023;
  static constexpr NodeID FIRST_NODE_ID = 1024;

  /*
   * The first 1024 IDs are reserved for future use.
   * ID 0: DBHeader
   * ID 1023: Error - an error occured while TreeBuilding
   */
  NodeID nextNodeID = FIRST_NODE_ID;

  const Config config{};
  size_t oidDataIndex;

  std::vector<NodeID> rootIDs{};

  /*
   * Used exclusively by `TreeBuilder::serialize()` to avoid having
   * to allocate a new buffer every time we serialize a `Node`.
   */
  std::unique_ptr<msgpack::sbuffer> buffer;
  rocksdb::DB* db = nullptr;

  uint64_t getDrgnTypeSize(struct drgn_type* type);
  uint64_t next();
  bool isContainer(const Variable& variable);
  bool isPrimitive(struct drgn_type* type);
  Node process(NodeID id, Variable variable);
  void processContainer(const Variable& variable, Node& node);
  template <class T>
  std::string_view serialize(const T&);
  void JSON(NodeID id, std::ofstream& output);

  static void setSize(TreeBuilder::Node& node,
                      uint64_t dynamicSize,
                      uint64_t memberSizes);
};
