#include <gtest/gtest.h>

#include "oi/type_graph/RemoveMembers.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(RemoveMembersTest, Match) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };

  test(RemoveMembers::createPass(membersToIgnore),
       R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
[1]     Class: ClassB (size: 4)
      Member: b (offset: 4)
        [1]
      Member: c (offset: 8)
        [1]
)",
       R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
[1]     Class: ClassB (size: 4)
      Member: c (offset: 8)
        [1]
)");
}

TEST(RemoveMembersTest, TypeMatchMemberMiss) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "x"},
  };

  testNoChange(RemoveMembers::createPass(membersToIgnore), R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
[1]     Class: ClassB (size: 4)
      Member: b (offset: 4)
        [1]
      Member: c (offset: 8)
        [1]
)");
}

TEST(RemoveMembersTest, MemberMatchWrongType) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassB", "b"},
  };

  testNoChange(RemoveMembers::createPass(membersToIgnore), R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
[1]     Class: ClassB (size: 4)
      Member: b (offset: 4)
        [1]
      Member: c (offset: 8)
        [1]
)");
}

TEST(RemoveMembersTest, RecurseClassParam) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };
  test(RemoveMembers::createPass(membersToIgnore),
       R"(
[0] Class: MyClass (size: 0)
      Param
[1]     Class: ClassA (size: 12)
          Member: a (offset: 0)
            Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
          Member: c (offset: 8)
            Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 0)
      Param
[1]     Class: ClassA (size: 12)
          Member: a (offset: 0)
            Primitive: int32_t
          Member: c (offset: 8)
            Primitive: int32_t
)");
}

TEST(RemoveMembersTest, RecurseClassParent) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };
  test(RemoveMembers::createPass(membersToIgnore),
       R"(
[0] Class: MyClass (size: 0)
      Parent (offset: 0)
[1]     Class: ClassA (size: 12)
          Member: a (offset: 0)
            Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
          Member: c (offset: 8)
            Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 0)
      Parent (offset: 0)
[1]     Class: ClassA (size: 12)
          Member: a (offset: 0)
            Primitive: int32_t
          Member: c (offset: 8)
            Primitive: int32_t
)");
}

TEST(RemoveMembersTest, RecurseClassMember) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };
  test(RemoveMembers::createPass(membersToIgnore),
       R"(
[0] Class: MyClass (size: 0)
      Member: xxx (offset: 0)
[1]     Class: ClassA (size: 12)
          Member: a (offset: 0)
            Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
          Member: c (offset: 8)
            Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 0)
      Member: xxx (offset: 0)
[1]     Class: ClassA (size: 12)
          Member: a (offset: 0)
            Primitive: int32_t
          Member: c (offset: 8)
            Primitive: int32_t
)");
}

TEST(RemoveMembersTest, RecurseClassChild) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };
  test(RemoveMembers::createPass(membersToIgnore),
       R"(
[0] Class: MyClass (size: 0)
      Child
[1]     Class: ClassA (size: 12)
          Member: a (offset: 0)
            Primitive: int32_t
          Member: b (offset: 4)
            Primitive: int32_t
          Member: c (offset: 8)
            Primitive: int32_t
)",
       R"(
[0] Class: MyClass (size: 0)
      Child
[1]     Class: ClassA (size: 12)
          Member: a (offset: 0)
            Primitive: int32_t
          Member: c (offset: 8)
            Primitive: int32_t
)");
}

TEST(RemoveMembersTest, Union) {
  test(RemoveMembers::createPass({}),
       R"(
[0] Union: MyUnion (size: 4)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 0)
        Primitive: int32_t
)",
       R"(
[0] Union: MyUnion (size: 4)
)");
}

TEST(RemoveMembersTest, Incomplete) {
  test(RemoveMembers::createPass({}),
       R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
[1]     Incomplete: [MyIncompleteType]
      Member: c (offset: 8)
        Primitive: int32_t
)",
       R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: c (offset: 8)
        Primitive: int32_t
)");
}
