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

#include <array>
#include <filesystem>
#include <memory>
#include <optional>

#include "OICodeGen.h"
#include "OIParser.h"
#include "SymbolService.h"

namespace fs = std::filesystem;

class OICache {
 public:
  fs::path basePath{};
  std::shared_ptr<SymbolService> symbols{};
  bool downloadedRemote = false;
  bool enableUpload = false;
  bool enableDownload = false;
  bool abortOnLoadFail = false;

  // We need the generator config to download the cache
  // with the matching configuration.
  OICodeGen::Config generatorConfig{};

  // Entity is used to index the `extensions` array
  // So we must keep the Entity enum and `extensions` array in sync!
  enum class Entity {
    Source,
    Object,
    FuncDescs,
    GlobalDescs,
    TypeHierarchy,
    PaddingInfo,
    MAX
  };
  static constexpr std::array<const char *, static_cast<size_t>(Entity::MAX)>
      extensions{".cc", ".o", ".fd", ".gd", ".th", ".pd"};

  bool isEnabled() const {
    return !basePath.empty();
  }
  std::optional<fs::path> getPath(const irequest &, Entity) const;
  template <typename T>
  bool store(const irequest &, Entity, const T &);
  template <typename T>
  bool load(const irequest &, Entity, T &);

  bool upload(const irequest &req);
  bool download(const irequest &req);

 private:
  std::string generateRemoteHash(const irequest &);
};
