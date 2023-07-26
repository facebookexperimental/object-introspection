#include <gtest/gtest.h>

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <variant>
#include <vector>

#include "oi/exporters/TypeCheckingWalker.h"
#include "oi/types/dy.h"

using namespace ObjectIntrospection;
using exporters::TypeCheckingWalker;

TEST(TypeCheckingWalker, TestUnit) {
  // ASSIGN
  std::vector<uint64_t> data;
  types::dy::Unit rootType;

  TypeCheckingWalker walker(rootType, data);

  // ACT
  auto first = walker.advance();

  // ASSERT
  ASSERT_FALSE(first.has_value());
}

TEST(TypeCheckingWalker, TestVarInt) {
  // ASSIGN
  uint64_t val = 51566;
  std::vector<uint64_t> data{val};
  types::dy::VarInt rootType;

  TypeCheckingWalker walker(rootType, data);

  // ACT
  auto first = walker.advance();

  // ASSERT
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::VarInt>(*first));
  EXPECT_EQ(std::get<TypeCheckingWalker::VarInt>(*first).value, val);
}

TEST(TypeCheckingWalker, TestPair) {
  // ASSIGN
  uint64_t firstVal = 37894;
  uint64_t secondVal = 6667;
  std::vector<uint64_t> data{firstVal, secondVal};

  types::dy::VarInt left;
  types::dy::VarInt right;
  types::dy::Pair rootType{left, right};

  TypeCheckingWalker walker(rootType, data);

  // ACT
  auto first = walker.advance();
  auto second = walker.advance();
  auto third = walker.advance();

  // ASSERT
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::VarInt>(*first));
  EXPECT_EQ(std::get<TypeCheckingWalker::VarInt>(*first).value, firstVal);

  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::VarInt>(*second));
  EXPECT_EQ(std::get<TypeCheckingWalker::VarInt>(*second).value, secondVal);

  ASSERT_FALSE(third.has_value());
}

TEST(TypeCheckingWalker, TestSumUnit) {
  // ASSIGN
  uint64_t sumIndex = 0;
  std::vector<uint64_t> data{sumIndex};

  types::dy::Unit unit;
  types::dy::VarInt varint;
  std::array<types::dy::Dynamic, 2> elements{unit, varint};
  types::dy::Sum rootType{elements};

  TypeCheckingWalker walker(rootType, data);

  // ACT
  auto first = walker.advance();
  auto second = walker.advance();

  // ASSERT
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::SumIndex>(*first));
  EXPECT_EQ(std::get<TypeCheckingWalker::SumIndex>(*first).index, sumIndex);

  ASSERT_FALSE(second.has_value());
}

TEST(TypeCheckingWalker, TestSumVarInt) {
  // ASSIGN
  uint64_t sumIndex = 1;
  uint64_t val = 63557;
  std::vector<uint64_t> data{sumIndex, val};

  types::dy::Unit unit;
  types::dy::VarInt varint;
  std::array<types::dy::Dynamic, 2> elements{unit, varint};
  types::dy::Sum rootType{elements};

  TypeCheckingWalker walker(rootType, data);

  // ACT
  auto first = walker.advance();
  auto second = walker.advance();
  auto third = walker.advance();

  // ASSERT
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::SumIndex>(*first));
  EXPECT_EQ(std::get<TypeCheckingWalker::SumIndex>(*first).index, sumIndex);

  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::VarInt>(*second));
  EXPECT_EQ(std::get<TypeCheckingWalker::VarInt>(*second).value, val);

  ASSERT_FALSE(third.has_value());
}

TEST(TypeCheckingWalker, TestListEmpty) {
  // ASSIGN
  uint64_t listLength = 0;
  std::vector<uint64_t> data{listLength};

  types::dy::VarInt varint;
  types::dy::List rootType{varint};

  TypeCheckingWalker walker(rootType, data);

  // ACT
  auto first = walker.advance();
  auto second = walker.advance();

  // ASSERT
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::ListLength>(*first));
  EXPECT_EQ(std::get<TypeCheckingWalker::ListLength>(*first).length,
            listLength);

  ASSERT_FALSE(second.has_value());
}

TEST(TypeCheckingWalker, TestListSome) {
  // ASSIGN
  std::array<uint64_t, 3> listElements{59942, 44126, 64525};
  std::vector<uint64_t> data{listElements.size(), listElements[0],
                             listElements[1], listElements[2]};

  types::dy::VarInt varint;
  types::dy::List rootType{varint};

  TypeCheckingWalker walker(rootType, data);

  // ACT
  auto first = walker.advance();
  auto second = walker.advance();
  auto third = walker.advance();
  auto fourth = walker.advance();
  auto fifth = walker.advance();

  // ASSERT
  ASSERT_TRUE(first.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::ListLength>(*first));
  EXPECT_EQ(std::get<TypeCheckingWalker::ListLength>(*first).length,
            listElements.size());

  ASSERT_TRUE(second.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::VarInt>(*second));
  EXPECT_EQ(std::get<TypeCheckingWalker::VarInt>(*second).value,
            listElements[0]);

  ASSERT_TRUE(third.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::VarInt>(*third));
  EXPECT_EQ(std::get<TypeCheckingWalker::VarInt>(*third).value,
            listElements[1]);

  ASSERT_TRUE(fourth.has_value());
  ASSERT_TRUE(std::holds_alternative<TypeCheckingWalker::VarInt>(*fourth));
  EXPECT_EQ(std::get<TypeCheckingWalker::VarInt>(*fourth).value,
            listElements[2]);

  ASSERT_FALSE(fifth.has_value());
}
