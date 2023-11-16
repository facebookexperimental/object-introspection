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
#include "oi/OICodeGen.h"

#include <folly/SharedMutex.h>
#include <glog/logging.h>

#include <boost/algorithm/string.hpp>
#include <boost/algorithm/string/join.hpp>
#include <boost/algorithm/string/regex.hpp>
#include <boost/algorithm/string/replace.hpp>
#include <boost/format.hpp>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <map>
#include <set>
#include <sstream>
#include <unordered_map>
#include <unordered_set>

#include "oi/DrgnUtils.h"
#include "oi/FuncGen.h"
#include "oi/OIParser.h"
#include "oi/PaddingHunter.h"
#include "oi/SymbolService.h"

namespace fs = std::filesystem;

namespace oi::detail {

static size_t g_level = 0;

#undef VLOG
#define VLOG(verboselevel) \
  LOG_IF(INFO, VLOG_IS_ON(verboselevel)) << std::string(2 * g_level, ' ')

std::unique_ptr<OICodeGen> OICodeGen::buildFromConfig(const Config& c,
                                                      SymbolService& s) {
  auto cg = std::unique_ptr<OICodeGen>(new OICodeGen(c, s));

  for (const auto& path : c.containerConfigPaths) {
    if (!cg->registerContainer(path)) {
      LOG(ERROR) << "failed to register container: " << path;
      return nullptr;
    }
  }

  return cg;
}

OICodeGen::OICodeGen(const Config& c, SymbolService& s)
    : config{c}, symbols{s} {
  membersToStub = config.membersToStub;
  for (const auto& type : typesToStub) {
    membersToStub.emplace_back(type, "*");
  }

  // `knownTypes` has been made obsolete by the introduction of using the
  // `OIInternal` namespace. Instead of deleting all of the related code
  // however, for now we just define the `knownTypes` list to be empty, as this
  // will make it easier to selectively revert our changes if it turns out that
  // there are issues with the new approach.
  knownTypes = {"IPAddress", "uintptr_t"};

  sizeMap["SharedMutex"] = sizeof(folly::SharedMutex);
}

bool OICodeGen::registerContainer(const fs::path& path) {
  VLOG(1) << "registering container, path: " << path;
  auto info = ContainerInfo::loadFromFile(path);
  if (!info) {
    return false;
  }
  if (info->requiredFeatures != (config.features & info->requiredFeatures)) {
    VLOG(1) << "Skipping container (feature conflict): " << info->typeName;
    return true;
  }

  VLOG(1) << "registered container, type: " << info->typeName;
  containerInfoList.emplace_back(std::move(info));
  return true;
}

bool OICodeGen::isKnownType(const std::string& type) {
  std::string matched;
  return isKnownType(type, matched);
}

bool OICodeGen::isKnownType(const std::string& type, std::string& matched) {
  if (auto it = std::ranges::find(knownTypes, type); it != knownTypes.end()) {
    matched = *it;
    return true;
  }

  if (type.rfind("allocator<", 0) == 0 ||
      type.rfind("new_allocator<", 0) == 0) {
    matched = type;
    return true;
  }

  if (auto opt = isTypeToStub(type)) {
    matched = opt.value();
    return true;
  }

  return false;
}

std::optional<const std::string_view> OICodeGen::fullyQualifiedName(
    drgn_type* type) {
  if (auto entry = fullyQualifiedNames.find(type);
      entry != fullyQualifiedNames.end()) {
    return entry->second.contents;
  }

  char* name = nullptr;
  size_t length = 0;
  auto* err = drgn_type_fully_qualified_name(type, &name, &length);
  if (err != nullptr || name == nullptr) {
    return std::nullopt;
  }

  auto typeNamePair =
      fullyQualifiedNames.emplace(type, DrgnString{name, length}).first;
  return typeNamePair->second.contents;
}

std::optional<std::reference_wrapper<const ContainerInfo>>
OICodeGen::getContainerInfo(drgn_type* type) {
  auto name = fullyQualifiedName(type);
  if (!name.has_value()) {
    return std::nullopt;
  }

  std::string nameStr = std::string(*name);
  for (auto it = containerInfoList.rbegin(); it != containerInfoList.rend();
       ++it) {
    const ContainerInfo& info = **it;
    if (std::regex_search(nameStr, info.matcher)) {
      return info;
    }
  }
  return std::nullopt;
}

bool OICodeGen::isContainer(drgn_type* type) {
  return getContainerInfo(type).has_value();
}

std::string OICodeGen::preProcessUniquePtr(drgn_type* type, std::string name) {
  std::string typeName;
  std::string deleterName;

  size_t end = name.rfind('>') - 1;
  size_t pos = end;
  size_t begin = 0;
  bool found = false;

  int unmatchedTemplate = 0;
  while (pos > 0) {
    if (name[pos] == '>') {
      unmatchedTemplate++;
    }
    if (name[pos] == '<') {
      unmatchedTemplate--;
    }
    if (unmatchedTemplate == 0 && name[pos] == ',') {
      begin = pos + 2;
      deleterName = name.substr(begin, end - begin + 1);

      size_t offset = std::string("unique_ptr<").size();
      typeName = name.substr(offset, pos - offset);
      found = true;

      break;
    }

    pos--;
  }

  VLOG(2) << "Deleter name: " << deleterName << " typeName: " << typeName;

  if (deleterName.find("default_delete") == std::string::npos && found) {
    size_t typeSize = drgn_type_size(type);

    constexpr size_t defaultDeleterSize = sizeof(std::unique_ptr<double>);
    constexpr size_t cFunctionDeleterSize =
        sizeof(std::unique_ptr<double, void (*)(double*)>);
    constexpr size_t stdFunctionDeleterSize =
        sizeof(std::unique_ptr<double, std::function<void(double*)>>);

    if (typeSize == defaultDeleterSize) {
      name.replace(begin, end - begin + 1, "default_delete<" + typeName + ">");
    } else if (typeSize == cFunctionDeleterSize) {
      name.replace(begin, end - begin + 1, "void(*)(" + typeName + "*)");
    } else if (typeSize == stdFunctionDeleterSize) {
      name.replace(
          begin, end - begin + 1, "std::function<void (" + typeName + "*)>");
    } else {
      LOG(ERROR) << "Unhandled case, unique_ptr size: " << typeSize;
    }
  }

  return name;
}

void OICodeGen::prependQualifiers(enum drgn_qualifiers qualifiers,
                                  std::string& sb) {
  // const qualifier is the only one we're interested in
  if ((qualifiers & DRGN_QUALIFIER_CONST) != 0) {
    sb += "const ";
  }
}

void OICodeGen::removeTemplateParamAtIndex(std::vector<std::string>& params,
                                           const size_t index) {
  if (index < params.size()) {
    params.erase(params.begin() + index);
  }
}

std::string OICodeGen::stripFullyQualifiedName(
    const std::string& fullyQualifiedName) {
  std::vector<std::string> lines;
  boost::iter_split(lines, fullyQualifiedName, boost::first_finder("::"));
  return lines[lines.size() - 1];
}

std::string OICodeGen::stripFullyQualifiedNameWithSeparators(
    const std::string& fullyQualifiedName) {
  std::vector<std::string> stack;
  std::string sep = " ,<>()";
  std::string tmp;
  constexpr std::string_view cond = "conditional_t";
  constexpr std::string_view stdCond = "std::conditional_t";
  static int cond_t_val = 0;

  if ((fullyQualifiedName.starts_with(cond)) ||
      (fullyQualifiedName.starts_with(stdCond))) {
    return "conditional_t_" + std::to_string(cond_t_val++);
  }

  for (auto& c : fullyQualifiedName) {
    if (sep.find(c) == std::string::npos) {
      stack.push_back(std::string(1, c));
    } else {
      tmp = "";
      while (!stack.empty() &&
             sep.find(stack[stack.size() - 1]) == std::string::npos) {
        tmp = stack[stack.size() - 1] + tmp;
        stack.pop_back();
      }
      tmp = stripFullyQualifiedName(tmp);

      stack.push_back(tmp);

      if (c == '>' || c == ')') {
        std::string findStr = (c == '>') ? "<" : "(";
        tmp = "";
        while (!stack.empty() && stack[stack.size() - 1] != findStr) {
          tmp = stack[stack.size() - 1] + tmp;
          stack.pop_back();
        }
        if (stack.empty())
          return "";

        // Pop the '<' or '('
        stack.pop_back();
        if (c == '>') {
          stack.push_back("<" + tmp + ">");
        } else {
          stack.push_back("(" + tmp + ")");
        }
      } else {
        stack.push_back(std::string(1, c));
      }
    }
  }

  tmp = "";
  for (auto& e : stack) {
    tmp += e;
  }

  return tmp;
}

// Replace a specific template parameter with a generic DummySizedOperator
void OICodeGen::replaceTemplateOperator(
    TemplateParamList& template_params,
    std::vector<std::string>& template_params_strings,
    size_t index) {
  if (index >= template_params.size()) {
    // Should this happen?
    return;
  }

  size_t comparatorSz = 1;
  drgn_type* cmpType = template_params[index].first.type;
  if (isDrgnSizeComplete(cmpType)) {
    comparatorSz = drgn_type_size(cmpType);
  } else {
    // Don't think there is anyway to accurately get data from the container
  }

  // Alignment is input to alignas. alignas(0) is supposed to be ignored.
  // So we can safely specify that if we can't query the alignment requirement.
  size_t alignment = 0;
  if (drgn_type_has_members(cmpType) && drgn_type_num_members(cmpType) == 0) {
    comparatorSz = 0;
  } else {
    auto alignmentOpt = getAlignmentRequirements(cmpType);
    if (!alignmentOpt.has_value()) {
      // Not sure when/if this would happen. Just log for now.
      LOG(ERROR) << "Failed to get alignment for " << cmpType;
    } else {
      alignment = *alignmentOpt / CHAR_BIT;
    }
  }

  template_params_strings[index] = "DummySizedOperator<" +
                                   std::to_string(comparatorSz) + "," +
                                   std::to_string(alignment) + ">";
}

void OICodeGen::replaceTemplateParameters(
    drgn_type* type,
    TemplateParamList& template_params,
    std::vector<std::string>& template_params_strings,
    const std::string& nameWithoutTemplate) {
  auto optContainerInfo = getContainerInfo(type);
  if (!optContainerInfo.has_value()) {
    LOG(ERROR) << "Unknown container type: " << nameWithoutTemplate;
    return;
  }
  const ContainerInfo& containerInfo = *optContainerInfo;

  // Some containers will need special handling
  if (nameWithoutTemplate == "bimap<") {
    // TODO: The end parameters seem to be wild cards to pass in things like
    // allocator or passing complex relationships between the keys.
    removeTemplateParamAtIndex(template_params_strings, 4);
    removeTemplateParamAtIndex(template_params_strings, 3);
    removeTemplateParamAtIndex(template_params_strings, 2);
  } else {
    for (auto const& index : containerInfo.replaceTemplateParamIndex) {
      replaceTemplateOperator(template_params, template_params_strings, index);
    }
    if (containerInfo.allocatorIndex) {
      removeTemplateParamAtIndex(template_params_strings,
                                 *containerInfo.allocatorIndex);
    }
  }
}

bool OICodeGen::buildName(drgn_type* type,
                          std::string& text,
                          std::string& outName) {
  int ptrDepth = 0;
  drgn_type* ut = type;
  while (drgn_type_kind(ut) == DRGN_TYPE_POINTER) {
    ut = drgn_type_type(ut).type;
    ptrDepth++;
  }

  size_t pos = text.find('<');
  if (pos == std::string::npos) {
    outName = text;
    return false;
  }

  std::string nameWithoutTemplate = text.substr(0, pos + 1);

  std::string tmpName;
  if (!buildNameInt(ut, nameWithoutTemplate, tmpName)) {
    return false;
  }
  outName = tmpName + std::string(ptrDepth, '*');
  return true;
}

bool OICodeGen::buildNameInt(drgn_type* type,
                             std::string& nameWithoutTemplate,
                             std::string& outName) {
  // Calling buildName only makes sense if a type is a container and has
  // template parameters. For a generic template class, we just flatten the
  // name into anything reasonable (e.g. Foo<int> becomes Foo_int_).
  if (!isContainer(type) || !drgn_type_has_template_parameters(type)) {
    return false;
  }
  size_t numTemplateParams = drgn_type_num_template_parameters(type);
  if (numTemplateParams == 0) {
    return false;
  }

  TemplateParamList templateParams;
  if (!getTemplateParams(type, numTemplateParams, templateParams)) {
    LOG(ERROR) << "Failed to get template params";
    return false;
  }

  if (templateParams.size() == 0) {
    LOG(ERROR) << "Template parameters missing";
  }

  std::vector<std::string> templateParamsStrings;

  for (size_t i = 0; i < templateParams.size(); ++i) {
    auto& [p, value] = templateParams[i];
    enum drgn_qualifiers qualifiers = p.qualifiers;
    prependQualifiers(qualifiers, outName);

    std::string templateParamName;

    if (!value.empty()) {
      if (drgn_type_kind(p.type) == DRGN_TYPE_ENUM) {
        /*
         * We must reference scoped enums by fully-qualified name to keep the
         * C++ compiler happy.
         *
         * i.e. we want to end up with:
         *   isset_bitset<1,
         * apache::thrift::detail::IssetBitsetOption::Unpacked>
         *
         * If we instead just used the enum's value, we'd instead have:
         *   isset_bitset<1, 0>
         * However, this implicit conversion from an integer is not valid for
         * C++ scoped enums.
         *
         * Unscoped enums (C-style enums) do not need this special treatment as
         * they are implicitly convertible to their integral values.
         * Conveniently for us, drgn returns them to us as DRGN_TYPE_INT so they
         * are not processed here.
         */
        // TODO Better handling of this enumVal?
        // We converted from a numeric value into a string earlier and now back
        // into a numeric value here.
        // Does this indexing work correctly for signed-integer-valued enums?
        //
        // This code only applies to containers with an enum template parameter,
        // of which we only currently have one, so I think these problems don't
        // matter for now.

        auto enumVal = std::stoull(value);
        drgn_type_enumerator* enumerators = drgn_type_enumerators(p.type);
        templateParamName = *fullyQualifiedName(p.type);
        templateParamName += "::";
        templateParamName += enumerators[enumVal].name;
      } else {
        // We can just directly use the value for other literals
        templateParamName = value;
      }
    } else if (auto it = typeToNameMap.find(p.type);
               it != typeToNameMap.end()) {
      // Use the type's allocated name if it exists
      templateParamName = it->second;
    } else if (drgn_type_has_tag(p.type)) {
      const char* templateParamNameChr = drgn_type_tag(p.type);
      if (templateParamNameChr != nullptr) {
        templateParamName = std::string(templateParamNameChr);
      } else {
        return false;
      }
    } else if (drgn_type_has_name(p.type)) {
      const char* templateParamNameChr = drgn_type_name(p.type);
      if (templateParamNameChr != nullptr) {
        templateParamName = std::string(templateParamNameChr);
      } else {
        return false;
      }
    } else if (drgn_type_kind(p.type) == DRGN_TYPE_FUNCTION) {
      char* defStr = nullptr;
      if (drgn_format_type_name(p, &defStr)) {
        LOG(ERROR) << "Failed to get formatted string for " << p.type;
        templateParamName = std::string("");
      } else {
        templateParamName = defStr;
        free(defStr);
      }
    } else if (drgn_type_kind(p.type) == DRGN_TYPE_POINTER) {
      char* defStr = nullptr;

      if (drgn_format_type_name(p, &defStr)) {
        LOG(ERROR) << "Failed to get formatted string for " << p.type;
        templateParamName = std::string("");
      } else {
        drgn_qualified_type underlyingType = drgn_type_type(p.type);
        templateParamName = defStr;
        free(defStr);

        if (drgn_type_kind(underlyingType.type) == DRGN_TYPE_FUNCTION) {
          size_t index = templateParamName.find(" ");

          if (index != std::string::npos) {
            templateParamName = stripFullyQualifiedNameWithSeparators(
                                    templateParamName.substr(0, index + 1)) +
                                "(*)" +
                                stripFullyQualifiedNameWithSeparators(
                                    templateParamName.substr(index + 1));
          }
        }
      }
    } else if (drgn_type_kind(p.type) == DRGN_TYPE_VOID) {
      templateParamName = std::string("void");
    } else if (drgn_type_kind(p.type) == DRGN_TYPE_ARRAY) {
      size_t elems = 1;
      drgn_type* arrayElementType = nullptr;
      drgn_utils::getDrgnArrayElementType(p.type, &arrayElementType, elems);

      if (drgn_type_has_name(arrayElementType)) {
        templateParamName = drgn_type_name(arrayElementType);
      } else {
        LOG(ERROR) << "Failed4 to get typename ";
        return false;
      }
    } else {
      LOG(ERROR) << "Failed3 to get typename ";
      return false;
    }

    // templateParamName = templateTransformType(templateParamName);
    templateParamName =
        stripFullyQualifiedNameWithSeparators(templateParamName);
    std::string recursiveParam;
    if (p.type != nullptr &&
        buildName(p.type, templateParamName, recursiveParam)) {
      templateParamName = recursiveParam;
    }

    templateParamsStrings.push_back(templateParamName);
  }

  replaceTemplateParameters(
      type, templateParams, templateParamsStrings, nameWithoutTemplate);

  outName = nameWithoutTemplate;
  for (size_t i = 0; i < templateParamsStrings.size(); ++i) {
    auto& [p, value] = templateParams[i];
    enum drgn_qualifiers qualifiers = p.qualifiers;
    prependQualifiers(qualifiers, outName);

    outName += templateParamsStrings[i];
    if (i != templateParamsStrings.size() - 1) {
      outName += ", ";
    } else {
      outName += " >";
    }
  }
  return true;
}

bool OICodeGen::getTemplateParams(
    drgn_type* type,
    size_t numTemplateParams,
    std::vector<std::pair<drgn_qualified_type, std::string>>& v) {
  drgn_type_template_parameter* tParams = drgn_type_template_parameters(type);

  for (size_t i = 0; i < numTemplateParams; ++i) {
    const drgn_object* obj = nullptr;
    if (auto* err = drgn_template_parameter_object(&tParams[i], &obj);
        err != nullptr) {
      LOG(ERROR) << "Error when looking up template parameter " << err->code
                 << " " << err->message;
      drgn_error_destroy(err);
      return false;
    }

    std::string value;
    drgn_qualified_type t{};

    if (obj == nullptr) {
      if (auto* err = drgn_template_parameter_type(&tParams[i], &t);
          err != nullptr) {
        LOG(ERROR) << "Error when looking up template parameter " << err->code
                   << " " << err->message;
        drgn_error_destroy(err);
        return false;
      }
    } else {
      t.type = obj->type;
      t.qualifiers = obj->qualifiers;
      if (obj->encoding == DRGN_OBJECT_ENCODING_BUFFER) {
        uint64_t size = drgn_object_size(obj);
        char* buf = nullptr;
        if (size <= sizeof(obj->value.ibuf)) {
          buf = (char*)&(obj->value.ibuf);
        } else {
          buf = obj->value.bufp;
        }

        if (buf != nullptr) {
          value = std::string(buf);
        }
      } else if (obj->encoding == DRGN_OBJECT_ENCODING_SIGNED) {
        value = std::to_string(obj->value.svalue);
      } else if (obj->encoding == DRGN_OBJECT_ENCODING_UNSIGNED) {
        value = std::to_string(obj->value.uvalue);
      } else if (obj->encoding == DRGN_OBJECT_ENCODING_FLOAT) {
        value = std::to_string(obj->value.fvalue);
      } else {
        LOG(ERROR) << "Unsupported OBJ encoding for getting template parameter";
        return false;
      }
    }

    v.push_back({t, value});
  }
  return true;
}

std::string OICodeGen::transformTypeName(drgn_type* type, std::string& text) {
  std::string tmp = stripFullyQualifiedNameWithSeparators(text);

  boost::regex re{"<typename\\s(\\w+)>"};
  std::string fmt{"<typename ::\\1>"};

  std::string output = boost::regex_replace(tmp, re, fmt);

  std::string buildNameOutput;
  if (!buildName(type, text, buildNameOutput)) {
    buildNameOutput = output;
  }

  if (buildNameOutput.starts_with("unique_ptr")) {
    buildNameOutput = preProcessUniquePtr(type, buildNameOutput);
  }

  return buildNameOutput;
}

bool OICodeGen::getContainerTemplateParams(drgn_type* type, bool& ifStub) {
  if (containerTypeMapDrgn.find(type) != containerTypeMapDrgn.end()) {
    return true;
  }

  std::string typeName;

  if (drgn_type_has_tag(type)) {
    typeName = drgn_type_tag(type);
  } else if (drgn_type_has_name(type)) {
    typeName = drgn_type_name(type);
  } else {
    LOG(ERROR) << "Failed1 to get typename: kind " << drgnKindStr(type);
    return false;
  }
  typeName = transformTypeName(type, typeName);

  auto optContainerInfo = getContainerInfo(type);
  if (!optContainerInfo.has_value()) {
    LOG(ERROR) << "Unknown container type: " << typeName;
    return false;
  }
  const ContainerInfo& containerInfo = *optContainerInfo;

  std::vector<size_t> paramIdxs;
  if (containerInfo.underlyingContainerIndex.has_value()) {
    if (containerInfo.numTemplateParams.has_value()) {
      LOG(ERROR) << "Container adapters should not enumerate their template "
                    "parameters";
      return false;
    }
    paramIdxs.push_back(*containerInfo.underlyingContainerIndex);
  } else {
    auto numTemplateParams = containerInfo.numTemplateParams;
    if (!numTemplateParams.has_value()) {
      if (!drgn_type_has_template_parameters(type)) {
        LOG(ERROR) << "Failed to find template params";
        return false;
      }
      numTemplateParams = drgn_type_num_template_parameters(type);
      VLOG(1) << "Num template params for " << typeName << " "
              << *numTemplateParams;
    }

    for (size_t i = 0; i < *numTemplateParams; ++i) {
      paramIdxs.push_back(i);
    }
  }

  return enumerateTemplateParamIdxs(type, containerInfo, paramIdxs, ifStub);
}

bool OICodeGen::enumerateTemplateParamIdxs(drgn_type* type,
                                           const ContainerInfo& containerInfo,
                                           const std::vector<size_t>& paramIdxs,
                                           bool& ifStub) {
  if (paramIdxs.empty()) {
    // Containers such as IOBuf and IOBufQueue don't have template params, but
    // still should be added to containerTypeMap
    containerTypeMapDrgn.emplace(
        type,
        std::pair(std::ref(containerInfo), std::vector<drgn_qualified_type>()));
    return true;
  }

  auto maxParamIdx = *std::max_element(paramIdxs.begin(), paramIdxs.end());
  if (!drgn_type_has_template_parameters(type) ||
      drgn_type_num_template_parameters(type) <= maxParamIdx) {
    LOG(ERROR) << "Failed to find template params";
    return false;
  }

  drgn_type_template_parameter* tParams = drgn_type_template_parameters(type);
  for (auto i : paramIdxs) {
    drgn_qualified_type t{};
    if (auto* err = drgn_template_parameter_type(&tParams[i], &t);
        err != nullptr) {
      LOG(ERROR) << "Error when looking up template parameter " << err->code
                 << " " << err->message;
      return false;
    }

    if (drgn_type_kind(t.type) != DRGN_TYPE_VOID &&
        !isDrgnSizeComplete(t.type)) {
      ifStub = true;
      return true;
    }
  }

  for (auto i : paramIdxs) {
    drgn_qualified_type t{};
    if (auto* err = drgn_template_parameter_type(&tParams[i], &t);
        err != nullptr) {
      LOG(ERROR) << "Error when looking up template parameter " << err->code
                 << " " << err->message;
      drgn_error_destroy(err);
      return false;
    }
    // TODO: This is painful, there seems to be a bug in drgn (or maybe it is
    // intentional). Consider a case :-
    // typedef struct {int a;} A;
    // unique_ptr<A> ptr;
    //
    // If you query template parameter type of unique_ptr<A>, it skips typdefs
    // and directly seems to return underlying type i.e. an unnamed struct.
    // Unfortunately, we might need special handling for this.

    if (!OICodeGen::enumerateTypesRecurse(t.type)) {
      return false;
    }
  }

  // Do not add `shared_ptr<void>`, `unique_ptr<void>`, or `weak_ptr<void>`
  // to `containerTypeMapDrgn`.
  if (containerInfo.ctype == SHRD_PTR_TYPE ||
      containerInfo.ctype == UNIQ_PTR_TYPE ||
      containerInfo.ctype == WEAK_PTR_TYPE) {
    drgn_qualified_type t{};
    // We checked that this succeeded in the previous loop
    drgn_template_parameter_type(&tParams[0], &t);
    if (drgn_type_kind(t.type) == DRGN_TYPE_VOID) {
      return true;
    }
  }

  auto& templateTypes =
      containerTypeMapDrgn
          .emplace(type,
                   std::pair(std::ref(containerInfo),
                             std::vector<drgn_qualified_type>()))
          .first->second.second;

  for (auto i : paramIdxs) {
    drgn_qualified_type t{};
    drgn_error* err = drgn_template_parameter_type(&tParams[i], &t);
    if (err) {
      LOG(ERROR) << "Error when looking up template parameter " << err->code
                 << " " << err->message;
      return false;
    }

    // TODO: Any reason this can't be done in the prior loop. Then
    // this loop can be deleted
    templateTypes.push_back(t);
  }

  return true;
}

void OICodeGen::addPaddingForBaseClass(drgn_type* type,
                                       std::vector<std::string>& def) {
  if (drgn_type_num_members(type) < 1) {
    return;
  }

  drgn_type_member* members = drgn_type_members(type);

  VLOG(2) << "Base member offset is " << members[0].bit_offset / CHAR_BIT;

  uint64_t byteOffset = members[0].bit_offset / CHAR_BIT;

  if (byteOffset > 0) {
    std::string memName =
        std::string("__") + "parentClass[" + std::to_string(byteOffset) + "]";
    std::string tname = "uint8_t";

    def.push_back(tname);
    def.push_back(memName);
    def.push_back(";");
  }
}

std::string_view OICodeGen::drgnKindStr(drgn_type* type) {
  switch (drgn_type_kind(type)) {
    case DRGN_TYPE_VOID:
      return "DRGN_TYPE_VOID";
    case DRGN_TYPE_INT:
      return "DRGN_TYPE_INT";
    case DRGN_TYPE_BOOL:
      return "DRGN_TYPE_BOOL";
    case DRGN_TYPE_FLOAT:
      return "DRGN_TYPE_FLOAT";
    case DRGN_TYPE_STRUCT:
      return "DRGN_TYPE_STRUCT";
    case DRGN_TYPE_UNION:
      return "DRGN_TYPE_UNION";
    case DRGN_TYPE_CLASS:
      return "DRGN_TYPE_CLASS";
    case DRGN_TYPE_ENUM:
      return "DRGN_TYPE_ENUM";
    case DRGN_TYPE_TYPEDEF:
      return "DRGN_TYPE_TYPEDEF";
    case DRGN_TYPE_POINTER:
      return "DRGN_TYPE_POINTER";
    case DRGN_TYPE_ARRAY:
      return "DRGN_TYPE_ARRAY";
    case DRGN_TYPE_FUNCTION:
      return "DRGN_TYPE_FUNCTION";
  }
  return "";
}

std::string OICodeGen::getAnonName(drgn_type* type, const char* template_) {
  std::string typeName;
  if (drgn_type_tag(type) != nullptr) {
    typeName = drgn_type_tag(type);
  } else {
    // Unnamed struct/union
    if (auto it = unnamedUnion.find(type); it != std::end(unnamedUnion)) {
      typeName = it->second;
    } else {
      typeName = template_ + std::to_string(unnamedUnion.size());
      unnamedUnion.emplace(type, typeName);
      // Leak a copy of the typeName to ensure its lifetime is greater than the
      // drgn_type
      char* typeNameCstr = new char[typeName.size() + 1];
      std::copy(std::begin(typeName), std::end(typeName), typeNameCstr);
      typeNameCstr[typeName.size()] = '\0';
      type->_private.tag = typeNameCstr;
      // We might need a second copy of the string to avoid double-free
      type->_private.oi_name = typeNameCstr;
    }
  }
  return transformTypeName(type, typeName);
}

bool OICodeGen::getMemberDefinition(drgn_type* type) {
  // Do a [] lookup to ensure `type` has a entry in classMembersMap
  // If it has no entry, the lookup will default construct on for us
  classMembersMap[type];

  structDefType.push_back(type);

  std::string outName;
  getDrgnTypeNameInt(type, outName);
  VLOG(1) << "Adding members to class " << outName << " " << type;

  // If the type is a union, don't expand the members
  if (drgn_type_kind(type) == DRGN_TYPE_UNION) {
    return true;
  }

  drgn_type_member* members = drgn_type_members(type);
  for (size_t i = 0; i < drgn_type_num_members(type); ++i) {
    auto& member = members[i];
    auto memberName = member.name ? std::string(member.name)
                                  : "__anon_member_" + std::to_string(i);

    drgn_qualified_type t{};
    uint64_t bitFieldSize = 0;
    if (auto* err = drgn_member_type(&member, &t, &bitFieldSize);
        err != nullptr) {
      LOG(ERROR) << "Error when looking up member type '" << memberName
                 << "': (" << err->code << ") " << err->message;
      drgn_error_destroy(err);
      continue;
    }

    if (drgn_type_kind(t.type) == DRGN_TYPE_FUNCTION) {
      continue;
    }

    std::string tname;
    getDrgnTypeNameInt(t.type, tname);
    isKnownType(tname, tname);

    bool isStubbed = isMemberToStub(outName, memberName).has_value();

    VLOG(2) << "Adding member; type: " << tname << " " << type
            << " name: " << memberName << " offset(bits): " << member.bit_offset
            << " offset(bytes): " << (float)member.bit_offset / (float)CHAR_BIT
            << " isStubbed: " << (isStubbed ? "true" : "false");

    classMembersMap[type].push_back(DrgnClassMemberInfo{
        t.type, memberName, member.bit_offset, bitFieldSize, isStubbed});
  }

  return true;
}

std::string OICodeGen::typeToTransformedName(drgn_type* type) {
  auto typeName = drgn_utils::typeToName(type);
  typeName = transformTypeName(type, typeName);
  return typeName;
}

bool OICodeGen::recordChildren(drgn_type* type) {
  drgn_type_template_parameter* parents = drgn_type_parents(type);

  for (size_t i = 0; i < drgn_type_num_parents(type); i++) {
    drgn_qualified_type t{};

    if (auto* err = drgn_template_parameter_type(&parents[i], &t);
        err != nullptr) {
      LOG(ERROR) << "Error when looking up parent class for type " << type
                 << " err " << err->code << " " << err->message;
      drgn_error_destroy(err);
      continue;
    }

    drgn_type* parent = drgn_utils::underlyingType(t.type);
    if (!isDrgnSizeComplete(parent)) {
      VLOG(1) << "Incomplete size for parent class (" << drgn_type_tag(parent)
              << ") of " << drgn_type_tag(type);
      continue;
    }

    const char* parentName = drgn_type_tag(parent);
    if (parentName == nullptr) {
      VLOG(1) << "No name for parent class (" << parent << ") of "
              << drgn_type_tag(type);
      continue;
    }

    /*
     * drgn pointers are not stable, so use string representation for reverse
     * mapping for now. We need to find a better way of creating this
     * childClasses map - ideally drgn would do this for us.
     */
    childClasses[parentName].push_back(type);
    VLOG(1) << drgn_type_tag(type) << "(" << type << ") is a child of "
            << drgn_type_tag(parent) << "(" << parent << ")";
  }

  return true;
}

/*
 * Build a mapping of Class -> Children
 *
 * drgn only gives us the mapping Class -> Parents, so we must iterate over all
 * types in the program to build the reverse mapping.
 */
bool OICodeGen::enumerateChildClasses() {
  if (!feature(Feature::PolymorphicInheritance)) {
    return true;
  }

  if ((setenv("DRGN_ENABLE_TYPE_ITERATOR", "1", 1)) < 0) {
    LOG(ERROR)
        << "Could not set DRGN_ENABLE_TYPE_ITERATOR environment variable";
    return false;
  }

  drgn_type_iterator* typesIterator;
  auto* prog = symbols.getDrgnProgram();
  drgn_error* err = drgn_type_iterator_create(prog, &typesIterator);
  if (err) {
    LOG(ERROR) << "Error initialising drgn_type_iterator: " << err->code << ", "
               << err->message;
    drgn_error_destroy(err);
    return false;
  }

  while (true) {
    drgn_qualified_type* t;
    err = drgn_type_iterator_next(typesIterator, &t);
    if (err) {
      LOG(ERROR) << "Error from drgn_type_iterator_next: " << err->code << ", "
                 << err->message;
      drgn_error_destroy(err);
      continue;
    }

    if (!t) {
      break;
    }

    auto kind = drgn_type_kind(t->type);
    if (kind != DRGN_TYPE_CLASS && kind != DRGN_TYPE_STRUCT) {
      continue;
    }

    if (!recordChildren(t->type)) {
      return false;
    }
  }

  drgn_type_iterator_destroy(typesIterator);
  return true;
}

// The top level function which enumerates the rootType object. This function
// fills out : -
// 1. struct/class definitions
// 2. function forward declarations
// 3. function definitions
//
// They are appended to the jit code in that order (1), (2), (3)
bool OICodeGen::populateDefsAndDecls() {
  if (!enumerateChildClasses()) {
    return false;
  }

  if (drgn_type_has_type(rootType.type) &&
      drgn_type_kind(rootType.type) == DRGN_TYPE_FUNCTION) {
    rootType = drgn_type_type(rootType.type);
  }

  auto* type = rootType.type;
  rootTypeToIntrospect = rootType;

  drgn_qualified_type qtype{};
  if (drgn_type_kind(type) == DRGN_TYPE_POINTER) {
    qtype = drgn_type_type(type);
    type = qtype.type;
    rootTypeToIntrospect = qtype;
  }

  auto typeName = typeToTransformedName(type);
  if (typeName.empty()) {
    return false;
  }
  if (typeName == "void") {
    LOG(ERROR) << "Argument to be introspected cannot be of type void";
    return false;
  }

  rootTypeStr = typeName;
  VLOG(1) << "Root type to introspect : " << rootTypeStr;

  return enumerateTypesRecurse(rootType.type);
}

std::optional<uint64_t> OICodeGen::getDrgnTypeSize(drgn_type* type) {
  uint64_t sz = 0;
  if (auto* err = drgn_type_sizeof(type, &sz); err != nullptr) {
    LOG(ERROR) << "dgn_type_sizeof(" << type << "): " << err->code << " "
               << err->message;
    drgn_error_destroy(err);

    auto typeName = getNameForType(type);
    if (!typeName.has_value()) {
      return std::nullopt;
    }

    for (auto& e : sizeMap) {
      if (typeName->starts_with(e.first)) {
        VLOG(1) << "Looked up " << *typeName << " in sizeMap";
        return e.second;
      }
    }

    LOG(ERROR) << "Failed to get size for " << *typeName << " " << type;
    return std::nullopt;
  }
  return sz;
}

bool OICodeGen::isDrgnSizeComplete(drgn_type* type) {
  uint64_t sz = 0;
  if (auto* err = drgn_type_sizeof(type, &sz); err != nullptr) {
    drgn_error_destroy(err);
  } else {
    return true;
  }

  if (drgn_type_kind(type) != DRGN_TYPE_ENUM) {
    std::string name;
    getDrgnTypeNameInt(type, name);

    for (auto& kv : sizeMap) {
      if (name.starts_with(kv.first)) {
        return true;
      }
    }
    VLOG(1) << "Failed to lookup size " << type << " " << name;
  }
  return false;
}

bool OICodeGen::enumerateClassParents(drgn_type* type,
                                      const std::string& typeName) {
  if (drgn_type_num_parents(type) == 0) {
    // Be careful to early exit here to avoid accidentally initialising
    // parentClasses when there are no parents.
    return true;
  }

  drgn_type_template_parameter* parents = drgn_type_parents(type);

  for (size_t i = 0; i < drgn_type_num_parents(type); ++i) {
    drgn_qualified_type t{};

    if (auto* err = drgn_template_parameter_type(&parents[i], &t);
        err != nullptr) {
      LOG(ERROR) << "Error when looking up parent class for type " << type
                 << " err " << err->code << " " << err->message;
      drgn_error_destroy(err);
      return false;
    }

    VLOG(2) << "Lookup parent for " << typeName << " " << type;

    if (!isDrgnSizeComplete(t.type)) {
      VLOG(2) << "Parent of " << typeName << " " << type << " is " << t.type
              << " which is incomplete";
      return false;
    }

    if (!OICodeGen::enumerateTypesRecurse(t.type)) {
      return false;
    }

    parentClasses[type].push_back({t.type, parents[i].bit_offset});
  }

  std::sort(parentClasses[type].begin(), parentClasses[type].end());

  return true;
}

bool OICodeGen::enumerateClassMembers(drgn_type* type,
                                      const std::string& typeName,
                                      bool& isStubbed) {
  drgn_type_member* members = drgn_type_members(type);

  for (size_t i = 0; i < drgn_type_num_members(type); ++i) {
    drgn_qualified_type t{};
    auto* err = drgn_member_type(&members[i], &t, nullptr);

    if (err != nullptr || !isDrgnSizeComplete(t.type)) {
      if (err != nullptr) {
        LOG(ERROR) << "Error when looking up member type " << err->code << " "
                   << err->message << " " << typeName << " " << members[i].name;
      }
      VLOG(1) << "Type " << typeName
              << " has an incomplete member; stubbing...";
      knownDummyTypeList.insert(type);
      isStubbed = true;
      return true;
    }

    std::string memberName;
    if (members[i].name != nullptr) {
      memberName.assign(members[i].name);
    }

    if (VLOG_IS_ON(2)) {
      std::string outName;
      getDrgnTypeNameInt(t.type, outName);
      VLOG(2) << "Processing member; type: " << outName << " " << t.type
              << " name: " << memberName;
    }

    if (!OICodeGen::enumerateTypesRecurse(t.type)) {
      return false;
    }
  }

  return true;
}

bool OICodeGen::enumerateClassTemplateParams(drgn_type* type,
                                             const std::string& typeName,
                                             bool& isStubbed) {
  bool ifStub = false;
  if (!getContainerTemplateParams(type, ifStub)) {
    LOG(ERROR) << "Failed to get container template params";
    return false;
  }

  if (ifStub) {
    VLOG(1) << "Type " << typeName
            << " has an incomplete size member in template params; stubbing...";
    knownDummyTypeList.insert(type);
    isStubbed = true;
    return true;
  }

  auto containerInfo = getContainerInfo(type);
  assert(containerInfo.has_value());
  containerTypesFuncDef.insert(*containerInfo);
  return true;
}

bool OICodeGen::ifGenerateMemberDefinition(const std::string& typeName) {
  return !isKnownType(typeName);
}

bool OICodeGen::generateMemberDefinition(drgn_type* type,
                                         std::string& typeName) {
  if (!getMemberDefinition(type)) {
    return false;
  }

  /* Unnamed type */
  if (typeName.empty()) {
    if (auto it = unnamedUnion.find(type); it != unnamedUnion.end()) {
      typeName = it->second;
    } else {
      LOG(ERROR) << "Failed to find type in unnamedUnion";
      return false;
    }

    uint64_t sz = 0;
    if (auto* err = drgn_type_sizeof(type, &sz); err != nullptr) {
      LOG(ERROR) << "Failed to get size: " << err->code << " " << err->message;
      drgn_error_destroy(err);
      return false;
    }
  }
  return true;
}

std::optional<std::pair<std::string_view, std::string_view>>
OICodeGen::isMemberToStub(const std::string& typeName,
                          const std::string& member) {
  auto it = std::ranges::find_if(membersToStub, [&](auto& s) {
    return typeName.starts_with(s.first) && s.second == member;
  });
  if (it == std::end(membersToStub)) {
    return std::nullopt;
  }
  return *it;
}

std::optional<std::string_view> OICodeGen::isTypeToStub(
    const std::string& typeName) {
  if (auto opt = isMemberToStub(typeName, "*")) {
    return std::ref(opt.value().first);
  }
  return std::nullopt;
}

bool OICodeGen::isTypeToStub(drgn_type* type, const std::string& typeName) {
  if (isTypeToStub(typeName)) {
    VLOG(1) << "Found type to stub ";
    knownDummyTypeList.insert(type);
    return true;
  }

  return false;
}

bool OICodeGen::isEmptyClassOrFunctionType(drgn_type* type,
                                           const std::string& typeName) {
  return (!isKnownType(typeName) && drgn_type_has_members(type) &&
          drgn_type_num_members(type) == 0);
}

/*
 * Returns true if the provided type represents a dynamic class.
 *
 * From the Itanium C++ ABI, a dynamic class is defined as:
 *   A class requiring a virtual table pointer (because it or its bases have
 *   one or more virtual member functions or virtual base classes).
 */
bool OICodeGen::isDynamic(drgn_type* type) const {
  if (!feature(Feature::PolymorphicInheritance) ||
      !drgn_type_has_virtuality(type)) {
    return false;
  }

  if (drgn_type_virtuality(type) != 0 /*DW_VIRTUALITY_none*/) {
    // Virtual class - not fully supported by OI yet
    return true;
  }

  drgn_type_member_function* functions = drgn_type_functions(type);
  for (size_t i = 0; i < drgn_type_num_functions(type); i++) {
    drgn_qualified_type t{};
    if (auto* err = drgn_member_function_type(&functions[i], &t)) {
      LOG(ERROR) << "Error when looking up member function for type " << type
                 << " err " << err->code << " " << err->message;
      drgn_error_destroy(err);
      continue;
    }
    if (drgn_type_virtuality(t.type) != 0 /*DW_VIRTUALITY_none*/) {
      // Virtual function
      return true;
    }
  }

  return false;
}

bool OICodeGen::enumerateClassType(drgn_type* type) {
  std::string typeName = getStructName(type);
  VLOG(2) << "Class name : " << typeName << " " << type;

  if (isTypeToStub(type, typeName)) {
    return true;
  }

  if (isKnownType(typeName)) {
    funcDefTypeList.insert(type);
    return true;
  }

  if (!isContainer(type)) {
    if (!enumerateClassParents(type, typeName)) {
      knownDummyTypeList.insert(type);
      return true;
    }

    bool isStubbed = false;
    VLOG(2) << "Processing members for class/union : " << typeName << " "
            << type;
    if (!enumerateClassMembers(type, typeName, isStubbed)) {
      return false;
    }
    if (isStubbed) {
      return true;
    }
  }

  if (isContainer(type)) {
    bool isStubbed = false;
    VLOG(1) << "Processing template params container: " << typeName << " "
            << type;

    if (!enumerateClassTemplateParams(type, typeName, isStubbed)) {
      return false;
    }
    if (isStubbed) {
      return true;
    }
  } else if (ifGenerateMemberDefinition(typeName)) {
    if (isDynamic(type)) {
      const auto& children = childClasses[drgn_type_tag(type)];
      for (const auto& child : children) {
        enumerateTypesRecurse(child);
      }
    }
    if (!generateMemberDefinition(type, typeName)) {
      return false;
    }
  } else if (isEmptyClassOrFunctionType(type, typeName)) {
    knownDummyTypeList.insert(type);
    VLOG(2) << "Empty class/function type " << type << " name " << typeName;
    return true;
  }

  funcDefTypeList.insert(type);
  return true;
}

bool OICodeGen::enumerateTypeDefType(drgn_type* type) {
  std::string typeName;
  if (drgn_type_has_name(type)) {
    typeName = drgn_type_name(type);
  }
  typeName = transformTypeName(type, typeName);
  VLOG(2) << "Transformed typename: " << typeName << " " << type;

  if (isTypeToStub(type, typeName)) {
    return true;
  }

  if (isKnownType(typeName)) {
    funcDefTypeList.insert(type);
    return true;
  }

  auto qtype = drgn_type_type(type);
  bool ret = enumerateTypesRecurse(qtype.type);
  std::string tname;

  if (drgn_type_has_tag(qtype.type)) {
    if (drgn_type_tag(qtype.type) != nullptr) {
      tname = drgn_type_tag(qtype.type);
    } else {
      if (drgn_type_kind(qtype.type) == DRGN_TYPE_UNION) {
        tname = getUnionName(qtype.type);
      } else {
        tname = getStructName(qtype.type);
      }

      typeName = tname;
      funcDefTypeList.insert(type);
    }
  } else if (drgn_type_has_name(qtype.type)) {
    tname = drgn_type_name(qtype.type);
  } else {
    uint64_t sz = 0;
    if (auto* err = drgn_type_sizeof(type, &sz); err != nullptr) {
      LOG(ERROR) << "Failed to get size: " << err->code << " " << err->message
                 << " " << typeName;
      drgn_error_destroy(err);
      return false;
    }

    if (sz == sizeof(uintptr_t)) {
      // This is a typedef'ed pointer
      tname = "uintptr_t";
    } else {
      LOG(ERROR) << "Failed to get typename: kind " << drgnKindStr(type);
      return false;
    }
  }

  typedefMap[typeName] = transformTypeName(qtype.type, tname);
  typedefTypes[type] = qtype.type;

  return ret;
}

bool OICodeGen::enumerateEnumType(drgn_type* type) {
  std::string typeName;

  if (drgn_type_tag(type) != nullptr) {
    typeName.assign(drgn_type_tag(type));
  } else {
    typeName = "UNNAMED";
  }

  if (isKnownType(typeName)) {
    return true;
  }

  std::string enumUnderlyingTypeStr;
  if (!getEnumUnderlyingTypeStr(type, enumUnderlyingTypeStr)) {
    return false;
  }
  VLOG(2) << "Converting enum " << enumUnderlyingTypeStr << " " << type
          << " tag: " << typeName;

  enumTypes.push_back(type);
  funcDefTypeList.insert(type);

  return true;
}

static drgn_type* getPtrUnderlyingType(drgn_type* type) {
  drgn_type* underlyingType = type;

  while (drgn_type_kind(underlyingType) == DRGN_TYPE_POINTER ||
         drgn_type_kind(underlyingType) == DRGN_TYPE_TYPEDEF) {
    underlyingType = drgn_type_type(underlyingType).type;
  }

  return underlyingType;
}

bool OICodeGen::enumeratePointerType(drgn_type* type) {
  // Not handling pointers right now. Pointer members in classes are going to be
  // tricky. If we enumerate objects from pointers there are many questions :-
  // 1. How to handle uninitialized pointers
  // 2. How to handle nullptr pointers
  // 3. How to handle cyclical references with pointers
  //    Very common for two structs/classes to have pointers to each other
  //    We will need to save previously encountered pointer values
  // 4. Smart pointers might make it easier to detect (1)/(2)

  bool ret = true;
  drgn_qualified_type qtype = drgn_type_type(type);
  funcDefTypeList.insert(type);

  // If type is a function pointer, directly store the underlying type in
  // pointerToTypeMap, so that TreeBuilder can easily replace function
  // pointers with uintptr_t
  drgn_type* utype = getPtrUnderlyingType(type);
  if (drgn_type_kind(utype) == DRGN_TYPE_FUNCTION) {
    VLOG(2) << "Type " << type << " is a function pointer to " << utype;
    utype->_private.oi_name = nullptr;
    pointerToTypeMap.emplace(type, utype);
    return ret;
  }

  pointerToTypeMap.emplace(type, qtype.type);
  drgn_type* underlyingType = drgn_utils::underlyingType(qtype.type);

  bool isComplete = isDrgnSizeComplete(underlyingType);
  if (drgn_type_kind(underlyingType) == DRGN_TYPE_FUNCTION || isComplete) {
    ret = enumerateTypesRecurse(qtype.type);
  } else if (!isComplete && drgn_type_kind(underlyingType) != DRGN_TYPE_VOID) {
    // If the underlying type is not complete create a stub for the pointer type
    knownDummyTypeList.insert(type);
  }

  return ret;
}

bool OICodeGen::enumeratePrimitiveType(drgn_type* type) {
  std::string typeName;

  if (!drgn_type_has_name(type)) {
    LOG(ERROR) << "Expected type to have a name: " << type;
    return false;
  }

  typeName = drgn_type_name(type);
  typeName = transformTypeName(type, typeName);

  funcDefTypeList.insert(type);
  return true;
}

bool OICodeGen::enumerateArrayType(drgn_type* type) {
  uint64_t ret = 0;

  if (auto* err = drgn_type_sizeof(type, &ret); err != nullptr) {
    LOG(ERROR) << "Error when looking up size from drgn " << err->code << " "
               << err->message << " " << std::endl;
    drgn_error_destroy(err);
    return false;
  }

  auto qtype = drgn_type_type(type);
  auto rval = enumerateTypesRecurse(qtype.type);

  VLOG(1) << "Array type sizeof " << rval << " len " << drgn_type_length(type);

  return true;
}

bool OICodeGen::enumerateTypesRecurse(drgn_type* type) {
  auto kind = drgn_type_kind(type);

  if (kind == DRGN_TYPE_VOID || kind == DRGN_TYPE_FUNCTION) {
    VLOG(1) << "Ignore type " << drgnKindStr(type);
    return true;
  }

  // We don't want to generate functions for the same type twice
  if (processedTypes.find(type) != processedTypes.end()) {
    VLOG(2) << "Already encountered " << type;
    return true;
  }

  std::string outName;
  getDrgnTypeNameInt(type, outName);

  drgn_qualified_type qtype = {type, {}};
  char* defStr = nullptr;
  std::string typeDefStr;

  if (drgn_format_type(qtype, &defStr) != nullptr) {
    LOG(ERROR) << "Failed to get formatted string for " << type;
    typeDefStr.assign("unknown");
  } else {
    typeDefStr.assign(defStr);
    free(defStr);
  }

  VLOG(1) << "START processing type: " << outName << " " << type << " "
          << drgnKindStr(type) << " {";

  g_level += 1;

  processedTypes.insert(type);

  bool ret = true;

  switch (kind) {
    case DRGN_TYPE_CLASS:
    case DRGN_TYPE_STRUCT:
    case DRGN_TYPE_UNION:
      ret = enumerateClassType(type);
      break;
    case DRGN_TYPE_ENUM:
      ret = enumerateEnumType(type);
      break;
    case DRGN_TYPE_TYPEDEF:
      ret = enumerateTypeDefType(type);
      break;
    case DRGN_TYPE_POINTER:
      ret = enumeratePointerType(type);
      break;
    case DRGN_TYPE_ARRAY:
      enumerateArrayType(type);
      break;
    case DRGN_TYPE_INT:
    case DRGN_TYPE_BOOL:
    case DRGN_TYPE_FLOAT:
      ret = enumeratePrimitiveType(type);
      break;

    default:
      LOG(ERROR) << "Unknown drgn type " << type;
      return false;
  }
  g_level -= 1;

  VLOG(1) << "END processing type: " << outName << " " << type << " "
          << drgnKindStr(type) << " ret:" << ret << " }";

  return ret;
}

std::optional<std::string> OICodeGen::getNameForType(drgn_type* type) {
  if (auto search = typeToNameMap.find(type); search != typeToNameMap.end()) {
    return search->second;
  }

  LOG(ERROR) << "Failed to find " << type;
  return std::nullopt;
}

void OICodeGen::getFuncDefClassMembers(
    std::string& code,
    drgn_type* type,
    std::unordered_map<std::string, int>& memberNames,
    bool skipPadding) {
  if (drgn_type_kind(type) == DRGN_TYPE_TYPEDEF) {
    // Handle case where parent is a typedef
    getFuncDefClassMembers(code, drgn_utils::underlyingType(type), memberNames);
    return;
  }

  if (parentClasses.find(type) != parentClasses.end()) {
    for (auto& p : parentClasses[type]) {
      // paddingIndexMap[type] already cover the parents' paddings,
      // so skip the parents' padding generation to avoid double counting
      getFuncDefClassMembers(code, p.type, memberNames, true);
    }
  }

  if (classMembersMap.find(type) == classMembersMap.end()) {
    return;
  }

  if (!skipPadding) {
    auto paddingIt = paddingIndexMap.find(type);
    if (paddingIt != paddingIndexMap.end()) {
      const auto& paddingRange = paddingIt->second;
      for (auto i = paddingRange.first; i < paddingRange.second; ++i) {
        code += "SAVE_SIZE(sizeof(t.__padding_" + std::to_string(i) + "));\n";
      }
    }
  }

  const auto& members = classMembersMap[type];

  bool captureThriftIsset = thriftIssetStructTypes.contains(type);
  if (captureThriftIsset) {
    assert(members[members.size() - 1].member_name == "__isset");
    auto name = *fullyQualifiedName(type);
    code += "using thrift_data = apache::thrift::TStructDataStorage<";
    code += name;
    code += ">;\n";
  }

  for (std::size_t i = 0; i < members.size(); ++i) {
    if (captureThriftIsset && i < members.size() - 1) {
      // Capture Thrift's isset value for each field, except __isset itself,
      // which we assume comes last
      assert(members[i].member_name != "__isset");
      std::string issetIdxStr =
          "thrift_data::isset_indexes[" + std::to_string(i) + "]";
      code += "{\n";
      code += "  if (&thrift_data::isset_indexes != nullptr &&\n";
      code += "      " + issetIdxStr + " != -1) {\n";
      code += "    SAVE_DATA(t.__isset.get(" + issetIdxStr + "));\n";
      code += "  } else {\n";
      code += "    SAVE_DATA(-1);\n";
      code += "  }\n";
      code += "}\n";
    }

    const auto& member = members[i];
    std::string memberName = member.member_name;
    std::replace(memberName.begin(), memberName.end(), '.', '_');

    deduplicateMemberName(memberNames, memberName);

    if (memberName.empty()) {
      continue;
    }
    /*
     * Check if the member is a bit field (bitFieldSize > 0).
     * If it is a bit field, we can't get its address with operator&.
     * If it is *NOT* a bit field, then we can print its address.
     */
    if (member.bit_field_size > 0) {
      code += "JLOG(\"" + memberName + " (bit_field)\\n\");\n";
    } else {
      code += "JLOG(\"" + memberName + " @\");\n";
      code += "JLOGPTR(&t." + memberName + ");\n";
    }

    code += "getSizeType(t." + memberName + ", returnArg);\n";
  }
}

void OICodeGen::enumerateDescendants(drgn_type* type, drgn_type* baseType) {
  auto it = childClasses.find(drgn_type_tag(type));
  if (it == childClasses.end()) {
    return;
  }

  // TODO this list may end up containing duplicates
  const auto& children = it->second;
  descendantClasses[baseType].insert(
      descendantClasses[baseType].end(), children.begin(), children.end());

  for (const auto& child : children) {
    enumerateDescendants(child, baseType);
  }
}

void OICodeGen::getFuncDefinitionStr(std::string& code,
                                     drgn_type* type,
                                     const std::string& typeName) {
  if (classMembersMap.find(type) == classMembersMap.end()) {
    return;
  }

  std::string funcName = "getSizeType";
  if (isDynamic(type)) {
    funcName = "getSizeTypeConcrete";
  }

  code +=
      "void " + funcName + "(const " + typeName + "& t, size_t& returnArg) {\n";

  std::unordered_map<std::string, int> memberNames;
  getFuncDefClassMembers(code, type, memberNames);

  code += "}\n";

  if (isDynamic(type)) {
    enumerateDescendants(type, type);
    auto it = descendantClasses.find(type);
    if (it == descendantClasses.end()) {
      return;
    }
    const auto& descendants = it->second;

    std::vector<std::pair<std::string, SymbolInfo>> concreteClasses;
    concreteClasses.reserve(descendants.size());

    for (const auto& child : descendants) {
      auto fqChildName = *fullyQualifiedName(child);

      // We must split this assignment and append because the C++ standard lacks
      // an operator for concatenating std::string and std::string_view...
      std::string childVtableName = "vtable for ";
      childVtableName += fqChildName;

      auto optVtableSym = symbols.locateSymbol(childVtableName, true);
      if (!optVtableSym) {
        LOG(ERROR) << "Failed to find vtable address for '" << childVtableName;
        LOG(ERROR) << "Falling back to non dynamic mode";
        concreteClasses.clear();
        break;
      }
      auto vtableSym = *optVtableSym;

      auto oiChildName = *getNameForType(child);
      concreteClasses.push_back({oiChildName, vtableSym});
    }

    for (const auto& child : concreteClasses) {
      const auto& className = child.first;
      code += "void getSizeTypeConcrete(const " + className +
              "& t, size_t& returnArg);\n";
    }

    code += std::string("void getSizeType(const ") + typeName +
            std::string("& t, size_t& returnArg) {\n");
    // The Itanium C++ ABI defines that the vptr must appear at the zero-offset
    // position in any class in which they are present.
    code += "  auto *vptr = *reinterpret_cast<uintptr_t * const *>(&t);\n";
    code += "  uintptr_t topOffset = *(vptr - 2);\n";
    code += "  uintptr_t vptrVal = reinterpret_cast<uintptr_t>(vptr);\n";

    for (size_t i = 0; i < concreteClasses.size(); i++) {
      // The vptr will point to *somewhere* in the vtable of this object's
      // concrete class. The exact offset into the vtable can vary based on a
      // number of factors, so we compare the vptr against the vtable range for
      // each possible class to determine the concrete type.
      //
      // This works for C++ compilers which follow the GNU v3 ABI, i.e. GCC and
      // Clang. Other compilers may differ.
      const auto& [className, vtableSym] = concreteClasses[i];
      uintptr_t vtableMinAddr = vtableSym.addr;
      uintptr_t vtableMaxAddr = vtableSym.addr + vtableSym.size;
      code += "  if (vptrVal >= 0x" +
              (boost::format("%x") % vtableMinAddr).str() + " && vptrVal < 0x" +
              (boost::format("%x") % vtableMaxAddr).str() + ") {\n";
      code += "    SAVE_DATA(" + std::to_string(i) + ");\n";
      code +=
          "    uintptr_t baseAddress = reinterpret_cast<uintptr_t>(&t) + "
          "topOffset;\n";
      code += "    getSizeTypeConcrete(*reinterpret_cast<const " + className +
              "*>(baseAddress), returnArg);\n";
      code += "    return;\n";
      code += "  }\n";
    }

    code += "  SAVE_DATA(-1);\n";
    code += "  getSizeTypeConcrete(t, returnArg);\n";
    code += "}\n";
  }
}

std::string OICodeGen::templateTransformType(const std::string& typeName) {
  std::string s;
  s.reserve(typeName.size());
  for (const auto& c : typeName) {
    if (c == '<' || c == '>' || c == ',' || c == ' ' || c == ':' || c == '(' ||
        c == ')' || c == '&' || c == '*' || c == '-' || c == '\'' || c == '[' ||
        c == ']') {
      s += '_';
    } else {
      s += c;
    }
  }
  return s;
}

std::string OICodeGen::structNameTransformType(const std::string& typeName) {
  std::string s;
  bool prevColon = false;

  s.reserve(typeName.size());
  for (const auto& c : typeName) {
    if (c == ':') {
      if (prevColon) {
        prevColon = false;
      } else {
        prevColon = true;
      }
    } else {
      if (prevColon) {
        s[s.size() - 1] = '_';
      }
      prevColon = false;
    }

    std::string valid_characters = " &*<>[](){},;:\n";
    if (std::isalnum(c) || valid_characters.find(c) != std::string::npos) {
      s += c;
    } else {
      s += '_';
    }
  }
  return s;
}

void OICodeGen::memberTransformName(
    std::map<std::string, std::string>& templateTransformMap,
    std::string& typeName) {
  std::vector<std::string> sortedTypes;

  for (auto& e : templateTransformMap) {
    sortedTypes.push_back(e.first);
  }

  std::sort(sortedTypes.begin(),
            sortedTypes.end(),
            [](const std::string& first, const std::string& second) {
              return first.size() > second.size();
            });

  for (auto& e : sortedTypes) {
    std::string search = e;
    std::string replace = templateTransformMap[e];
    boost::replace_all(typeName, search, replace);
  }
}

OICodeGen::SortedTypeDefMap OICodeGen::getSortedTypeDefMap(
    const std::map<drgn_type*, drgn_type*>& typedefTypeMap) {
  auto typeMap = typedefTypeMap;
  SortedTypeDefMap typedefVec;

  while (!typeMap.empty()) {
    for (auto it = typeMap.cbegin(); it != typeMap.cend();) {
      if (typeMap.find(it->second) == typeMap.end()) {
        typedefVec.push_back(std::make_pair(it->first, it->second));
        it = typeMap.erase(it);
      } else {
        ++it;
      }
    }
  }
  return typedefVec;
}

bool OICodeGen::getEnumUnderlyingTypeStr(drgn_type* e,
                                         std::string& enumUnderlyingTypeStr) {
  std::string name;
  if (drgn_type_tag(e) != nullptr) {
    name.assign(drgn_type_tag(e));
  } else {
    VLOG(2) << "Enum tag lookup failed";
  }

  uint64_t sz = 0;
  if (auto* err = drgn_type_sizeof(e, &sz); err != nullptr) {
    LOG(ERROR) << "Error when looking up size from drgn " << err->code << " "
               << err->message << " ";
    drgn_error_destroy(err);
    return false;
  }

  VLOG(2) << "Enum " << name << " size " << sz;

  switch (sz) {
    case 8:
      enumUnderlyingTypeStr = "uint64_t";
      break;
    case 4:
      enumUnderlyingTypeStr = "uint32_t";
      break;
    case 2:
      enumUnderlyingTypeStr = "uint16_t";
      break;
    case 1:
      enumUnderlyingTypeStr = "uint8_t";
      break;
    default:
      LOG(ERROR) << "Error invalid enum size " << name << " " << sz;
      return false;
  }

  return true;
}

bool OICodeGen::getDrgnTypeNameInt(drgn_type* type, std::string& outName) {
  std::string name;

  if (drgn_type_kind(type) == DRGN_TYPE_ENUM) {
    if (!getEnumUnderlyingTypeStr(type, name)) {
      return false;
    }
  } else if (drgn_type_has_tag(type)) {
    if (drgn_type_tag(type) != nullptr) {
      name.assign(drgn_type_tag(type));
    } else {
      if (drgn_type_kind(type) == DRGN_TYPE_UNION) {
        name = getUnionName(type);
      } else {
        name = getStructName(type);
      }
    }
  } else if (drgn_type_has_name(type)) {
    name.assign(drgn_type_name(type));
  } else if (drgn_type_kind(type) == DRGN_TYPE_POINTER) {
    drgn_type* underlyingType = getPtrUnderlyingType(type);
    if (feature(Feature::ChaseRawPointers) &&
        drgn_type_kind(underlyingType) != DRGN_TYPE_FUNCTION) {
      // For pointers, figure out name for the underlying type then add
      // appropriate number of '*'
      {
        int ptrDepth = 0;
        drgn_type* ut = type;
        while (drgn_type_kind(ut) == DRGN_TYPE_POINTER) {
          ut = drgn_type_type(ut).type;
          ptrDepth++;
        }

        std::string tmpName;
        if (!getDrgnTypeNameInt(ut, tmpName)) {
          LOG(ERROR) << "Failed to get name for type " << type;
          return false;
        }
        outName = tmpName + std::string(ptrDepth, '*');
        return true;
      }
    } else {
      name.assign("uintptr_t");
    }
  } else if (drgn_type_kind(type) == DRGN_TYPE_VOID) {
    name.assign("void");
  } else {
    VLOG(2) << "Failed to get tag/name for type " << type;
    return false;
  }

  name = transformTypeName(type, name);
  name = structNameTransformType(name);
  outName = name;

  return true;
}

bool OICodeGen::getDrgnTypeName(drgn_type* type, std::string& outName) {
  return getDrgnTypeNameInt(type, outName);
}

void OICodeGen::addTypeToName(drgn_type* type, std::string name) {
  VLOG(2) << "Trying to assign name to type: " << name << " " << type;

  if (typeToNameMap.find(type) != typeToNameMap.end()) {
    VLOG(2) << "Name already assigned to type: " << name << " " << type;
    return;
  }

  name = structNameTransformType(name);

  if (nameToTypeMap.find(name) != nameToTypeMap.end()) {
    VLOG(1) << "Name conflict : " << type << " " << name << " "
            << drgnKindStr(type) << " conflict with " << nameToTypeMap[name]
            << " " << drgnKindStr(nameToTypeMap[name]);

    if (std::ranges::find(structDefType, type) != structDefType.end() ||
        std::ranges::find(enumTypes, type) != enumTypes.end() ||
        typedefTypes.find(type) != typedefTypes.end() ||
        knownDummyTypeList.find(type) != knownDummyTypeList.end()) {
      int tIndex = 0;
      // Name clashes with another type. Attach an ID at the end and make sure
      // that name isn't already present for some other drgn type.
      //
      while (nameToTypeMap.find(name + "__" + std::to_string(tIndex)) !=
             nameToTypeMap.end()) {
        tIndex++;
      }

      if (name != "uint8_t" && name != "uint32_t" && name != "uintptr_t") {
        name = name + "__" + std::to_string(tIndex);
      } else {
        VLOG(1) << "Not renaming " << name;
      }
    }
  }

  VLOG(1) << "Assigned name to type: " << name << " " << type;

  typeToNameMap[type] = name;
  nameToTypeMap[name] = type;
}

void OICodeGen::getClassMembersIncludingParent(
    drgn_type* type, std::vector<DrgnClassMemberInfo>& out) {
  if (drgn_type_kind(type) == DRGN_TYPE_TYPEDEF) {
    // Handle case where parent is a typedef
    getClassMembersIncludingParent(drgn_utils::underlyingType(type), out);
    return;
  }

  if (parentClasses.find(type) != parentClasses.end()) {
    for (auto& parent : parentClasses[type]) {
      getClassMembersIncludingParent(parent.type, out);
    }
  }

  for (auto& mem : classMembersMap[type]) {
    out.push_back(mem);
  }
}

std::map<drgn_type*, std::vector<DrgnClassMemberInfo>>&
OICodeGen::getClassMembersMap() {
  for (auto& e : classMembersMap) {
    std::vector<DrgnClassMemberInfo> v;
    getClassMembersIncludingParent(e.first, v);
    classMembersMapCopy[e.first] = v;
  }

  for (auto& e : classMembersMapCopy) {
    VLOG(1) << "ClassCopy " << e.first << std::endl;
    for (auto& m : e.second) {
      VLOG(1) << "      " << m.type << "       " << m.member_name << std::endl;
    }
  }

  return classMembersMapCopy;
}

// 1. First iterate through structs which we have to define (i.e. structDefType
// and knownDummyTypeList) and apply templateTransformType.
// 2. After that, go through typedefList and while assigning name to type, make
// sure it is transformed (i.e. memberTransformName)
// 3. Do the same for funcDefTypeList

void OICodeGen::printAllTypes() {
  VLOG(2) << "Printing all types";
  VLOG(2) << "Classes";

  for (auto& e : classMembersMap) {
    VLOG(2) << "Class " << e.first;

    auto& members = e.second;
    for (auto& m : members) {
      VLOG(2) << "   " << m.member_name << " " << m.type;
    }
  }

  VLOG(2) << "Struct defs ";
  for (auto& e : structDefType) {
    VLOG(2) << "Defined struct " << e;
  }

  VLOG(2) << "FuncDef structs ";
  for (auto& e : funcDefTypeList) {
    VLOG(2) << "FuncDef struct " << e;
  }

  VLOG(2) << "Dummy structs ";
  for (const auto& e : knownDummyTypeList) {
    VLOG(2) << "Dummy struct " << e;
  }
}

void OICodeGen::printAllTypeNames() {
  VLOG(2) << "Printing all type names ";
  VLOG(2) << "Classes";

  for (auto& e : classMembersMap) {
    auto typeName = getNameForType(e.first);
    if (!typeName.has_value()) {
      continue;
    }

    VLOG(2) << "Class " << *typeName << " " << e.first;
    auto& members = e.second;
    for (auto& m : members) {
      VLOG(2) << "   " << m.member_name << " " << m.type;
    }
  }

  for (auto& kv : unnamedUnion) {
    VLOG(2) << "Unnamed union/struct " << kv.first << " " << kv.second;
  }

  VLOG(2) << "Structs defs ";
  for (auto& e : structDefType) {
    auto typeName = getNameForType(e);
    if (!typeName.has_value()) {
      continue;
    }

    VLOG(2) << "Defined struct " << *typeName << " " << e;
  }

  VLOG(2) << "\nFuncDef structs ";
  for (auto& e : funcDefTypeList) {
    auto typeName = getNameForType(e);
    if (!typeName.has_value()) {
      continue;
    }

    VLOG(2) << "FuncDef struct " << *typeName << " " << e;
  }

  VLOG(2) << "\nDummy structs ";

  for (auto& e : knownDummyTypeList) {
    auto typeName = getNameForType(e);
    if (!typeName.has_value()) {
      continue;
    }

    VLOG(2) << "Dummy struct " << *typeName << " " << e;
  }
}

bool OICodeGen::generateStructDef(drgn_type* e, std::string& code) {
  if (classMembersMap.find(e) == classMembersMap.end()) {
    LOG(ERROR) << "Failed to find in classMembersMap " << e;
    return false;
  }

  auto kind = drgn_type_kind(e);

  if (kind != DRGN_TYPE_STRUCT && kind != DRGN_TYPE_CLASS &&
      kind != DRGN_TYPE_UNION) {
    LOG(ERROR) << "Failed to read type";
    return false;
  }

  std::string generatedMembers;
  PaddingInfo paddingInfo{};
  bool violatesAlignmentRequirement = false;
  auto tmpStr = getNameForType(e);

  if (!tmpStr.has_value()) {
    return false;
  }

  auto sz = getDrgnTypeSize(e);
  if (!sz.has_value()) {
    return false;
  }

  VLOG(1) << "Generate members for " << *tmpStr << " " << e << " {";
  // paddingIndexMap saves the range of padding indexes used for the current
  // struct We save the current pad_index as the start of the range...
  auto startingPadIndex = pad_index;

  uint64_t offsetBits = 0;
  std::unordered_map<std::string, int> memberNames;
  if (!generateStructMembers(e,
                             memberNames,
                             generatedMembers,
                             offsetBits,
                             paddingInfo,
                             violatesAlignmentRequirement,
                             0)) {
    return false;
  }

  // After the members have been generated, we save the updated pad_index as the
  // end of the range
  if (startingPadIndex != pad_index) {
    [[maybe_unused]] auto [_, paddingInserted] =
        paddingIndexMap.emplace(e, std::make_pair(startingPadIndex, pad_index));
    assert(paddingInserted);
  }

  uint64_t offsetBytes = offsetBits / CHAR_BIT;
  if (drgn_type_kind(e) != DRGN_TYPE_UNION && *sz != offsetBytes) {
    VLOG(1) << "size mismatch " << e << " when generating drgn: " << *sz << " "
            << offsetBytes << " " << *tmpStr;
    // Special case when class is empty
    if (!(offsetBytes == 0 && *sz == 1)) {
    }
  } else {
    VLOG(2) << "Size matches " << *tmpStr << " " << e << " sz " << *sz
            << " offsetBytes " << offsetBytes;
  }

  std::string structDefinition;

  if (paddingInfo.paddingSize != 0 && feature(Feature::GenPaddingStats)) {
    structDefinition.append("/* offset    |  size */ ");
  }

  if (kind == DRGN_TYPE_STRUCT || kind == DRGN_TYPE_CLASS) {
    structDefinition.append("struct");
  } else if (kind == DRGN_TYPE_UNION) {
    structDefinition.append("union");
    auto alignment = getAlignmentRequirements(e);
    if (!alignment.has_value()) {
      return false;
    }
    structDefinition.append(" alignas(" +
                            std::to_string(*alignment / CHAR_BIT) + ")");
  }

  if (feature(Feature::PackStructs) &&
      (kind == DRGN_TYPE_STRUCT || kind == DRGN_TYPE_CLASS) &&
      violatesAlignmentRequirement && paddingInfo.paddingSize == 0) {
    structDefinition.append(" __attribute__((__packed__))");
  }

  structDefinition.append(" ");
  structDefinition.append(*tmpStr);
  structDefinition.append(" {\n");
  if (kind == DRGN_TYPE_UNION) {
    // Pad out unions
    structDefinition.append("char union_padding[" + std::to_string(*sz) +
                            "];\n");
  } else {
    structDefinition.append(generatedMembers);
  }
  VLOG(1) << "}";

  structDefinition.append("};\n");

  if (feature(Feature::GenPaddingStats)) {
    auto paddedStructFound = paddedStructs.find(*tmpStr);

    if (paddedStructFound == paddedStructs.end()) {
      if (paddingInfo.paddingSize || paddingInfo.isSetSize) {
        paddingInfo.structSize = offsetBytes;
        paddingInfo.paddingSize /= CHAR_BIT;
        paddingInfo.definition = structDefinition;

        paddingInfo.computeSaving();

        if (paddingInfo.savingSize > 0) {
          paddedStructs.insert({*tmpStr, paddingInfo});
        }
      }
    }
  }

  code.append(structDefinition);

  //  In FuncGen add static_assert for sizes from this array
  if (drgn_type_kind(e) != DRGN_TYPE_UNION) {
    topoSortedStructTypes.push_back(e);
  }

  return true;
}

bool OICodeGen::isNumMemberGreaterThanZero(drgn_type* type) {
  if (drgn_type_num_members(type) > 0) {
    return true;
  }

  if (parentClasses.find(type) != parentClasses.end()) {
    for (auto& p : parentClasses[type]) {
      auto* underlyingType = drgn_utils::underlyingType(p.type);
      if (isNumMemberGreaterThanZero(underlyingType)) {
        return true;
      }
    }
  }

  return false;
}

bool OICodeGen::addPadding(uint64_t padding_bits, std::string& code) {
  if (padding_bits == 0) {
    return false;
  }

  VLOG(1) << "Add padding bits " << padding_bits;
  if (padding_bits % CHAR_BIT != 0) {
    VLOG(1) << "WARNING: Padding not aligned to a byte";
  }

  if ((padding_bits / CHAR_BIT) > 0) {
    std::ostringstream info;

    bool isByteMultiple = padding_bits % CHAR_BIT == 0;

    if (isByteMultiple) {
      info << "/* XXX" << std::setw(3) << std::right << padding_bits / CHAR_BIT
           << "-byte hole  */ ";
    } else {
      info << "/* XXX" << std::setw(3) << std::right << padding_bits
           << "-bit  hole  */ ";
    }

    code.append(info.str());
    code.append("char __padding_" + std::to_string(pad_index++) + "[" +
                std::to_string(padding_bits / CHAR_BIT) + "];\n");
  }
  return true;
}

static inline void addSizeComment(bool genPaddingStats,
                                  std::string& code,
                                  size_t offset,
                                  size_t sizeInBits) {
  if (!genPaddingStats) {
    return;
  }

  bool isByteMultiple = sizeInBits % CHAR_BIT == 0;
  size_t sizeInBytes = (sizeInBits + CHAR_BIT - 1) / CHAR_BIT;

  std::ostringstream info;
  if (isByteMultiple) {
    info << "/* " << std::setw(10) << std::left << offset / CHAR_BIT << "| "
         << std::setw(5) << std::right << sizeInBytes << " */ ";
  } else {
    info << "/* " << std::setw(4) << std::left << offset / CHAR_BIT
         << ": 0   | " << std::setw(5) << std::right << sizeInBytes << " */ ";
  }
  code.append(info.str());
}

void OICodeGen::deduplicateMemberName(
    std::unordered_map<std::string, int>& memberNames,
    std::string& memberName) {
  if (!memberName.empty()) {
    auto srchRes = memberNames.find(memberName);
    if (srchRes == memberNames.end()) {
      memberNames[memberName] = 1;
    } else {
      auto newCurrentIndex = srchRes->second + 1;
      srchRes->second = newCurrentIndex;
      memberName += std::to_string(newCurrentIndex);
    }
  }
}

std::optional<uint64_t> OICodeGen::generateMember(
    const DrgnClassMemberInfo& m,
    std::unordered_map<std::string, int>& memberNames,
    uint64_t currOffsetBits,
    std::string& code,
    bool isInUnion) {
  // Generate unique name for member
  std::string memberName = m.member_name;
  deduplicateMemberName(memberNames, memberName);
  std::replace(memberName.begin(), memberName.end(), '.', '_');

  auto* memberType = m.type;
  auto szBytes = getDrgnTypeSize(memberType);

  if (!szBytes.has_value()) {
    return std::nullopt;
  }

  if (m.isStubbed) {
    code.append("char ");
    code.append(memberName);
    code.append("[");
    code.append(std::to_string(szBytes.value()));
    code.append("]; // Member stubbed per config's membersToStub\n");
    return currOffsetBits + 8 * szBytes.value();
  }

  if (drgn_type_kind(memberType) == DRGN_TYPE_ARRAY) {
    // TODO: No idea how to handle flexible array member or zero length array
    size_t elems = 1;

    drgn_type* arrayElementType = nullptr;
    drgn_utils::getDrgnArrayElementType(memberType, &arrayElementType, elems);
    auto tmpStr = getNameForType(arrayElementType);

    if (!tmpStr.has_value()) {
      return std::nullopt;
    }

    if (elems == 0 || isInUnion) {
      std::string memName = memberName + "[" + std::to_string(elems) + "]";
      code.append(*tmpStr);
      code.append(" ");
      code.append(memName);
      code.append(";");

      code.append("\n");

      if (isInUnion) {
        currOffsetBits = 0;
        VLOG(1) << "Member size: " << *szBytes * CHAR_BIT;
      }
    } else {
      code.append("std::array<");
      code.append(*tmpStr);
      code.append(", ");
      code.append(std::to_string(elems));
      code.append("> ");
      code.append(memberName);
      code.append(";\n");
      currOffsetBits = currOffsetBits + (*szBytes * CHAR_BIT);
    }
  } else {
    auto tmpStr = getNameForType(memberType);
    uint64_t memberSize = 0;

    if (!tmpStr.has_value()) {
      return std::nullopt;
    }

    if (m.bit_field_size > 0) {
      memberSize = m.bit_field_size;
    } else {
      auto drgnTypeSize = getDrgnTypeSize(memberType);
      if (!drgnTypeSize.has_value()) {
        return false;
      }
      memberSize = *drgnTypeSize * CHAR_BIT;
    }

    if (isInUnion) {
      currOffsetBits = 0;
      VLOG(1) << "Member size: " << memberSize;
    } else {
      addSizeComment(
          feature(Feature::GenPaddingStats), code, currOffsetBits, memberSize);
      currOffsetBits = currOffsetBits + memberSize;
    }

    code.append(*tmpStr);
    code.append(" ");
    code.append(memberName);
    if (m.bit_field_size > 0) {
      code.append(":" + std::to_string(m.bit_field_size));
    }
    code.append(";\n");
  }
  return currOffsetBits;
}

bool OICodeGen::generateParent(
    drgn_type* p,
    std::unordered_map<std::string, int>& memberNames,
    uint64_t& currOffsetBits,
    std::string& code,
    size_t offsetToNextMember) {
  // Parent class could be a typedef
  PaddingInfo paddingInfo{};
  bool violatesAlignmentRequirement = false;
  auto* underlyingType = drgn_utils::underlyingType(p);
  uint64_t offsetBits = 0;

  if (!generateStructMembers(underlyingType,
                             memberNames,
                             code,
                             offsetBits,
                             paddingInfo,
                             violatesAlignmentRequirement,
                             offsetToNextMember)) {
    return false;
  }

  // *Sigh* account for base class optimization
  auto typeSize = getDrgnTypeSize(underlyingType);
  if (!typeSize.has_value()) {
    return false;
  }

  if (*typeSize > 1 || isNumMemberGreaterThanZero(underlyingType)) {
    currOffsetBits += offsetBits;
  } else {
    VLOG(1) << "Zero members " << p << " skipping parent class "
            << underlyingType;
  }

  return true;
}

/*Helper function that returns the alignment constraints in bits*/
std::optional<uint64_t> OICodeGen::getAlignmentRequirements(drgn_type* e) {
  const uint64_t minimumAlignmentBits = CHAR_BIT;
  uint64_t alignmentRequirement = CHAR_BIT;
  std::string outName;
  std::optional<uint64_t> typeSize;

  switch (drgn_type_kind(e)) {
    case DRGN_TYPE_VOID:
    case DRGN_TYPE_FUNCTION:
      LOG(ERROR) << "Alignment req unhandled " << getDrgnTypeName(e, outName);
      return std::nullopt;
    case DRGN_TYPE_INT:
    case DRGN_TYPE_BOOL:
    case DRGN_TYPE_FLOAT:
    case DRGN_TYPE_ENUM:
      typeSize = getDrgnTypeSize(e);
      if (!typeSize.has_value()) {
        return std::nullopt;
      }

      return *typeSize * CHAR_BIT;
    case DRGN_TYPE_STRUCT:
    case DRGN_TYPE_CLASS:
    case DRGN_TYPE_UNION:
      if (isContainer(e)) {
        alignmentRequirement = 64;
      } else {
        auto numMembers = drgn_type_num_members(e);
        auto* members = drgn_type_members(e);
        for (size_t i = 0; i < numMembers; ++i) {
          drgn_qualified_type memberType{};
          if (drgn_member_type(&members[i], &memberType, nullptr) != nullptr) {
            continue;
          }
          size_t currentMemberAlignmentRequirement =
              getAlignmentRequirements(memberType.type)
                  .value_or(minimumAlignmentBits);
          alignmentRequirement =
              std::max(alignmentRequirement, currentMemberAlignmentRequirement);
        }
        for (size_t parentIndex = 0; parentIndex < parentClasses[e].size();
             ++parentIndex) {
          size_t parentAlignment =
              getAlignmentRequirements(parentClasses[e][parentIndex].type)
                  .value_or(minimumAlignmentBits);
          alignmentRequirement =
              std::max(alignmentRequirement, parentAlignment);
        }
      }

      return alignmentRequirement;
    case DRGN_TYPE_POINTER:
      return 64;
    case DRGN_TYPE_TYPEDEF:
    case DRGN_TYPE_ARRAY:
      auto* underlyingType = drgn_type_type(e).type;
      return getAlignmentRequirements(underlyingType);
  }
  return minimumAlignmentBits;
}

bool OICodeGen::generateStructMembers(
    drgn_type* e,
    std::unordered_map<std::string, int>& memberNames,
    std::string& code,
    uint64_t& out_offset_bits,
    PaddingInfo& paddingInfo,
    bool& violatesAlignmentRequirement,
    size_t offsetToNextMemberInSubclass) {
  if (classMembersMap.find(e) == classMembersMap.end()) {
    VLOG(1) << "Failed to find type in classMembersMap: " << e;
  }

  size_t parentIndex = 0;
  size_t memberIndex = 0;

  auto& members = classMembersMap[e];

  uint64_t currOffsetBits = 0;
  uint64_t alignmentRequirement = 8;

  bool hasParents = (parentClasses.find(e) != parentClasses.end());

  do {
    uint64_t memberOffsetBits = ULONG_MAX;

    if (memberIndex < members.size()) {
      memberOffsetBits = members[memberIndex].bit_offset;
    }

    uint64_t parentOffsetBits = ULONG_MAX;

    if (hasParents && parentIndex < parentClasses[e].size()) {
      parentOffsetBits = parentClasses[e][parentIndex].bit_offset;
    }

    if (memberOffsetBits == ULONG_MAX && parentOffsetBits == ULONG_MAX) {
      break;
    }

    // Truncate the typenames while printing. They can be huge.
    if (memberOffsetBits < parentOffsetBits) {
      std::string typeName;

      if (typeToNameMap.find(members[memberIndex].type) !=
          typeToNameMap.end()) {
        std::optional<std::string> tmpStr =
            getNameForType(members[memberIndex].type);
        if (!tmpStr.has_value())
          return false;

        typeName = std::move(*tmpStr);
      }

      VLOG(1) << "Add class member type: " << typeName.substr(0, 15) << "... "
              << members[memberIndex].type << " currOffset: " << currOffsetBits
              << " expectedOffset: " << memberOffsetBits;

      if (drgn_type_kind(e) != DRGN_TYPE_UNION) {
        if (currOffsetBits > memberOffsetBits) {
          LOG(ERROR) << "Failed to match " << e << " currOffsetBits "
                     << currOffsetBits << " memberOffsetBits "
                     << memberOffsetBits;
          return false;
        }
        if (addPadding(memberOffsetBits - currOffsetBits, code)) {
          paddingInfo.paddingSize += memberOffsetBits - currOffsetBits;
          paddingInfo.paddings.push_back(memberOffsetBits - currOffsetBits);
        }
        currOffsetBits = memberOffsetBits;
      }

      size_t prevOffsetBits = currOffsetBits;

      auto newCurrOffsetBits =
          generateMember(members[memberIndex],
                         memberNames,
                         currOffsetBits,
                         code,
                         drgn_type_kind(e) == DRGN_TYPE_UNION);

      if (!newCurrOffsetBits.has_value()) {
        return false;
      }
      currOffsetBits = *newCurrOffsetBits;

      // Determine whether this type is a Thrift struct. Only try and read the
      // isset values if the struct layout is as expected.
      // i.e. __isset must be the last member in the struct
      const auto& memberName = members[memberIndex].member_name;
      bool isThriftIssetStruct =
          typeName.starts_with("isset_bitset<") && memberName == "__isset";

      if (feature(Feature::CaptureThriftIsset) && isThriftIssetStruct &&
          memberIndex == members.size() - 1) {
        thriftIssetStructTypes.insert(e);
      }

      if (feature(Feature::GenPaddingStats)) {
        paddingInfo.isThriftStruct = isThriftIssetStruct;

        /*
         * Thrift has a __isset member for each field to represent that field
         * is "set" or not. One byte is normally used for each field(has
         * Unpacked in the name) by using @cpp.PackIsset annotation, we use
         * one bit for each field instead (has Packed in the name). We're
         * only looking for Unpacked __isset as those are the only ones we can
         * optimise
         */
        if (paddingInfo.isThriftStruct &&
            typeName.find("Unpacked") != std::string::npos) {
          size_t unpackedSize = (currOffsetBits - prevOffsetBits) / CHAR_BIT;
          size_t packedSize = (unpackedSize + CHAR_BIT - 1) / CHAR_BIT;
          size_t savingFromPacking = unpackedSize - packedSize;

          if (savingFromPacking > 0) {
            paddingInfo.isSetSize = unpackedSize;
            paddingInfo.isSetOffset = currOffsetBits / CHAR_BIT;
          }
        }
      }

      auto currentMemberAlignmentRequirement =
          getAlignmentRequirements(members[memberIndex].type);

      if (!currentMemberAlignmentRequirement.has_value()) {
        return false;
      }

      alignmentRequirement =
          std::max(alignmentRequirement, *currentMemberAlignmentRequirement);
      paddingInfo.alignmentRequirement = alignmentRequirement / CHAR_BIT;

      memberIndex++;
    } else if (parentOffsetBits < memberOffsetBits) {
      std::string typeName;

      if (typeToNameMap.find(parentClasses[e][parentIndex].type) !=
          typeToNameMap.end()) {
        std::optional<std::string> tmpStr =
            getNameForType(parentClasses[e][parentIndex].type);

        if (!tmpStr.has_value()) {
          return false;
        }

        typeName = std::move(*tmpStr);
        typeName.resize(15);
      }

      VLOG(1) << "Add parent type: " << typeName << "(...) "
              << parentClasses[e][parentIndex].type
              << " currOffset: " << currOffsetBits
              << " expectedOffset: " << parentOffsetBits;

      if (currOffsetBits > parentOffsetBits && parentOffsetBits != 0) {
        // If a parent is empty, drgn can return parentOffsetBits as 0. So,
        // unfortunately, this check needs to be loosened.
        LOG(ERROR) << "Failed to match " << e << " currOffsetBits "
                   << currOffsetBits << " parentOffsetBits "
                   << parentOffsetBits;
        return false;
      }

      if (parentOffsetBits > currOffsetBits) {
        addPadding(parentOffsetBits - currOffsetBits, code);
        paddingInfo.paddingSize += parentOffsetBits - currOffsetBits;
        paddingInfo.paddings.push_back(parentOffsetBits - currOffsetBits);
        currOffsetBits = parentOffsetBits;
      }

      auto parentTypeName = getNameForType(parentClasses[e][parentIndex].type);
      if (!parentTypeName.has_value()) {
        return false;
      }

      VLOG(1) << "Generating parent " << *parentTypeName << " "
              << parentClasses[e][parentIndex].type << " {";
      g_level += 1;

      size_t offsetToNextMember = 0;
      if (parentIndex + 1 < parentClasses[e].size()) {
        offsetToNextMember = parentClasses[e][parentIndex + 1].bit_offset;
      } else {
        if (memberOffsetBits != ULONG_MAX) {
          offsetToNextMember = memberOffsetBits - currOffsetBits;
        } else {  // when members.size() == 0
          offsetToNextMember = 0;
        }
      }

      if (!generateParent(parentClasses[e][parentIndex].type,
                          memberNames,
                          currOffsetBits,
                          code,
                          offsetToNextMember)) {
        return false;
      }

      auto currentMemberAlignmentRequirement =
          getAlignmentRequirements(parentClasses[e][parentIndex].type);

      if (!currentMemberAlignmentRequirement.has_value()) {
        return false;
      }

      alignmentRequirement =
          std::max(alignmentRequirement, *currentMemberAlignmentRequirement);
      paddingInfo.alignmentRequirement = alignmentRequirement / CHAR_BIT;

      g_level -= 1;
      VLOG(1) << "}";
      parentIndex++;
    } else {
      // if memberOffsetBits and parentOffsetBits are same, parent class must be
      // empty
      parentIndex++;
    }
  } while (true);

  auto szBytes = getDrgnTypeSize(e);
  if (!szBytes.has_value()) {
    return false;
  }

  if (drgn_type_kind(e) != DRGN_TYPE_UNION &&
      // If class is empty, class size is at least one. But don't add the
      // explicit padding. It can mess up with inheritance
      (*szBytes > 1 || isNumMemberGreaterThanZero(e))) {
    uint64_t szBits = (*szBytes * CHAR_BIT);

    if (offsetToNextMemberInSubclass && offsetToNextMemberInSubclass < szBits) {
      /* We are laying out the members of a parent class*/
      szBits = offsetToNextMemberInSubclass;
    }

    if (currOffsetBits < szBits) {
      uint64_t paddingBits = szBits - currOffsetBits;
      VLOG(1) << "Add padding to end of struct; curr " << currOffsetBits
              << " sz " << szBits;
      addPadding(paddingBits, code);
      paddingInfo.paddingSize += paddingBits;

      if (paddingInfo.isSetSize == 0) {
        /*
         * We already account for the unnaligned remainder at the end of a
         * struct if the struct has an Unpacked __is_set field. Example from
         * PaddingHunter.h:
         * 	if (isThriftStruct) {
         *		if (isSet_size) {
         *    	saving_size = saving_from_packing();
         *    	odd_sum += isSet_offset - saving_from_packing();
         *  	}
         *  }
         */
        paddingInfo.paddings.push_back(paddingBits);
      }
    }
    currOffsetBits = szBits;
  } else if (drgn_type_kind(e) == DRGN_TYPE_UNION) {
    currOffsetBits = (*szBytes * CHAR_BIT);
  }

  if (currOffsetBits % alignmentRequirement != 0) {
    violatesAlignmentRequirement = true;
  }

  VLOG(1) << "Returning " << e << " " << currOffsetBits;

  out_offset_bits = currOffsetBits;
  return true;
}

bool OICodeGen::generateStructDefs(std::string& code) {
  std::vector<drgn_type*> structDefTypeCopy = structDefType;
  std::map<drgn_type*, std::vector<ParentMember>> parentClassesCopy =
      parentClasses;

  while (!structDefTypeCopy.empty()) {
    for (auto it = structDefTypeCopy.cbegin();
         it != structDefTypeCopy.cend();) {
      drgn_type* e = *it;
      if (classMembersMap.find(e) == classMembersMap.end()) {
        LOG(ERROR) << "Failed to find in classMembersMap " << e;
        return false;
      }

      bool skip = false;

      // Make sure that all parent class types are defined
      if (parentClassesCopy.find(e) != parentClassesCopy.end()) {
        auto& parents = parentClassesCopy[e];
        for (auto& p : parents) {
          auto it2 = std::find(
              structDefTypeCopy.begin(), structDefTypeCopy.end(), p.type);
          if (it2 != structDefTypeCopy.cend()) {
            skip = true;
            break;
          }
        }
      }

      // Make sure that all member types are defined
      if (!skip) {
        auto& members = classMembersMap[e];
        for (auto& m : members) {
          auto* underlyingType = drgn_utils::underlyingType(m.type);

          if (underlyingType != e) {
            auto it2 = std::find(structDefTypeCopy.begin(),
                                 structDefTypeCopy.end(),
                                 underlyingType);
            if (it2 != structDefTypeCopy.cend()) {
              skip = true;
              break;
            }
          }
        }
      }

      auto typeName = getNameForType(e);
      if (!typeName.has_value()) {
        return false;
      }

      if (skip) {
        ++it;
        VLOG(1) << "Skipping " << *typeName << " " << e;
        continue;
      } else {
        VLOG(1) << "Generating struct " << *typeName << " " << e << " {";
        g_level += 1;

        if (!generateStructDef(e, code)) {
          return false;
        }

        g_level -= 1;
        VLOG(1) << "}";
        it = structDefTypeCopy.erase(it);
      }
    }
  }
  return true;
}

bool OICodeGen::addStaticAssertsForType(drgn_type* type,
                                        bool generateAssertsForOffsets,
                                        std::string& code) {
  auto struct_name = getNameForType(type);
  if (!struct_name.has_value()) {
    return false;
  }

  auto sz = getDrgnTypeSize(type);
  if (!sz.has_value()) {
    return false;
  }

  std::string assertStr =
      "static_assert(sizeof(" + *struct_name + ") == " + std::to_string(*sz);
  assertStr += ", \"Unexpected size of struct " + *struct_name + "\");\n";

  std::unordered_map<std::string, int> memberNames;
  if (generateAssertsForOffsets &&
      !staticAssertMemberOffsets(*struct_name, type, assertStr, memberNames)) {
    return false;
  }

  code.append(assertStr);
  return true;
}

/*
 * Generates a declaration for a given fully-qualified type.
 *
 * e.g. Given "nsA::nsB::Foo"
 *
 * The folowing is generated:
 *   namespace nsA {
 *   namespace nsB {
 *   struct Foo;
 *   } // nsB
 *   } // nsA
 */
void OICodeGen::declareThriftStruct(std::string& code, std::string_view name) {
  if (auto pos = name.find("::"); pos != name.npos) {
    auto ns = name.substr(0, pos);
    code += "namespace ";
    code += ns;
    code += " {\n";
    // Recurse, continuing from after the "::"
    declareThriftStruct(code, name.substr(pos + 2));
    code += "} // namespace ";
    code += ns;
    code += "\n";
  } else {
    code += "struct ";
    code += name;
    code += ";\n";
  }
}

bool OICodeGen::generateJitCode(std::string& code) {
  FuncGen::DeclareExterns(code);
  // Include relevant headers
  code.append("// relevant header includes -----\n");

  // We always generate functions for `std::array` since we transform
  // raw C arrays into equivalent instances of `std::array`
  // This is kind of hacky and introduces a dependency on `std_array.toml` even
  // when people may not expect it.
  {
    bool found = false;
    for (const auto& el : containerInfoList) {
      if (el->typeName == "std::array<") {
        containerTypesFuncDef.insert(*el);
        found = true;
        break;
      }
    }
    if (!found) {
      LOG(ERROR) << "an implementation for `std::array` is required but none "
                    "was found";
      return false;
    }
  }

  std::set<std::string> includedHeaders = config.defaultHeaders;
  for (const ContainerInfo& e : containerTypesFuncDef) {
    includedHeaders.insert(e.header);
  }

  // these headers are included in OITraceCode.cpp
  includedHeaders.erase("xmmintrin.h");
  includedHeaders.erase("utility");

  // Required for the offsetof() macro
  includedHeaders.insert("cstddef");

  if (config.features[Feature::JitTiming]) {
    includedHeaders.emplace("chrono");
  }

  for (const auto& e : includedHeaders) {
    code.append("#include <");
    code.append(e);
    code.append(">\n");
  }

  code.append(R"(
    // storage macro definitions -----
    #define SAVE_SIZE(val)
    #define SAVE_DATA(val)    StoreData(val, returnArg)
  )");

  code.append("namespace {\n");
  code.append("static struct Context {\n");
  code.append("  PointerHashSet<> pointers;\n");
  code.append("} ctx;\n");
  code.append("} // namespace\n");

  FuncGen::DefineJitLog(code, config.features);

  // The purpose of the anonymous namespace within `OIInternal` is that
  // anything defined within an anonymous namespace has internal-linkage,
  // and therefore won't appear in the symbol table of the resulting object
  // file. Both OIL and OID do a linear search through the symbol table for
  // the top-level `getSize` function to locate the probe entry point, so
  // by keeping the contents of the symbol table to a minimum, we make that
  // process faster.
  std::string definitionsCode;
  definitionsCode.append("namespace OIInternal {\nnamespace {\n");
  // Use relevant namespaces
  definitionsCode.append("// namespace uses\n");

  std::set<std::string> usedNamespaces = config.defaultNamespaces;
  for (const ContainerInfo& v : containerTypesFuncDef) {
    for (auto& e : v.ns) {
      usedNamespaces.insert(std::string(e));
    }
  }

  for (auto& e : usedNamespaces) {
    definitionsCode.append("using ");
    definitionsCode.append(e);
    definitionsCode.append(";\n");
  }

  printAllTypeNames();

  definitionsCode.append("// forward declarations -----\n");
  for (auto& e : structDefType) {
    if (drgn_type_kind(e) == DRGN_TYPE_STRUCT ||
        drgn_type_kind(e) == DRGN_TYPE_CLASS) {
      definitionsCode.append("struct");
    } else if (drgn_type_kind(e) == DRGN_TYPE_UNION) {
      definitionsCode.append("union");
    } else {
      LOG(ERROR) << "Failed to read type";
      return false;
    }

    definitionsCode.append(" ");
    if (typeToNameMap.find(e) == typeToNameMap.end()) {
      LOG(ERROR) << "Failed to find " << e << " in typeToNameMap ";
      return false;
    }
    auto typeName = getNameForType(e);
    if (!typeName.has_value()) {
      return false;
    }

    definitionsCode.append(typeName.value());
    definitionsCode.append(";\n");
  }

  definitionsCode.append("// stubbed classes -----\n");
  for (const auto& e : knownDummyTypeList) {
    std::string name;
    if (!getDrgnTypeName(e, name)) {
      return false;
    }

    auto typeName = getNameForType(e);
    if (!typeName.has_value()) {
      return false;
    }

    uint64_t sz = 0;
    if (auto* err = drgn_type_sizeof(e, &sz); err != nullptr) {
      bool shouldReturn = false;
      std::string knownTypeName;

      if (auto opt = isTypeToStub(*typeName)) {
        knownTypeName = opt.value();
      }

      LOG(ERROR) << "Failed to query size from drgn; look in sizeMap "
                 << knownTypeName << std::endl;

      if (auto it = sizeMap.find(knownTypeName); it != sizeMap.end()) {
        sz = it->second;
      } else {
        LOG(ERROR) << "Failed to get size: " << err->code << " " << err->message
                   << " " << e;
        shouldReturn = true;
      }

      drgn_error_destroy(err);

      if (shouldReturn) {
        return false;
      }
    }

    auto alignment = getAlignmentRequirements(e).value_or(CHAR_BIT) / CHAR_BIT;
    if (!isKnownType(name)) {
      definitionsCode.append("struct alignas(" + std::to_string(alignment) +
                             ") " + (*typeName) + " { char dummy[" +
                             std::to_string(sz) + "];};\n");
    } else {
      if (isTypeToStub(name)) {
        definitionsCode.append("struct alignas(" + std::to_string(alignment) +
                               ") " + (*typeName) + " { char dummy[" +
                               std::to_string(sz) + "];};\n");
      }
    }
  }

  definitionsCode.append("// enums -----\n");
  for (auto& e : enumTypes) {
    auto name = getNameForType(e);

    if (!name.has_value()) {
      return false;
    }

    uint64_t sz = 0;
    if (auto* err = drgn_type_sizeof(e, &sz); err != nullptr) {
      LOG(ERROR) << "Error when looking up size from drgn " << err->code << " "
                 << err->message << " ";
      drgn_error_destroy(err);
      return false;
    }

    switch (sz) {
      case 8:
        definitionsCode.append("typedef uint64_t " + *name + ";\n");
        break;
      case 4:
        definitionsCode.append("typedef uint32_t " + *name + ";\n");
        break;
      case 2:
        definitionsCode.append("typedef uint16_t " + *name + ";\n");
        break;
      case 1:
        definitionsCode.append("typedef uint8_t " + *name + ";\n");
        break;
      default:
        LOG(ERROR) << "Error invalid enum size " << *name << " " << sz;
        return false;
    }
  }

  // This is basically a topological sort. We need to make sure typedefs
  // are defined in the correct order.
  auto sortedTypeDefMap = getSortedTypeDefMap(typedefTypes);

  definitionsCode.append("// typedefs -----\n");

  // Typedefs
  for (auto const& e : sortedTypeDefMap) {
    auto tmpStr1 = getNameForType(e.first);
    auto tmpStr2 = getNameForType(e.second);

    if (!tmpStr1.has_value() || !tmpStr2.has_value()) {
      return false;
    }

    definitionsCode.append("typedef " + *tmpStr2 + " " + *tmpStr1 + ";\n");
  }

  definitionsCode.append("// struct definitions -----\n");

  if (!generateStructDefs(definitionsCode)) {
    return false;
  }

  funcGen.DeclareGetContainer(definitionsCode);
  definitionsCode.append("}\n\n} // namespace OIInternal\n");

  std::string functionsCode;
  functionsCode.append("namespace OIInternal {\nnamespace {\n");
  functionsCode.append("// functions -----\n");
  if (!funcGen.DeclareGetSizeFuncs(
          functionsCode, containerTypesFuncDef, config.features)) {
    LOG(ERROR) << "declaring get size for containers failed";
    return false;
  }

  if (feature(Feature::ChaseRawPointers)) {
    functionsCode.append(R"(
    template<typename T>
    void getSizeType(const T* t, size_t& returnArg);

    void getSizeType(const void *s_ptr, size_t& returnArg);
    )");
  }

  for (auto& e : structDefType) {
    if (drgn_type_kind(e) != DRGN_TYPE_UNION) {
      auto typeName = getNameForType(e);

      if (!typeName.has_value()) {
        return false;
      }

      funcGen.DeclareGetSize(functionsCode, *typeName);
    }
  }

  funcGen.DeclareStoreData(functionsCode);
  funcGen.DeclareEncodeData(functionsCode);
  funcGen.DeclareEncodeDataSize(functionsCode);

  if (!funcGen.DefineGetSizeFuncs(
          functionsCode, containerTypesFuncDef, config.features)) {
    LOG(ERROR) << "defining get size for containers failed";
    return false;
  }

  if (feature(Feature::ChaseRawPointers)) {
    functionsCode.append(R"(
    template<typename T>
    void getSizeType(const T* s_ptr, size_t& returnArg)
    {
      JLOG("ptr val @");
      JLOGPTR(s_ptr);
      StoreData((uintptr_t)(s_ptr), returnArg);
      if (s_ptr && ctx.pointers.add((uintptr_t)s_ptr)) {
        StoreData(1, returnArg);
        getSizeType(*(s_ptr), returnArg);
      } else {
        StoreData(0, returnArg);
      }
    }

    void getSizeType(const void *s_ptr, size_t& returnArg)
    {
      JLOG("void ptr @");
      JLOGPTR(s_ptr);
      StoreData((uintptr_t)(s_ptr), returnArg);
    }
    )");
  }

  for (auto& e : structDefType) {
    auto name = getNameForType(e);

    if (!name.has_value()) {
      return false;
    }

    if (name->find("TypedValue") == 0) {
      funcGen.DefineGetSizeTypedValueFunc(functionsCode, *name);
    } else if (drgn_type_kind(e) != DRGN_TYPE_UNION) {
      getFuncDefinitionStr(functionsCode, e, *name);
    }
  }

  funcGen.DefineStoreData(functionsCode);
  funcGen.DefineEncodeData(functionsCode);
  funcGen.DefineEncodeDataSize(functionsCode);

  for (auto& structType : structDefType) {
    // Don't generate member offset asserts for unions since we pad them out
    bool generateOffsetAsserts =
        (drgn_type_kind(structType) != DRGN_TYPE_UNION);

    if (!addStaticAssertsForType(
            structType, generateOffsetAsserts, functionsCode)) {
      return false;
    }
  }

  for (auto& container : containerTypeMapDrgn) {
    // Don't generate member offset asserts since we don't unwind containers
    if (!addStaticAssertsForType(container.first, false, functionsCode)) {
      return false;
    }
  }
  {
    auto* type = rootTypeToIntrospect.type;
    auto tmpStr = getNameForType(type);

    if (!tmpStr.has_value()) {
      return false;
    }

    std::string typeName = *tmpStr;

    // The top-level function needs to appear outside the `OIInternal`
    // namespace, or otherwise strange relocation issues may occur. We create a
    // `typedef` with a known name (`__ROOT_TYPE__`) *inside* the `OIInternal`
    // namespace so that we can refer to it from the top-level function without
    // having to worry about appropriately referring to types inside the
    // `OIInternal` namespace from outside of it, which would involve dealing
    // with all the complexities of prepending `OIInternal::` to not just the
    // root type but all of its (possible) template parameters.
    functionsCode.append("\ntypedef " + typeName + " __ROOT_TYPE__;\n");
    functionsCode.append("}\n\n} // namespace OIInternal\n");

    /* Start function definitions. First define top level func for root object
     */
    auto rawTypeName = drgn_utils::typeToName(type);
    if (rootTypeStr.starts_with("unique_ptr") ||
        rootTypeStr.starts_with("LowPtr") ||
        rootTypeStr.starts_with("shared_ptr")) {
      funcGen.DefineTopLevelGetSizeSmartPtr(
          functionsCode, rawTypeName, config.features);
    } else {
      funcGen.DefineTopLevelGetSizeRef(
          functionsCode, rawTypeName, config.features);
    }
  }

  // Add definitions for Thrift data storage types.
  // This is very brittle, but changes in the Thrift compiler should be caught
  // by our integration tests. It might be better to build the definition of
  // this struct from the debug info like we do for the types we're probing.
  // That would require significant refactoring of CodeGen, however.
  std::string thriftDefinitions;
  for (const auto& t : thriftIssetStructTypes) {
    auto fullyQualified = *fullyQualifiedName(t);
    declareThriftStruct(thriftDefinitions, fullyQualified);
    thriftDefinitions.append(R"(
      namespace apache { namespace thrift {
      template <> struct TStructDataStorage<)");
    thriftDefinitions.append(fullyQualified);
    thriftDefinitions.append(R"(> {
        static constexpr const std::size_t fields_size = 1; // Invalid, do not use
        static const std::array<folly::StringPiece, fields_size> fields_names;
        static const std::array<int16_t, fields_size> fields_ids;
        static const std::array<protocol::TType, fields_size> fields_types;

        static const std::array<folly::StringPiece, fields_size> storage_names;
        static const std::array<int, fields_size> __attribute__((weak)) isset_indexes;
      };
      }} // namespace thrift, namespace apache
    )");
  }

