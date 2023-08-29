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
#include <string_view>

namespace oi::detail::headers {

// These externs are provided by our build system. See resources/CMakeLists.txt
extern const std::string_view oi_IntrospectionResult_h;
extern const std::string_view oi_IntrospectionResult_inl_h;
extern const std::string_view oi_OITraceCode_cpp;
extern const std::string_view oi_exporters_ParsedData_h;
extern const std::string_view oi_exporters_inst_h;
extern const std::string_view oi_result_Element_h;
extern const std::string_view oi_types_dy_h;
extern const std::string_view oi_types_st_h;

}  // namespace oi::detail::headers
