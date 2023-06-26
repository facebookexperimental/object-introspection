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

#include <array>
#include <string_view>

#include "oi/EnumBitset.h"

#define OI_FEATURE_LIST                         \
  X(ChaseRawPointers, "chase-raw-pointers")     \
  X(PackStructs, "pack-structs")                \
  X(GenPaddingStats, "gen-padding-stats")       \
  X(CaptureThriftIsset, "capture-thrift-isset") \
  X(TypeGraph, "type-graph")                    \
  X(TypedDataSegment, "typed-data-segment")     \
  X(GenJitDebug, "gen-jit-debug")               \
  X(JitLogging, "jit-logging")                  \
  X(PolymorphicInheritance, "polymorphic-inheritance")

namespace ObjectIntrospection {

enum class Feature {
  UnknownFeature,
#define X(name, _) name,
  OI_FEATURE_LIST
#undef X
};

Feature featureFromStr(std::string_view);
const char* featureToStr(Feature);

constexpr std::array allFeatures = {
#define X(name, _) Feature::name,
    OI_FEATURE_LIST
#undef X
};

// Use "size+1" to account for UnknownFeature"
using FeatureSet = EnumBitset<Feature, allFeatures.size() + 1>;

}  // namespace ObjectIntrospection