  code.append(definitionsCode);
  code.append(thriftDefinitions);
  code.append(functionsCode);

  std::ofstream ifs("/tmp/tmp_oid_output_2.cpp");
  ifs << code;
  ifs.close();

  return true;
}

bool OICodeGen::isUnnamedStruct(drgn_type* type) {
  return unnamedUnion.find(type) != unnamedUnion.end();
}

bool OICodeGen::generateNamesForTypes() {
  std::map<std::string, std::string> templateTransformMap;

  printAllTypes();

  for (auto& e : structDefType) {
    std::string name;
    if (!getDrgnTypeName(e, name)) {
      return false;
    }
    std::string tname = templateTransformType(name);
    tname = structNameTransformType(tname);

    templateTransformMap[name] = tname;
    addTypeToName(e, tname);
  }

  // This sucks, make sure enumTypes are named before typedefTypes
  // Otherwise there is a really confusing issue to untangle e.g.
  //   Assume groupa::groupb::OffendingType is an enum
  //   using OffendingType = groupa::groupb::OffendingType;
  //
  // This would result in 2 types a typedef and an enum. Make sure the
  // underlying enum is named first.
  for (auto& e : enumTypes) {
    if (drgn_type_tag(e)) {
      std::string name = drgn_type_tag(e);
      addTypeToName(e, name);
    } else {
      static int index = 0;
      std::string name = "__unnamed_enum_" + std::to_string(index);
      addTypeToName(e, name);
    }
  }

  for (auto& e : typedefTypes) {
    std::string name;
    if (!getDrgnTypeName(e.first, name)) {
      return false;
    }
    std::string tname = templateTransformType(name);
    tname = structNameTransformType(tname);
    addTypeToName(e.first, tname);
  }

  VLOG(1) << "Printing size map";
  for (auto& e : sizeMap) {
    VLOG(1) << "sizeMap[" << e.first << "] : " << e.second;
  }

  for (auto& e : knownDummyTypeList) {
    std::string name;
    if (!getDrgnTypeName(e, name)) {
      return false;
    }

    std::string tname = templateTransformType(name);

    if (tname.size() == 0) {
      static int index = 0;
      tname = "__unnamed_dummy_struct_" + std::to_string(index);
      index++;
    }
    tname = structNameTransformType(tname);
    templateTransformMap[name] = tname;
    addTypeToName(e, tname);
  }

  // funcDefTypeList should already include every type in classMembersMap?

  // This is rather unfortunate. Assume there is an type which is a pointer to
  // another concrete type, e.g. struct Foo and Foo *. If Foo is renamed
  // to Foo__0 because of a conflict, the pointer also needs to be renamed to
  // Foo__0 *. In the first pass of funcDefTypeList and typedefTypes, assign
  // names to all types except pointer types. Then in a second pass assign names
  // to pointers (and also handle multiple levels of pointer).

  // TODO: Just create a separate list for pointer types. That would avoid
  // having to iterate 2 times.

  // First pass, ignore pointer types
  for (const auto& e : funcDefTypeList) {
    if (isUnnamedStruct(e)) {
      bool found = false;
      for (auto& t : typedefTypes) {
        if (t.second == e && drgn_type_kind(t.second) != DRGN_TYPE_ENUM) {
          found = true;
          break;
        }
      }
      if (found) {
        continue;
      }
    } else if (drgn_type_kind(e) == DRGN_TYPE_POINTER) {
      continue;
    }

    std::string name;
    if (!getDrgnTypeName(e, name)) {
      return false;
    }
    memberTransformName(templateTransformMap, name);
    addTypeToName(e, name);
  }

  // First pass, ignore pointer types
  for (auto& e : typedefTypes) {
    if (isUnnamedStruct(e.second) &&
        drgn_type_kind(e.second) != DRGN_TYPE_ENUM) {
    } else {
      // Apply template transform to only to the type which is already known
      // i.e. for "typedef A B", only apply template transform to A

      if (drgn_type_kind(e.second) != DRGN_TYPE_POINTER) {
        std::string name;
        if (!getDrgnTypeName(e.second, name)) {
          return false;
        }
        memberTransformName(templateTransformMap, name);
        addTypeToName(e.second, name);
      }
    }
  }

  // Second pass, name the pointer types
  for (auto& e : funcDefTypeList) {
    if (drgn_type_kind(e) == DRGN_TYPE_POINTER) {
      int ptrDepth = 0;
      drgn_type* ut = e;
      while (drgn_type_kind(ut) == DRGN_TYPE_POINTER) {
        ut = drgn_type_type(ut).type;
        ptrDepth++;
      }

      auto ut_name = getNameForType(ut);
      if (ut_name.has_value()) {
        addTypeToName(e, (*ut_name) + std::string(ptrDepth, '*'));
      } else {
        std::string name;
        if (!getDrgnTypeName(e, name)) {
          return false;
        }
        memberTransformName(templateTransformMap, name);
        addTypeToName(e, name);
      }
    }
  }

  // Second pass, name the pointer types
  for (auto& e : typedefTypes) {
    if (drgn_type_kind(e.second) == DRGN_TYPE_POINTER) {
      int ptrDepth = 0;
      drgn_type* ut = e.second;
      while (drgn_type_kind(ut) == DRGN_TYPE_POINTER) {
        ut = drgn_type_type(ut).type;
        ptrDepth++;
      }

      auto utName = getNameForType(ut);
      if (utName.has_value()) {
        addTypeToName(e.second, (*utName) + std::string(ptrDepth, '*'));
      } else {
        std::string name;
        if (!getDrgnTypeName(e.second, name)) {
          return false;
        }
        memberTransformName(templateTransformMap, name);
        addTypeToName(e.second, name);
      }
    }
  }

  {
    // TODO: Do we need to apply a template transform to rootType
    std::string name;
    if (!getDrgnTypeName(rootType.type, name)) {
      return false;
    }
    addTypeToName(rootType.type, name);
  }
  return true;
}

