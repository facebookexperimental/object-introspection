#include <gtest/gtest.h>

#include "oi/type_graph/NodeTracker.h"

using namespace oi::detail::type_graph;

TEST(NodeTrackerTest, LeafNodes) {
  Primitive myint32{Primitive::Kind::Int32};
  Primitive myint64{Primitive::Kind::Int64};

  NodeTracker tracker;

  // First visit
  EXPECT_FALSE(tracker.visit(myint32));
  EXPECT_FALSE(tracker.visit(myint64));

  // Second visit
  EXPECT_FALSE(tracker.visit(myint32));
  EXPECT_FALSE(tracker.visit(myint64));
}

TEST(NodeTrackerTest, Basic) {
  Class myclass{0, Class::Kind::Class, "myclass", 0};
  Array myarray{1, myclass, 3};

  NodeTracker tracker;

  // First visit
  EXPECT_FALSE(tracker.visit(myarray));
  EXPECT_FALSE(tracker.visit(myclass));

  // Second visit
  EXPECT_TRUE(tracker.visit(myarray));
  EXPECT_TRUE(tracker.visit(myclass));

  // Third visit
  EXPECT_TRUE(tracker.visit(myarray));
  EXPECT_TRUE(tracker.visit(myclass));
}

TEST(NodeTrackerTest, Clear) {
  Class myclass{0, Class::Kind::Class, "myclass", 0};
  Array myarray{1, myclass, 3};

  NodeTracker tracker;

  // First visit
  EXPECT_FALSE(tracker.visit(myarray));
  EXPECT_FALSE(tracker.visit(myclass));

  // Second visit
  EXPECT_TRUE(tracker.visit(myarray));
  EXPECT_TRUE(tracker.visit(myclass));

  // Reset
  tracker.reset();

  // First visit
  EXPECT_FALSE(tracker.visit(myarray));
  EXPECT_FALSE(tracker.visit(myclass));

  // Second visit
  EXPECT_TRUE(tracker.visit(myarray));
  EXPECT_TRUE(tracker.visit(myclass));
}

TEST(NodeTrackerTest, LargeIds) {
  Class myclass1{100, Class::Kind::Class, "myclass1", 0};
  Class myclass2{100000, Class::Kind::Class, "myclass2", 0};

  NodeTracker tracker;

  // First visit
  EXPECT_FALSE(tracker.visit(myclass1));
  EXPECT_FALSE(tracker.visit(myclass2));

  // Second visit
  EXPECT_TRUE(tracker.visit(myclass1));
  EXPECT_TRUE(tracker.visit(myclass2));

  // Third visit
  EXPECT_TRUE(tracker.visit(myclass1));
  EXPECT_TRUE(tracker.visit(myclass2));
}

TEST(NodeTrackerTest, NodeTrackerHolder) {
  Class myclass{0, Class::Kind::Class, "myclass", 0};
  Array myarray{1, myclass, 3};

  NodeTracker baseTracker_doNotUse;
  NodeTrackerHolder holder{baseTracker_doNotUse};

  {
    auto& tracker = holder.get();
    // First visit
    EXPECT_FALSE(tracker.visit(myarray));
    EXPECT_FALSE(tracker.visit(myclass));

    // Second visit
    EXPECT_TRUE(tracker.visit(myarray));
    EXPECT_TRUE(tracker.visit(myclass));
  }

  {
    auto& tracker = holder.get();
    // First visit, fresh tracker
    EXPECT_FALSE(tracker.visit(myarray));
    EXPECT_FALSE(tracker.visit(myclass));

    // Second visit, fresh tracker
    EXPECT_TRUE(tracker.visit(myarray));
    EXPECT_TRUE(tracker.visit(myclass));
  }
}
