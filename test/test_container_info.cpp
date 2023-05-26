#include <gtest/gtest.h>

#include "oi/ContainerInfo.h"

TEST(ContainerInfoTest, matcher) {
  ContainerInfo info{"std::vector", SEQ_TYPE, "vector"};

  EXPECT_TRUE(std::regex_search("std::vector<int>", info.matcher));
  EXPECT_TRUE(std::regex_search("std::vector<std::list<int>>", info.matcher));
  EXPECT_TRUE(std::regex_search("std::vector", info.matcher));

  EXPECT_FALSE(std::regex_search("vector", info.matcher));
  EXPECT_FALSE(std::regex_search("non_std::vector<int>", info.matcher));
  EXPECT_FALSE(std::regex_search("std::vector_other<int>", info.matcher));
  EXPECT_FALSE(std::regex_search("std::list<std::vector<int>>", info.matcher));
  EXPECT_FALSE(std::regex_search("std::vector::value_type", info.matcher));
  EXPECT_FALSE(std::regex_search("std::vector<int>::value_type", info.matcher));
  EXPECT_FALSE(std::regex_search("std::vector<std::vector<int>>::value_type",
                                 info.matcher));
  // Uh-oh, here's a case that I don't think regexes are powerful enough to
  // match: EXPECT_FALSE(std::regex_search("std::vector<int>::subtype<bool>",
  // info.matcher));
}