bool OICodeGen::generate(std::string& code) {
  if (!populateDefsAndDecls()) {
    return false;
  }

  if (!generateNamesForTypes()) {
    return false;
  }

  if (!generateJitCode(code)) {
    return false;
  }

  return true;
}

/**
 * Generate static_asserts for the offsets of each member of the given type
 */
bool OICodeGen::staticAssertMemberOffsets(
    const std::string& struct_name,
    drgn_type* struct_type,
    std::string& assert_str,
    std::unordered_map<std::string, int>& memberNames,
    uint64_t base_offset) {
  if (knownDummyTypeList.find(struct_type) != knownDummyTypeList.end()) {
    return true;
  }

  if (drgn_type_kind(struct_type) == DRGN_TYPE_TYPEDEF) {
    // Operate on the underlying type for typedefs
    return staticAssertMemberOffsets(struct_name,
                                     drgn_utils::underlyingType(struct_type),
                                     assert_str,
                                     memberNames,
                                     base_offset);
  }

  const auto* tag = drgn_type_tag(struct_type);
  if (tag && isContainer(struct_type)) {
    // We don't generate members for container types
    return true;
  }

  if (parentClasses.find(struct_type) != parentClasses.end()) {
    // Recurse into parents to find inherited members
    for (const auto& parent : parentClasses[struct_type]) {
      auto parentOffset = base_offset + parent.bit_offset / CHAR_BIT;
      if (!staticAssertMemberOffsets(struct_name,
                                     parent.type,
                                     assert_str,
                                     memberNames,
                                     parentOffset)) {
        return false;
      }
    }
  }

  if (!drgn_type_has_members(struct_type)) {
    return true;
  }

  auto* members = drgn_type_members(struct_type);
  for (size_t i = 0; i < drgn_type_num_members(struct_type); ++i) {
    if (members[i].name == nullptr) {
      // Types can be defined in a class without assigning a member name e.g.
      // struct A { struct {int i;} ;}; is a valid struct with size 4.
      continue;
    }
    std::string memberName = members[i].name;
    deduplicateMemberName(memberNames, memberName);

    std::replace(memberName.begin(), memberName.end(), '.', '_');

    drgn_qualified_type memberQualType{};
    uint64_t bitFieldSize = 0;
    if (auto* err =
            drgn_member_type(&members[i], &memberQualType, &bitFieldSize);
        err != nullptr) {
      LOG(ERROR) << "Error when looking up member type " << err->code << " "
                 << err->message << " " << memberName;
      drgn_error_destroy(err);
      return false;
    }

    // Can't use "offsetof" on bitfield members
    if (bitFieldSize > 0) {
      continue;
    }

    auto expectedOffset = base_offset + members[i].bit_offset / CHAR_BIT;
    assert_str += "static_assert(offsetof(" + struct_name + ", " + memberName +
                  ") == " + std::to_string(expectedOffset) +
                  ", \"Unexpected offset of " + struct_name +
                  "::" + memberName + "\");\n";
  }

  return true;
}

