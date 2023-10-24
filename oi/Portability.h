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

namespace oi::detail {

#ifdef OI_META_INTERNAL

#define OI_PORTABILITY_META_INTERNAL() 1
static constexpr bool kIsMetaInternal = true;

#else

#define OI_PORTABILITY_META_INTERNAL() 0
static constexpr bool kIsMetaInternal = false;

#endif

}  // namespace oi::detail
