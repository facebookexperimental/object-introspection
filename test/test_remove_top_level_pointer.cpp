#include <gtest/gtest.h>

#include "oi/type_graph/RemoveTopLevelPointer.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(RemoveTopLevelPointerTest, TopLevelPointerRemoved) {
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);

  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 4);
  myclass->members.push_back(Member(myint.get(), "n", 0));

  auto ptrA = std::make_unique<Pointer>(myclass.get());

  test(RemoveTopLevelPointer::createPass(), {*ptrA}, R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
        Primitive: int32_t
)");
}

TEST(RemoveTopLevelPointerTest, TopLevelClassUntouched) {
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);

  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 4);
  myclass->members.push_back(Member(myint.get(), "n", 0));

  test(RemoveTopLevelPointer::createPass(), {*myclass}, R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
        Primitive: int32_t
)");
}

TEST(RemoveTopLevelPointerTest, IntermediatePointerUntouched) {
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);
  auto ptrInt = std::make_unique<Pointer>(myint.get());

  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 4);
  myclass->members.push_back(Member(ptrInt.get(), "n", 0));

  test(RemoveTopLevelPointer::createPass(), {*myclass}, R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
[1]     Pointer
          Primitive: int32_t
)");
}
