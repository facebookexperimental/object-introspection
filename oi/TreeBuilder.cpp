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
#include "oi/TreeBuilder.h"

#include <glog/logging.h>

#include <boost/algorithm/string/regex.hpp>
#include <boost/scope_exit.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <msgpack.hpp>
#include <stdexcept>

#include "oi/ContainerInfo.h"
#include "oi/DrgnUtils.h"
#include "oi/Metrics.h"
#include "oi/OICodeGen.h"
#include "oi/PaddingHunter.h"
#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/statistics.h"

extern "C" {
#include <drgn.h>
#include <sys/types.h>
}

namespace oi::detail {

/* Tag indicating if the pointer has  been followed or skipped */
enum class TrackPointerTag : uint64_t {
  /* The content has been skipped.
   * It prevents double counting the footprint of a node the JIT code has seen
   * before, double counting content being stored inline, or getting stuck in an
   * infinite loop when processing circular linked list.
   */
  skipped = 0,
  followed = 1,
};

TreeBuilder::TreeBuilder(Config c) : config{std::move(c)} {
  buffer = std::make_unique<msgpack::sbuffer>();

  auto testdbPath = "/tmp/testdb_" + std::to_string(getpid());
  if (auto status = rocksdb::DestroyDB(testdbPath, {}); !status.ok()) {
    LOG(FATAL) << "RocksDB error while destroying database: "
               << status.ToString();
  }

  const int twoMinutes = 120;
  rocksdb::Options options;
  options.compression = rocksdb::kZSTD;
  options.create_if_missing = true;
  options.statistics = rocksdb::CreateDBStatistics();
  options.stats_dump_period_sec = twoMinutes;
  options.PrepareForBulkLoad();
  options.OptimizeForSmallDb();

  if (auto status = rocksdb::DB::Open(options, testdbPath, &db); !status.ok()) {
    LOG(FATAL) << "RocksDB error while opening database: " << status.ToString();
  }
}

struct TreeBuilder::Variable {
  struct drgn_type* type;
  std::string_view name;
  std::string typePath;
  std::optional<bool> isset = std::nullopt;
  bool isStubbed = false;
};

struct TreeBuilder::DBHeader {
  /**
   * Version of the database schema. See TreeBuilder.h for more info.
   */
  Version version;

  /**
   * List of IDs corresponding to the root of the probed objects.
   */
  std::vector<NodeID> rootIDs;

  MSGPACK_DEFINE_ARRAY(version, rootIDs)
};

struct TreeBuilder::Node {
  struct ContainerStats {
    /**
     * The number of elements currently present in the container
     * (e.g. `std::vector::size()`).
     */
    size_t length;
    /**
     * The maximum number of elements the container can
     * currently hold (e.g. `std::vector::capacity()`).
     */
    size_t capacity;
    /**
     * The static size (see comment for `staticSize` below for clarification on
     * what this means) of each element in a container. For example, if this
     * node corresponds to a `std::vector<int>` then `elementStaticSize`
     * would be `sizeof(int)`.
     */
    size_t elementStaticSize;
    MSGPACK_DEFINE_ARRAY(length, capacity, elementStaticSize)
  };

  /**
   * The unique identifier for this node, used as the key for this
   * node's entry in RocksDB.
   */
  NodeID id;
  /**
   * Roughly corresponds to the name you would use to refer to this node in
   * the code (e.g. variable name, member name, etc.). In some cases there is
   * no meaningful name (e.g. the elements of a vector, the node behind a
   * `typedef`) and this is left empty.
   */
  std::string_view name{};
  /**
   * The type of this node, as it would be written in the code
   * (e.g. `std::vector<int>`, `float`, `MyStruct`).
   */
  std::string typeName{};
  std::string typePath{};
  bool isTypedef{};
  /**
   * The compile-time-determinable size (i.e. memory footprint measured in
   * bytes) of this node, essentially corresponding to `sizeof(TYPE)`.
   * Just like the semantics of `sizeof`, this is inherently inclusive of the
   * type's members (if it is a `struct`, `class`, or `union`).
   */
  size_t staticSize{};
  /**
   * The size (i.e. memory usage measured in bytes) of the dynamically
   * allocated data used by this node (e.g. the heap-allocated memory
   * associated with a `std::vector`). This includes the `dynamicSize` of all
   * children (whether they be `struct`/`class` members, or the elements of a
   * container).
   */
  size_t dynamicSize{};
  std::optional<size_t> paddingSavingsSize{std::nullopt};
  std::optional<uintptr_t> pointer{std::nullopt};
  std::optional<ContainerStats> containerStats{std::nullopt};
  /**
   * Range of this node's children (start is inclusive, end is exclusive)
   *
   * If this node represents a container, `children` contains all
   * of the container's elements.
   * If this node represents a `struct` or `class`, `children`
   * contains all of its members.
   * If this node is a `typedef` or a pointer, `children` should contain a
   * single entry corresponding to the referenced type.
   */
  std::optional<std::pair<NodeID, NodeID>> children{std::nullopt};

