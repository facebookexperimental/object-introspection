#include <gtest/gtest.h>

#include "oi/type_graph/TypeIdentifier.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(TypeIdentifierTest, StubbedParam) {
  test(TypeIdentifier::createPass({}), R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyParam (size: 4)
          Member: a (offset: 0)
            Primitive: int32_t
      Param
        Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        Dummy (size: 4)
      Param
        Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, Allocator) {
  test(TypeIdentifier::createPass({}), R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 8)
          Param
            Primitive: int32_t
          Function: allocate
          Function: deallocate
      Param
        Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 8)
          Primitive: int32_t
      Param
        Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, AllocatorSize1) {
  test(TypeIdentifier::createPass({}), R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Param
            Primitive: int32_t
          Function: allocate
          Function: deallocate
      Param
        Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 0)
          Primitive: int32_t
      Param
        Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, PassThroughTypes) {
  std::vector<ContainerInfo> passThroughTypes;
  passThroughTypes.emplace_back("std::allocator", DUMMY_TYPE, "memory");

  test(TypeIdentifier::createPass(passThroughTypes), R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Class: std::allocator (size: 1)
          Param
            Primitive: int32_t
          Function: allocate
          Function: deallocate
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[2]     Container: std::allocator (size: 1)
          Param
            Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, ContainerNotReplaced) {
  test(TypeIdentifier::createPass({}), R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Container: std::allocator (size: 1)
          Param
            Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Container: std::allocator (size: 1)
          Param
            Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, DummyNotReplaced) {
  testNoChange(TypeIdentifier::createPass({}), R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        Dummy (size: 22)
)");
}

TEST(TypeIdentifierTest, DummyAllocatorNotReplaced) {
  testNoChange(TypeIdentifier::createPass({}), R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 22)
          Primitive: int32_t
)");
}
