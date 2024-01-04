#include "test_drgn_parser.h"

#include <gtest/gtest.h>

#include <regex>

#include "oi/OIParser.h"
#include "oi/SymbolService.h"
#include "oi/type_graph/NodeTracker.h"
#include "oi/type_graph/Printer.h"
#include "oi/type_graph/TypeGraph.h"
#include "oi/type_graph/Types.h"

using namespace type_graph;

// TODO setup google logging for tests so it doesn't appear on terminal by
// default

SymbolService* DrgnParserTest::symbols_ = nullptr;

void DrgnParserTest::SetUpTestSuite() {
  symbols_ = new SymbolService{TARGET_EXE_PATH};
}

void DrgnParserTest::TearDownTestSuite() {
  delete symbols_;
}

DrgnParser DrgnParserTest::getDrgnParser(TypeGraph& typeGraph,
                                         DrgnParserOptions options) {
  DrgnParser drgnParser{typeGraph, options};
  return drgnParser;
}

drgn_type* DrgnParserTest::getDrgnRoot(std::string_view function) {
  irequest req{"entry", std::string{function}, "arg0"};
  auto* drgnRoot = symbols_->getRootType(req)->type.type;
  return drgnRoot;
}

std::string DrgnParserTest::run(std::string_view function,
                                DrgnParserOptions options) {
  TypeGraph typeGraph;
  auto drgnParser = getDrgnParser(typeGraph, options);
  auto* drgnRoot = getDrgnRoot(function);

  Type& type = drgnParser.parse(drgnRoot);

  std::stringstream out;
  NodeTracker tracker;
  Printer printer{out, tracker, typeGraph.size()};
  printer.print(type);

  return out.str();
}

void DrgnParserTest::test(std::string_view function,
                          std::string_view expected,
                          DrgnParserOptions options) {
  auto actual = run(function, options);

  expected.remove_prefix(1);  // Remove initial '\n'
  EXPECT_EQ(expected, actual);
}

void DrgnParserTest::test(std::string_view function,
                          std::string_view expected) {
  // Enable options in unit tests so we get more coverage
  DrgnParserOptions options = {
      .chaseRawPointers = true,
      .readEnumValues = true,
  };
  test(function, expected, options);
}

std::pair<size_t, size_t> globMatch(std::string_view pattern,
                                    std::string_view str) {
  size_t i = 0;
  size_t j = 0;
  size_t prevWildcardIdx = -1;
  size_t backtrackIdx = -1;
  size_t patternLineStart = 0;
  size_t strLineStart = 0;

  while (i < str.size()) {
    if (i + 1 < str.size() && str[i] == '\n')
      strLineStart = i + 1;
    if (j + 1 < pattern.size() && pattern[j] == '\n')
      patternLineStart = j + 1;

    if (j < pattern.size() && str[i] == pattern[j]) {
      // Exact character match
      i++;
      j++;
    } else if (j < pattern.size() && pattern[j] == '*') {
      // Wildcard
      backtrackIdx = i + 1;
      prevWildcardIdx = j++;
    } else if (prevWildcardIdx != static_cast<size_t>(-1)) {
      // No match, backtrack to previous wildcard
      i = ++backtrackIdx;
      j = prevWildcardIdx + 1;
    } else {
      // No match
      return {patternLineStart, strLineStart};
    }
  }

  while (j < pattern.size() && pattern[j] == '*') {
    j++;
  }

  // If the pattern has been fully consumed then it's a match
  return {j, i};
}

std::string prefixLines(std::string_view str,
                        std::string_view prefix,
                        size_t maxLen) {
  std::string res;
  res += prefix;
  for (size_t i = 0; i < maxLen && i < str.size(); i++) {
    char c = str[i];
    res += c;
    if (c == '\n') {
      res += prefix;
    }
  }
  if (str.size() > maxLen)
    res += "...";
  return res;
}

void DrgnParserTest::testGlob(std::string_view function,
                              std::string_view expected,
                              DrgnParserOptions options) {
  auto actual = run(function, options);

  expected.remove_prefix(1);  // Remove initial '\n'
  auto [expectedIdx, actualIdx] = globMatch(expected, actual);
  if (expected.size() != expectedIdx) {
    ADD_FAILURE() << prefixLines(expected.substr(expectedIdx), "-", 10000)
                  << "\n"
                  << prefixLines(actual.substr(actualIdx), "+", 10000);
  }
}