std::map<std::string, PaddingInfo> OICodeGen::getPaddingInfo() {
  return paddedStructs;
}

drgn_qualified_type OICodeGen::getRootType() {
  return rootTypeToIntrospect;
};

void OICodeGen::setRootType(drgn_qualified_type rt) {
  rootType = rt;
}

TypeHierarchy OICodeGen::getTypeHierarchy() {
  std::map<
      struct drgn_type*,
      std::pair<ContainerTypeEnum, std::vector<struct drgn_qualified_type>>>
      containerTypeMap;
  for (auto const& [k, v] : containerTypeMapDrgn) {
    const ContainerInfo& cinfo = v.first;
    containerTypeMap[k] = {cinfo.ctype, v.second};
  }

  return {
      .classMembersMap = getClassMembersMap(),
      .containerTypeMap = std::move(containerTypeMap),
      .typedefMap = typedefTypes,
      .sizeMap = sizeMap,
      .knownDummyTypeList = knownDummyTypeList,
      .pointerToTypeMap = pointerToTypeMap,
      .thriftIssetStructTypes = thriftIssetStructTypes,
      .descendantClasses = descendantClasses,
  };
}

std::string OICodeGen::Config::toString() const {
  using namespace std::string_literals;

  // The list of ignored members must also be part of the remote hash
  std::string ignoreMembers = "IgnoreMembers=";
  for (const auto& ignore : membersToStub) {
    ignoreMembers += ignore.first;
    ignoreMembers += "::";
    ignoreMembers += ignore.second;
    ignoreMembers += ';';
  }

  return boost::algorithm::join(toOptions(), ",") + "," + ignoreMembers;
}

std::vector<std::string> OICodeGen::Config::toOptions() const {
  std::vector<std::string> options;
  options.reserve(allFeatures.size());

  for (const auto f : allFeatures) {
    if (features[f]) {
      options.emplace_back(std::string("-f") + featureToStr(f));
    } else {
      options.emplace_back(std::string("-F") + featureToStr(f));
    }
  }

  return options;
}

void OICodeGen::initializeCodeGen() {
  LOG(WARNING) << "OICodeGen::initializeCodeGen is no longer necessary";
};

}  // namespace oi::detail
