#include <gtest/gtest.h>

#include "oi/ContainerInfo.h"

TEST(ContainerInfoTest, matcher) {
  ContainerInfo info{"std::vector", SEQ_TYPE, "vector"};

  EXPECT_TRUE(info.matches("std::vector<int>"));
  EXPECT_TRUE(info.matches("std::vector<std::list<int>>"));
  EXPECT_TRUE(info.matches("std::vector"));

  EXPECT_FALSE(info.matches("vector"));
  EXPECT_FALSE(info.matches("non_std::vector<int>"));
  EXPECT_FALSE(info.matches("std::vector_other<int>"));
  EXPECT_FALSE(info.matches("std::list<std::vector<int>>"));
  EXPECT_FALSE(info.matches("std::vector::value_type"));
  EXPECT_FALSE(info.matches("std::vector<int>::value_type"));
  EXPECT_FALSE(info.matches("std::vector<std::vector<int>>::value_type"));
  // Uh-oh, here's a case that I don't think regexes are powerful enough to
  // match: EXPECT_FALSE(info.matches("std::vector<int>::subtype<bool>"));
}