void DrgnParserTest::testMultiCompiler(
    std::string_view function,
    [[maybe_unused]] std::string_view expectedClang,
    [[maybe_unused]] std::string_view expectedGcc,
    DrgnParserOptions options) {
#if defined(__clang__)
  test(function, expectedClang, options);
#else
  test(function, expectedGcc, options);
#endif
}

void DrgnParserTest::testMultiCompilerGlob(
    std::string_view function,
    [[maybe_unused]] std::string_view expectedClang,
    [[maybe_unused]] std::string_view expectedGcc,
    DrgnParserOptions options) {
#if defined(__clang__)
  testGlob(function, expectedClang, options);
#else
  testGlob(function, expectedGcc, options);
#endif
}

TEST_F(DrgnParserTest, SimpleStruct) {
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

TEST_F(DrgnParserTest, SimpleClass) {
  test("oid_test_case_simple_class", R"(
[1] Pointer
[0]   Class: SimpleClass [ns_simple::SimpleClass] (size: 16)
        Member: a (offset: 0)
          Primitive: int32_t
        Member: b (offset: 4)
          Primitive: int8_t
        Member: c (offset: 8)
          Primitive: int64_t
)");
}

TEST_F(DrgnParserTest, SimpleUnion) {
  test("oid_test_case_simple_union", R"(
[1] Pointer
[0]   Union: SimpleUnion [ns_simple::SimpleUnion] (size: 8)
        Member: a (offset: 0)
          Primitive: int32_t
        Member: b (offset: 0)
          Primitive: int8_t
        Member: c (offset: 0)
          Primitive: int64_t
)");
}

TEST_F(DrgnParserTest, Inheritance) {
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

TEST_F(DrgnParserTest, InheritanceMultiple) {
  test("oid_test_case_inheritance_multiple_a", R"(
[6] Pointer
[0]   Struct: Derived_2 [ns_inheritance_multiple::Derived_2] (size: 24)
        Parent (offset: 0)
[1]       Struct: Base_1 [ns_inheritance_multiple::Base_1] (size: 4)
            Member: a (offset: 0)
              Primitive: int32_t
        Parent (offset: 4)
[2]       Struct: Derived_1 [ns_inheritance_multiple::Derived_1] (size: 12)
            Parent (offset: 0)
[3]           Struct: Base_2 [ns_inheritance_multiple::Base_2] (size: 4)
                Member: b (offset: 0)
                  Primitive: int32_t
            Parent (offset: 4)
[4]           Struct: Base_3 [ns_inheritance_multiple::Base_3] (size: 4)
                Member: c (offset: 0)
                  Primitive: int32_t
            Member: d (offset: 8)
              Primitive: int32_t
        Parent (offset: 16)
[5]       Struct: Base_4 [ns_inheritance_multiple::Base_4] (size: 4)
            Member: e (offset: 0)
              Primitive: int32_t
        Member: f (offset: 20)
          Primitive: int32_t
)");
}

TEST_F(DrgnParserTest, Container) {
  testMultiCompilerGlob("oid_test_case_std_vector_int_empty",
                        R"(
[13] Pointer
[0]    Class: vector<int, std::allocator<int> > [std::vector<int, std::allocator<int> >] (size: 24)
         Param
           Primitive: int32_t
         Param
[1]        Class: allocator<int> [std::allocator<int>] (size: 1)
             Param
               Primitive: int32_t
             Parent (offset: 0)
[3]            Typedef: __allocator_base<int>
[2]              Class: new_allocator<int> [__gnu_cxx::new_allocator<int>] (size: 1)
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
         Parent (offset: 0)
*
)",
                        R"(
[9]  Pointer
[0]    Class: vector<int, std::allocator<int> > [std::vector<int, std::allocator<int> >] (size: 24)
         Param
           Primitive: int32_t
         Param
[1]        Class: allocator<int> [std::allocator<int>] (size: 1)
             Parent (offset: 0)
[2]            Class: new_allocator<int> [__gnu_cxx::new_allocator<int>] (size: 1)
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
         Parent (offset: 0)
*
)");
}
// TODO test vector with custom allocator

TEST_F(DrgnParserTest, Enum) {
  test("oid_test_case_enums_scoped", R"(
    Enum: ScopedEnum (size: 4)
      Enumerator: 0:CaseA
      Enumerator: 1:CaseB
      Enumerator: 2:CaseC
)");
}

TEST_F(DrgnParserTest, EnumUint8) {
  test("oid_test_case_enums_scoped_uint8", R"(
    Enum: ScopedEnumUint8 (size: 1)
      Enumerator: 2:CaseA
      Enumerator: 3:CaseB
      Enumerator: 4:CaseC
)");
}

TEST_F(DrgnParserTest, UnscopedEnum) {
  test("oid_test_case_enums_unscoped", R"(
    Enum: UNSCOPED_ENUM (size: 4)
      Enumerator: -2:CASE_B
      Enumerator: 5:CASE_A
      Enumerator: 20:CASE_C
)");
}

TEST_F(DrgnParserTest, EnumNoValues) {
  DrgnParserOptions options{
      .readEnumValues = false,
  };
  test("oid_test_case_enums_scoped",
       R"(
    Enum: ScopedEnum (size: 4)
)",
       options);
}

