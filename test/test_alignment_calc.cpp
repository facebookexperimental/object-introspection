#include <gtest/gtest.h>

#include "oi/type_graph/AlignmentCalc.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(AlignmentCalcTest, PrimitiveMembers) {
  auto myclass = Class{Class::Kind::Class, "MyClass", 16};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  auto myint64 = Primitive{Primitive::Kind::Int64};
  myclass.members.push_back(Member(&myint8, "n", 0));
  myclass.members.push_back(Member(&myint64, "n", 8));

  test(AlignmentCalc::createPass(), {myclass}, R"(
[0] Class: MyClass (size: 16, align: 8)
      Member: n (offset: 0, align: 1)
        Primitive: int8_t
      Member: n (offset: 8, align: 8)
        Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, StructMembers) {
  auto mystruct = Class{Class::Kind::Struct, "MyStruct", 8};
  auto myint32 = Primitive{Primitive::Kind::Int32};
  mystruct.members.push_back(Member(&myint32, "n1", 0));
  mystruct.members.push_back(Member(&myint32, "n2", 4));

  auto myclass = Class{Class::Kind::Class, "MyClass", 12};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  myclass.members.push_back(Member(&myint8, "n", 0));
  myclass.members.push_back(Member(&mystruct, "s", 4));

  test(AlignmentCalc::createPass(), {myclass}, R"(
[0] Class: MyClass (size: 12, align: 4)
      Member: n (offset: 0, align: 1)
        Primitive: int8_t
      Member: s (offset: 4, align: 4)
[1]     Struct: MyStruct (size: 8, align: 4)
          Member: n1 (offset: 0, align: 4)
            Primitive: int32_t
          Member: n2 (offset: 4, align: 4)
            Primitive: int32_t
)");
}

TEST(AlignmentCalcTest, StructInContainer) {
  auto myclass = Class{Class::Kind::Class, "MyClass", 16};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  auto myint64 = Primitive{Primitive::Kind::Int64};
  myclass.members.push_back(Member(&myint8, "n", 0));
  myclass.members.push_back(Member(&myint64, "n", 8));

  auto mycontainer = Container{ContainerInfo{}, 8};
  mycontainer.templateParams.push_back(&myclass);

  test(AlignmentCalc::createPass(), {mycontainer}, R"(
[0] Container:  (size: 8)
      Param
[1]     Class: MyClass (size: 16, align: 8)
          Member: n (offset: 0, align: 1)
            Primitive: int8_t
          Member: n (offset: 8, align: 8)
            Primitive: int64_t
)");
}

TEST(AlignmentCalcTest, Packed) {
  auto mystruct = Class{Class::Kind::Struct, "MyStruct", 9};
  auto myint8 = Primitive{Primitive::Kind::Int8};
  auto myint64 = Primitive{Primitive::Kind::Int64};
  mystruct.members.push_back(Member(&myint8, "n1", 0));
  mystruct.members.push_back(Member(&myint64, "n2", 1));

  test(AlignmentCalc::createPass(), {mystruct}, R"(
[0] Struct: MyStruct (size: 9, align: 8, packed)
      Member: n1 (offset: 0, align: 1)
        Primitive: int8_t
      Member: n2 (offset: 1, align: 8)
        Primitive: int64_t
)");
}
