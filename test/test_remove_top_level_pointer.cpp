#include <gtest/gtest.h>

#include "oi/type_graph/Printer.h"
#include "oi/type_graph/RemoveTopLevelPointer.h"
#include "oi/type_graph/Types.h"

using namespace type_graph;

namespace {
void test(std::vector<std::reference_wrapper<Type>> types,
          std::string_view expected) {
  RemoveTopLevelPointer pass;
  pass.removeTopLevelPointers(types);

  std::stringstream out;
  Printer printer(out);

  for (const auto& type : types) {
    printer.print(type);
  }

  expected.remove_prefix(1);  // Remove initial '\n'
  EXPECT_EQ(expected, out.str());
}
}  // namespace

TEST(RemoveTopLevelPointerTest, TopLevelPointerRemoved) {
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);

  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 4);
  myclass->members.push_back(Member(myint.get(), "n", 0));

  auto ptrA = std::make_unique<Pointer>(myclass.get());

  test({*ptrA}, R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
        Primitive: int32_t
)");
}

TEST(RemoveTopLevelPointerTest, TopLevelClassUntouched) {
  auto myint = std::make_unique<Primitive>(Primitive::Kind::Int32);

  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 4);
  myclass->members.push_back(Member(myint.get(), "n", 0));

  test({*myclass}, R"(
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

  test({*myclass}, R"(
[0] Class: MyClass (size: 4)
      Member: n (offset: 0)
[1]     Pointer
          Primitive: int32_t
)");
}
