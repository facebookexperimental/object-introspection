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
#include "oi/OILibraryImpl.h"
#include "oi/oi-jit.h"

namespace oi {

OILibrary::OILibrary(void* atomicHole,
                     std::unordered_set<Feature> fs,
                     GeneratorOptions opts)
    : pimpl_{std::make_unique<detail::OILibraryImpl>(
          atomicHole, std::move(fs), std::move(opts))} {
}
OILibrary::~OILibrary() {
}

std::pair<void*, const exporters::inst::Inst&> OILibrary::init() {
  return pimpl_->init();
}

}  // namespace oi
