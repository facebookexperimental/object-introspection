#include <gtest/gtest.h>

#include "oi/type_graph/RemoveIgnored.h"
#include "oi/type_graph/Types.h"
#include "test/type_graph_utils.h"

using namespace type_graph;

TEST(RemoveIgnoredTest, Match) {
  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};

  auto classA = Class{0, Class::Kind::Class, "ClassA", 12};
  classA.members.push_back(Member{classB, "a", 0});
  classA.members.push_back(Member{classB, "b", 4 * 8});
  classA.members.push_back(Member{classB, "c", 8 * 8});

  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };

  test(RemoveIgnored::createPass(membersToIgnore), {classA}, R"(
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
      Member: __oi_padding (offset: 4)
[2]     Array: (length: 4)
          Primitive: int8_t
      Member: c (offset: 8)
        [1]
)");
}

TEST(RemoveIgnoredTest, TypeMatchMemberMiss) {
  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};

  auto classA = Class{0, Class::Kind::Class, "ClassA", 12};
  classA.members.push_back(Member{classB, "a", 0});
  classA.members.push_back(Member{classB, "b", 4 * 8});
  classA.members.push_back(Member{classB, "c", 8 * 8});

  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "x"},
  };

  test(RemoveIgnored::createPass(membersToIgnore), {classA}, R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
[1]     Class: ClassB (size: 4)
      Member: b (offset: 4)
        [1]
      Member: c (offset: 8)
        [1]
)");
}

TEST(RemoveIgnoredTest, MemberMatchWrongType) {
  auto classB = Class{1, Class::Kind::Class, "ClassB", 4};

  auto classA = Class{0, Class::Kind::Class, "ClassA", 12};
  classA.members.push_back(Member{classB, "a", 0});
  classA.members.push_back(Member{classB, "b", 4 * 8});
  classA.members.push_back(Member{classB, "c", 8 * 8});

  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassB", "b"},
  };

  test(RemoveIgnored::createPass(membersToIgnore), {classA}, R"(
[0] Class: ClassA (size: 12)
      Member: a (offset: 0)
[1]     Class: ClassB (size: 4)
      Member: b (offset: 4)
        [1]
      Member: c (offset: 8)
        [1]
)");
}

TEST(RemoveIgnoredTest, RecurseClassParam) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };
  test(RemoveIgnored::createPass(membersToIgnore), R"(
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
          Member: __oi_padding (offset: 4)
[2]         Array: (length: 4)
              Primitive: int8_t
          Member: c (offset: 8)
            Primitive: int32_t
)");
}

TEST(RemoveIgnoredTest, RecurseClassParent) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };
  test(RemoveIgnored::createPass(membersToIgnore), R"(
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
          Member: __oi_padding (offset: 4)
[2]         Array: (length: 4)
              Primitive: int8_t
          Member: c (offset: 8)
            Primitive: int32_t
)");
}

TEST(RemoveIgnoredTest, RecurseClassMember) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };
  test(RemoveIgnored::createPass(membersToIgnore), R"(
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
          Member: __oi_padding (offset: 4)
[2]         Array: (length: 4)
              Primitive: int8_t
          Member: c (offset: 8)
            Primitive: int32_t
)");
}

TEST(RemoveIgnoredTest, RecurseClassChild) {
  const std::vector<std::pair<std::string, std::string>>& membersToIgnore = {
      {"ClassA", "b"},
  };
  test(RemoveIgnored::createPass(membersToIgnore), R"(
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
          Member: __oi_padding (offset: 4)
[2]         Array: (length: 4)
              Primitive: int8_t
          Member: c (offset: 8)
            Primitive: int32_t
)");
}
