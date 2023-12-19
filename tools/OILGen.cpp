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

#include <clang/Tooling/CommonOptionsParser.h>
#include <gflags/gflags.h>
#include <glog/logging.h>

#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <range/v3/range/conversion.hpp>
#include <range/v3/view/transform.hpp>
#include <vector>

#include "oi/OICodeGen.h"
#include "oi/OIGenerator.h"

namespace fs = std::filesystem;
using namespace oi::detail;

static llvm::cl::OptionCategory OilgenCategory("oilgen options");

static llvm::cl::list<std::string> ConfigFiles(
    "config-file",
    llvm::cl::desc(R"(</path/to/oid.toml>)"),
    llvm::cl::cat(OilgenCategory));
static llvm::cl::opt<std::string> OutputFile(
    "output",
    llvm::cl::desc(R"(Write output to this file.)"),
    llvm::cl::init("a.o"),
    llvm::cl::cat(OilgenCategory));
static llvm::cl::opt<int> DebugLevel(
    "debug-level",
    llvm::cl::desc(R"(Verbose level for logging.)"),
    llvm::cl::init(-1),
    llvm::cl::cat(OilgenCategory));
static llvm::cl::opt<std::string> DumpJit(
    "dump-jit",
    llvm::cl::desc(R"(Write the generated code to a file.)"),
    llvm::cl::init("jit.cpp"),
    llvm::cl::cat(OilgenCategory));

int main(int argc, const char* argv[]) {
  google::InitGoogleLogging(argv[0]);
  FLAGS_minloglevel = 0;
  FLAGS_stderrthreshold = 0;

  auto expectedParser =
      clang::tooling::CommonOptionsParser::create(argc, argv, OilgenCategory);
  if (!expectedParser) {
    llvm::errs() << expectedParser.takeError();
    return -1;
  }
  clang::tooling::CommonOptionsParser& options = expectedParser.get();
  auto& compilations = options.getCompilations();

  if (DebugLevel.getNumOccurrences()) {
    google::LogToStderr();
    google::SetStderrLogging(google::INFO);
    google::SetVLOGLevel("*", DebugLevel);
    // Upstream glog defines `GLOG_INFO` as 0 https://fburl.com/ydjajhz0,
    // but internally it's defined as 1 https://fburl.com/code/9fwams75
    gflags::SetCommandLineOption("minloglevel", "0");
  }

  OIGenerator oigen;

  oigen.setConfigFilePaths(ConfigFiles |
                           ranges::views::transform([](const auto& p) {
                             return std::filesystem::path(p);
                           }) |
                           ranges::to<std::vector>());
  if (DumpJit.getNumOccurrences())
    oigen.setSourceFileDumpPath(DumpJit.getValue());

  oigen.setOutputPath(OutputFile.getValue());

  oigen.setFailIfNothingGenerated(true);
  return oigen.generate(compilations, options.getSourcePathList());
}
