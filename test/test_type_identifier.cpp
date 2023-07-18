#include <gtest/gtest.h>

#include "oi/type_graph/TypeIdentifier.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(TypeIdentifierTest, StubbedParam) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myparam = Class{1, Class::Kind::Struct, "MyParam", 4};
  myparam.members.push_back(Member{myint, "a", 0});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{myint});
  container.templateParams.push_back(TemplateParam{myparam});
  container.templateParams.push_back(TemplateParam{myint});

  test(TypeIdentifier::createPass({}), {container}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyParam (size: 4)
          Member: a (offset: 0)
            Primitive: int32_t
      Param
        Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        Dummy (size: 4)
      Param
        Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, Allocator) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myalloc = Class{1, Class::Kind::Struct, "MyAlloc", 8};
  myalloc.templateParams.push_back(TemplateParam{myint});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{myint});
  container.templateParams.push_back(TemplateParam{myalloc});
  container.templateParams.push_back(TemplateParam{myint});

  test(TypeIdentifier::createPass({}), {container}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 8)
          Param
            Primitive: int32_t
          Function: allocate
          Function: deallocate
      Param
        Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 8)
          Primitive: int32_t
      Param
        Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, AllocatorSize1) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myalloc = Class{1, Class::Kind::Struct, "MyAlloc", 1};
  myalloc.templateParams.push_back(TemplateParam{myint});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{myint});
  container.templateParams.push_back(TemplateParam{myalloc});
  container.templateParams.push_back(TemplateParam{myint});

  test(TypeIdentifier::createPass({}), {container}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Struct: MyAlloc (size: 1)
          Param
            Primitive: int32_t
          Function: allocate
          Function: deallocate
      Param
        Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 0)
          Primitive: int32_t
      Param
        Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, PassThroughTypes) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myalloc = Class{1, Class::Kind::Class, "std::allocator", 1};
  myalloc.templateParams.push_back(TemplateParam{myint});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{myint});
  container.templateParams.push_back(TemplateParam{myalloc});

  std::vector<ContainerInfo> passThroughTypes;
  passThroughTypes.emplace_back("std::allocator", DUMMY_TYPE, "memory");

  test(TypeIdentifier::createPass(passThroughTypes), {container}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Class: std::allocator (size: 1)
          Param
            Primitive: int32_t
          Function: allocate
          Function: deallocate
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Container: std::allocator (size: 1)
          Param
            Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, ContainerNotReplaced) {
  auto myint = Primitive{Primitive::Kind::Int32};

  ContainerInfo allocatorInfo{"std::allocator", DUMMY_TYPE, "memory"};
  auto myalloc = Container{1, allocatorInfo, 1};
  myalloc.templateParams.push_back(TemplateParam{myint});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{myint});
  container.templateParams.push_back(TemplateParam{myalloc});

  test(TypeIdentifier::createPass({}), {container}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Container: std::allocator (size: 1)
          Param
            Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
[1]     Container: std::allocator (size: 1)
          Param
            Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, DummyNotReplaced) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto dummy = Dummy{22, 0};

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{myint});
  container.templateParams.push_back(TemplateParam{dummy});

  test(TypeIdentifier::createPass({}), {container}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        Dummy (size: 22)
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        Dummy (size: 22)
)");
}

TEST(TypeIdentifierTest, DummyAllocatorNotReplaced) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto dummy = DummyAllocator{myint, 22, 0};

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{myint});
  container.templateParams.push_back(TemplateParam{dummy});

  test(TypeIdentifier::createPass({}), {container}, R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 22)
          Primitive: int32_t
)",
       R"(
[0] Container: std::vector (size: 24)
      Param
        Primitive: int32_t
      Param
        DummyAllocator (size: 22)
          Primitive: int32_t
)");
}

TEST(TypeIdentifierTest, ClassParam) {
  test(TypeIdentifier::createPass({}), R"(
[0] Class: MyClass (size: 0)
      Param
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Param
[2]         Struct: MyParam (size: 4)
              Member: a (offset: 0)
                Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 0)
      Param
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Param
            Dummy (size: 4)
)");
}

TEST(TypeIdentifierTest, ClassParent) {
  test(TypeIdentifier::createPass({}), R"(
[0] Class: MyClass (size: 0)
      Parent (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Param
[2]         Struct: MyParam (size: 4)
              Member: a (offset: 0)
                Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 0)
      Parent (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Param
            Dummy (size: 4)
)");
}

TEST(TypeIdentifierTest, ClassMember) {
  test(TypeIdentifier::createPass({}), R"(
[0] Class: MyClass (size: 0)
      Member: xxx (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Param
[2]         Struct: MyParam (size: 4)
              Member: a (offset: 0)
                Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 0)
      Member: xxx (offset: 0)
[1]     Container: std::vector (size: 24)
          Param
            Primitive: int32_t
          Param
            Dummy (size: 4)
)");
}

TEST(TypeIdentifierTest, ClassChild) {
  test(TypeIdentifier::createPass({}), R"(
[0] Class: MyClass (size: 0)
      Child
[1]     Class: ChildClass (size: 0)
          Parent (offset: 0)
            [0]
          Member: c (offset: 0)
[2]         Container: std::vector (size: 24)
              Param
                Primitive: int32_t
              Param
[3]             Struct: MyParam (size: 4)
                  Member: a (offset: 0)
                    Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 0)
      Child
[1]     Class: ChildClass (size: 0)
          Parent (offset: 0)
            [0]
          Member: c (offset: 0)
[2]         Container: std::vector (size: 24)
              Param
                Primitive: int32_t
              Param
                Dummy (size: 4)
)");
}

TEST(TypeIdentifierTest, Union) {
  test(TypeIdentifier::createPass({}), R"(
[0] Union: MyUnion (size: 12)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
        Primitive: int32_t
      Member: c (offset: 8)
        Primitive: int32_t
)",
       R"(
[0] Union: MyUnion (size: 12)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
        Primitive: int32_t
      Member: c (offset: 8)
        Primitive: int32_t
)");
}