TEST_F(DrgnParserTest, Typedef) {
  test("oid_test_case_typedefs_c_style", R"(
[2] Typedef: TdUInt64
[1]   Typedef: uint64_t
[0]     Typedef: __uint64_t
          Primitive: uint64_t
)");
}

TEST_F(DrgnParserTest, Using) {
  test("oid_test_case_typedefs_using", R"(
[2] Typedef: UsingUInt64
[1]   Typedef: uint64_t
[0]     Typedef: __uint64_t
          Primitive: uint64_t
)");
}

TEST_F(DrgnParserTest, ArrayMember) {
  test("oid_test_case_arrays_member_int10", R"(
[2] Pointer
[0]   Struct: Foo10 [ns_arrays::Foo10] (size: 40)
        Member: arr (offset: 0)
[1]       Array: (length: 10)
            Primitive: int32_t
)");
}

TEST_F(DrgnParserTest, ArrayRef) {
  test("oid_test_case_arrays_ref_int10", R"(
[1] Pointer
[0]   Array: (length: 10)
        Primitive: int32_t
)");
}

TEST_F(DrgnParserTest, ArrayDirect) {
  test("oid_test_case_arrays_direct_int10", R"(
[0] Pointer
      Primitive: int32_t
)");
}

TEST_F(DrgnParserTest, Pointer) {
  test("oid_test_case_pointers_struct_primitive_ptrs", R"(
[3] Pointer
[0]   Struct: PrimitivePtrs [ns_pointers::PrimitivePtrs] (size: 24)
        Member: a (offset: 0)
          Primitive: int32_t
        Member: b (offset: 8)
[1]       Pointer
            Primitive: int32_t
        Member: c (offset: 16)
[2]       Pointer
            Primitive: void
)");
}

TEST_F(DrgnParserTest, PointerNoFollow) {
  DrgnParserOptions options{
      .chaseRawPointers = false,
  };
  test("oid_test_case_pointers_struct_primitive_ptrs",
       R"(
[1] Pointer
[0]   Struct: PrimitivePtrs [ns_pointers::PrimitivePtrs] (size: 24)
        Member: a (offset: 0)
          Primitive: int32_t
        Member: b (offset: 8)
          Primitive: StubbedPointer
        Member: c (offset: 16)
          Primitive: StubbedPointer
)",
       options);
}

TEST_F(DrgnParserTest, PointerIncomplete) {
  test("oid_test_case_pointers_incomplete_raw", R"(
[1] Pointer
[0]   Incomplete: [IncompleteType]
)");
}

TEST_F(DrgnParserTest, Cycle) {
  test("oid_test_case_cycles_raw_ptr", R"(
[2] Pointer
[0]   Struct: RawNode [ns_cycles::RawNode] (size: 16)
        Member: value (offset: 0)
          Primitive: int32_t
        Member: next (offset: 8)
[1]       Pointer
            [0]
)");
}

TEST_F(DrgnParserTest, ClassTemplateInt) {
  test("oid_test_case_templates_int", R"(
[1] Pointer
[0]   Class: TemplatedClass1<int> [ns_templates::TemplatedClass1<int>] (size: 4)
        Param
          Primitive: int32_t
        Member: val (offset: 0)
          Primitive: int32_t
)");
}

TEST_F(DrgnParserTest, ClassTemplateTwo) {
  test("oid_test_case_templates_two", R"(
[3] Pointer
[0]   Class: TemplatedClass2<ns_templates::Foo, int> [ns_templates::TemplatedClass2<ns_templates::Foo, int>] (size: 12)
        Param
[1]       Struct: Foo [ns_templates::Foo] (size: 8)
            Member: a (offset: 0)
              Primitive: int32_t
            Member: b (offset: 4)
              Primitive: int32_t
        Param
          Primitive: int32_t
        Member: tc1 (offset: 0)
[2]       Class: TemplatedClass1<ns_templates::Foo> [ns_templates::TemplatedClass1<ns_templates::Foo>] (size: 8)
            Param
              [1]
            Member: val (offset: 0)
              [1]
        Member: val2 (offset: 8)
          Primitive: int32_t
)");
}

