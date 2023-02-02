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

#include "DrgnUtils.h"

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

symbols program::find_symbols_by_name(const char* name) {
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
