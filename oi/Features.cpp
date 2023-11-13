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
#include "Features.h"

#include <glog/logging.h>

#include <map>
#include <numeric>
#include <optional>
#include <span>
#include <stdexcept>

namespace oi::detail {
namespace {

std::optional<std::string_view> featureHelp(Feature f) {
  switch (f) {
    case Feature::ChaseRawPointers:
      return "Chase raw pointers in the probed object.";
    case Feature::PackStructs:
      return "Pack structs.";
    case Feature::GenPaddingStats:
      return "Generate statistics on padding of structures.";
    case Feature::CaptureThriftIsset:
      return "Capture isset data for Thrift object.";
    case Feature::TypeGraph:
      return "Use Type Graph for code generation (CodeGen v2).";
    case Feature::PruneTypeGraph:
      return "Prune unreachable nodes from the type graph";
    case Feature::Library:
      return std::nullopt;  // Hide in OID help
    case Feature::TreeBuilderV2:
      return "Use Tree Builder v2 for reading the data segment";
    case Feature::GenJitDebug:
      return "Generate debug information for the JIT object.";
    case Feature::JitLogging:
      return "Log information from the JIT code for debugging.";
    case Feature::PolymorphicInheritance:
      return "Follow polymorphic inheritance hierarchies in the probed object.";
    case Feature::JitTiming:
      return "Instrument the JIT code with timing for performance testing.";

    case Feature::UnknownFeature:
      throw std::runtime_error("should not ask for help for UnknownFeature!");
  }
}

std::span<const Feature> requirements(Feature f) {
  switch (f) {
    case Feature::TreeBuilderV2:
      static constexpr std::array tb2 = {Feature::TypeGraph};
      return tb2;
    case Feature::Library:
      static constexpr std::array lib = {Feature::TreeBuilderV2};
      return lib;
    default:
      return {};
  }
}

std::span<const Feature> conflicts(Feature f) {
  switch (f) {
    case Feature::Library:
      static constexpr std::array lib = {Feature::JitLogging,
                                         Feature::JitTiming};
      return lib;
    default:
      return {};
  }
}

}  // namespace

Feature featureFromStr(std::string_view str) {
  static const std::map<std::string_view, Feature> nameMap = {
#define X(name, str) {str, Feature::name},
      OI_FEATURE_LIST
#undef X
  };

  if (auto search = nameMap.find(str); search != nameMap.end()) {
    return search->second;
  }
  return Feature::UnknownFeature;
}

const char* featureToStr(Feature f) {
  switch (f) {
#define X(name, str)  \
  case Feature::name: \
    return str;
    OI_FEATURE_LIST
#undef X

    default:
      return "UnknownFeature";
  }
}

void featuresHelp(std::ostream& out) {
  out << "FEATURES SUMMARY" << std::endl;

  size_t longestName = std::accumulate(
      allFeatures.begin(), allFeatures.end(), 0, [](size_t acc, Feature f) {
        return std::max(acc, std::string_view(featureToStr(f)).size());
      });

  for (Feature f : allFeatures) {
    if (const auto h = featureHelp(f)) {
      std::string_view name(featureToStr(f));
      out << "  " << name << std::string(longestName - name.size() + 2, ' ')
          << *h << std::endl;
    }
  }
}

std::optional<FeatureSet> handleFeatureConflicts(FeatureSet enabled,
                                                 const FeatureSet& disabled) {
  FeatureSet toCheck = enabled;
  while (toCheck.any()) {
    for (const auto f : allFeatures) {
      if (!toCheck[f])
        continue;
      toCheck[f] = false;

      for (const auto r : requirements(f)) {
        if (enabled[r])
          continue;
        if (disabled[r]) {
          LOG(ERROR) << featureToStr(f) << " feature requires "
                     << featureToStr(r) << "but it was explicitly disabled!";
          return std::nullopt;
        }
        enabled[r] = true;
        toCheck[r] = true;
        LOG(WARNING) << featureToStr(f) << " feature requires "
                     << featureToStr(r) << ", enabling it now.";
      }
    }
  }

  for (const auto f : allFeatures) {
    if (!enabled[f])
      continue;

    for (const auto c : conflicts(f)) {
      if (enabled[c]) {
        LOG(ERROR) << featureToStr(f) << " feature conflicts with "
                   << featureToStr(c) << " but both are enabled!";
        return std::nullopt;
      }
    }
  }

  return enabled;
}

}  // namespace oi::detail
