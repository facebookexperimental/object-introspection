/*
 * Copyright (c) Meta Platforms, Inc. and affiliates.
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "oi/OIGenerator.h"

#include <clang/AST/Mangle.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/CompilerInvocation.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Sema/Sema.h>
#include <clang/Tooling/Tooling.h>
#include <glog/logging.h>

#include <fstream>
#include <range/v3/core.hpp>
#include <range/v3/view/drop.hpp>
#include <range/v3/view/filter.hpp>
#include <range/v3/view/for_each.hpp>
#include <range/v3/view/take.hpp>
#include <range/v3/view/transform.hpp>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <variant>

#include "oi/CodeGen.h"
#include "oi/Config.h"
#include "oi/Headers.h"
#include "oi/type_graph/ClangTypeParser.h"
#include "oi/type_graph/TypeGraph.h"
#include "oi/type_graph/Types.h"

namespace oi::detail {
namespace {

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
  ConsumerContext(const std::vector<std::unique_ptr<ContainerInfo>>& cis)
      : containerInfos{cis} {
  }

  type_graph::TypeGraph typeGraph;
  std::unordered_map<std::string, type_graph::Type*> nameToTypeMap;
  std::optional<bool> pic;
  const std::vector<std::unique_ptr<ContainerInfo>>& containerInfos;
  std::set<std::string_view> typesToStub;
  std::set<std::string_view> mustProcessTemplateParams;

 private:
  clang::Sema* sema = nullptr;
  friend CreateTypeGraphConsumer;
  friend CreateTypeGraphAction;
};

}  // namespace

int OIGenerator::generate(clang::tooling::CompilationDatabase& db,
                          const std::vector<std::string>& sourcePaths) {
  std::map<Feature, bool> featuresMap = {
      {Feature::TypeGraph, true},
      {Feature::TreeBuilderV2, true},
      {Feature::Library, true},
      {Feature::PackStructs, true},
      {Feature::PruneTypeGraph, true},
  };

  OICodeGen::Config generatorConfig{};
  OICompiler::Config compilerConfig{};

  auto features = config::processConfigFiles(
      configFilePaths, featuresMap, compilerConfig, generatorConfig);
  if (!features) {
    LOG(ERROR) << "failed to process config file";
    return -1;
  }
  generatorConfig.features = *features;
  compilerConfig.features = *features;

  std::vector<std::unique_ptr<ContainerInfo>> containerInfos;
  containerInfos.reserve(generatorConfig.containerConfigPaths.size());
  try {
    for (const auto& path : generatorConfig.containerConfigPaths) {
      auto info = std::make_unique<ContainerInfo>(path);
      if (info->requiredFeatures != (*features & info->requiredFeatures)) {
        VLOG(1) << "Skipping container (feature conflict): " << info->typeName;
        continue;
      }
      containerInfos.emplace_back(std::move(info));
    }
  } catch (const ContainerInfoError& err) {
    LOG(ERROR) << "Error reading container TOML file " << err.what();
    return -1;
  }

  ConsumerContext ctx{containerInfos};

  for (const auto& [stubType, stubMember] : generatorConfig.membersToStub) {
    if (stubMember == "*")
      ctx.typesToStub.insert(stubType);
  }

  for (const auto& cInfo : generatorConfig.passThroughTypes)
    ctx.mustProcessTemplateParams.insert(cInfo.typeName);

  CreateTypeGraphActionFactory factory{ctx};

  clang::tooling::ClangTool tool{db, sourcePaths};
  if (auto ret = tool.run(&factory); ret != 0) {
    return ret;
  }

  if (ctx.nameToTypeMap.size() > 1)
    throw std::logic_error(
        "found more than one site to generate for but we can't currently "
        "handle this case");

  if (ctx.nameToTypeMap.empty()) {
    LOG(ERROR) << "Nothing to generate!";
    return failIfNothingGenerated ? -1 : 0;
  }
  const auto& linkageName = ctx.nameToTypeMap.begin()->first;

  compilerConfig.usePIC = ctx.pic.value();
  CodeGen codegen{generatorConfig};
  for (auto&& ptr : containerInfos)
    codegen.registerContainer(std::move(ptr));
  codegen.transform(ctx.typeGraph);

  std::string code;
  codegen.generate(ctx.typeGraph, code, CodeGen::ExactName{linkageName});

  std::string sourcePath = sourceFileDumpPath;
  if (sourceFileDumpPath.empty()) {
    // This is the path Clang acts as if it has compiled from e.g. for debug
    // information. It does not need to exist.
    sourcePath = "oil_jit.cpp";
  } else {
    std::ofstream outputFile(sourcePath);
    outputFile << code;
  }

  OICompiler compiler{{}, compilerConfig};
  return compiler.compile(code, sourcePath, outputPath) ? 0 : -1;
}

namespace {

class CreateTypeGraphConsumer : public clang::ASTConsumer {
 private:
  ConsumerContext& ctx;

 public:
  CreateTypeGraphConsumer(ConsumerContext& ctx_) : ctx(ctx_) {
  }

  void HandleTranslationUnit(clang::ASTContext& Context) override {
    auto* tu_decl = Context.getTranslationUnitDecl();
    auto decls = tu_decl->decls();
    auto oi_namespaces = decls | ranges::views::transform([](auto* p) {
                           return llvm::dyn_cast<clang::NamespaceDecl>(p);
                         }) |
                         ranges::views::filter([](auto* ns) {
                           return ns != nullptr && ns->getName() == "oi";
                         });
    if (oi_namespaces.empty()) {
      LOG(WARNING) << "Failed to find `oi` namespace. Does this input "
                      "include <oi/oi.h>?";
      return;
    }

    auto introspectImpl =
        std::move(oi_namespaces) |
        ranges::views::for_each([](auto* ns) { return ns->decls(); }) |
        ranges::views::transform([](auto* p) {
          return llvm::dyn_cast<clang::FunctionTemplateDecl>(p);
        }) |
        ranges::views::filter([](auto* td) {
          return td != nullptr && td->getName() == "introspectImpl";
        }) |
        ranges::views::take(1) | ranges::to<std::vector>();
    if (introspectImpl.empty()) {
      LOG(WARNING)
          << "Failed to find `oi::introspect` within the `oi` namespace. Did "
             "you compile with `OIL_AOT_COMPILATION=1`?";
      return;
    }

    auto nameToClangTypeMap =
        introspectImpl | ranges::views::for_each([](auto* td) {
          return td->specializations();
        }) |
        ranges::views::transform(
            [](auto* p) { return llvm::dyn_cast<clang::FunctionDecl>(p); }) |
        ranges::views::filter([](auto* p) { return p != nullptr; }) |
        ranges::views::transform(
            [](auto* fd) -> std::pair<std::string, const clang::Type*> {
              clang::ASTContext& Ctx = fd->getASTContext();
              clang::ASTNameGenerator ASTNameGen(Ctx);
              std::string name = ASTNameGen.getName(fd);

              assert(fd->getNumParams() == 1);
              const clang::Type* type =
                  fd->parameters()[0]->getType().getTypePtr();
              return {name, type};
            }) |
        ranges::to<std::unordered_map>();
    if (nameToClangTypeMap.empty())
      return;

    type_graph::ClangTypeParserOptions opts;
    opts.typesToStub = ctx.typesToStub;
    opts.mustProcessTemplateParams = ctx.mustProcessTemplateParams;

    type_graph::ClangTypeParser parser{ctx.typeGraph, ctx.containerInfos, opts};

    auto& Sema = *ctx.sema;
    auto els = nameToClangTypeMap |
               ranges::views::transform(
                   [&parser, &Context, &Sema](
                       auto& p) -> std::pair<std::string, type_graph::Type*> {
                     return {p.first, &parser.parse(Context, Sema, *p.second)};
                   });
    ctx.nameToTypeMap.insert(els.begin(), els.end());

    for (const auto& [name, type] : ctx.nameToTypeMap)
      ctx.typeGraph.addRoot(*type);
  }
};

void CreateTypeGraphAction::ExecuteAction() {
  clang::CompilerInstance& CI = getCompilerInstance();

  // Compile the output as position independent if any input is position
  // independent
  bool pic = CI.getCodeGenOpts().RelocationModel == llvm::Reloc::PIC_;
  ctx.pic = ctx.pic.value_or(false) || pic;

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

}  // namespace
}  // namespace oi::detail
