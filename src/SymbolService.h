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
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <variant>
#include <vector>

#include "Common.h"
#include "Descs.h"

namespace fs = std::filesystem;

struct drgn_program;
struct irequest;

struct SymbolInfo {
  uint64_t addr;
  uint64_t size;
};

class SymbolService {
 public:
  SymbolService(std::variant<pid_t, fs::path>);
  ~SymbolService();

  struct drgn_program *getDrgnProgram();

  std::optional<std::string> locateBuildID();
  std::optional<SymbolInfo> locateSymbol(const std::string &,
                                         bool demangle = false);

  std::shared_ptr<FuncDesc> findFuncDesc(const irequest &);
  std::shared_ptr<GlobalDesc> findGlobalDesc(const std::string &);
  static std::string getTypeName(struct drgn_type *);
  std::optional<RootInfo> getRootType(const irequest &);

  std::unordered_map<std::string, std::shared_ptr<FuncDesc>> funcDescs;
  std::unordered_map<std::string, std::shared_ptr<GlobalDesc>> globalDescs;

  void setHardDisableDrgn(bool val) {
    hardDisableDrgn = val;
  }

 private:
  std::variant<pid_t, fs::path> target{0};
  struct drgn_program *prog{nullptr};

  std::vector<std::pair<uint64_t, uint64_t>> executableAddrs{};
  bool hardDisableDrgn = false;
};
