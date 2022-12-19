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
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

class SymbolService;
struct irequest;

#include "Common.h"
#include "ContainerInfo.h"
#include "FuncGen.h"
#include "PaddingHunter.h"

extern "C" {
#include <drgn.h>
}

namespace fs = std::filesystem;

struct ParentMember {
  struct drgn_type *type;
  uint64_t bit_offset;

  bool operator<(const ParentMember &parent) const {
    return (bit_offset < parent.bit_offset);
  }
};

class OICodeGen {
 public:
  struct Config {
    /*
     * Note: don't set default values for the config so the user gets an
     * uninitialized field" warning if they missed any.
     */
    bool useDataSegment;
    bool chaseRawPointers;
    bool packStructs;
    bool genPaddingStats;
    bool captureThriftIsset;

    std::set<fs::path> containerConfigPaths{};
    std::set<std::string> defaultHeaders{};
    std::set<std::string> defaultNamespaces{};
    std::vector<std::pair<std::string, std::string>> membersToStub{};

    std::string toString() const;
    std::vector<std::string> toOptions() const;
  };

 private:
  // Private constructor. Please use the fallible `OICodeGen::buildFromConfig`
  // for the expected behaviour.
  OICodeGen(const Config &);

 public:
  static std::unique_ptr<OICodeGen> buildFromConfig(const Config &);
  bool generate(std::string &code);

  [[deprecated("Use generate(std::string&) instead.")]] bool
  generateFunctionsForTypesDrgn(std::string &code) {
    return generate(code);
  }

  static std::optional<RootInfo> getRootType(SymbolService &, const irequest &);

  bool registerContainer(const fs::path &);

  // TODO: remove me once all the callsites are gone
  static void initializeCodeGen();

  struct drgn_qualified_type getRootType();
  void setRootType(struct drgn_qualified_type rt);
  void setLinkageName(std::string name) {
    linkageName = name;
  };
  TypeHierarchy getTypeHierarchy();
  std::map<std::string, PaddingInfo> getPaddingInfo();

  static void getDrgnArrayElementType(struct drgn_type *type,
                                      struct drgn_type **outElemType,
                                      size_t &outNumElems);

  bool isContainer(struct drgn_type *type);

  static struct drgn_type *drgnUnderlyingType(struct drgn_type *type);

  bool buildName(struct drgn_type *type, std::string &text,
                 std::string &outName);

  std::string typeToTransformedName(struct drgn_type *type);
  static std::string typeToName(struct drgn_type *type);

  bool enumerateTypesRecurse(struct drgn_type *type);
  static std::string_view drgnKindStr(struct drgn_type *type);
  std::set<struct drgn_type *> processedTypes;

 private:
  Config config{};
  FuncGen funcGen;

  using ContainerTypeMap =
      std::pair<ContainerInfo, std::vector<struct drgn_qualified_type>>;

  using TemplateParamList =
      std::vector<std::pair<struct drgn_qualified_type, std::string>>;

  using SortedTypeDefMap =
      std::vector<std::pair<struct drgn_type *, struct drgn_type *>>;

  std::string rootTypeStr;
  std::string linkageName;
  std::map<struct drgn_type *, std::string> unnamedUnion;
  std::map<std::string, size_t> sizeMap;
  std::map<struct drgn_type *, ContainerTypeMap> containerTypeMapDrgn;
  std::vector<std::unique_ptr<ContainerInfo>> containerInfoList;
  std::vector<struct drgn_type *> enumTypes;
  std::vector<struct drgn_type *> typePath;
  std::vector<std::string> knownTypes;
  struct drgn_qualified_type rootType;
  struct drgn_qualified_type rootTypeToIntrospect;

  std::map<std::string, std::string> typedefMap;
  std::map<struct drgn_type *, std::vector<ParentMember>> parentClasses;

  size_t pad_index = 0;
  std::unordered_map<struct drgn_type *, std::pair<size_t, size_t>>
      paddingIndexMap;