TEST_F(DrgnParserTest, ClassTemplateValue) {
  test("oid_test_case_templates_value", R"(
[2] Pointer
[0]   Struct: TemplatedClassVal<3> [ns_templates::TemplatedClassVal<3>] (size: 12)
        Param
          Value: 3
          Primitive: int32_t
        Member: arr (offset: 0)
[1]       Array: (length: 3)
            Primitive: int32_t
)");
}

TEST_F(DrgnParserTest, TemplateEnumValue) {
  testMultiCompilerGlob("oid_test_case_enums_params_scoped_enum_val",
                        R"(
[1] Pointer
[0]   Class: MyClass<ns_enums_params::MyNS::ScopedEnum::One> [ns_enums_params::MyClass<ns_enums_params::MyNS::ScopedEnum::One>] (size: 4)
        Param
          Value: ns_enums_params::MyNS::ScopedEnum::One
          Enum: ScopedEnum (size: 4)
*
)",
                        R"(
[1] Pointer
[0]   Class: MyClass<(ns_enums_params::MyNS::ScopedEnum)1> [ns_enums_params::MyClass<(ns_enums_params::MyNS::ScopedEnum)1>] (size: 4)
        Param
          Value: ns_enums_params::MyNS::ScopedEnum::One
          Enum: ScopedEnum (size: 4)
*
)");
}

TEST_F(DrgnParserTest, TemplateEnumValueGaps) {
  testMultiCompilerGlob("oid_test_case_enums_params_scoped_enum_val_gaps",
                        R"(
[1] Pointer
[0]   Class: ClassGaps<ns_enums_params::MyNS::EnumWithGaps::Twenty> [ns_enums_params::ClassGaps<ns_enums_params::MyNS::EnumWithGaps::Twenty>] (size: 4)
        Param
          Value: ns_enums_params::MyNS::EnumWithGaps::Twenty
          Enum: EnumWithGaps (size: 4)
*
)",
                        R"(
[1] Pointer
[0]   Class: ClassGaps<(ns_enums_params::MyNS::EnumWithGaps)20> [ns_enums_params::ClassGaps<(ns_enums_params::MyNS::EnumWithGaps)20>] (size: 4)
        Param
          Value: ns_enums_params::MyNS::EnumWithGaps::Twenty
          Enum: EnumWithGaps (size: 4)
*
)");
}

TEST_F(DrgnParserTest, TemplateEnumValueNegative) {
  testMultiCompilerGlob("oid_test_case_enums_params_scoped_enum_val_negative",
                        R"(
[1] Pointer
[0]   Class: ClassGaps<ns_enums_params::MyNS::EnumWithGaps::MinusTwo> [ns_enums_params::ClassGaps<ns_enums_params::MyNS::EnumWithGaps::MinusTwo>] (size: 4)
        Param
          Value: ns_enums_params::MyNS::EnumWithGaps::MinusTwo
          Enum: EnumWithGaps (size: 4)
*
)",
                        R"(
[1] Pointer
[0]   Class: ClassGaps<(ns_enums_params::MyNS::EnumWithGaps)-2> [ns_enums_params::ClassGaps<(ns_enums_params::MyNS::EnumWithGaps)-2>] (size: 4)
        Param
          Value: ns_enums_params::MyNS::EnumWithGaps::MinusTwo
          Enum: EnumWithGaps (size: 4)
*
)");
}

// TODO
// TEST_F(DrgnParserTest, ClassFunctions) {
//  test("TestClassFunctions", R"(
//[0] Pointer
//[1]   Class: ClassFunctions (size: 4)
//        Member: memberA (offset: 0)
//          Primitive: int32_t
//        Function: foo (virtuality: 0)
//        Function: bar (virtuality: 0)
//)");
//}

TEST_F(DrgnParserTest, StructAlignment) {
  GTEST_SKIP() << "Alignment not reported by drgn yet";
  test("oid_test_case_alignment_struct", R"(
[1] Pointer
[0]   Struct: Align16 [ns_alignment::Align16] (size: 16, align: 16)
        Member: c (offset: 0)
          Primitive: int8_t
)");
}

TEST_F(DrgnParserTest, MemberAlignment) {
  GTEST_SKIP() << "Alignment not reported by drgn yet";
  test("oid_test_case_alignment_member_alignment", R"(
[1] Pointer
[0]   Struct: MemberAlignment [ns_alignment::MemberAlignment] (size: 64)
        Member: c (offset: 0)
          Primitive: int8_t
        Member: c32 (offset: 32, align: 32)
          Primitive: int8_t
)");
}

