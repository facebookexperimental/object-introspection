#pragma once

#include <gmock/gmock.h>

#include "oi/type_graph/DrgnParser.h"

namespace oi::detail {
class SymbolService;
}
namespace oi::detail::type_graph {
class TypeGraph;
}
struct drgn_type;

using namespace oi::detail;

class DrgnParserTest : public ::testing::Test {
 protected:
  static void SetUpTestSuite() {
    symbols_ = new SymbolService{TARGET_EXE_PATH};
  }

  static void TearDownTestSuite() {
    delete symbols_;
  }

  static type_graph::DrgnParser getDrgnParser(type_graph::TypeGraph& typeGraph,
                                              bool chaseRawPointers);
  drgn_type* getDrgnRoot(std::string_view function);

  virtual std::string run(std::string_view function, bool chaseRawPointers);
  void test(std::string_view function,
            std::string_view expected,
            bool chaseRawPointers = true);
  void testContains(std::string_view function,
                    std::string_view expected,
                    bool chaseRawPointers = true);
  void testMultiCompiler(std::string_view function,
                         std::string_view expectedClang,
                         std::string_view expectedGcc,
                         bool chaseRawPointers = true);
  void testMultiCompilerContains(std::string_view function,
                                 std::string_view expectedClang,
                                 std::string_view expectedGcc,
                                 bool chaseRawPointers = true);

  static SymbolService* symbols_;
};
