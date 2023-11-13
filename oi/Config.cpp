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
#include "oi/Config.h"

#include <glog/logging.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>
#include <filesystem>

#include "oi/support/Toml.h"

namespace fs = std::filesystem;

namespace oi::detail::config {

using namespace std::literals;

namespace {

std::optional<FeatureSet> processConfigFile(const std::string& configFilePath,
                                            OICompiler::Config& compilerConfig,
                                            OICodeGen::Config& generatorConfig);

}

std::optional<FeatureSet> processConfigFiles(
    std::span<const fs::path> configFilePaths,
    std::map<Feature, bool> featureMap,
    OICompiler::Config& compilerConfig,
    OICodeGen::Config& generatorConfig) {
  FeatureSet enables;
  FeatureSet disables;

  for (fs::path p : configFilePaths) {
    auto fs = processConfigFile(p, compilerConfig, generatorConfig);
    if (!fs.has_value())
      return std::nullopt;
    enables |= *fs;
  }

  // Override anything from the config files with command line options
  for (auto [k, v] : featureMap) {
    enables[k] = v;
    disables[k] = !v;
  }
  return handleFeatureConflicts(enables, disables);
}

namespace {

std::optional<FeatureSet> processConfigFile(
    const std::string& configFilePath,
    OICompiler::Config& compilerConfig,
    OICodeGen::Config& generatorConfig) {
  fs::path configDirectory = fs::path{configFilePath}.remove_filename();

  toml::table config;
  try {
    config = toml::parse_file(configFilePath);
  } catch (const toml::parse_error& ex) {
    LOG(ERROR) << "processConfigFileToml: " << configFilePath << " : "
               << ex.description();
    return {};
  }

  FeatureSet enabledFeatures;
  if (toml::array* features = config["features"].as_array()) {
    for (auto&& el : *features) {
      auto* featureStr = el.as_string();
      if (!featureStr) {
        LOG(ERROR) << "enabled features must be strings";
        return {};
      }

      if (auto f = featureFromStr(featureStr->get());
          f != Feature::UnknownFeature) {
        enabledFeatures[f] = true;
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
    if (toml::array* arr = (*codegen)["capture_keys"].as_array()) {
      for (auto&& el : *arr) {
        if (toml::table* captureKeys = el.as_table()) {
          auto* type = (*captureKeys)["type"].as_string();
          auto* topLevel = (*captureKeys)["top_level"].as_boolean();
          if (!((type == nullptr) ^ (topLevel == nullptr))) {
            LOG(ERROR) << "Config entry 'capture_keys' must specify either a "
                          "type or 'top_level'";
            return {};
          }

          if (type) {
            auto* members = (*captureKeys)["members"].as_array();
            if (!members) {
              generatorConfig.keysToCapture.push_back(
                  OICodeGen::Config::KeyToCapture{
                      type->value_or(""), "*", false});
            } else {
              for (auto&& member : *members) {
                generatorConfig.keysToCapture.push_back(
                    OICodeGen::Config::KeyToCapture{
                        type->value_or(""), member.value_or(""), false});
              }
            }
          } else if (topLevel) {
            generatorConfig.keysToCapture.push_back(
                OICodeGen::Config::KeyToCapture{
                    std::nullopt, std::nullopt, true});
          }
        }
      }
    }
  }

  return enabledFeatures;
}

}  // namespace
}  // namespace oi::detail::config
