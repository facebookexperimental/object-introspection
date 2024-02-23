#include <clang/AST/Mangle.h>
#include <clang/AST/QualTypeNames.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Sema/Lookup.h>
#include <clang/Sema/Sema.h>
#include <clang/Tooling/CompilationDatabase.h>
#include <clang/Tooling/Tooling.h>
#include <gtest/gtest.h>

#include <range/v3/core.hpp>
#include <range/v3/view/drop.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/for_each.hpp>
#include <range/v3/view/take.hpp>
#include <range/v3/view/transform.hpp>
#include <string>
#include <string_view>

#include "oi/type_graph/ClangTypeParser.h"
#include "oi/type_graph/Printer.h"

using namespace oi::detail;
using namespace oi::detail::type_graph;

class ClangTypeParserTest : public ::testing::Test {
 public:
  std::string run(std::string_view function, ClangTypeParserOptions opt);
  void test(std::string_view function,
            std::string_view expected,
            ClangTypeParserOptions opts = {
                .chaseRawPointers = true,
                .readEnumValues = true,
            });
};

// This stuff is a total mess. Set up factories as ClangTooling expects them.
class ConsumerContext;
class CreateTypeGraphConsumer;

class CreateTypeGraphAction : public clang::ASTFrontendAction {
 public:
  CreateTypeGraphAction(ConsumerContext& ctx_) : ctx{ctx_} {
  }

  void ExecuteAction() override;
  std::unique_ptr<clang::ASTConsumer> CreateASTConsumer(
      clang::CompilerInstance& CI, clang::StringRef file) override;

 private:
  ConsumerContext& ctx;
};

class CreateTypeGraphActionFactory
    : public clang::tooling::FrontendActionFactory {
 public:
  CreateTypeGraphActionFactory(ConsumerContext& ctx_) : ctx{ctx_} {
  }

  std::unique_ptr<clang::FrontendAction> create() override {
    return std::make_unique<CreateTypeGraphAction>(ctx);
  }

 private:
  ConsumerContext& ctx;
};

class ConsumerContext {
 public:
  ConsumerContext(
      const std::vector<std::unique_ptr<ContainerInfo>>& containerInfos_)
      : containerInfos{containerInfos_} {
  }
  std::string_view fullyQualifiedName;
  ClangTypeParserOptions opts;
  const std::vector<std::unique_ptr<ContainerInfo>>& containerInfos;
  TypeGraph typeGraph;
  Type* result = nullptr;

 private:
  clang::Sema* sema = nullptr;
  friend CreateTypeGraphConsumer;
  friend CreateTypeGraphAction;
};

class CreateTypeGraphConsumer : public clang::ASTConsumer {
 private:
  ConsumerContext& ctx;

 public:
  CreateTypeGraphConsumer(ConsumerContext& ctx_) : ctx{ctx_} {
  }

  void HandleTranslationUnit(clang::ASTContext& Context) override {
    const clang::Type* type = nullptr;
    for (const clang::Type* ty : Context.getTypes()) {
      std::string fqnWithoutTemplateParams;
      switch (ty->getTypeClass()) {
        case clang::Type::Record:
          fqnWithoutTemplateParams = llvm::cast<const clang::RecordType>(ty)
                                         ->getDecl()
                                         ->getQualifiedNameAsString();
          break;
        default:
          continue;
      }
      if (fqnWithoutTemplateParams != ctx.fullyQualifiedName)
        continue;

      type = ty;
      break;
    }
    EXPECT_NE(type, nullptr);

    ClangTypeParser parser{ctx.typeGraph, ctx.containerInfos, ctx.opts};
    ctx.result = &parser.parse(Context, *ctx.sema, *type);
  }
};

void CreateTypeGraphAction::ExecuteAction() {
  clang::CompilerInstance& CI = getCompilerInstance();

  if (!CI.hasSema())
    CI.createSema(clang::TU_Complete, nullptr);
  ctx.sema = &CI.getSema();

  clang::ASTFrontendAction::ExecuteAction();
}

std::unique_ptr<clang::ASTConsumer> CreateTypeGraphAction::CreateASTConsumer(
    [[maybe_unused]] clang::CompilerInstance& CI,
    [[maybe_unused]] clang::StringRef file) {
  return std::make_unique<CreateTypeGraphConsumer>(ctx);
}

std::string ClangTypeParserTest::run(std::string_view type,
                                     ClangTypeParserOptions opts) {
  std::string err{"failed to load compilation database"};
  auto db =
      clang::tooling::CompilationDatabase::loadFromDirectory(BUILD_DIR, err);
  if (!db) {
    throw std::runtime_error("failed to load compilation database");
  }

  std::vector<std::unique_ptr<ContainerInfo>> cis;
  ConsumerContext ctx{cis};
  ctx.fullyQualifiedName = type;
  ctx.opts = std::move(opts);
  CreateTypeGraphActionFactory factory{ctx};

  std::vector<std::string> sourcePaths{TARGET_CPP_PATH};
  clang::tooling::ClangTool tool{*db, sourcePaths};
  if (auto retCode = tool.run(&factory); retCode != 0) {
    throw std::runtime_error("clang type parsing failed");
  }

  std::stringstream out;
  NodeTracker tracker;
  Printer printer{out, tracker, ctx.typeGraph.size()};
  printer.print(*ctx.result);

  return std::move(out).str();
}

void ClangTypeParserTest::test(std::string_view type,
                               std::string_view expected,
                               ClangTypeParserOptions opts) {
  std::string actual = run(type, std::move(opts));
  expected.remove_prefix(1);  // Remove initial '\n'
  EXPECT_EQ(expected, actual);
}

TEST_F(ClangTypeParserTest, SimpleStruct) {
  test("ns_simple::SimpleStruct", R"(
[0] Struct: SimpleStruct [ns_simple::SimpleStruct] (size: 16, align: 8)
      Member: a (offset: 0)
        Primitive: int32_t
      Member: b (offset: 4)
        Primitive: int8_t
      Member: c (offset: 8)
        Primitive: int64_t
)");
}

TEST_F(ClangTypeParserTest, MemberAlignment) {
  test("ns_alignment::MemberAlignment", R"(
[0] Struct: MemberAlignment [ns_alignment::MemberAlignment] (size: 64, align: 32)
      Member: c (offset: 0)
        Primitive: int8_t
      Member: c32 (offset: 32, align: 32)
        Primitive: int8_t
)");
}
