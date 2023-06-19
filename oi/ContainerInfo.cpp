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
#include "oi/ContainerInfo.h"

#include <glog/logging.h>

#include <map>

#include "oi/support/Toml.h"

namespace fs = std::filesystem;

ContainerTypeEnum containerTypeEnumFromStr(std::string& str) {
  static const std::map<std::string, ContainerTypeEnum> nameMap = {
#define X(name) {#name, name},
      LIST_OF_CONTAINER_TYPES
#undef X
  };

  if (!nameMap.contains(str)) {
    return UNKNOWN_TYPE;
  }
  return nameMap.at(str);
}

const char* containerTypeEnumToStr(ContainerTypeEnum ty) {
  switch (ty) {
#define X(name) \
  case name:    \
    return #name;
    LIST_OF_CONTAINER_TYPES
#undef X

    default:
      return "UNKNOWN_TYPE";
  }
}

[[deprecated]] std::unique_ptr<ContainerInfo> ContainerInfo::loadFromFile(
    const fs::path& path) {
  toml::table container;
  try {
    container = toml::parse_file(std::string(path));
  } catch (const toml::parse_error& ex) {
    LOG(ERROR) << "ContainerInfo::loadFromFile: " << path << " : "
               << ex.description();
    return nullptr;
  }

  toml::table* info = container["info"].as_table();
  if (!info) {
    LOG(ERROR) << "a container info file requires an `info` table";
    return nullptr;
  }

  std::string typeName;
  if (std::optional<std::string> str =
          (*info)["typeName"].value<std::string>()) {
    typeName = std::move(*str);
  } else {
    LOG(ERROR) << "`info.typeName` is a required field";
    return nullptr;
  }

  std::regex matcher;
  if (std::optional<std::string> str =
          (*info)["matcher"].value<std::string>()) {
    matcher = std::regex(*str, std::regex_constants::grep);
  } else {
    matcher = std::regex("^" + typeName, std::regex_constants::grep);
  }

  std::optional<size_t> numTemplateParams =
      (*info)["numTemplateParams"].value<size_t>();

  ContainerTypeEnum ctype;
  if (std::optional<std::string> str = (*info)["ctype"].value<std::string>()) {
    ctype = containerTypeEnumFromStr(*str);
    if (ctype == UNKNOWN_TYPE) {
      LOG(ERROR) << "`" << (*str) << "` is not a valid container type";
      return nullptr;
    }
  } else {
    LOG(ERROR) << "`info.ctype` is a required field";
    return nullptr;
  }

  std::string header;
  if (std::optional<std::string> str = (*info)["header"].value<std::string>()) {
    header = std::move(*str);
  } else {
    LOG(ERROR) << "`info.header` is a required field";
    return nullptr;
  }

  std::vector<std::string> ns;
  if (toml::array* arr = (*info)["ns"].as_array()) {
    ns.reserve(arr->size());
    arr->for_each([&](auto&& el) {
      if constexpr (toml::is_string<decltype(el)>) {
        ns.emplace_back(el);
      }
    });
  }

  std::vector<size_t> replaceTemplateParamIndex{};
  if (toml::array* arr = (*info)["replaceTemplateParamIndex"].as_array()) {
    replaceTemplateParamIndex.reserve(arr->size());
    arr->for_each([&](auto&& el) {
      if constexpr (toml::is_integer<decltype(el)>) {
        replaceTemplateParamIndex.push_back(*el);
      }
    });
  }

  std::optional<size_t> allocatorIndex =
      (*info)["allocatorIndex"].value<size_t>();
  std::optional<size_t> underlyingContainerIndex =
      (*info)["underlyingContainerIndex"].value<size_t>();

  toml::table* codegen = container["codegen"].as_table();
  if (!codegen) {
    LOG(ERROR) << "a container info file requires an `codegen` table";
    return nullptr;
  }

  std::string decl;
  if (std::optional<std::string> str =
          (*codegen)["decl"].value<std::string>()) {
    decl = std::move(*str);
  } else {
    LOG(ERROR) << "`codegen.decl` is a required field";
    return nullptr;
  }

  std::string func;
  if (std::optional<std::string> str =
          (*codegen)["func"].value<std::string>()) {
    func = std::move(*str);
  } else {
    LOG(ERROR) << "`codegen.func` is a required field";
    return nullptr;
  }

  return std::unique_ptr<ContainerInfo>(new ContainerInfo{
      std::move(typeName),
      std::move(matcher),
      numTemplateParams,
      ctype,
      std::move(header),
      std::move(ns),
      std::move(replaceTemplateParamIndex),
      allocatorIndex,
      underlyingContainerIndex,
      {},
      {
          std::move(decl),
          std::move(func),
      },
  });
}

