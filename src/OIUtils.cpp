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
#include <glog/logging.h>
#include <toml++/toml.h>

#include <boost/algorithm/string/classification.hpp>
#include <boost/algorithm/string/split.hpp>
#include <boost/property_tree/ini_parser.hpp>
#include <boost/property_tree/ptree.hpp>

#include "OICodeGen.h"
#include "OICompiler.h"

namespace OIUtils {

using namespace std::literals;

bool processConfigFile(const std::string& configFilePath,
                       OICompiler::Config& compilerConfig,
                       OICodeGen::Config& generatorConfig) {
  toml::table config;
  try {
    config = toml::parse_file(configFilePath);
  } catch (const toml::parse_error& ex) {
    LOG(ERROR) << "processConfigFileToml: " << configFilePath << " : "
               << ex.description();
    return false;
  }

  if (toml::table* types = config["types"].as_table()) {
    if (toml::array* arr = (*types)["containers"].as_array()) {
      arr->for_each([&](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
          generatorConfig.containerConfigPaths.emplace(std::string(el));
        }
      });
    }
  }

  if (toml::table* headers = config["headers"].as_table()) {
    if (toml::array* arr = (*headers)["user_paths"].as_array()) {
      arr->for_each([&](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
          compilerConfig.userHeaderPaths.emplace_back(el);
        }
      });
    }
    if (toml::array* arr = (*headers)["system_paths"].as_array()) {
      arr->for_each([&](auto&& el) {
        if constexpr (toml::is_string<decltype(el)>) {
          compilerConfig.sysHeaderPaths.emplace_back(el);
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
            return false;
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

  return true;
}

}  // namespace OIUtils
