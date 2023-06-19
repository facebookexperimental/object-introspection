#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <memory>
#include <msgpack.hpp>
#include <optional>
#include <span>
#include <vector>

#include "rocksdb/db.h"
#include "rocksdb/options.h"
#include "rocksdb/statistics.h"

using Version = uint64_t;
using NodeID = uint64_t;

static constexpr NodeID ROOT_NODE_ID = 0;
static constexpr NodeID ERROR_NODE_ID = 1023;
static constexpr NodeID FIRST_NODE_ID = 1024;

struct DBHeader {
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

std::ostream& operator<<(std::ostream& os, const DBHeader& header) {
  os << "Database header:\n";
  os << "  Version: " << header.version << "\n";
  os << "  Root IDs: ";
  for (const auto& id : header.rootIDs)
    os << id << " ";
  return os;
}

struct Node {
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

std::ostream& operator<<(std::ostream& os, const Node& node) {
  os << "Node #" << node.id << ":\n";
  os << "  Name: " << node.name << "\n";
  os << "  Type name: " << node.typeName << "\n";
  os << "  Type path: " << node.typePath << "\n";
  os << "  Is typedef node: " << node.isTypedef << "\n";
  os << "  Static size: " << node.staticSize << "\n";
  os << "  Dynamic size: " << node.dynamicSize << "\n";
  os << "  Padding savings: "
     << (node.paddingSavingsSize ? *node.paddingSavingsSize : -1) << "\n";
  if (node.containerStats) {
    const auto& stats = *node.containerStats;
    os << "  Container stats:\n";
    os << "    Length: " << stats.length << "\n";
    os << "    Capacity: " << stats.capacity << "\n";
    os << "    Element static size: " << stats.elementStaticSize << "\n";
  } else {
    os << "  Container stats not available\n";
  }
  if (node.pointer.has_value()) {
    os << "  Pointer: " << reinterpret_cast<void*>(node.pointer.value())
       << "\n";
  } else {
    os << "  Pointer not available\n";
  }

  if (node.children.has_value()) {
    auto children = node.children.value();
    os << "  Children: " << children.first << " -> " << children.second << "\n";
  } else {
    os << "  Children not available\n";
  }

  if (node.isset.has_value()) {
    os << "  Is set: " << *node.isset << "\n";
  } else {
    os << "  Is set not available\n";
  }
  os << "  Exclusive size: " << node.exclusiveSize << "\n";
  return os;
}

int main(int argc, const char** argv) {
  if (argc < 3) {
    fprintf(stderr, "usage: %s <db_dir> <ranges>...\n", argv[0]);
    fprintf(stderr, "  where <ranges> are a single number\n");
    fprintf(stderr, "  or a '-' separated span of indexes\n");
    return 1;
  }

  std::filesystem::path dbpath{argv[1]};
  std::span<const char*> ranges{&argv[2], &argv[argc]};

  assert(std::filesystem::exists(dbpath));
  assert(std::filesystem::is_directory(dbpath));
  assert(ranges.size() > 0);

  // Open the database...
  rocksdb::Options options{};
  options.compression = rocksdb::kZSTD;
  options.create_if_missing = false;
  options.statistics = rocksdb::CreateDBStatistics();
  options.OptimizeForSmallDb();

  auto close_db = [](rocksdb::DB* db) {
    if (db)
      db->Close();
  };
  auto db = std::unique_ptr<rocksdb::DB, decltype(close_db)>{nullptr};

  {  // Open the database, then safely store its pointer in a unique_ptr for
     // lifetime management.
    rocksdb::DB* _db = nullptr;
    if (auto status = rocksdb::DB::Open(options, dbpath.string(), &_db);
        !status.ok()) {
      fprintf(stderr, "Failed to open DB '%s' with error %s\n",
              dbpath.string().c_str(), status.ToString().c_str());
      return 1;
    }
    db.reset(_db);
  }

  // Iterate over the given ranges...
  for (const auto& range : ranges) {
    NodeID start = 0, end = 0;

    // Parse the range into two integers; start and end.
    // If the range contains a single integer, that integer becomes the whole
    // range.
    if (const char* dash = std::strchr(range, '-')) {
      start = std::strtoul(range, nullptr, 10);
      end = std::strtoul(dash + 1, nullptr, 10);
    } else {
      start = std::strtoul(range, nullptr, 10);
      end = start;
    }

    // Print the contents of the nodes...
    for (NodeID id = start; id <= end; id++) {
      std::string data;
      if (auto status =
              db->Get(rocksdb::ReadOptions(), std::to_string(id), &data);
          !status.ok()) {
        continue;
      }

      // Nodes with ID < FIRST_NODE_ID are reserved for internal use.
      // They have their own type, so we need to unpack and print them
      // appropriately.
      if (id == ROOT_NODE_ID) {
        DBHeader header;
        msgpack::unpack(data.data(), data.size()).get().convert(header);
        std::cout << header << "\n";
      } else if (id == ERROR_NODE_ID) {
        continue;
      } else if (id >= FIRST_NODE_ID) {
        Node node;
        msgpack::unpack(data.data(), data.size()).get().convert(node);
        std::cout << node << "\n";
      } else {
        continue;
      }

      std::cout << std::endl;
    }
  }

  return 0;
}
