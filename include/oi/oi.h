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
#ifndef INCLUDED_OI_OI_H
#define INCLUDED_OI_OI_H 1

#include <oi/IntrospectionResult.h>
#include <oi/types/dy.h>

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <vector>

namespace oi {

enum class Feature {
  ChaseRawPointers,
  CaptureThriftIsset,
  GenJitDebug,
};

#ifdef OIL_AOT_COMPILATION

template <class T, Feature... Fs>
IntrospectionResult __attribute__((weak)) introspectImpl(const T& objectAddr);

template <typename T, Feature... Fs>
IntrospectionResult introspect(const T& objectAddr) {
  if (!introspectImpl<T, Fs...>)
    throw std::logic_error(
        "OIL is expecting AoT compilation but it doesn't appear to have run.");

  return introspectImpl<T, Fs...>(objectAddr);
}

template <typename T, Feature... Fs>
std::optional<IntrospectionResult> tryIntrospect(const T& objectAddr) {
  if (!introspectImpl<T, Fs...>)
    return std::nullopt;

  // This checks twice but is necessary for compile time as it currently
  // depends on the presence of the strong symbol.
  return introspect(objectAddr);
}

#endif

}  // namespace oi

#ifndef OIL_AOT_COMPILATION
#include "oi/oi-jit.h"
#endif

#endif
