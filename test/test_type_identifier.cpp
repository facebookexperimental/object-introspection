#include <gtest/gtest.h>

#include "oi/type_graph/TypeIdentifier.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(TypeIdentifierTest, StubbedParam) {
  auto myint = Primitive{Primitive::Kind::Int32};

  auto myparam = Class{Class::Kind::Struct, "MyParam", 4};
  myparam.members.push_back(Member{&myint, "a", 0});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{&myint});
  container.templateParams.push_back(TemplateParam{&myparam});
  container.templateParams.push_back(TemplateParam{&myint});

  test(TypeIdentifier::createPass(), {container}, R"(
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

  auto myalloc = Class{Class::Kind::Struct, "MyAlloc", 8};
  myalloc.templateParams.push_back(TemplateParam{&myint});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{&myint});
  container.templateParams.push_back(TemplateParam{&myalloc});
  container.templateParams.push_back(TemplateParam{&myint});

  test(TypeIdentifier::createPass(), {container}, R"(
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

  auto myalloc = Class{Class::Kind::Struct, "MyAlloc", 1};
  myalloc.templateParams.push_back(TemplateParam{&myint});
  myalloc.functions.push_back(Function{"allocate"});
  myalloc.functions.push_back(Function{"deallocate"});

  auto container = getVector();
  container.templateParams.push_back(TemplateParam{&myint});
  container.templateParams.push_back(TemplateParam{&myalloc});
  container.templateParams.push_back(TemplateParam{&myint});

  test(TypeIdentifier::createPass(), {container}, R"(
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
