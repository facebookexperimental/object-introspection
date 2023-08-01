#include <gtest/gtest.h>

#include "oi/type_graph/AddPadding.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(AddPaddingTest, BetweenMembers) {
  test(AddPadding::createPass(), R"(
[0] Class: MyClass (size: 16)
      Member: n1 (offset: 0)
        Primitive: int8_t
      Member: n2 (offset: 8)
        Primitive: int64_t
)",
       R"(
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

TEST(AddPaddingTest, AtBeginning) {
  test(AddPadding::createPass(), R"(
[0] Struct: MyStruct (size: 16)
      Member: n1 (offset: 8)
        Primitive: int64_t
)",
       R"(
[0] Struct: MyStruct (size: 16)
      Member: __oi_padding (offset: 0)
[1]     Array: (length: 8)
          Primitive: int8_t
      Member: n1 (offset: 8)
        Primitive: int64_t
)");
}

TEST(AddPaddingTest, AtEnd) {
  test(AddPadding::createPass(), R"(
[0] Struct: MyStruct (size: 16)
      Member: n1 (offset: 0)
        Primitive: int64_t
      Member: n2 (offset: 8)
        Primitive: int8_t
)",
       R"(
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

TEST(AddPaddingTest, UnionBetweenMembers) {
  test(AddPadding::createPass(), R"(
[0] Union: MyUnion (size: 8)
      Member: n1 (offset: 0)
        Primitive: int64_t
      Member: n2 (offset: 0)
        Primitive: int8_t
)",
       R"(
[0] Union: MyUnion (size: 8)
      Member: n1 (offset: 0)
        Primitive: int64_t
      Member: n2 (offset: 0)
        Primitive: int8_t
)");
}

TEST(AddPaddingTest, UnionAtEnd) {
  test(AddPadding::createPass(), R"(
[0] Union: MyUnion (size: 16)
      Member: n1 (offset: 0)
        Primitive: int64_t
      Member: n2 (offset: 0)
        Primitive: int8_t
)",
       R"(
[0] Union: MyUnion (size: 16)
      Member: n1 (offset: 0)
        Primitive: int64_t
      Member: n2 (offset: 0)
        Primitive: int8_t
      Member: __oi_padding (offset: 0)
[1]     Array: (length: 16)
          Primitive: int8_t
)");
}

TEST(AddPaddingTest, Bitfields) {
  test(AddPadding::createPass(), R"(
[0] Class: MyClass (size: 16)
      Member: b1 (offset: 0, bitsize: 3)
        Primitive: int64_t
      Member: b2 (offset: 0.375, bitsize: 2)
        Primitive: int8_t
      Member: b3 (offset: 1, bitsize: 1)
        Primitive: int8_t
      Member: b4 (offset: 8, bitsize: 24)
        Primitive: int64_t
      Member: n (offset: 12)
        Primitive: int16_t
)",
       R"(
[0] Class: MyClass (size: 16)
      Member: b1 (offset: 0, bitsize: 3)
        Primitive: int64_t
      Member: b2 (offset: 0.375, bitsize: 2)
        Primitive: int8_t
      Member: __oi_padding (offset: 0.625, bitsize: 3)
        Primitive: int8_t
      Member: b3 (offset: 1, bitsize: 1)
        Primitive: int8_t
      Member: __oi_padding (offset: 1.125, bitsize: 7)
        Primitive: int8_t
      Member: __oi_padding (offset: 2)
[1]     Array: (length: 6)
          Primitive: int8_t
      Member: b4 (offset: 8, bitsize: 24)
        Primitive: int64_t
      Member: __oi_padding (offset: 11)
[2]     Array: (length: 1)
          Primitive: int8_t
      Member: n (offset: 12)
        Primitive: int16_t
      Member: __oi_padding (offset: 14)
[3]     Array: (length: 2)
          Primitive: int8_t
)");
}

TEST(AddPaddingTest, EmptyClass) {
  testNoChange(AddPadding::createPass(), R"(
[0] Class: MyClass (size: 0)
)");
}

TEST(AddPaddingTest, MemberlessClass) {
  test(AddPadding::createPass(), R"(
[0] Class: MyClass (size: 12)
)",
       R"(
[0] Class: MyClass (size: 12)
      Member: __oi_padding (offset: 0)
[1]     Array: (length: 12)
          Primitive: int8_t
)");
}

TEST(AddPaddingTest, MemberlessUnion) {
  test(AddPadding::createPass(), R"(
[0] Union: MyUnion (size: 16)
)",
       R"(
[0] Union: MyUnion (size: 16)
      Member: __oi_padding (offset: 0)
[1]     Array: (length: 16)
          Primitive: int8_t
)");
}

// TODO test we follow class members, templates, children
