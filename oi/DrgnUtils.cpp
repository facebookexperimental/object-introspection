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

#include "oi/DrgnUtils.h"

#include <glog/logging.h>

extern "C" {
#include <drgn.h>
}

namespace drgnplusplus {

void error::Deleter::operator()(drgn_error* err) noexcept {
  drgn_error_destroy(err);
}

const char* error::what() const noexcept {
  return ptr->message;
}

program::program() {
  struct drgn_program* prog;
  error err(drgn_program_create(NULL, &prog));
  if (err) {
    throw err;
  }
  ptr.reset(prog);
}

void program::Deleter::operator()(drgn_program* prog) noexcept {
  drgn_program_destroy(prog);
}

func_iterator::func_iterator(drgn_program* prog) {
  drgn_func_iterator* ret;
  error err(drgn_func_iterator_create(prog, &ret));
  if (err) {
    throw err;
  }
  iter.reset(ret, Deleter());
}

func_iterator::func_iterator(program& prog) : func_iterator(prog.get()){};

void func_iterator::Deleter::operator()(drgn_func_iterator* _iter) noexcept {
  drgn_func_iterator_destroy(_iter);
}

func_iterator& func_iterator::operator++() {
  auto err = drgn_func_iterator_next(iter.get(), &current);
  if (err) {
    throw error(err);
  }
  if (current == nullptr) {
    iter = nullptr;
  }
  return *this;
}

void SymbolsDeleter::operator()(std::span<drgn_symbol*>* syms) noexcept {
  drgn_symbols_destroy(syms->data(), syms->size());
  delete syms;
}

symbols program::find_all_symbols() {
  drgn_symbol** syms;
  size_t count;

  if (error err(
          drgn_program_find_symbols_by_name(ptr.get(), nullptr, &syms, &count));
      err) {
    throw err;
  }

  return std::unique_ptr<std::span<drgn_symbol*>, SymbolsDeleter>(
      new std::span(syms, count));
}

const char* symbol::name(drgn_symbol* sym) {
  return drgn_symbol_name(sym);
}

}  // namespace drgnplusplus

namespace drgn_utils {

void getDrgnArrayElementType(drgn_type* type,
                             drgn_type** outElemType,
                             size_t& outNumElems) {
  size_t elems = 1;

  // for multi dimensional arrays
  auto* arrayElementType = type;
  while (drgn_type_kind(arrayElementType) == DRGN_TYPE_ARRAY) {
    size_t l = drgn_type_length(arrayElementType);
    elems *= l;
    auto qtype = drgn_type_type(arrayElementType);
    arrayElementType = qtype.type;
  }

  *outElemType = arrayElementType;
  outNumElems = elems;
}

std::string typeToName(drgn_type* type) {
  std::string typeName;
  if (drgn_type_has_tag(type)) {
    const char* typeTag = drgn_type_tag(type);
    if (typeTag != nullptr) {
      typeName = typeTag;
    } else if (type->_private.oi_name != nullptr) {
      typeName = type->_private.oi_name;
    } else {
      typeName = "";
    }
    // TODO: Lookup unnamed union in type->string flag
  } else if (drgn_type_has_name(type)) {
    typeName = drgn_type_name(type);
  } else if (drgn_type_kind(type) == DRGN_TYPE_POINTER) {
    char* defStr = nullptr;
    drgn_qualified_type qtype = {type, {}};
    if (drgn_format_type_name(qtype, &defStr) != nullptr) {
      LOG(ERROR) << "Failed to get formatted string for " << type;
      typeName = "";
    } else {
      typeName.assign(defStr);
      free(defStr);
    }
  } else if (drgn_type_kind(type) == DRGN_TYPE_VOID) {
    return "void";
  } else if (drgn_type_kind(type) == DRGN_TYPE_ARRAY) {
    size_t elems = 1;
    drgn_type* arrayElementType = nullptr;
    getDrgnArrayElementType(type, &arrayElementType, elems);

    if (drgn_type_has_name(arrayElementType)) {
      typeName = drgn_type_name(arrayElementType);
    } else if (drgn_type_has_tag(arrayElementType)) {
      typeName = drgn_type_tag(arrayElementType);
    } else {
      LOG(ERROR) << "Failed4 to get typename ";
      return "";
    }
  } else {
    LOG(ERROR) << "Failed3 to get typename ";
    return "";
  }
  return typeName;
}

// This function is similar to OICodeGen::isDrgnSizeComplete(), but does not
// special-case folly::SharedMutex. This needs some more investigation and may
// need to be changed in the future.
bool isSizeComplete(struct drgn_type* type) {
  uint64_t sz;
  struct drgn_error* err = drgn_type_sizeof(type, &sz);
  bool isComplete = (err == nullptr);
  drgn_error_destroy(err);
  return isComplete;
}

/*
 * underlyingType
 *
 * Recurses through typedefs to return the underlying concrete type.
 */
drgn_type* underlyingType(drgn_type* type) {
  auto* underlyingType = type;

  while (drgn_type_kind(underlyingType) == DRGN_TYPE_TYPEDEF) {
    underlyingType = drgn_type_type(underlyingType).type;
  }

  return underlyingType;
}

}  // namespace drgn_utils
