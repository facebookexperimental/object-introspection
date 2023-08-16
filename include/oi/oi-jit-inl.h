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
#if defined(INCLUDED_OI_OI_JIT_INL_H) || !defined(INCLUDED_OI_OI_JIT_H)
static_assert(
    false,
    "oi-jit-inl.h provides inline declarations for oi-jit.h and should only "
    "be included by oi-jit.h");
#endif
#define INCLUDED_OI_OI_JIT_INL_H 1

#include <stdexcept>

#include "oi-jit.h"

namespace oi {

template <typename T, Feature... Fs>
inline std::optional<IntrospectionResult> setupAndIntrospect(
    const T& objectAddr, const GeneratorOptions& opts) {
  if (!CodegenHandler<T, Fs...>::init(opts))
    return std::nullopt;

  return CodegenHandler<T, Fs...>::introspect(objectAddr);
}

template <typename T, Feature... Fs>
inline std::atomic<bool>& CodegenHandler<T, Fs...>::getIsCritical() {
  static std::atomic<bool> isCritical = false;
  return isCritical;
}

template <typename T, Feature... Fs>
inline std::atomic<void (*)(const T&, std::vector<uint8_t>&)>&
CodegenHandler<T, Fs...>::getIntrospectionFunc() {
  static std::atomic<void (*)(const T&, std::vector<uint8_t>&)> func = nullptr;
  return func;
}

template <typename T, Feature... Fs>
inline std::atomic<const exporters::inst::Inst*>&
CodegenHandler<T, Fs...>::getTreeBuilderInstructions() {
  static std::atomic<const exporters::inst::Inst*> ty = nullptr;
  return ty;
}

template <typename T, Feature... Fs>
inline bool CodegenHandler<T, Fs...>::init(const GeneratorOptions& opts) {
  if (getIntrospectionFunc().load() != nullptr &&
      getTreeBuilderInstructions().load() != nullptr)
    return true;  // already initialised
  if (getIsCritical().exchange(true))
    return false;  // other thread is initialising/has failed

  auto lib =
      OILibrary(reinterpret_cast<void*>(&getIntrospectionFunc), {Fs...}, opts);
  auto [vfp, ty] = lib.init();

  getIntrospectionFunc().store(reinterpret_cast<func_type>(vfp));
  getTreeBuilderInstructions().store(&ty);
  return true;
}

template <typename T, Feature... Fs>
inline IntrospectionResult CodegenHandler<T, Fs...>::introspect(
    const T& objectAddr) {
  func_type func = getIntrospectionFunc().load();
  const exporters::inst::Inst* ty = getTreeBuilderInstructions().load();

  if (func == nullptr || ty == nullptr)
    throw std::logic_error("introspect(const T&) called when uninitialised");

  std::vector<uint8_t> buf;
  static_assert(sizeof(std::vector<uint8_t>) == 24);
  func(objectAddr, buf);
  return IntrospectionResult{std::move(buf), *ty};
}

}  // namespace oi
