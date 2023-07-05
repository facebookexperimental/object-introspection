#include <gtest/gtest.h>

#include "oi/type_graph/AddPadding.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(AddPaddingTest, BetweenMembers) {
  auto myclass = Class{0, Class::Kind::Class, "MyClass", 16};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  auto myint64 = Primitive{Primitive::Kind::Int64};
  myclass.members.push_back(Member{myint8, "n1", 0});
  myclass.members.push_back(Member{myint64, "n2", 8 * 8});

  test(AddPadding::createPass(), {myclass}, R"(
[0] Class: MyClass (size: 16)
      Member: n1 (offset: 0)
        Primitive: int8_t
      Member: __oi_padding (offset: 1)
[1]     Array: (length: 7)
          Primitive: int8_t
      Member: n2 (offset: 8)
        Primitive: int64_t
)");
}

TEST(AddPaddingTest, AtEnd) {
  auto myclass = Class{0, Class::Kind::Struct, "MyStruct", 16};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  auto myint64 = Primitive{Primitive::Kind::Int64};
  myclass.members.push_back(Member{myint64, "n1", 0});
  myclass.members.push_back(Member{myint8, "n2", 8 * 8});

  test(AddPadding::createPass(), {myclass}, R"(
[0] Struct: MyStruct (size: 16)
      Member: n1 (offset: 0)
        Primitive: int64_t
      Member: n2 (offset: 8)
        Primitive: int8_t
      Member: __oi_padding (offset: 9)
[1]     Array: (length: 7)
          Primitive: int8_t
)");
}

TEST(AddPaddingTest, UnionNotPadded) {
  auto myclass = Class{0, Class::Kind::Union, "MyUnion", 8};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  auto myint64 = Primitive{Primitive::Kind::Int64};
  myclass.members.push_back(Member{myint64, "n1", 0});
  myclass.members.push_back(Member{myint8, "n2", 0});

  test(AddPadding::createPass(), {myclass}, R"(
[0] Union: MyUnion (size: 8)
      Member: n1 (offset: 0)
        Primitive: int64_t
      Member: n2 (offset: 0)
        Primitive: int8_t
)");
}

TEST(AddPaddingTest, Bitfields) {
  auto myclass = Class{0, Class::Kind::Class, "MyClass", 16};
  auto myint64 = Primitive{Primitive::Kind::Int64};
  auto myint16 = Primitive{Primitive::Kind::Int16};
  auto myint8 = Primitive{Primitive::Kind::Int8};

  Member b1{myint64, "b1", 0};
  b1.bitsize = 3;
  Member b2{myint8, "b2", 3};
  b2.bitsize = 2;
  // There may be a 0-sized bitfield between these two that does not appear
  // in the DWARF. This would add padding and push b3 to the next byte.
  Member b3{myint8, "b3", 8};
  b3.bitsize = 1;

  Member b4{myint64, "b4", 8 * 8};
  b4.bitsize = 24;

  Member n{myint16, "n", 12 * 8};

  myclass.members.push_back(b1);
  myclass.members.push_back(b2);
  myclass.members.push_back(b3);
  myclass.members.push_back(b4);
  myclass.members.push_back(n);

  test(AddPadding::createPass(), {myclass}, R"(
[0] Class: MyClass (size: 16)
      Member: b1 (offset: 0, bitsize: 3)
        Primitive: int64_t
      Member: b2 (offset: 0.375, bitsize: 2)
        Primitive: int8_t
      Member: __oi_padding (offset: 0.625, bitsize: 3)
        Primitive: int64_t
      Member: b3 (offset: 1, bitsize: 1)
        Primitive: int8_t
      Member: __oi_padding (offset: 1.125, bitsize: 55)
        Primitive: int64_t
      Member: b4 (offset: 8, bitsize: 24)
        Primitive: int64_t
      Member: __oi_padding (offset: 11)
[1]     Array: (length: 1)
          Primitive: int8_t
      Member: n (offset: 12)
        Primitive: int16_t
      Member: __oi_padding (offset: 14)
[2]     Array: (length: 2)
          Primitive: int8_t
)");
}

// TODO test we follow class members, templates, children
