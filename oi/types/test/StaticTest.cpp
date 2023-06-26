#include <gtest/gtest.h>

#define DEFINE_DESCRIBE 1
#include "oi/types/dy.h"
#include "oi/types/st.h"

using namespace ObjectIntrospection;

class DummyDataBuffer {};

TEST(StaticTypes, TestUnitToDynamic) {
  // ASSIGN
  using ty = types::st::Unit<DummyDataBuffer>;

  // ACT
  types::dy::Dynamic dynamicType = ty::describe;

  // ASSERT
  ASSERT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::Unit>>(
          dynamicType));
}

TEST(StaticTypes, TestVarIntToDynamic) {
  // ASSIGN
  using ty = types::st::VarInt<DummyDataBuffer>;

  // ACT
  types::dy::Dynamic dynamicType = ty::describe;

  // ASSERT
  ASSERT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::VarInt>>(
          dynamicType));
}

TEST(StaticTypes, TestPairToDynamic) {
  // ASSIGN
  using left = types::st::VarInt<DummyDataBuffer>;
  using right = types::st::VarInt<DummyDataBuffer>;
  using ty = types::st::Pair<DummyDataBuffer, left, right>;

  // ACT
  types::dy::Dynamic dynamicType = ty::describe;

  // ASSERT
  ASSERT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::Pair>>(
          dynamicType));
  const types::dy::Pair& pairType =
      std::get<std::reference_wrapper<const types::dy::Pair>>(dynamicType);

  EXPECT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::VarInt>>(
          pairType.first));
  EXPECT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::VarInt>>(
          pairType.second));
}

TEST(StaticTypes, TestSumToDynamic) {
  // ASSIGN
  using left = types::st::Unit<DummyDataBuffer>;
  using right = types::st::VarInt<DummyDataBuffer>;
  using ty = types::st::Sum<DummyDataBuffer, left, right>;

  // ACT
  types::dy::Dynamic dynamicType = ty::describe;

  // ASSERT
  ASSERT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::Sum>>(
          dynamicType));
  const types::dy::Sum& sumType =
      std::get<std::reference_wrapper<const types::dy::Sum>>(dynamicType);

  ASSERT_EQ(sumType.variants.size(), 2);
  EXPECT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::Unit>>(
          sumType.variants[0]));
  EXPECT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::VarInt>>(
          sumType.variants[1]));
}

TEST(StaticTypes, TestListToDynamic) {
  // ASSIGN
  using el = types::st::VarInt<DummyDataBuffer>;
  using ty = types::st::List<DummyDataBuffer, el>;

  // ACT
  types::dy::Dynamic dynamicType = ty::describe;

  // ASSERT
  ASSERT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::List>>(
          dynamicType));
  const types::dy::List& listType =
      std::get<std::reference_wrapper<const types::dy::List>>(dynamicType);

  EXPECT_TRUE(
      std::holds_alternative<std::reference_wrapper<const types::dy::VarInt>>(
          listType.element));
}
