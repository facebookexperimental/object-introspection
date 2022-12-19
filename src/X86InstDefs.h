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

static constexpr uint8_t int3Inst = 0xCC;
static constexpr uint8_t nopInst = 0x90;
static constexpr uint8_t movabsrdi0Inst = 0x48; /* movabs %rdi */
static constexpr uint8_t movabsrdi1Inst = 0xbf;
static constexpr uint8_t movabsrax0Inst = 0x48; /* movabs %rax */
static constexpr uint8_t movabsrax1Inst = 0xb8;
static constexpr uint8_t callRaxInst0Inst = 0xff;
static constexpr uint8_t callRaxInst1Inst = 0xd0;
static constexpr long syscallInsts = 0x9090909090050fcc;
