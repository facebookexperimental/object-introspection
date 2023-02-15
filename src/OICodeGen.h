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
  drgn_type *type;
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
    bool polymorphicInheritance;

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
  OICodeGen(const Config &, SymbolService &);

 public:
  static std::unique_ptr<OICodeGen> buildFromConfig(const Config &,
                                                    SymbolService &);
  bool generate(std::string &code);

  [[deprecated("Use generate(std::string&) instead.")]] bool
  generateFunctionsForTypesDrgn(std::string &code) {
    return generate(code);
  }

  bool registerContainer(const fs::path &);

  // TODO: remove me once all the callsites are gone
  static void initializeCodeGen();

  drgn_qualified_type getRootType();
  void setRootType(drgn_qualified_type rt);
  void setLinkageName(std::string name) {
    linkageName = name;
  };
  TypeHierarchy getTypeHierarchy();
  std::map<std::string, PaddingInfo> getPaddingInfo();

  static void getDrgnArrayElementType(drgn_type *type, drgn_type **outElemType,
                                      size_t &outNumElems);

  bool isContainer(drgn_type *type);

  static drgn_type *drgnUnderlyingType(drgn_type *type);

  bool buildName(drgn_type *type, std::string &text, std::string &outName);

  std::string typeToTransformedName(drgn_type *type);
  static std::string typeToName(drgn_type *type);

  bool enumerateTypesRecurse(drgn_type *type);
  static std::string_view drgnKindStr(drgn_type *type);
  std::set<drgn_type *> processedTypes;
  bool isDynamic(drgn_type *type) const;

 private:
  Config config{};
  FuncGen funcGen;

  using ContainerTypeMap =
      std::pair<ContainerInfo, std::vector<drgn_qualified_type>>;

  using TemplateParamList =
      std::vector<std::pair<drgn_qualified_type, std::string>>;

  using SortedTypeDefMap = std::vector<std::pair<drgn_type *, drgn_type *>>;

  std::string rootTypeStr;
  std::string linkageName;
  std::map<drgn_type *, std::string> unnamedUnion;
  std::map<std::string, size_t> sizeMap;
  std::map<drgn_type *, ContainerTypeMap> containerTypeMapDrgn;
  std::vector<std::unique_ptr<ContainerInfo>> containerInfoList;
  std::vector<drgn_type *> enumTypes;
  std::vector<drgn_type *> typePath;
  std::vector<std::string> knownTypes;
  drgn_qualified_type rootType;
  drgn_qualified_type rootTypeToIntrospect;

  std::map<std::string, std::string> typedefMap;
  std::map<drgn_type *, std::vector<ParentMember>> parentClasses;
  std::map<std::string, std::vector<drgn_type *>> childClasses;
  std::map<drgn_type *, std::vector<drgn_type *>> descendantClasses;

  SymbolService &symbols;

  size_t pad_index = 0;
  std::unordered_map<drgn_type *, std::pair<size_t, size_t>> paddingIndexMap;

  std::map<drgn_type *, drgn_type *> typedefTypes;
  std::map<drgn_type *, std::vector<DrgnClassMemberInfo>> classMembersMap;
  std::map<drgn_type *, std::vector<DrgnClassMemberInfo>> classMembersMapCopy;
  std::map<drgn_type *, std::string> typeToNameMap;
  std::map<std::string, drgn_type *> nameToTypeMap;
  std::set<drgn_type *> funcDefTypeList;
  std::vector<drgn_type *> structDefType;
  std::set<drgn_type *> knownDummyTypeList;
  std::map<drgn_type *, drgn_type *> pointerToTypeMap;
  std::set<drgn_type *> thriftIssetStructTypes;
  std::vector<drgn_type *> topoSortedStructTypes;

  std::set<ContainerInfo> containerTypesFuncDef;

  std::map<std::string, PaddingInfo> paddedStructs;

  std::map<drgn_type *, std::vector<DrgnClassMemberInfo>> &getClassMembersMap();

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
  std::unordered_map<drgn_type *, DrgnString> fullyQualifiedNames;
  std::optional<const std::string_view> fullyQualifiedName(drgn_type *type);
  static SortedTypeDefMap getSortedTypeDefMap(
      const std::map<drgn_type *, drgn_type *> &typedefTypeMap);

  std::optional<ContainerInfo> getContainerInfo(drgn_type *type);
  void printAllTypes();
  void printAllTypeNames();

  static void addPaddingForBaseClass(drgn_type *type,
                                     std::vector<std::string> &def);
  void addTypeToName(drgn_type *type, std::string name);
  bool generateNamesForTypes();
  bool generateJitCode(std::string &code);
  bool generateStructDefs(std::string &code);
  bool generateStructDef(drgn_type *e, std::string &code);
  bool getDrgnTypeName(drgn_type *type, std::string &outName);

  bool getDrgnTypeNameInt(drgn_type *type, std::string &outName);
  bool recordChildren(drgn_type *type);
  bool enumerateChildClasses();
  bool populateDefsAndDecls();
  static void memberTransformName(
      std::map<std::string, std::string> &templateTransformMap,
      std::string &typeName);

  bool getMemberDefinition(drgn_type *type);
  bool isKnownType(const std::string &type);
  bool isKnownType(const std::string &type, std::string &matched);

  static bool getTemplateParams(
      drgn_type *type, size_t numTemplateParams,
      std::vector<std::pair<drgn_qualified_type, std::string>> &v);

  bool enumerateTemplateParamIdxs(drgn_type *type,
                                  const ContainerInfo &containerInfo,
                                  const std::vector<size_t> &paramIdxs,
                                  bool &ifStub);
  bool getContainerTemplateParams(drgn_type *type, bool &ifStub);
  void enumerateDescendants(drgn_type *type, drgn_type *baseType);
  void getFuncDefinitionStr(std::string &code, drgn_type *type,
                            const std::string &typeName);
  std::optional<uint64_t> getDrgnTypeSize(drgn_type *type);

  std::optional<std::string> getNameForType(drgn_type *type);

  static std::string preProcessUniquePtr(drgn_type *type, std::string name);
  std::string transformTypeName(drgn_type *type, std::string &text);
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
  bool generateParent(drgn_type *p,
                      std::unordered_map<std::string, int> &memberNames,
                      uint64_t &currOffsetBits, std::string &code,
                      size_t offsetToNextMember);
  std::optional<uint64_t> getAlignmentRequirements(drgn_type *e);
  bool generateStructMembers(drgn_type *e,
                             std::unordered_map<std::string, int> &memberNames,
                             std::string &code, uint64_t &out_offset_bits,
                             PaddingInfo &paddingInfo,
                             bool &violatesAlignmentRequirement,
                             size_t offsetToNextMember);
  void getFuncDefClassMembers(std::string &code, drgn_type *type,
                              std::unordered_map<std::string, int> &memberNames,
                              bool skipPadding = false);
  bool isDrgnSizeComplete(drgn_type *type);

  static bool getEnumUnderlyingTypeStr(drgn_type *e,
                                       std::string &enumUnderlyingTypeStr);

  bool ifEnumerateClass(const std::string &typeName);

  bool enumerateClassParents(drgn_type *type, const std::string &typeName);
  bool enumerateClassMembers(drgn_type *type, const std::string &typeName,
                             bool &isStubbed);
  bool enumerateClassTemplateParams(drgn_type *type,
                                    const std::string &typeName,
                                    bool &isStubbed);
  bool ifGenerateMemberDefinition(const std::string &typeName);
  bool generateMemberDefinition(drgn_type *type, std::string &typeName);
  std::optional<std::pair<std::string_view, std::string_view>> isMemberToStub(
      const std::string &type, const std::string &member);
  std::optional<std::string_view> isTypeToStub(const std::string &typeName);
  bool isTypeToStub(drgn_type *type, const std::string &typeName);
  bool isEmptyClassOrFunctionType(drgn_type *type, const std::string &typeName);
  bool enumerateClassType(drgn_type *type);
  bool enumerateTypeDefType(drgn_type *type);
  bool enumerateEnumType(drgn_type *type);
  bool enumeratePointerType(drgn_type *type);
  bool enumeratePrimitiveType(drgn_type *type);
  bool enumerateArrayType(drgn_type *type);

  bool isUnnamedStruct(drgn_type *type);

  std::string getAnonName(drgn_type *, const char *);
  std::string getStructName(drgn_type *type) {
    return getAnonName(type, "__anon_struct_");
  }
  std::string getUnionName(drgn_type *type) {
    return getAnonName(type, "__anon_union_");
  }
  static void declareThriftStruct(std::string &code, std::string_view name);

  bool isNumMemberGreaterThanZero(drgn_type *type);
  void getClassMembersIncludingParent(drgn_type *type,
                                      std::vector<DrgnClassMemberInfo> &out);
  bool staticAssertMemberOffsets(
      const std::string &struct_name, drgn_type *struct_type,
      std::string &assert_str,
      std::unordered_map<std::string, int> &member_names,
      uint64_t base_offset = 0);
  bool addStaticAssertsForType(drgn_type *type, bool generateAssertsForOffsets,
                               std::string &code);
  bool buildNameInt(drgn_type *type, std::string &nameWithoutTemplate,
                    std::string &outName);
  void replaceTemplateOperator(
      std::vector<std::pair<drgn_qualified_type, std::string>> &template_params,
      std::vector<std::string> &template_params_strings, size_t index);
  void replaceTemplateParameters(
      drgn_type *type,
      std::vector<std::pair<drgn_qualified_type, std::string>> &template_params,
      std::vector<std::string> &template_params_strings,
      const std::string &nameWithoutTemplate);
};
