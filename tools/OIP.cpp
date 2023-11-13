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
#include <boost/archive/text_iarchive.hpp>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>

#include "oi/OICompiler.h"
#include "oi/PaddingHunter.h"
#include "oi/Serialize.h"

namespace fs = std::filesystem;

void usage(char* name) {
  printf("usage: %s <cache_file>\n", name);
}

void printDrgnType(const struct drgn_type* type) {
  printf("{");
  printf("\"this\":\"%p\",", static_cast<const void*>(type));
  printf("\"kind\":%d,", type->_private.kind);
  printf("\"name\":\"%s\",", type->_private.name);
  printf("\"size\":%zu", type->_private.size);
  printf("}");
}

void printDrgnClassMemberInfo(const DrgnClassMemberInfo& m) {
  printf("{");
  printf("\"type\":");
  printDrgnType(m.type);
  printf(",");
  printf("\"member_name\":\"%s\",", m.member_name.c_str());
  printf("\"bit_offset\":%zu,", m.bit_offset);
  printf("\"bit_field_size\":%zu", m.bit_field_size);
  printf("}");
}

void printClassMembersMap(
    const std::map<struct drgn_type*, std::vector<DrgnClassMemberInfo>>&
        classMembersMap) {
  printf("{");
  bool isFirstItem = true;
  for (const auto& [type, members] : classMembersMap) {
    if (!isFirstItem)
      printf(",");
    printf("\"%p\":[", static_cast<const void*>(type));
    {
      bool isInnerFirstItem = true;
      for (const auto& member : members) {
        if (!isInnerFirstItem)
          printf(",");
        printDrgnClassMemberInfo(member);
        isInnerFirstItem = false;
      }
    }
    isFirstItem = false;
    printf("]");
  }
  printf("}");
}

void printContainerTypeMap(
    const std::map<
        struct drgn_type*,
        std::pair<ContainerTypeEnum, std::vector<struct drgn_qualified_type>>>&
        containerTypeMap) {
  printf("{");
  bool isFirstItem = true;
  for (const auto& [type, pair] : containerTypeMap) {
    if (!isFirstItem)
      printf(",");
    printf("\"%p\":[", static_cast<const void*>(type));
    {
      bool isInnerFirstItem = true;
      for (const auto& container : pair.second) {
        if (!isInnerFirstItem)
          printf(",");
        printDrgnType(container.type);
        isInnerFirstItem = false;
      }
    }
    printf("]");
    isFirstItem = false;
  }
  printf("}");
}

void printTypedefMap(
    const std::map<struct drgn_type*, struct drgn_type*>& typedefMap) {
  printf("{");
  bool isFirstItem = true;
  for (const auto& [typedefType, type] : typedefMap) {
    if (!isFirstItem)
      printf(",");
    printf("\"%p\":", static_cast<const void*>(typedefType));
    printDrgnType(type);
    isFirstItem = false;
  }
  printf("}");
}

void printSizeMap(const std::map<std::string, size_t>& sizeMap) {
  printf("{");
  bool isFirstItem = true;
  for (const auto& [typeName, size] : sizeMap) {
    if (!isFirstItem) {
      printf(",");
    }
    printf("\"%s\":%zu", typeName.c_str(), size);
    isFirstItem = false;
  }
  printf("}");
}

void printDrgnTypeSet(const std::set<struct drgn_type*>& dummyList) {
  printf("[");
  bool isFirstItem = true;
  for (const auto& type : dummyList) {
    if (!isFirstItem) {
      printf(",");
    }
    printDrgnType(type);
    isFirstItem = false;
  }
  printf("]");
}

void printPaddedStruct(
    std::map<std::string, PaddingInfo>::value_type paddedStruct) {
  printf("{\n");
  printf("Name: %s", paddedStruct.first.c_str());
  printf(", object size: %ld", paddedStruct.second.structSize);
  printf(", saving size: %ld", paddedStruct.second.savingSize);
  printf(", padding size: %ld", paddedStruct.second.paddingSize);
  printf(", isSet size: %ld", paddedStruct.second.isSetSize);
  printf("\nSaving opportunity: %ld bytes\n\n",
         paddedStruct.second.paddingSize * paddedStruct.second.instancesCnt);
  printf("%s\n", paddedStruct.second.definition.c_str());
  printf("}");
}

void printPaddedStructs(
    const std::map<std::string, PaddingInfo>& paddedStructs) {
  printf("[");
  bool isFirstItem = true;
  for (const auto& paddedStruct : paddedStructs) {
    if (!isFirstItem)
      printf(",");
    printPaddedStruct(paddedStruct);
    isFirstItem = false;
  }
  printf("]");
}

void printTypeHierarchy(const TypeHierarchy& th) {
  printf("{");
  printf("\"classMembersMap\":");
  printClassMembersMap(th.classMembersMap);
  printf(",");
  printf("\"containerTypeMap\":");
  printContainerTypeMap(th.containerTypeMap);
  printf(",");
  printf("\"typedefMap\":");
  printTypedefMap(th.typedefMap);
  printf(",");
  printf("\"sizeMap\":");
  printSizeMap(th.sizeMap);
  printf(",");
  printf("\"knownDummyTypeList\":");
  printDrgnTypeSet(th.knownDummyTypeList);
  printf(",");
  printf("\"pointerToTypeMap\":");
  // Re-using printTypedefMap to display pointerToTypeMap
  printTypedefMap(th.pointerToTypeMap);
  printf(",");
  printf("\"thriftIssetStructTypes\":");
  printDrgnTypeSet(th.thriftIssetStructTypes);
  printf("}\n");
}

