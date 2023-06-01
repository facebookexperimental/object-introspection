#include <gtest/gtest.h>

#include "oi/type_graph/AddPadding.h"
#include "oi/type_graph/Printer.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(AddPaddingTest, BetweenMembers) {
  auto myclass = std::make_unique<Class>(Class::Kind::Class, "MyClass", 16);
  auto myint8 = std::make_unique<Primitive>(Primitive::Kind::Int8);
  auto myint64 = std::make_unique<Primitive>(Primitive::Kind::Int64);
  myclass->members.push_back(Member(myint8.get(), "n1", 0));
  myclass->members.push_back(Member(myint64.get(), "n2", 8));

  test(AddPadding::createPass(), {*myclass}, R"(
[0] Class: MyClass (size: 16)
      Member: n1 (offset: 0)
        Primitive: int8_t
      Member: __oid_padding (offset: 1)
[1]     Array: (length: 7)
          Primitive: int8_t
      Member: n2 (offset: 8)
        Primitive: int64_t
)");
}

TEST(AddPaddingTest, AtEnd) {
  auto myclass = std::make_unique<Class>(Class::Kind::Struct, "MyStruct", 16);
  auto myint8 = std::make_unique<Primitive>(Primitive::Kind::Int8);
  auto myint64 = std::make_unique<Primitive>(Primitive::Kind::Int64);
  myclass->members.push_back(Member(myint64.get(), "n1", 0));
  myclass->members.push_back(Member(myint8.get(), "n2", 8));

  test(AddPadding::createPass(), {*myclass}, R"(
[0] Struct: MyStruct (size: 16)
      Member: n1 (offset: 0)
        Primitive: int64_t
      Member: n2 (offset: 8)
        Primitive: int8_t
      Member: __oid_padding (offset: 9)
[1]     Array: (length: 7)
          Primitive: int8_t
)");
}

TEST(AddPaddingTest, UnionNotPadded) {
  auto myclass = std::make_unique<Class>(Class::Kind::Union, "MyUnion", 8);
  auto myint8 = std::make_unique<Primitive>(Primitive::Kind::Int8);
  auto myint64 = std::make_unique<Primitive>(Primitive::Kind::Int64);
  myclass->members.push_back(Member(myint64.get(), "n1", 0));
  myclass->members.push_back(Member(myint8.get(), "n2", 0));

  test(AddPadding::createPass(), {*myclass}, R"(
[0] Union: MyUnion (size: 8)
      Member: n1 (offset: 0)
        Primitive: int64_t
      Member: n2 (offset: 0)
        Primitive: int8_t
)");
}

// TODO test we follow class members, templates, children
