#include <gtest/gtest.h>

#include "oi/SymbolService.h"
#include "oi/type_graph/AddChildren.h"
#include "oi/type_graph/Printer.h"
#include "oi/type_graph/TypeGraph.h"
#include "test_drgn_parser.h"

using namespace type_graph;

class AddChildrenTest : public DrgnParserTest {
 protected:
  std::string run(std::string_view function, bool chaseRawPointers) override;
};

std::string AddChildrenTest::run(std::string_view function,
                                 bool chaseRawPointers) {
  TypeGraph typeGraph;
  auto drgnParser = getDrgnParser(typeGraph, chaseRawPointers);
  auto* drgnRoot = getDrgnRoot(function);

  Type& type = drgnParser.parse(drgnRoot);
  typeGraph.addRoot(type);

  auto pass = AddChildren::createPass(drgnParser, *symbols_);
  pass.run(typeGraph);

  std::stringstream out;
  Printer printer{out, typeGraph.size()};
  printer.print(type);

  return out.str();
}

TEST_F(AddChildrenTest, SimpleStruct) {
  // Should do nothing
  test("oid_test_case_simple_struct", R"(
[0] Pointer
[1]   Struct: SimpleStruct (size: 16)
        Member: a (offset: 0)
          Primitive: int32_t
        Member: b (offset: 4)
          Primitive: int8_t
        Member: c (offset: 8)
          Primitive: int64_t
)");
}

TEST_F(AddChildrenTest, InheritanceStatic) {
  // Should do nothing
  test("oid_test_case_inheritance_access_public", R"(
[0] Pointer
[1]   Class: Public (size: 8)
        Parent (offset: 0)
[2]       Class: Base (size: 4)
            Member: base_int (offset: 0)
              Primitive: int32_t
        Member: public_int (offset: 4)
          Primitive: int32_t
)");
}

TEST_F(AddChildrenTest, InheritancePolymorphic) {
  testMultiCompiler("oid_test_case_inheritance_polymorphic_a_as_a", R"(
[0]  Pointer
[1]    Class: A (size: 16)
         Member: _vptr$A (offset: 0)
           Primitive: uintptr_t
         Member: int_a (offset: 8)
           Primitive: int32_t
         Function: ~A (virtual)
         Function: myfunc (virtual)
         Function: A
         Function: A
         Child
[2]        Class: B (size: 40)
             Parent (offset: 0)
               [1]
             Member: vec_b (offset: 16)
[3]            Container: std::vector (size: 24)
                 Param
                   Primitive: int32_t
                 Param
[4]                Class: allocator<int> (size: 1)
                     Param
                       Primitive: int32_t
                     Parent (offset: 0)
[5]                    Typedef: __allocator_base<int>
[6]                      Class: new_allocator<int> (size: 1)
                           Param
                             Primitive: int32_t
                           Function: new_allocator
                           Function: new_allocator
                           Function: allocate
                           Function: deallocate
                           Function: _M_max_size
                     Function: allocator
                     Function: allocator
                     Function: operator=
                     Function: ~allocator
                     Function: allocate
                     Function: deallocate
             Function: ~B (virtual)
             Function: myfunc (virtual)
             Function: B
             Function: B
             Child
[7]            Class: C (size: 48)
                 Parent (offset: 0)
                   [2]
                 Member: int_c (offset: 40)
                   Primitive: int32_t
                 Function: ~C (virtual)
                 Function: myfunc (virtual)
                 Function: C
                 Function: C
)",
                    R"(
[0]  Pointer
[1]    Class: A (size: 16)
         Member: _vptr.A (offset: 0)
           Primitive: uintptr_t
         Member: int_a (offset: 8)
           Primitive: int32_t
         Function: operator=
         Function: A
         Function: A
         Function: ~A (virtual)
         Function: myfunc (virtual)
         Child
[2]        Class: B (size: 40)
             Parent (offset: 0)
               [1]
             Member: vec_b (offset: 16)
[3]            Container: std::vector (size: 24)
                 Param
                   Primitive: int32_t
                 Param
[4]                Class: allocator<int> (size: 1)
                     Parent (offset: 0)
[5]                    Class: new_allocator<int> (size: 1)
                         Param
                           Primitive: int32_t
                         Function: new_allocator
                         Function: new_allocator
                         Function: allocate
                         Function: deallocate
                         Function: _M_max_size
                     Function: allocator
                     Function: allocator
                     Function: operator=
                     Function: ~allocator
                     Function: allocate
                     Function: deallocate
             Function: operator=
             Function: B
             Function: B
             Function: ~B (virtual)
             Function: myfunc (virtual)
             Child
[6]            Class: C (size: 48)
                 Parent (offset: 0)
                   [2]
                 Member: int_c (offset: 40)
                   Primitive: int32_t
                 Function: operator=
                 Function: C
                 Function: C
                 Function: ~C (virtual)
                 Function: myfunc (virtual)
)");
}
