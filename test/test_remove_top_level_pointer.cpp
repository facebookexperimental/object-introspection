#include <gtest/gtest.h>

#include "oi/type_graph/RemoveTopLevelPointer.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(RemoveTopLevelPointerTest, TopLevelPointerRemoved) {
  test(RemoveTopLevelPointer::createPass(), R"(
[0] Pointer
[1]   Class: MyClass (size: 4)
        Member: n (offset: 0)
          Primitive: int32_t
)",
       R"(
[1] Class: MyClass (size: 4)
      Member: n (offset: 0)
        Primitive: int32_t
)");
}

TEST(RemoveTopLevelPointerTest, TopLevelClassUntouched) {
  testNoChange(RemoveTopLevelPointer::createPass(), R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
        Primitive: int32_t
)");
}

TEST(RemoveTopLevelPointerTest, IntermediatePointerUntouched) {
  testNoChange(RemoveTopLevelPointer::createPass(), R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
[1]     Pointer
          Primitive: int32_t
)");
}