void printFuncArg(const std::shared_ptr<FuncDesc::TargetObject>& funcObj) {
  printf("{");
  printf("\"valid\":%s,", funcObj->valid ? "true" : "false");
  printf("\"typeName\":\"%s\"", funcObj->typeName.c_str());

  const auto* funcArg = dynamic_cast<const FuncDesc::Arg*>(funcObj.get());
  if (funcArg != nullptr) {
    printf(",");
    printf("\"locations\":[");
    for (size_t i = 0; i < funcArg->locator.locations_size; i++) {
      if (i > 0) {
        printf(",");
      }
      const auto& location = funcArg->locator.locations[i];
      printf(
          "{\"start\":\"0x%zx\",\"end\":\"0x%zx\",\"expr_size\":%zu,\"expr\":[",
          location.start,
          location.end,
          location.expr_size);
      for (size_t j = 0; j < location.expr_size; j++) {
        if (j > 0) {
          printf(",");
        }

        printf("\"0x%hhx\"", location.expr[j]);
      }
      printf("]}");
    }
    printf("]");
  }
  printf("}");
}

void printFuncDesc(const std::shared_ptr<FuncDesc>& funcDesc) {
  printf("{");
  printf("\"symName\":\"%s\",", funcDesc->symName.c_str());
  printf("\"ranges\":[");
  bool isFirstRange = true;
  for (auto& range : funcDesc->ranges) {
    if (!isFirstRange) {
      printf(",");
    }
    printf(
        "{\"start\": \"0x%zx\", \"end\": \"0x%zx\"}", range.start, range.end);
    isFirstRange = false;
  }
  printf("],");
  printf("\"isMethod\":%s,", funcDesc->isMethod ? "true" : "false");
  printf("\"funcArgs\":[");

  bool isFirstItem = true;
  if (funcDesc->isMethod) {
    printFuncArg(funcDesc->getThis());
    isFirstItem = false;
  }

  for (size_t i = 0; i < funcDesc->numArgs(); ++i) {
    if (!isFirstItem) {
      printf(",");
    }
    printFuncArg(funcDesc->getArgument(i));
    isFirstItem = false;
  }
  printf("],");
  printf("\"retval\":");
  if (funcDesc->retval) {
    printFuncArg(funcDesc->retval);
  } else {
    printf("null");
  }
  printf("}");
}

void printFuncDescs(
    const std::unordered_map<std::string, std::shared_ptr<FuncDesc>>&
        funcDescs) {
  printf("{");
  bool isFirstItem = true;
  for (const auto& [name, funcDesc] : funcDescs) {
    if (!isFirstItem)
      printf(",");
    printf("\"%s\":", name.c_str());
    printFuncDesc(funcDesc);
    isFirstItem = false;
  }
  printf("}\n");
}

void printGlobalDesc(const std::shared_ptr<GlobalDesc>& globalDesc) {
  printf("{");
  printf("\"symName\":\"%s\",", globalDesc->symName.c_str());
  printf("\"typeName\":\"%s\",", globalDesc->typeName.c_str());
  printf("\"baseAddr\":\"%p\"",
         reinterpret_cast<const void*>(globalDesc->baseAddr));
  printf("}");
}

void printGlobalDescs(
    const std::unordered_map<std::string, std::shared_ptr<GlobalDesc>>&
        globalDescs) {
  printf("{");
  bool isFirstItem = true;
  for (const auto& [name, globalDesc] : globalDescs) {
    if (!isFirstItem) {
      printf(",");
    }
    printf("\"%s\":", name.c_str());
    printGlobalDesc(globalDesc);
    isFirstItem = false;
  }
  printf("}");
}

int main(int argc, char* argv[]) {
  if (argc != 2) {
    usage(argv[0]);
    return EXIT_FAILURE;
  }

  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    usage(argv[0]);
    return EXIT_SUCCESS;
  }

  fs::path cachePath = argv[1];
  if (!fs::exists(cachePath)) {
    fprintf(stderr, "File not found: %s\n", cachePath.c_str());
    return EXIT_FAILURE;
  }
  std::ifstream cacheFile{cachePath};
  boost::archive::text_iarchive cacheArchive{cacheFile};

  std::string buildID;
  cacheArchive >> buildID;
  printf("{\"buildID\":\"%s\",", buildID.c_str());

  printf("\"data\":");
  if (cachePath.extension() == ".fd") {
    std::unordered_map<std::string, std::shared_ptr<FuncDesc>> funcDescs;
    cacheArchive >> funcDescs;
    printFuncDescs(funcDescs);
  } else if (cachePath.extension() == ".gd") {
    std::unordered_map<std::string, std::shared_ptr<GlobalDesc>> globalDescs;
    cacheArchive >> globalDescs;
    printGlobalDescs(globalDescs);
  } else if (cachePath.extension() == ".th") {
    std::pair<RootInfo, TypeHierarchy> typeHierarchy;
    cacheArchive >> typeHierarchy;
    printTypeHierarchy(typeHierarchy.second);
  } else if (cachePath.extension() == ".pd") {
    std::map<std::string, PaddingInfo> paddedStructs;
    cacheArchive >> paddedStructs;
    printPaddedStructs(paddedStructs);
  } else {
    fprintf(stderr, "Unknown file type: %s\n", cachePath.c_str());
    return EXIT_FAILURE;
  }
  printf("}\n");

  return EXIT_SUCCESS;
}
