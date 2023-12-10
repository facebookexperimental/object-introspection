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
#include "LLDBParser.h"

#include <glog/logging.h>

namespace oi::detail::type_graph {

Type& LLDBParser::parse(lldb::SBType* root) {
  // The following two lines silence a warning about unused variables.
  // Remove them once we have enough implementation to resolve the warning.
  typeGraph_.size();
  options_ = {};

  depth_ = 0;
  throw std::runtime_error("Not implemented yet!");
}

}  // namespace oi::detail::type_graph
