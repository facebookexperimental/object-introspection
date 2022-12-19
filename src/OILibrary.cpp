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
#include "OILibraryImpl.h"

bool debug = false;

namespace ObjectIntrospection {

bool operator==(const options& lhs, const options& rhs) {
  return lhs.configFilePath == rhs.configFilePath &&
         lhs.cacheDirPath == rhs.cacheDirPath &&
         lhs.debugFilePath == rhs.debugFilePath &&
         lhs.debugLevel == rhs.debugLevel &&
         lhs.chaseRawPointers == rhs.chaseRawPointers;
}

bool operator!=(const options& lhs, const options& rhs) {
  return !(lhs == rhs);
}

OILibrary::OILibrary(void* TemplateFunc, options opt) : opts(opt) {
  this->pimpl_ = new OILibraryImpl(this, TemplateFunc);
}

OILibrary::~OILibrary() {
  delete pimpl_;
}

int OILibrary::init() {
  if (!pimpl_->processConfigFile()) {
    return Response::OIL_BAD_CONFIG_FILE;
  }

  if (!pimpl_->mapSegment()) {
    return Response::OIL_SEGMENT_INIT_FAIL;
  }

  pimpl_->initCompiler();
  return pimpl_->compileCode();
}

int OILibrary::getObjectSize(void* ObjectAddr, size_t* size) {
  if (fp == nullptr) {
    return Response::OIL_UNINITIALISED;
  }

  *size = (*fp)(ObjectAddr);
  return Response::OIL_SUCCESS;
}
}  // namespace ObjectIntrospection
