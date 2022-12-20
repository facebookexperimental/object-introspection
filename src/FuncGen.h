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
#include <string>

#include "ContainerInfo.h"

namespace fs = std::filesystem;

class FuncGen {
 public:
  bool RegisterContainer(ContainerTypeEnum, const fs::path& path);

  void DeclareStoreData(std::string& testCode);
  void DefineStoreData(std::string& testCode);

  void DeclareAddData(std::string& testCode);
  void DefineAddData(std::string& testCode);

  void DeclareEncodeData(std::string& testCode);
  void DefineEncodeData(std::string& testCode);

  void DeclareEncodeDataSize(std::string& testCode);
  void DefineEncodeDataSize(std::string& testCode);

  bool DeclareGetSizeFuncs(std::string& testCode,
                           const std::set<ContainerInfo>& containerInfo,
                           bool chaseRawPointers);
  bool DefineGetSizeFuncs(std::string& testCode,
                          const std::set<ContainerInfo>& containerInfo,
                          bool chaseRawPointers);

  void DeclareGetContainer(std::string& testCode);

  void DeclareGetSize(std::string& testCode, const std::string& type);

  void DeclareTopLevelGetSize(std::string& testCode, const std::string& type);
  void DefineTopLevelGetObjectSize(std::string& testCode,
                                   const std::string& type,
                                   const std::string& linkageName);

  void DefineTopLevelGetSizePtr(std::string& testCode, const std::string& type,
                                const std::string& rawType);

  void DefineTopLevelGetSizeRef(std::string& testCode,
                                const std::string& rawType);

  void DefineTopLevelGetSizePtrRet(std::string& testCode,
                                   const std::string& type);

  void DefineTopLevelGetSizeSmartPtr(std::string& testCode,
                                     const std::string& rawType);

  void DefineGetSizeTypedValueFunc(std::string& testCode,
                                   const std::string& ctype);

 private:
  std::map<ContainerTypeEnum, std::string> typeToDeclMap;
  std::map<ContainerTypeEnum, std::string> typeToFuncMap;
};
