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

#define LIST_OF_CONTAINER_TYPES  \
  X(UNKNOWN_TYPE)                \
  X(ARRAY_TYPE)                  \
  X(SMALL_VEC_TYPE)              \
  X(SET_TYPE)                    \
  X(UNORDERED_SET_TYPE)          \
  X(SEQ_TYPE)                    \
  X(LIST_TYPE)                   \
  X(STD_MAP_TYPE)                \
  X(STD_UNORDERED_MAP_TYPE)      \
  X(MAP_SEQ_TYPE)                \
  X(BY_MULTI_QRT_TYPE)           \
  X(F14_MAP)                     \
  X(F14_SET)                     \
  X(FEED_QUICK_HASH_SET)         \
  X(FEED_QUICK_HASH_MAP)         \
  X(RADIX_TREE_TYPE)             \
  X(PAIR_TYPE)                   \
  X(STRING_TYPE)                 \
  X(FOLLY_IOBUF_TYPE)            \
  X(FOLLY_IOBUFQUEUE_TYPE)       \
  X(FB_STRING_TYPE)              \
  X(UNIQ_PTR_TYPE)               \
  X(SHRD_PTR_TYPE)               \
  X(FB_HASH_MAP_TYPE)            \
  X(FB_HASH_SET_TYPE)            \
  X(FOLLY_OPTIONAL_TYPE)         \
  X(OPTIONAL_TYPE)               \
  X(TRY_TYPE)                    \
  X(REF_WRAPPER_TYPE)            \
  X(SORTED_VEC_SET_TYPE)         \
  X(REPEATED_FIELD_TYPE)         \
  X(CAFFE2_BLOB_TYPE)            \
  X(MULTI_MAP_TYPE)              \
  X(FOLLY_SMALL_HEAP_VECTOR_MAP) \
  X(CONTAINER_ADAPTER_TYPE)      \
  X(MICROLIST_TYPE)              \
  X(ENUM_MAP_TYPE)               \
  X(BOOST_BIMAP_TYPE)            \
  X(STD_VARIANT_TYPE)            \
  X(THRIFT_ISSET_TYPE)           \
  X(DUMMY_TYPE)                  \
  X(WEAK_PTR_TYPE)

enum ContainerTypeEnum {
#define X(name) name,
  LIST_OF_CONTAINER_TYPES
#undef X
};
