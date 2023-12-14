#include <gtest/gtest.h>

#include "oi/type_graph/Prune.h"
#include "test/type_graph_utils.h"

using type_graph::Prune;

TEST(PruneTest, PruneClass) {
  test(Prune::createPass(),
       R"(
[0] Class: MyClass (size: 8)
      Param
        Primitive: int32_t
      Param
        Value: "123"
        Primitive: int32_t
      Parent (offset: 0)
[1]     Class: MyParent (size: 4)
          Member: a (offset: 0)
            Primitive: int32_t
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
        Primitive: int32_t
      Function: foo
      Function: bar
)",
       R"(
[0] Class: MyClass (size: 8)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
        Primitive: int32_t
)");
}

TEST(PruneTest, RecurseClassMember) {
  test(Prune::createPass(),
       R"(
[0] Class: MyClass (size: 0)
      Member: xxx (offset: 0)
[1]     Class: ClassA (size: 12)
          Function: foo
)",
       R"(
[0] Class: MyClass (size: 0)
      Member: xxx (offset: 0)
[1]     Class: ClassA (size: 12)
)");
}

TEST(PruneTest, RecurseClassChild) {
  test(Prune::createPass(),
       R"(
[0] Class: MyClass (size: 0)
      Child
[1]     Class: ClassA (size: 12)
          Function: foo
)",
       R"(
[0] Class: MyClass (size: 0)
      Child
[1]     Class: ClassA (size: 12)
)");
}

TEST(PruneTest, PruneContainer) {
  test(Prune::createPass(),
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        Value: "123"
        Primitive: int32_t
      Underlying
[1]     Class: vector<int32_t> (size: 24)
          Parent (offset: 0)
[2]         Class: MyParent (size: 4)
              Member: a (offset: 0)
                Primitive: int32_t
          Member: a (offset: 0)
            Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        Value: "123"
        Primitive: int32_t
)");
}
