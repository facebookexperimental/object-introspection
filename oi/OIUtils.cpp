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
#include "oi/OIUtils.h"

#include <glog/logging.h>

#include <bitset>
#include <chrono>
#include <filesystem>
#include <ostream>
#include <set>
#include <string_view>
#include <utility>
#include <vector>

#include "oi/ContainerInfo.h"
#include "oi/ContainerTypeEnum.h"
#include "oi/support/Toml.h"

namespace fs = std::filesystem;

namespace OIUtils {

using namespace ObjectIntrospection;
using namespace std::literals;

std::optional<ObjectIntrospection::FeatureSet> processConfigFile(
    const std::string& configFilePath,
    std::map<Feature, bool> featureMap,
    OICompiler::Config& compilerConfig,
    OICodeGen::Config& generatorConfig) {
  fs::path configDirectory = fs::path(configFilePath).remove_filename();

  toml::table config;
  try {
    config = toml::parse_file(configFilePath);
  } catch (const toml::parse_error& ex) {
    LOG(ERROR) << "processConfigFileToml: " << configFilePath << " : "
               << ex.description();
    return {};
  }

  if (toml::array* features = config["features"].as_array()) {
    for (auto&& el : *features) {
      auto* featureStr = el.as_string();
      if (!featureStr) {
        LOG(ERROR) << "enabled features must be strings";
        return {};
      }

      if (auto f = featureFromStr(featureStr->get());
          f != Feature::UnknownFeature) {
        // Inserts element(s) into the container, if the container doesn't
        // already contain an element with an equivalent key. Hence prefer
        // command line enabling/disabling.
        featureMap.insert({f, true});
      } else {
        LOG(ERROR) << "unrecognised feature: " << featureStr->get()
                   << " specified in config";
        return {};
      }
    }
  }

  if (toml::table* types = config["types"].as_table()) {
    if (toml::array* arr = (*types)["containers"].as_array()) {
      arr->for_each([&](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
          /*
          The / operator on std::filesystem::path will choose the right path
          if the right path is absolute, else append the right path to the
          left path.
          */
          generatorConfig.containerConfigPaths.emplace(configDirectory /
                                                       el.get());
        }
      });
    }
    if (toml::array* arr = (*types)["pass_through"].as_array()) {
      for (auto&& el : *arr) {
        auto* type = el.as_array();
        if (type && type->size() == 2 && (*type)[0].is_string() &&
            (*type)[1].is_string()) {
          std::string name = (*type)[0].as_string()->get();
          std::string header = (*type)[1].as_string()->get();
          generatorConfig.passThroughTypes.emplace_back(
              std::move(name), DUMMY_TYPE, std::move(header));
        } else {
          LOG(ERROR) << "pass_through elements must be lists of [type_name, "
                        "header_file]";
          return {};
        }
      }
    } else {
      LOG(ERROR) << "pass_through must be a list";
      return {};
    }
  }

  if (toml::table* headers = config["headers"].as_table()) {
    if (toml::array* arr = (*headers)["user_paths"].as_array()) {
      arr->for_each([&](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
          /*
          The / operator on std::filesystem::path will choose the right path
          if the right path is absolute, else append the right path to the
          left path.
          */
          compilerConfig.userHeaderPaths.emplace_back(configDirectory /
                                                      el.get());
        }
      });
    }
    if (toml::array* arr = (*headers)["system_paths"].as_array()) {
      arr->for_each([&](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
          /*
          The / operator on std::filesystem::path will choose the right path
          if the right path is absolute, else append the right path to the
          left path.
          */
          compilerConfig.sysHeaderPaths.emplace_back(configDirectory /
                                                     el.get());
        }
      });
    }
  }

  if (toml::table* codegen = config["codegen"].as_table()) {
    if (toml::array* arr = (*codegen)["default_headers"].as_array()) {
      arr->for_each([&](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
          generatorConfig.defaultHeaders.emplace(el);
        }
      });
    }
    if (toml::array* arr = (*codegen)["default_namespaces"].as_array()) {
      arr->for_each([&](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
          generatorConfig.defaultNamespaces.emplace(el);
        }
      });
    }
    if (toml::array* arr = (*codegen)["ignore"].as_array()) {
      for (auto&& el : *arr) {
        if (toml::table* ignore = el.as_table()) {
          auto* type = (*ignore)["type"].as_string();
          if (!type) {
            LOG(ERROR) << "Config entry 'ignore' must specify a type";
            return {};
          }

          auto* members = (*ignore)["members"].as_array();
          if (!members) {
            generatorConfig.membersToStub.emplace_back(type->value_or(""sv),
                                                       "*"sv);
          } else {
            for (auto&& member : *members) {
              generatorConfig.membersToStub.emplace_back(type->value_or(""sv),
                                                         member.value_or(""sv));
            }
          }
        }
      }
    }
  }

  ObjectIntrospection::FeatureSet featuresSet;
  for (auto [k, v] : featureMap) {
    if (v) {
      featuresSet[k] = true;
    }
  }

  if (featuresSet[Feature::TreeBuilderTypeChecking] &&
      !featuresSet[Feature::TypedDataSegment]) {
    if (auto search = featureMap.find(Feature::TypedDataSegment);
        search != featureMap.end() && !search->second) {
      LOG(ERROR) << "TreeBuilderTypeChecking feature requires TypedDataSegment "
                    "feature to be enabled but it was explicitly disabled!";
      return {};
    }
    featuresSet[Feature::TypedDataSegment] = true;
    LOG(WARNING) << "TreeBuilderTypeChecking feature requires TypedDataSegment "
                    "feature to be enabled, enabling now.";
  }
  if (featuresSet[Feature::TypedDataSegment] &&
      !featuresSet[Feature::TypeGraph]) {
    if (auto search = featureMap.find(Feature::TypeGraph);
        search != featureMap.end() && !search->second) {
      LOG(ERROR) << "TypedDataSegment feature requires TypeGraph feature to be "
                    "enabled but it was explicitly disabled!";
      return {};
    }
    featuresSet[Feature::TypeGraph] = true;
    LOG(WARNING) << "TypedDataSegment feature requires TypeGraph feature to be "
                    "enabled, enabling now.";
  }

  return featuresSet;
}

}  // namespace OIUtils
