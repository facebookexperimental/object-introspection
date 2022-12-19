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
#include <map>
#include <string>
#include <vector>

struct PaddingInfo {
 public:
  PaddingInfo() = default;
  PaddingInfo(size_t strSize, int saveSz, size_t paddSz, size_t issetSz,
              std::string def, size_t instCnt)
      : structSize{strSize},
        alignmentRequirement{8},
        savingSize{static_cast<size_t>(saveSz)},
        paddingSize{paddSz},
        isSetSize{issetSz},
        isSetOffset{0},
        definition{def},
        instancesCnt{instCnt},
        isThriftStruct{false} {};

  size_t structSize;
  size_t alignmentRequirement;
  size_t savingSize;
  size_t paddingSize;
  size_t isSetSize;
  size_t isSetOffset;
  std::string definition;
  size_t instancesCnt;
  bool isThriftStruct;
  std::vector<size_t> paddings;

  size_t savingFromPacking() const {
    size_t unpackedSize = isSetSize;
    size_t packedSize = (unpackedSize + 8 - 1) / 8;

    return unpackedSize - packedSize;
  }

  void computeSaving() {
    /* Sum of members whose size is not multiple of alignment */
    size_t oddSum = 0;
    savingSize = 0;

    for (size_t padding : paddings) {
      oddSum += (alignmentRequirement - padding / 8);
    }

    if (isThriftStruct) {
      if (isSetSize) {
        savingSize = savingFromPacking();
        oddSum += isSetOffset - savingFromPacking();
      }

      savingSize +=
          paddingSize - (alignmentRequirement - oddSum % alignmentRequirement) %
                            alignmentRequirement;
    } else {
      savingSize = paddingSize;
    }
  }
};

class PaddingHunter {
 public:
  std::map<std::string, PaddingInfo> paddedStructs;
  std::map<std::string, PaddingInfo> localPaddedStructs;
  std::string paddingStatsFileName = "PADDING";

  // we do a max reduction on instance count across the probe points
  void processLocalPaddingInfo();

  void outputPaddingInfo();
};