  std::map<struct drgn_type *, struct drgn_type *> typedefTypes;
  std::map<struct drgn_type *, std::vector<DrgnClassMemberInfo>>
      classMembersMap;
  std::map<struct drgn_type *, std::vector<DrgnClassMemberInfo>>
      classMembersMapCopy;
  std::map<struct drgn_type *, std::string> typeToNameMap;
  std::map<std::string, struct drgn_type *> nameToTypeMap;
  std::set<struct drgn_type *> funcDefTypeList;
  std::vector<struct drgn_type *> structDefType;
  std::set<struct drgn_type *> knownDummyTypeList;
  std::map<struct drgn_type *, struct drgn_type *> pointerToTypeMap;
  std::set<struct drgn_type *> thriftIssetStructTypes;
  std::vector<struct drgn_type *> topoSortedStructTypes;

  std::set<ContainerInfo> containerTypesFuncDef;

  std::map<std::string, PaddingInfo> paddedStructs;

  std::map<struct drgn_type *, std::vector<DrgnClassMemberInfo>>
      &getClassMembersMap();

  class DrgnString {
    struct FreeDeleter {
      void operator()(void *allocated) {
        free(allocated);
      }
    };

   public:
    std::string_view contents;
    DrgnString(char *data, size_t length)
        : contents{data, length}, _data{data} {
    }
    DrgnString() = delete;

   private:
    std::unique_ptr<char, FreeDeleter> _data;
  };

  static void prependQualifiers(enum drgn_qualifiers, std::string &sb);
  static std::string stripFullyQualifiedName(
      const std::string &fullyQualifiedName);
  std::string stripFullyQualifiedNameWithSeparators(
      const std::string &fullyQualifiedname);
  static void removeTemplateParamAtIndex(std::vector<std::string> &params,
                                         const size_t index);
  std::unordered_map<struct drgn_type *, DrgnString> fullyQualifiedNames;
  std::optional<const std::string_view> fullyQualifiedName(
      struct drgn_type *type);
  static SortedTypeDefMap getSortedTypeDefMap(
      const std::map<struct drgn_type *, struct drgn_type *> &typedefTypeMap);

  std::optional<ContainerInfo> getContainerInfo(struct drgn_type *type);
  void printAllTypes();
  void printAllTypeNames();
  void printTypePath();

  static void addPaddingForBaseClass(struct drgn_type *type,
                                     std::vector<std::string> &def);
  void addTypeToName(struct drgn_type *type, std::string name);
  bool generateNamesForTypes();
  bool generateJitCode(std::string &code);
  bool generateStructDefs(std::string &code);
  bool generateStructDef(struct drgn_type *e, std::string &code);
  bool getDrgnTypeName(struct drgn_type *type, std::string &outName);

  bool getDrgnTypeNameInt(struct drgn_type *type, std::string &outName);
  bool populateDefsAndDecls();
  static void memberTransformName(
      std::map<std::string, std::string> &templateTransformMap,
      std::string &typeName);

  bool getMemberDefinition(struct drgn_type *type);
  bool isKnownType(const std::string &type);
  bool isKnownType(const std::string &type, std::string &matched);

  static bool getTemplateParams(
      struct drgn_type *type, size_t numTemplateParams,
      std::vector<std::pair<struct drgn_qualified_type, std::string>> &v);

  bool enumerateTemplateParamIdxs(struct drgn_type *type,
                                  const ContainerInfo &containerInfo,
                                  const std::vector<size_t> &paramIdxs,
                                  bool &ifStub);
  bool getContainerTemplateParams(struct drgn_type *type, bool &ifStub);
  void getFuncDefinitionStr(std::string &code, struct drgn_type *type,
                            const std::string &typeName);
  std::optional<uint64_t> getDrgnTypeSize(struct drgn_type *type);

  std::optional<std::string> getNameForType(struct drgn_type *type);

  static std::string preProcessUniquePtr(struct drgn_type *type,
                                         std::string name);
  std::string transformTypeName(struct drgn_type *type, std::string &text);
  static std::string templateTransformType(const std::string &typeName);
  static std::string structNameTransformType(const std::string &typeName);