TEST_F(DrgnParserTest, VirtualFunctions) {
  testMultiCompiler("oid_test_case_inheritance_polymorphic_a_as_a",
                    R"(
[1] Pointer
[0]   Class: A [ns_inheritance_polymorphic::A] (size: 16)
        Member: _vptr$A (offset: 0)
          Primitive: StubbedPointer
        Member: int_a (offset: 8)
          Primitive: int32_t
        Function: ~A (virtual)
        Function: myfunc (virtual)
        Function: A
        Function: A
)",
                    R"(
[1] Pointer
[0]   Class: A [ns_inheritance_polymorphic::A] (size: 16)
        Member: _vptr.A (offset: 0)
          Primitive: StubbedPointer
        Member: int_a (offset: 8)
          Primitive: int32_t
        Function: operator=
        Function: A
        Function: A
        Function: ~A (virtual)
        Function: myfunc (virtual)
)");
}

TEST_F(DrgnParserTest, BitfieldsSingle) {
  test("oid_test_case_bitfields_single", R"(
[1] Pointer
[0]   Struct: Single [ns_bitfields::Single] (size: 4)
        Member: bitfield (offset: 0, bitsize: 3)
          Primitive: int32_t
)");
}

TEST_F(DrgnParserTest, BitfieldsWithinBytes) {
  test("oid_test_case_bitfields_within_bytes", R"(
[1] Pointer
[0]   Struct: WithinBytes [ns_bitfields::WithinBytes] (size: 2)
        Member: a (offset: 0, bitsize: 3)
          Primitive: int8_t
        Member: b (offset: 0.375, bitsize: 5)
          Primitive: int8_t
        Member: c (offset: 1, bitsize: 7)
          Primitive: int8_t
)");
}

TEST_F(DrgnParserTest, BitfieldsStraddleBytes) {
  test("oid_test_case_bitfields_straddle_bytes", R"(
[1] Pointer
[0]   Struct: StraddleBytes [ns_bitfields::StraddleBytes] (size: 3)
        Member: a (offset: 0, bitsize: 7)
          Primitive: int8_t
        Member: b (offset: 1, bitsize: 7)
          Primitive: int8_t
        Member: c (offset: 2, bitsize: 2)
          Primitive: int8_t
)");
}

TEST_F(DrgnParserTest, BitfieldsMixed) {
  test("oid_test_case_bitfields_mixed", R"(
[1] Pointer
[0]   Struct: Mixed [ns_bitfields::Mixed] (size: 12)
        Member: a (offset: 0)
          Primitive: int32_t
        Member: b (offset: 4, bitsize: 4)
          Primitive: int8_t
        Member: c (offset: 4.5, bitsize: 12)
          Primitive: int16_t
        Member: d (offset: 6)
          Primitive: int8_t
        Member: e (offset: 8, bitsize: 22)
          Primitive: int32_t
)");
}

TEST_F(DrgnParserTest, BitfieldsMoreBitsThanType) {
  GTEST_SKIP() << "drgn errors out";
  test("oid_test_case_bitfields_more_bits_than_type", R"(
[1] Pointer
[0]   Struct: MoreBitsThanType [ns_bitfields::MoreBitsThanType] (size: 4)
        Member: a (offset: 0, bitsize: 8)
          Primitive: int8_t
)");
}

TEST_F(DrgnParserTest, BitfieldsZeroBits) {
  test("oid_test_case_bitfields_zero_bits", R"(
[1] Pointer
[0]   Struct: ZeroBits [ns_bitfields::ZeroBits] (size: 2)
        Member: b1 (offset: 0, bitsize: 3)
          Primitive: int8_t
        Member: b2 (offset: 1, bitsize: 2)
          Primitive: int8_t
)");
}

TEST_F(DrgnParserTest, BitfieldsEnum) {
  test("oid_test_case_bitfields_enum", R"(
[1] Pointer
[0]   Struct: Enum [ns_bitfields::Enum] (size: 4)
        Member: e (offset: 0, bitsize: 2)
          Enum: MyEnum (size: 4)
            Enumerator: 0:One
            Enumerator: 1:Two
            Enumerator: 2:Three
        Member: f (offset: 0.25, bitsize: 4)
          Enum: MyEnum (size: 4)
            Enumerator: 0:One
            Enumerator: 1:Two
            Enumerator: 2:Three
)");
}

// TODO test virtual classes
