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
#ifndef INCLUDED_OI_EXPORTERS_JSON_H
#define INCLUDED_OI_EXPORTERS_JSON_H 1

#include <oi/IntrospectionResult.h>

#include <ostream>

namespace oi::exporters {

class Json {
 public:
  Json(std::ostream& out);

  void print(const IntrospectionResult&);
  void print(IntrospectionResult::const_iterator& it,
             IntrospectionResult::const_iterator end);

  void setPretty(bool pretty) {
    pretty_ = pretty;
  }

 private:
  bool pretty_ = false;
  std::ostream& out_;
};

}  // namespace oi::exporters

#endif
