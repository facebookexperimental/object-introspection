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
#include <optional>
#include <regex>
#include <set>
#include <string>
#include <vector>

#include "oi/ContainerTypeEnum.h"
#include "oi/Features.h"

ContainerTypeEnum containerTypeEnumFromStr(std::string& str);
const char* containerTypeEnumToStr(ContainerTypeEnum ty);

struct ContainerInfo {
  struct Processor {
    std::string type;
    std::string func;
  };

  struct Codegen {
    std::string decl;
    std::string func;
    std::string traversalFunc = "";
    std::string extra = "";
    std::vector<Processor> processors{};
  };

  explicit ContainerInfo(const std::filesystem::path& path);  // Throws
  ContainerInfo(std::string typeName,
                ContainerTypeEnum ctype,
                std::string header);

  // Old ctors, remove with OICodeGen:
  ContainerInfo() = default;
  ContainerInfo(std::string typeName_,
                std::regex matcher_,
                std::optional<size_t> numTemplateParams_,
                ContainerTypeEnum ctype_,
                std::string header_,
                std::vector<std::string> ns_,
                std::vector<size_t> replaceTemplateParamIndex_,
                std::optional<size_t> allocatorIndex_,
                std::optional<size_t> underlyingContainerIndex_,
                std::vector<size_t> stubTemplateParams_,
                oi::detail::FeatureSet requiredFeatures,
                ContainerInfo::Codegen codegen_)
      : typeName(std::move(typeName_)),
        matcher(std::move(matcher_)),
        numTemplateParams(numTemplateParams_),
        ctype(ctype_),
        header(std::move(header_)),
        ns(std::move(ns_)),
        replaceTemplateParamIndex(std::move(replaceTemplateParamIndex_)),
        allocatorIndex(allocatorIndex_),
        underlyingContainerIndex(underlyingContainerIndex_),
        stubTemplateParams(std::move(stubTemplateParams_)),
        requiredFeatures(requiredFeatures),
        codegen(std::move(codegen_)) {
  }

  ContainerInfo(ContainerInfo&&) = default;
  ContainerInfo& operator=(ContainerInfo&&) = default;

  // Explicit interface for copying
  ContainerInfo clone() const {
    ContainerInfo copy{*this};
    return copy;
  }

  std::string typeName;
  std::regex matcher;
  std::optional<size_t> numTemplateParams;
  ContainerTypeEnum ctype = UNKNOWN_TYPE;
  std::string header;
  std::vector<std::string> ns;
  std::vector<size_t> replaceTemplateParamIndex{};
  std::optional<size_t> allocatorIndex{};
  // Index of underlying container in template parameters for a container
  // adapter
  std::optional<size_t> underlyingContainerIndex{};
  std::vector<size_t> stubTemplateParams{};
  bool captureKeys = false;
  oi::detail::FeatureSet requiredFeatures;

  Codegen codegen;

  static std::unique_ptr<ContainerInfo> loadFromFile(
      const std::filesystem::path& path);

  bool operator<(const ContainerInfo& rhs) const {
    return (typeName < rhs.typeName);
  }

 private:
  ContainerInfo(const ContainerInfo&) = default;
  ContainerInfo& operator=(const ContainerInfo& other) = default;
};

class ContainerInfoError : public std::runtime_error {
 public:
  ContainerInfoError(const std::filesystem::path& path, const std::string& msg)
      : std::runtime_error{std::string{path} + ": " + msg} {
  }
};

using ContainerInfoRefSet =
    std::set<std::reference_wrapper<const ContainerInfo>,
             std::less<ContainerInfo>>;
