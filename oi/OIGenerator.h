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

#pragma once

#include <filesystem>
#include <vector>

#include "oi/DrgnUtils.h"
#include "oi/OICodeGen.h"
#include "oi/OICompiler.h"

namespace clang::tooling {
class CompilationDatabase;
}

namespace oi::detail {
namespace type_graph {
class Type;
}

class OIGenerator {
 public:
  int generate(clang::tooling::CompilationDatabase&,
               const std::vector<std::string>&);

  void setOutputPath(fs::path _outputPath) {
    outputPath = std::move(_outputPath);
  }
  void setConfigFilePaths(std::vector<fs::path> _configFilePaths) {
    configFilePaths = std::move(_configFilePaths);
  }
  void setSourceFileDumpPath(fs::path _sourceFileDumpPath) {
    sourceFileDumpPath = std::move(_sourceFileDumpPath);
  }
  void setFailIfNothingGenerated(bool fail) {
    failIfNothingGenerated = fail;
  }
  void setClangArgs(std::vector<std::string> args_) {
    clangArgs = std::move(args_);
  }

 private:
  std::filesystem::path outputPath;
  std::vector<std::filesystem::path> configFilePaths;
  std::filesystem::path sourceFileDumpPath;
  bool failIfNothingGenerated = false;
  std::vector<std::string> clangArgs;
};

}  // namespace oi::detail
