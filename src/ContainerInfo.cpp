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
#include "ContainerInfo.h"

#include <glog/logging.h>
#include <toml++/toml.h>

#include <map>

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

std::unique_ptr<ContainerInfo> ContainerInfo::loadFromFile(
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
  });
}
