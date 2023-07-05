#include <gtest/gtest.h>

#include "oi/type_graph/RemoveTopLevelPointer.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(RemoveTopLevelPointerTest, TopLevelPointerRemoved) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myclass = Class{1, Class::Kind::Class, "MyClass", 4};
  myclass.members.push_back(Member{myint, "n", 0});

  auto ptrA = Pointer{0, myclass};

  test(RemoveTopLevelPointer::createPass(), {ptrA}, R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
        Primitive: int32_t
)");
}

TEST(RemoveTopLevelPointerTest, TopLevelClassUntouched) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myclass = Class{0, Class::Kind::Class, "MyClass", 4};
  myclass.members.push_back(Member{myint, "n", 0});

  test(RemoveTopLevelPointer::createPass(), {myclass}, R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
        Primitive: int32_t
)");
}

TEST(RemoveTopLevelPointerTest, IntermediatePointerUntouched) {
  auto myint = Primitive{Primitive::Kind::Int32};
  auto ptrInt = Pointer{1, myint};

  auto myclass = Class{0, Class::Kind::Class, "MyClass", 4};
  myclass.members.push_back(Member{ptrInt, "n", 0});

  test(RemoveTopLevelPointer::createPass(), {myclass}, R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
[1]     Pointer
          Primitive: int32_t
)");
}
