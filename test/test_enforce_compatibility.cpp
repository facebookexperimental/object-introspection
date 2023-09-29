#include <gtest/gtest.h>

#include "oi/type_graph/EnforceCompatibility.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(EnforceCompatibilityTest, ParentContainers) {
  test(EnforceCompatibility::createPass(), R"(
[0] Class: MyClass (size: 24)
      Member: __oi_parent (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 24)
)");
}

TEST(EnforceCompatibilityTest, TypesToStub) {
  test(EnforceCompatibility::createPass(), R"(
[0] Class: EnumMap (size: 8)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
        Primitive: int32_t
)",
       R"(
[0] Class: EnumMap (size: 8)
)");
}

TEST(EnforceCompatibilityTest, VoidPointer) {
  test(EnforceCompatibility::createPass(), R"(
[0] Class: MyClass (size: 8)
      Member: p (offset: 0)
[1]     Pointer
          Incomplete
            Primitive: void
)",
       R"(
[0] Class: MyClass (size: 8)
)");
}