namespace {
/*
 * Create a regex to match a given type name.
 *
 * The type name "name" should match "name" and "name<xxx>".
 */
std::regex getMatcher(const std::string& typeName) {
  return std::regex("^" + typeName + "$|^" + typeName + "<.*>$");
}
}  // namespace

ContainerInfo::ContainerInfo(const fs::path& path) {
  toml::table container;
  try {
    container = toml::parse_file(std::string(path));
  } catch (const toml::parse_error& err) {
    // Convert into a std::runtime_error, just to avoid having to include
    // the huge TOML++ header in the caller's file. Use toml::parse_error's
    // operator<< to generate a pretty message with error location.
    std::stringstream ss;
    ss << err;
    throw std::runtime_error(ss.str());
  }

  if (!container["info"].is_table()) {
    throw std::runtime_error("a container info file requires an `info` table");
  }

  const auto& info = container["info"];

  if (std::optional<std::string> str = info["type_name"].value<std::string>()) {
    typeName = std::move(*str);
  } else {
    throw std::runtime_error("`info.type_name` is a required field");
  }

  matcher = getMatcher(typeName);

  if (std::optional<std::string> str = info["ctype"].value<std::string>()) {
    ctype = containerTypeEnumFromStr(*str);
    if (ctype == UNKNOWN_TYPE) {
      throw std::runtime_error("`" + *str + "` is not a valid container type");
    }
  } else {
    throw std::runtime_error("`info.ctype` is a required field");
  }

  if (std::optional<std::string> str = info["header"].value<std::string>()) {
    header = std::move(*str);
  } else {
    throw std::runtime_error("`info.header` is a required field");
  }

  if (toml::array* arr = info["stub_template_params"].as_array()) {
    stubTemplateParams.reserve(arr->size());
    arr->for_each([&](auto&& el) {
      if constexpr (toml::is_integer<decltype(el)>) {
        stubTemplateParams.push_back(*el);
      } else {
        throw std::runtime_error(
            "stub_template_params should only contain integers");
      }
    });
  }

  underlyingContainerIndex = info["underlying_container_index"].value<size_t>();

  if (!container["codegen"].is_table()) {
    throw std::runtime_error(
        "a container info file requires a `codegen` table");
  }

  const auto& codegenToml = container["codegen"];

  if (std::optional<std::string> str =
          codegenToml["func"].value<std::string>()) {
    codegen.func = std::move(*str);
  } else {
    throw std::runtime_error("`codegen.func` is a required field");
  }
  if (std::optional<std::string> str =
          codegenToml["decl"].value<std::string>()) {
    codegen.decl = std::move(*str);
  } else {
    throw std::runtime_error("`codegen.decl` is a required field");
  }
  if (std::optional<std::string> str =
          codegenToml["handler"].value<std::string>()) {
    codegen.handler = std::move(*str);
  }
}

ContainerInfo::ContainerInfo(std::string typeName_,
                             ContainerTypeEnum ctype_,
                             std::string header_)
    : typeName(std::move(typeName_)),
      matcher(getMatcher(typeName)),
      ctype(ctype_),
      header(std::move(header_)) {
}