  std::optional<bool> isset{std::nullopt};

  /**
   * An estimation of the "exclusive size" of a type, trying to
   * attribute every byte once and only once to types in the tree.
   */
  size_t exclusiveSize{};

  MSGPACK_DEFINE_ARRAY(id,
                       name,
                       typeName,
                       typePath,
                       isTypedef,
                       staticSize,
                       dynamicSize,
                       paddingSavingsSize,
                       containerStats,
                       pointer,
                       children,
                       isset,
                       exclusiveSize)
};

TreeBuilder::~TreeBuilder() {
  /* FB: Remove error IDs, Strobelight doesn't handle them yet */
  std::erase(rootIDs, ERROR_NODE_ID);

  /*
   * Now that all the Nodes have been inserted in the DB,
   * we can insert the DBHeader with the proper list of rootIDs.
   */
  const DBHeader header{.version = VERSION, .rootIDs = std::move(rootIDs)};
  auto serializedHeader = serialize(header);

  rocksdb::WriteOptions options{};
  options.disableWAL = true;
  if (auto status =
          db->Put(options, std::to_string(ROOT_NODE_ID), serializedHeader);
      !status.ok()) {
    LOG(ERROR) << "RocksDB error while writing DBHeader: " << status.ToString();
  }

  if (auto status = db->Close(); !status.ok()) {
    LOG(ERROR) << "RocksDB error while closing database: " << status.ToString();
  }

  delete db;
}

bool TreeBuilder::emptyOutput() const {
  return std::ranges::all_of(rootIDs,
                             [](auto& id) { return id == ERROR_NODE_ID; });
}

void TreeBuilder::build(const std::vector<uint64_t>& data,
                        const std::string& argName,
                        struct drgn_type* type,
                        const TypeHierarchy& typeHierarchy) {
  th = &typeHierarchy;
  oidData = &data;

  oidDataIndex = 4;  // HACK: OID's first 4 outputs are dummy 0s

  metrics::Tracing _("build_tree");
  VLOG(1) << "Building tree...";

  {
    auto& rootID = rootIDs.emplace_back(nextNodeID++);

    try {
      process(rootID, {.type = type, .name = argName, .typePath = argName});
    } catch (...) {
      // Mark the failure using the error node ID
      rootID = ERROR_NODE_ID;
      throw;
    }
  }

  VLOG(1) << "Finished building tree";
  rocksdb::CompactRangeOptions opts;
  rocksdb::Status s = db->CompactRange(opts, nullptr, nullptr);
  if (!s.ok()) {
    LOG(FATAL) << "RocksDB error while compacting: " << s.ToString();
  }
  VLOG(1) << "Finished compacting db";

  // Were all object sizes consumed?
  if (oidDataIndex != oidData->size()) {
    if (config.strict) {
      LOG(FATAL) << "some object sizes not consumed and OID is in strict mode!"
                 << "reported: " << oidData->size() << " consumed "
                 << oidDataIndex;
    }
    LOG(WARNING) << "WARNING: some object sizes not consumed;"
                 << "object tree may be inaccurate. "
                 << "reported: " << oidData->size() << " consumed "
                 << oidDataIndex;
  } else {
    VLOG(1) << "Consumed all object sizes: " << oidDataIndex;
  }

  th = nullptr;
  oidData = nullptr;
}

void TreeBuilder::dumpJson() {
  if (!config.jsonPath.has_value()) {
    LOG(ERROR) << "No output path was provided for JSON";
    return;
  }

  std::ofstream output(*config.jsonPath);
  output << '[';
  for (auto rootID : rootIDs) {
    if (rootID == ERROR_NODE_ID) {
      // On error, output an empty object to maintain offsets
      output << "{},";
    } else {
      JSON(rootID, output);
      output << ',';
    }
  }

  /* Remove the trailing comma */
  if (!rootIDs.empty()) {
    output.seekp(-1, std::ios_base::cur);
  }

  output << "]\n";  // Text files should end with a newline per POSIX

  VLOG(1) << "Finished writing JSON to disk";
}

void TreeBuilder::setPaddedStructs(
    std::map<std::string, PaddingInfo>* _paddedStructs) {
  this->paddedStructs = _paddedStructs;
}

static std::string drgnTypeToName(struct drgn_type* type) {
  if (type->_private.program != nullptr) {
    return drgn_utils::typeToName(type);
  }

  return type->_private.oi_name ? type->_private.oi_name : "";
}

static struct drgn_error* drgnTypeSizeof(struct drgn_type* type,
                                         uint64_t* ret) {
  static struct drgn_error incompleteTypeError = {
      .code = DRGN_ERROR_TYPE,
      .needs_destroy = false,
      .errnum = 0,
      .path = NULL,
      .address = 0,
      .message = (char*)"cannot get size of incomplete type",
  };

  if (drgn_type_kind(type) == DRGN_TYPE_FUNCTION) {
    *ret = sizeof(uintptr_t);
    return nullptr;
  }

  if (type->_private.program != nullptr) {
    return drgn_type_sizeof(type, ret);
  }

  // If type has no size, report an error to trigger a sizeMap lookup
  if (type->_private.oi_size ==
      std::numeric_limits<decltype(type->_private.size)>::max()) {
    return &incompleteTypeError;
  }

  *ret = type->_private.oi_size;
  return nullptr;
}

uint64_t TreeBuilder::getDrgnTypeSize(struct drgn_type* type) {
  uint64_t size = 0;
  struct drgn_error* err = drgnTypeSizeof(type, &size);
  BOOST_SCOPE_EXIT(err) {
    drgn_error_destroy(err);
  }
  BOOST_SCOPE_EXIT_END
  if (err == nullptr) {
    return size;
  }

  std::string typeName = drgnTypeToName(type);
  for (auto& [typeName2, size2] : th->sizeMap)
    if (typeName.starts_with(typeName2))
      return size2;

  if (typeName.starts_with("basic_string<char, std::char_traits<char>, "
                           "std::allocator<char> >")) {
    return sizeof(std::string);
  }

  throw std::runtime_error("Failed to get size: " + std::to_string(err->code) +
                           " " + err->message);
}

uint64_t TreeBuilder::next() {
  if (oidDataIndex >= oidData->size()) {
    throw std::runtime_error("Unexpected end of data");
  }
  VLOG(3) << "next = " << (void*)(*oidData)[oidDataIndex];
  return (*oidData)[oidDataIndex++];
}

bool TreeBuilder::isContainer(const Variable& variable) {
  if (th->containerTypeMap.contains(variable.type)) {
    return true;
  }

  if (drgn_type_kind(variable.type) == DRGN_TYPE_ARRAY) {
    /* CodeGen v1 does not consider zero-length array as containers,
     * but CodeGen v2 does. This discrepancy is handled here.
     * TODO: Cleanup this workaround once CodeGen v1 is gone. See #453
     */
    return config.features[Feature::TypeGraph] ||
           drgn_type_length(variable.type) > 0;
  }

  return false;
}

bool TreeBuilder::isPrimitive(struct drgn_type* type) {
  while (drgn_type_kind(type) == DRGN_TYPE_TYPEDEF) {
    auto entry = th->typedefMap.find(type);
    if (entry == th->typedefMap.end())
      return false;
    type = entry->second;
  }
  return drgn_type_primitive(type) != DRGN_NOT_PRIMITIVE_TYPE;
}

static std::string_view drgnKindStr(struct drgn_type* type) {
  auto kind = OICodeGen::drgnKindStr(type);
  // -1 is for the null terminator
  kind.remove_prefix(sizeof("DRGN_TYPE_") - 1);
  return kind;
}

void TreeBuilder::setSize(TreeBuilder::Node& node,
                          uint64_t dynamicSize,
                          uint64_t memberSizes) {
  node.dynamicSize = dynamicSize;
  if (memberSizes > node.staticSize + node.dynamicSize) {
    // TODO handle this edge case in a more elegant way.
    // This can occur when handling bitfields or vector<bool>
    // (as we cannot accurately track sub-byte sizes currently)
    node.exclusiveSize = 0;
  } else {
    node.exclusiveSize = node.staticSize + node.dynamicSize - memberSizes;
  }
}

TreeBuilder::Node TreeBuilder::process(NodeID id, Variable variable) {
  Node node{
      .id = id,
      .name = variable.name,
      .typeName = drgnTypeToName(variable.type),
      .typePath = std::move(variable.typePath),
      .staticSize = getDrgnTypeSize(variable.type),
      .isset = variable.isset,
  };
  VLOG(2) << "Processing node [" << id << "] (name: '" << variable.name
          << "', typeName: '" << node.typeName
          << "', kind: " << drgnKindStr(variable.type) << ")"
          << (variable.isStubbed ? " STUBBED" : "")
          << (th->knownDummyTypeList.contains(variable.type) ? " DUMMY" : "");
  // Default dynamic size to 0 and calculate fallback exclusive size
  setSize(node, 0, 0);
  if (!variable.isStubbed) {
    switch (drgn_type_kind(variable.type)) {
      case DRGN_TYPE_POINTER:
        if (config.features[Feature::ChaseRawPointers]) {
          // Pointers to incomplete types are stubbed out
          // See OICodeGen::enumeratePointerType
          if (th->knownDummyTypeList.contains(variable.type)) {
            break;
          }

          auto entry = th->pointerToTypeMap.find(variable.type);
          if (entry != th->pointerToTypeMap.end()) {
            auto innerTypeKind = drgn_type_kind(entry->second);
            if (innerTypeKind != DRGN_TYPE_FUNCTION) {
              node.pointer = next();
              if (innerTypeKind == DRGN_TYPE_VOID) {
                break;
              }
              if (next() == (uint64_t)TrackPointerTag::skipped) {
                break;
              }
            }
            auto childID = nextNodeID++;
            auto child = process(childID, Variable{entry->second, "", ""});
            node.children = {childID, childID + 1};
            setSize(node,
                    child.staticSize + child.dynamicSize,
                    child.staticSize + child.dynamicSize);
          }
        }
        break;
      case DRGN_TYPE_TYPEDEF: {
        const static boost::regex standardIntegerRegex{
            "((s?size)|(u?int(_fast|_least)?(8|16|32|64|128|ptr))|(ptrdiff))_"
            "t"};
        // We don't expand typedefs for well-known integer types from `stdint.h`
        // to prevent our output from being extremely verbose. We treat them as
        // if they are primitives directly (hence this check coming *before* we
        // set `node.isTypedef`).
        if (boost::regex_match(node.typeName, standardIntegerRegex)) {
          break;
        }
        node.isTypedef = true;
        auto entry = th->typedefMap.find(variable.type);
        if (entry != th->typedefMap.end()) {
          auto childID = nextNodeID++;
          auto child = process(childID, Variable{entry->second, "", ""});
          node.children = {childID, childID + 1};
          setSize(
              node, child.dynamicSize, child.dynamicSize + child.staticSize);
        }
      } break;
      case DRGN_TYPE_CLASS:
      case DRGN_TYPE_STRUCT:
      case DRGN_TYPE_ARRAY:
        if (th->knownDummyTypeList.contains(variable.type)) {
          break;
        } else if (isContainer(variable)) {
          processContainer(variable, node);
        } else {
          drgn_type* objectType = variable.type;
          if (auto it = th->descendantClasses.find(objectType);
              it != th->descendantClasses.end()) {
            // The first item of data in dynamic classes identifies which
            // concrete type we should process it as, represented as an index
            // into the vector of child classes, or -1 to processes this type
            // as itself.
            const auto& descendants = it->second;
            auto val = next();
            if (val != (uint64_t)-1) {
              objectType = descendants[val];
              node.typeName = drgnTypeToName(objectType);
              node.staticSize = getDrgnTypeSize(objectType);
            }
          }

          auto entry = th->classMembersMap.find(objectType);
          if (entry == th->classMembersMap.end() || entry->second.empty()) {
            break;
          }

          const auto& members = entry->second;
          node.children = {nextNodeID, nextNodeID + members.size()};
          nextNodeID += members.size();
          auto childID = node.children->first;

          bool captureThriftIsset =
              th->thriftIssetStructTypes.contains(objectType);

          uint64_t memberSizes = 0;
          for (std::size_t i = 0; i < members.size(); i++) {
            std::optional<bool> isset;
            if (captureThriftIsset && i < members.size() - 1) {
              // Retrieve isset value for each member variable, except Thrift's
              // __isset field, which we assume comes last.
              // A value of -1 indicates a non-optional field for which we
              // don't record an isset value.
              auto val = next();
              if (val != (uint64_t)-1) {
                isset = val;
              }
            }
            const auto& member = members[i];
            auto child = process(childID++,
                                 Variable{member.type,
                                          member.member_name,
                                          member.member_name,
                                          isset,
                                          member.isStubbed});
            node.dynamicSize += child.dynamicSize;
            memberSizes += child.dynamicSize + child.staticSize;
          }
          setSize(node, node.dynamicSize, memberSizes);
        }
        break;
      default:
        // The remaining types are all described entirely by their static size,
        // and hence need no special handling.
        break;
    }

    if (config.features[Feature::GenPaddingStats]) {
      auto entry = paddedStructs->find(node.typeName);
      if (entry != paddedStructs->end()) {
        entry->second.instancesCnt++;
        node.paddingSavingsSize = entry->second.savingSize;
      }
    }
  }

  rocksdb::WriteOptions options{};
  options.disableWAL = true;
  auto status = db->Put(options, std::to_string(node.id), serialize(node));
  if (!status.ok()) {
    throw std::runtime_error("RocksDB error while inserting node [" +
                             std::to_string(node.id) +
                             "]: " + status.ToString());
  }
  return node;
}

void TreeBuilder::processContainer(const Variable& variable, Node& node) {
  VLOG(1) << "Processing container [" << node.id << "] of type '"
          << node.typeName << "'";
  ContainerTypeEnum kind = UNKNOWN_TYPE;
  std::vector<struct drgn_qualified_type> elementTypes;

  if (drgn_type_kind(variable.type) == DRGN_TYPE_ARRAY) {
    kind = ARRAY_TYPE;
    struct drgn_type* arrayElementType = nullptr;
    size_t numElems = 0;
    if (config.features[Feature::TypeGraph]) {
      arrayElementType = drgn_type_type(variable.type).type;
      numElems = drgn_type_length(variable.type);
    } else {
      drgn_utils::getDrgnArrayElementType(
          variable.type, &arrayElementType, numElems);
    }
    assert(numElems > 0);
    elementTypes.push_back(
        drgn_qualified_type{arrayElementType, (enum drgn_qualifiers)(0)});
  } else {
    auto entry = th->containerTypeMap.find(variable.type);
    if (entry == th->containerTypeMap.end()) {
      throw std::runtime_error(
          "Could not find container information for type with name '" +
          node.typeName + "'");
    }

    auto& [containerKind, templateTypes] = entry->second;
    kind = containerKind;
    for (const auto& tt : templateTypes) {
      elementTypes.push_back(tt);
    }
  }

  /**
   * Some containers (conditionally) store their contents *directly* inside
   * themselves (as opposed to having a pointer to heap-allocated memory).
   * `std::pair` and `std::array` are two trivial examples, but some types vary
   * whether their contents are stored inline or externally depending on
   * runtime conditions (usually the number of elements currently present in
   * the container).
   */
  bool contentsStoredInline = false;

  // Initialize, then take a reference to the underlying value for convenience
  // so that we don't have to dereference the optional every time we want to use
  // it.
  auto& containerStats =
      node.containerStats.emplace(Node::ContainerStats{0, 0, 0});

  for (auto& type : elementTypes) {
    containerStats.elementStaticSize += getDrgnTypeSize(type.type);
  }

  switch (kind) {
    case OPTIONAL_TYPE:
      contentsStoredInline = true;
      containerStats.length = containerStats.capacity = 1;
      if (next() == 0U) {
        containerStats.length = 0;
        return;
      }
      break;
    case FOLLY_OPTIONAL_TYPE:
      // TODO: Not sure why we are capturing pointer for folly::Optional but
      // not std::optional. Both are supposed to store data inline.
      contentsStoredInline = true;
      node.pointer = next();
      containerStats.length = containerStats.capacity = 1;
      if (*node.pointer == 0) {
        containerStats.length = 0;
        return;
      }
      break;
    case WEAK_PTR_TYPE:
      // Do not handle weak pointers beyond their static size for now.
      break;
    case SHRD_PTR_TYPE:
    case UNIQ_PTR_TYPE:
      node.pointer = next();
      containerStats.length = *node.pointer ? 1 : 0;
      containerStats.capacity = 1;
      if (next() == (uint64_t)TrackPointerTag::skipped) {
        return;
      }
      break;
    case TRY_TYPE:
    case REF_WRAPPER_TYPE:
      node.pointer = next();
      containerStats.length = containerStats.capacity = 1;
      if (next() == (uint64_t)TrackPointerTag::skipped) {
        return;
      }
      break;
    case SORTED_VEC_SET_TYPE:
    case CONTAINER_ADAPTER_TYPE: {
      node.pointer = next();

      // Copy the underlying container's sizes and stats directly into this
      // container adapter
      node.children = {nextNodeID, nextNodeID + 1};
      nextNodeID += 1;
      auto childID = node.children->first;
      // elementTypes is only populated with the underlying container type for
      // container adapters
      auto containerType = elementTypes[0];
      auto child =
          process(childID++,
                  {.type = containerType.type,
                   .name = "",
                   .typePath = drgnTypeToName(containerType.type) + "[]"});

      setSize(node, child.dynamicSize, child.dynamicSize + child.staticSize);
      node.containerStats = child.containerStats;
      return;
    }
    case STD_VARIANT_TYPE: {
      containerStats.length = containerStats.capacity = 1;
      containerStats.elementStaticSize = 0;
      for (auto& type : elementTypes) {
        auto paramSize = getDrgnTypeSize(type.type);
        containerStats.elementStaticSize =
            std::max(containerStats.elementStaticSize, paramSize);
      }

      node.dynamicSize = 0;

      // When a std::variant is valueless_by_exception, its index will be
      // std::variant_npos (i.e. 0xffffffffffffffff).
      //
      // libstdc++ and libc++ both optimise the storage required for#
      // std::variant's index value by using fewer than 8-bytes when possible.
      // e.g. for a std::variant<A, B>, only three index values are required:
      // one each for A and B and one for variant_npos. variant_npos may be
      // represented internally by 0xff and only converted back to
      // 0xffffffffffffffff when index() is called.
      //
      // However, this conversion may be optimised away in the target process,
      // so we need to treat any invalid index as variant_npos.
      if (auto index = next(); index < elementTypes.size()) {
        // Recurse only into the type of the template parameter which
        // is currently stored in this variant
        node.children = {nextNodeID, nextNodeID + 1};
        nextNodeID += 1;
        auto childID = node.children->first;

        auto elementType = elementTypes[index];
        auto child =
            process(childID++,
                    {.type = elementType.type,
                     .name = "",
                     .typePath = drgnTypeToName(elementType.type) + "[]"});

        setSize(node, child.dynamicSize, child.dynamicSize + child.staticSize);
      }
      return;
    }
    case PAIR_TYPE:
      contentsStoredInline = true;
      containerStats.length = containerStats.capacity = 1;
      break;
    case SEQ_TYPE:
    case MICROLIST_TYPE:
    case FEED_QUICK_HASH_SET:
    case FEED_QUICK_HASH_MAP:
    case FB_HASH_MAP_TYPE:
    case FB_HASH_SET_TYPE:
    case MAP_SEQ_TYPE:
    case FOLLY_SMALL_HEAP_VECTOR_MAP:
    case REPEATED_FIELD_TYPE:
      node.pointer = next();
      containerStats.capacity = next();
      containerStats.length = next();
      break;
    case LIST_TYPE:
      node.pointer = next();
      containerStats.length = containerStats.capacity = next();
      break;
    case FOLLY_IOBUFQUEUE_TYPE:
      node.pointer = next();
      containerStats.length = containerStats.capacity = 0;
      if (next() == (uint64_t)TrackPointerTag::skipped) {
        return;
      }
      // Fallthrough to the IOBuf data if we have a valid pointer
      [[fallthrough]];
    case FOLLY_IOBUF_TYPE:
      containerStats.capacity = next();
      containerStats.length = next();
      break;
    case FB_STRING_TYPE:
      node.pointer = next();
      containerStats.capacity = next();
      containerStats.length = next();
      // Contents are either stored inline or have been seen before in the JIT
      // code. Set to true either way so as not to double count.
      contentsStoredInline = next() == 0;

      {
        constexpr int sharedCutOff = 255;
        if (containerStats.capacity < sharedCutOff) {
          // No sense in recording the pointer value if the string isn't
          // potentially shared.
          node.pointer.reset();
        }
      }
      break;
    case STRING_TYPE:
      containerStats.capacity = next();
      containerStats.length = next();
      // Account for Small String Optimization (SSO)
      // LLVM libc++:   sizeof(string) = 24, SSO cutoff = 22
      // GNU libstdc++: sizeof(string) = 32, SSO cutoff = 15
      {
        const int llvmSizeOf = 24;
        const int llvmSsoCutOff = 22;
        [[maybe_unused]] const int gnuSizeOf = 32;
        const int gnuSsoCutOff = 15;
        assert(node.staticSize == llvmSizeOf || node.staticSize == gnuSizeOf);
        size_t ssoCutoff =
            node.staticSize == llvmSizeOf ? llvmSsoCutOff : gnuSsoCutOff;
        contentsStoredInline = containerStats.capacity <= ssoCutoff;
      }
      break;
    case CAFFE2_BLOB_TYPE:
      // This is a weird one, need to ask why we just overwite size like this
      setSize(node, next(), 0);
      return;
    case ARRAY_TYPE:
      contentsStoredInline = true;
      containerStats.length = containerStats.capacity = next();
      break;
    case SMALL_VEC_TYPE: {
      size_t requestedMaxInline = next();
      size_t maxInline =
          requestedMaxInline == 0
              ? 0
              : std::max(sizeof(uintptr_t) / containerStats.elementStaticSize,
                         requestedMaxInline);
      containerStats.capacity = next();
      containerStats.length = next();
      contentsStoredInline = containerStats.capacity <= maxInline;
    } break;
    case BOOST_BIMAP_TYPE:
      // TODO: Hard to know the overhead of boost bimap. It isn't documented in
      // the boost docs. Need to look closer at the implementation.
      containerStats.length = containerStats.capacity = next();
      break;
    case SET_TYPE:
    case STD_MAP_TYPE:
      // Account for node overhead
      containerStats.elementStaticSize += next();
      containerStats.length = containerStats.capacity = next();
      break;
    case UNORDERED_SET_TYPE:
    case UNORDERED_MULTISET_TYPE:
    case STD_UNORDERED_MULTIMAP_TYPE:
    case STD_UNORDERED_MAP_TYPE: {
      // Account for node overhead
      containerStats.elementStaticSize += next();
      size_t bucketCount = next();
      // Both libc++ and libstdc++ define buckets as an array of raw pointers
      setSize(node, node.dynamicSize + bucketCount * sizeof(void*), 0);
      containerStats.length = containerStats.capacity = next();
    } break;
    case F14_MAP:
    case F14_SET:
      // F14 maps/sets don't actually store their contents inline, but the
      // intention of setting this to `true` is to skip the usual calculation
      // performed to determine `node.dynamicSize`, since F14 maps very
      // conveniently provide a `getAllocatedMemorySize()` method which we can
      // use instead.
      contentsStoredInline = true;
      setSize(node, node.dynamicSize + next(), 0);
      containerStats.capacity = next();
      containerStats.length = next();
      break;
    case RADIX_TREE_TYPE:
    case MULTI_SET_TYPE:
    case MULTI_MAP_TYPE:
    case BY_MULTI_QRT_TYPE:
      containerStats.length = containerStats.capacity = next();
      break;
    case THRIFT_ISSET_TYPE:
    case DUMMY_TYPE:
      // Dummy container
      containerStats.elementStaticSize = 0;
      break;
    default:
      throw std::runtime_error("Unknown container (type was 0x" +
                               std::to_string(kind) + ")");
      break;
  }

  if (!contentsStoredInline) {
    setSize(node,
            node.dynamicSize +
                containerStats.elementStaticSize * containerStats.capacity,
            0);
  }

  // A cutoff value used to sanity-check our results. If a container
  // is larger than this, chances are that we've read uninitialized data,
  // or there's a bug in Codegen.
  constexpr size_t CONTAINER_SIZE_THRESHOLD = 1ULL << 38;
  if (containerStats.elementStaticSize * containerStats.capacity >=
      CONTAINER_SIZE_THRESHOLD) {
    throw std::runtime_error(
        "Container size exceeds threshold, this is likely due to reading "
        "uninitialized data in the target process");
  }
  if (std::ranges::all_of(
          elementTypes.cbegin(), elementTypes.cend(), [this](auto& type) {
            return isPrimitive(type.type);
          })) {
    VLOG(1)
        << "Container [" << node.id
        << "] contains only primitive types, skipping processing its members";
    return;
  }

  auto numChildren = containerStats.length * elementTypes.size();
  if (numChildren == 0) {
    VLOG(1) << "Container [" << node.id << "] has no children";
    return;
  }
  node.children = {nextNodeID, nextNodeID + numChildren};
  VLOG(1) << "Container [" << node.id << "]'s children cover range ["
          << node.children->first << ", " << node.children->second << ")";
  nextNodeID += numChildren;
  auto childID = node.children->first;
  uint64_t memberSizes = 0;
  for (size_t i = 0; i < containerStats.length; i++) {
    for (auto& type : elementTypes) {
      auto child = process(childID++,
                           {.type = type.type,
                            .name = "",
                            .typePath = drgnTypeToName(type.type) + "[]"});
      node.dynamicSize += child.dynamicSize;
      memberSizes += child.dynamicSize + child.staticSize;
    }
  }
  setSize(node, node.dynamicSize, memberSizes);
}

template <class T>
std::string_view TreeBuilder::serialize(const T& data) {
  buffer->clear();
  msgpack::pack(*buffer, data);
  // It is *very* important that we construct the `std::string_view` with an
  // explicit length, since `buffer->data()` may contain null bytes.
  return std::string_view(buffer->data(), buffer->size());
}

void TreeBuilder::JSON(NodeID id, std::ofstream& output) {
  std::string data;
  auto status = db->Get(rocksdb::ReadOptions(), std::to_string(id), &data);
  if (!status.ok()) {
    throw std::runtime_error("RocksDB error while reading node [" +
                             std::to_string(id) + "]: " + status.ToString());
  }

  Node node;
  msgpack::unpack(data.data(), data.size()).get().convert(node);
  // Remove all backslashes to ensure the output is valid JSON
  std::replace(node.typePath.begin(), node.typePath.end(), '\\', ' ');
  std::replace(node.typeName.begin(), node.typeName.end(), '\\', ' ');
  output << "{";
  output << "\"name\":\"" << node.name << "\",";
  output << "\"typePath\":\"" << node.typePath << "\",";
  output << "\"typeName\":\"" << node.typeName << "\",";
  output << "\"isTypedef\":" << (node.isTypedef ? "true" : "false") << ",";
  output << "\"staticSize\":" << node.staticSize << ",";
  output << "\"dynamicSize\":" << node.dynamicSize << ",";
  output << "\"exclusiveSize\":" << node.exclusiveSize;
  if (node.paddingSavingsSize.has_value()) {
    output << ",";
    output << "\"paddingSavingsSize\":" << *node.paddingSavingsSize;
  }
  if (node.pointer.has_value()) {
    output << ",";
    output << "\"pointer\":" << *node.pointer;
  }
  if (node.containerStats.has_value()) {
    output << ",";
    output << "\"length\":" << node.containerStats->length << ",";
    output << "\"capacity\":" << node.containerStats->capacity << ",";
    output << "\"elementStaticSize\":"
           << node.containerStats->elementStaticSize;
  }
  if (node.isset.has_value()) {
    output << ",";
    output << "\"isset\":" << (*node.isset ? "true" : "false");
  }
  if (node.children.has_value()) {
    output << ",";
    output << "\"members\":[";
    auto [childIDStart, childIDEnd] = *node.children;
    assert(childIDStart < childIDEnd);
    // Trailing commas are disallowed in JSON, so we pull
    // out the first iteration of the loop.
    JSON(childIDStart, output);
    for (auto childID = childIDStart + 1; childID < childIDEnd; childID++) {
      output << ",";
      JSON(childID, output);
    }
    output << "]";
  }
  output << "}";
}

}  // namespace oi::detail