  bool addPadding(uint64_t padding_bits, std::string &code);
  static void deduplicateMemberName(
      std::unordered_map<std::string, int> &memberNames,
      std::string &memberName);
  std::optional<uint64_t> generateMember(
      const DrgnClassMemberInfo &m,
      std::unordered_map<std::string, int> &memberNames,
      uint64_t currOffsetBits, std::string &code, bool isInUnion);
  bool generateParent(struct drgn_type *p,
                      std::unordered_map<std::string, int> &memberNames,
                      uint64_t &currOffsetBits, std::string &code,
                      size_t offsetToNextMember);
  std::optional<uint64_t> getAlignmentRequirements(struct drgn_type *e);
  bool generateStructMembers(struct drgn_type *e,
                             std::unordered_map<std::string, int> &memberNames,
                             std::string &code, uint64_t &out_offset_bits,
                             PaddingInfo &paddingInfo,
                             bool &violatesAlignmentRequirement,
                             size_t offsetToNextMember);
  void getFuncDefClassMembers(std::string &code, struct drgn_type *type,
                              std::unordered_map<std::string, int> &memberNames,
                              bool skipPadding = false);
  bool isDrgnSizeComplete(struct drgn_type *type);

  static bool getEnumUnderlyingTypeStr(struct drgn_type *e,
                                       std::string &enumUnderlyingTypeStr);

  bool ifEnumerateClass(const std::string &typeName);

  bool enumerateClassParents(struct drgn_type *type,
                             const std::string &typeName);
  bool enumerateClassMembers(struct drgn_type *type,
                             const std::string &typeName, bool &isStubbed);
  bool enumerateClassTemplateParams(struct drgn_type *type,
                                    const std::string &typeName,
                                    bool &isStubbed);
  bool ifGenerateMemberDefinition(const std::string &typeName);
  bool generateMemberDefinition(struct drgn_type *type, std::string &typeName);
  std::optional<std::pair<std::string_view, std::string_view>> isMemberToStub(
      const std::string &type, const std::string &member);
  std::optional<std::string_view> isTypeToStub(const std::string &typeName);
  bool isTypeToStub(struct drgn_type *type, const std::string &typeName);
  bool isEmptyClassOrFunctionType(struct drgn_type *type,
                                  const std::string &typeName);
  bool enumerateClassType(struct drgn_type *type);
  bool enumerateTypeDefType(struct drgn_type *type);
  bool enumerateEnumType(struct drgn_type *type);
  bool enumeratePointerType(struct drgn_type *type);
  bool enumeratePrimitiveType(struct drgn_type *type);
  bool enumerateArrayType(struct drgn_type *type);

  bool isUnnamedStruct(struct drgn_type *type);

  std::string getAnonName(struct drgn_type *, const char *);
  std::string getStructName(struct drgn_type *type) {
    return getAnonName(type, "__anon_struct_");
  }
  std::string getUnionName(struct drgn_type *type) {
    return getAnonName(type, "__anon_union_");
  }
  static void declareThriftStruct(std::string &code, std::string_view name);

  bool isNumMemberGreaterThanZero(struct drgn_type *type);
  void getClassMembersIncludingParent(struct drgn_type *type,
                                      std::vector<DrgnClassMemberInfo> &out);
  bool staticAssertMemberOffsets(
      const std::string &struct_name, struct drgn_type *struct_type,
      std::string &assert_str,
      std::unordered_map<std::string, int> &member_names,
      uint64_t base_offset = 0);
  bool addStaticAssertsForType(struct drgn_type *type,
                               bool generateAssertsForOffsets,
                               std::string &code);
  bool buildNameInt(struct drgn_type *type, std::string &nameWithoutTemplate,
                    std::string &outName);
  void replaceTemplateOperator(
      std::vector<std::pair<struct drgn_qualified_type, std::string>>
          &template_params,
      std::vector<std::string> &template_params_strings, size_t index);
  void replaceTemplateParameters(
      struct drgn_type *type,
      std::vector<std::pair<struct drgn_qualified_type, std::string>>
          &template_params,
      std::vector<std::string> &template_params_strings,
      const std::string &nameWithoutTemplate);
};
