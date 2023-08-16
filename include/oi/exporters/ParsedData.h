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
#ifndef INCLUDED_OI_EXPORTERS_PARSED_DATA_H
#define INCLUDED_OI_EXPORTERS_PARSED_DATA_H 1

#include <oi/types/dy.h>

#include <cassert>
#include <cstdint>
#include <variant>
#include <vector>

namespace oi::exporters {

struct ParsedData {
  class Lazy {
   public:
    Lazy(std::vector<uint8_t>::const_iterator& it, types::dy::Dynamic ty)
        : it_(it), ty_(ty) {
    }

    ParsedData operator()() {
      return ParsedData::parse(it_, ty_);
    }

   private:
    std::vector<uint8_t>::const_iterator& it_;
    types::dy::Dynamic ty_;
  };

  struct Unit {};
  struct VarInt {
    uint64_t value;
  };
  struct Pair {
    Lazy first;
    Lazy second;
  };
  struct List {
    uint64_t length;
    Lazy values;
  };
  struct Sum {
    uint64_t index;
    Lazy value;
  };

  static ParsedData parse(std::vector<uint8_t>::const_iterator& it,
                          types::dy::Dynamic ty);

  ParsedData(Unit&& val_) : val(val_) {
  }
  ParsedData(VarInt&& val_) : val(val_) {
  }
  ParsedData(Pair&& val_) : val(val_) {
  }
  ParsedData(List&& val_) : val(val_) {
  }
  ParsedData(Sum&& val_) : val(val_) {
  }

  std::variant<Unit, VarInt, Pair, List, Sum> val;
};

}  // namespace oi::exporters

#endif
