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

#include <exception>
#include <iterator>
#include <memory>
#include <span>
#include <sstream>
#include <variant>

extern "C" {
// Declare drgn structs and only refer to them by pointers to avoid exposing
// drgn.h.
struct drgn_error;
struct drgn_func_iterator;
struct drgn_program;
struct drgn_qualified_type;
struct drgn_symbol;
struct drgn_type;
}

namespace drgnplusplus {

class error : public std::exception {
 public:
  struct Deleter {
    void operator()(drgn_error* err) noexcept;
  };
  error(drgn_error* err) : ptr(err){};

  operator bool() const {
    return static_cast<bool>(ptr);
  }

  const char* what() const noexcept final;

 private:
  std::unique_ptr<drgn_error, Deleter> ptr;
};

struct SymbolsDeleter {
  void operator()(std::span<drgn_symbol*>*) noexcept;
};
using symbols = std::unique_ptr<std::span<drgn_symbol*>, SymbolsDeleter>;

class program {
 public:
  struct Deleter {
    void operator()(drgn_program* prog) noexcept;
  };

  program();
  program(drgn_program* prog) : ptr(prog){};

  symbols find_all_symbols();

  drgn_program* get() {
    return ptr.get();
  }

 private:
  std::unique_ptr<drgn_program, Deleter> ptr;
};

class func_iterator {
 public:
  using iterator_category = std::input_iterator_tag;
  using difference_type = std::ptrdiff_t;
  using value_type = drgn_qualified_type;
  using pointer = drgn_qualified_type*;
  using reference = drgn_qualified_type&;

  struct Deleter {
    void operator()(drgn_func_iterator* _iter) noexcept;
  };

  func_iterator(drgn_program* prog);
  func_iterator(program& prog);
  func_iterator() = default;
  func_iterator(drgn_func_iterator* _iter) : iter(_iter, Deleter()) {
  }

  reference operator*() const {
    return *current;
  }
  pointer operator->() {
    return current;
  }
  func_iterator& operator++();
  friend bool operator==(const func_iterator& a, const func_iterator& b) {
    return a.iter == b.iter;
  };
  friend bool operator!=(const func_iterator& a, const func_iterator& b) {
    return !(a == b);
  };

  func_iterator begin() {
    return ++(*this);
  }
  func_iterator end() {
    return func_iterator();
  }

 private:
  std::shared_ptr<drgn_func_iterator> iter = nullptr;
  pointer current = nullptr;
};

namespace symbol {
const char* name(drgn_symbol*);
}

}  // namespace drgnplusplus

namespace drgn_utils {
/*
 * These utils are not intended to be permanent. As part of the transition to
 * type-graph based CodeGen, we need to break dependencies on legacy OICodeGen
 * from other parts of OI.
 *
 * Parts of OICodeGen used by other parts of OI, but  which only return drgn
 * data can be moved here.
 */

void getDrgnArrayElementType(drgn_type* type,
                             drgn_type** outElemType,
                             size_t& outNumElems);
std::string typeToName(drgn_type* type);
bool isSizeComplete(struct drgn_type* type);
drgn_type* underlyingType(drgn_type* type);

}  // namespace drgn_utils
