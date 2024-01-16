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
#include <map>
#include <set>
#include <span>
#include <string>

#include "oi/ContainerInfo.h"
#include "oi/Features.h"

namespace fs = std::filesystem;

namespace oi::detail {

class FuncGen {
 public:
  static void DeclareExterns(std::string& code);
  static void DefineJitLog(std::string& code, FeatureSet features);

  static void DeclareStoreData(std::string& testCode);
  static void DefineStoreData(std::string& testCode);

  static void DeclareEncodeData(std::string& testCode);
  static void DefineEncodeData(std::string& testCode);

  static void DeclareEncodeDataSize(std::string& testCode);
  static void DefineEncodeDataSize(std::string& testCode);

  bool DeclareGetSizeFuncs(std::string& testCode,
                           const ContainerInfoRefSet& containerInfo,
                           FeatureSet features);
  bool DefineGetSizeFuncs(std::string& testCode,
                          const ContainerInfoRefSet& containerInfo,
                          FeatureSet features);

  static void DeclareGetContainer(std::string& testCode);

  static void DeclareGetSize(std::string& testCode, const std::string& type);

  static void DefineTopLevelIntrospect(std::string& code,
                                       const std::string& type);
  static void DefineTopLevelIntrospectNamed(std::string& code,
                                            const std::string& type,
                                            const std::string& linkageName);

  static void DefineTopLevelGetSizeRef(std::string& testCode,
                                       const std::string& rawType,
                                       FeatureSet features);
  static void DefineTreeBuilderInstructions(
      std::string& testCode,
      const std::string& rawType,
      size_t exclusiveSize,
      std::span<const std::string_view> typeNames);

  static void DefineTopLevelGetSizeSmartPtr(std::string& testCode,
                                            const std::string& rawType,
                                            FeatureSet features);

  static void DefineGetSizeTypedValueFunc(std::string& testCode,
                                          const std::string& ctype);

  static void DefineDataSegmentDataBuffer(std::string& testCode);
  static void DefineBackInserterDataBuffer(std::string& code);
  static void DefineBasicTypeHandlers(std::string& code);

  static ContainerInfo GetOiArrayContainerInfo();
};

}  // namespace oi::detail
