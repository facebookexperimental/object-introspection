#include <gtest/gtest.h>

#include "oi/SymbolService.h"
#include "oi/type_graph/AddChildren.h"
#include "oi/type_graph/NodeTracker.h"
#include "oi/type_graph/Printer.h"
#include "oi/type_graph/TypeGraph.h"
#include "test_drgn_parser.h"

using namespace type_graph;

class AddChildrenTest : public DrgnParserTest {
 protected:
  std::string run(std::string_view function,
                  DrgnParserOptions options) override;
};

std::string AddChildrenTest::run(std::string_view function,
                                 DrgnParserOptions options) {
  TypeGraph typeGraph;
  auto drgnParser = getDrgnParser(typeGraph, options);
  auto* drgnRoot = getDrgnRoot(function);

  Type& type = drgnParser.parse(drgnRoot);
  typeGraph.addRoot(type);
  NodeTracker tracker;

  auto pass = AddChildren::createPass(drgnParser, *symbols_);
  pass.run(typeGraph, tracker);

  std::stringstream out;
  Printer printer{out, tracker, typeGraph.size()};
  printer.print(type);

  return out.str();
}

TEST_F(AddChildrenTest, SimpleStruct) {
  // Should do nothing
  test("oid_test_case_simple_struct", R"(
[1] Pointer
[0]   Struct: SimpleStruct [ns_simple::SimpleStruct] (size: 16)
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
[4] Pointer
[0]   Class: Public [ns_inheritance_access::Public] (size: 8)
        Parent (offset: 0)
[1]       Class: Base [ns_inheritance_access::Base] (size: 4)
            Member: base_int (offset: 0)
[3]           Typedef: int32_t
[2]             Typedef: __int32_t
                  Primitive: int32_t
        Member: public_int (offset: 4)
          [3]
)");
}

TEST_F(AddChildrenTest, InheritancePolymorphic) {
  testMultiCompilerGlob("oid_test_case_inheritance_polymorphic_a_as_a",
                        R"(
[1]  Pointer
[0]    Class: A [ns_inheritance_polymorphic::A] (size: 16)
         Member: _vptr$A (offset: 0)
           Primitive: StubbedPointer
         Member: int_a (offset: 8)
           Primitive: int32_t
         Function: ~A (virtual)
         Function: myfunc (virtual)
         Function: A
         Function: A
         Child
[17]       Class: B [ns_inheritance_polymorphic::B] (size: 40)
             Parent (offset: 0)
               [0]
             Member: vec_b (offset: 16)
[4]            Class: vector<int, std::allocator<int> > [std::vector<int, std::allocator<int> >] (size: 24)
                 Param
                   Primitive: int32_t
                 Param
[5]                Class: allocator<int> [std::allocator<int>] (size: 1)
                     Param
                       Primitive: int32_t
                     Parent (offset: 0)
[7]                    Typedef: __allocator_base<int>
[6]                      Class: new_allocator<int> [__gnu_cxx::new_allocator<int>] (size: 1)
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
*
             Function: ~B (virtual)
             Function: myfunc (virtual)
             Function: B
             Function: B
             Child
[19]           Class: C [ns_inheritance_polymorphic::C] (size: 48)
                 Parent (offset: 0)
                   [17]
                 Member: int_c (offset: 40)
                   Primitive: int32_t
                 Function: ~C (virtual)
                 Function: myfunc (virtual)
                 Function: C
                 Function: C
)",
                        R"(
[1]  Pointer
[0]    Class: A [ns_inheritance_polymorphic::A] (size: 16)
         Member: _vptr.A (offset: 0)
           Primitive: StubbedPointer
         Member: int_a (offset: 8)
           Primitive: int32_t
         Function: operator=
         Function: A
         Function: A
         Function: ~A (virtual)
         Function: myfunc (virtual)
         Child
[13]       Class: B [ns_inheritance_polymorphic::B] (size: 40)
             Parent (offset: 0)
               [0]
             Member: vec_b (offset: 16)
[4]            Class: vector<int, std::allocator<int> > [std::vector<int, std::allocator<int> >] (size: 24)
                 Param
                   Primitive: int32_t
                 Param
[5]                Class: allocator<int> [std::allocator<int>] (size: 1)
                     Parent (offset: 0)
[6]                    Class: new_allocator<int> [__gnu_cxx::new_allocator<int>] (size: 1)
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
*
             Function: operator=
             Function: B
             Function: B
             Function: ~B (virtual)
             Function: myfunc (virtual)
             Child
[15]           Class: C [ns_inheritance_polymorphic::C] (size: 48)
                 Parent (offset: 0)
                   [13]
                 Member: int_c (offset: 40)
                   Primitive: int32_t
                 Function: operator=
                 Function: C
                 Function: C
                 Function: ~C (virtual)
                 Function: myfunc (virtual)
)");
}
