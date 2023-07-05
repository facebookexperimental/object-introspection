#include <gtest/gtest.h>

#include "oi/type_graph/CycleFinder.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(CycleFinderTest, NoCycles) {
  // No change
  // Original and After:
  //   struct MyStruct { int n0; };
  //   class MyClass {
  //     int n;
  //     MyEnum e;
  //     MyStruct mystruct;
  //   };
  testNoChange(CycleFinder::createPass(), R"(
[0] Class: MyClass (size: 12)
      Member: n (offset: 0)
        Primitive: int32_t
      Member: e (offset: 4)
        Enum: MyEnum (size: 4)
      Member: mystruct (offset: 8)
[1]     Struct: MyStruct (size: 4)
          Member: n0 (offset: 0)
            Primitive: int32_t
)");
}

TEST(CycleFinderTest, RawPointer) {
  // Original:
  //   class RawNode {
  //     int value;
  //     class RawNode* next;
  //   };
  //
  // After:
  //   class RawNodeP_RawNode;
  //   class RawNode {
  //     int value;
  //     RawNodeP_RawNode* next;
  //   };
  test(CycleFinder::createPass(), R"(
[0] Struct: RawNode (size: 16)
      Member: value (offset: 0)
        Primitive: int32_t
      Member: next (offset: 8)
[1]     Pointer
          [0]
)",
       R"(
[0] Struct: RawNode (size: 16)
      Member: value (offset: 0)
        Primitive: int32_t
      Member: next (offset: 8)
[1]     Pointer
[2]       CycleBreaker
            [0]
)");
}
