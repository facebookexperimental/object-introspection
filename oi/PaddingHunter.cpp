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
#include "oi/PaddingHunter.h"

#include <algorithm>
#include <fstream>

void PaddingHunter::processLocalPaddingInfo() {
  for (auto& lPS : localPaddedStructs) {
    if (paddedStructs.find(lPS.first) != paddedStructs.end()) {
      if (localPaddedStructs[lPS.first].instancesCnt >
          paddedStructs[lPS.first].instancesCnt) {
        paddedStructs[lPS.first].instancesCnt =
            localPaddedStructs[lPS.first].instancesCnt;
      }
    } else {
      paddedStructs[lPS.first] = lPS.second;
    }
  }
}

void PaddingHunter::outputPaddingInfo() {
  std::ofstream paddingStatsFile;
  paddingStatsFile.open(paddingStatsFileName);
  uint64_t sum = 0;

  std::vector<std::pair<std::string, PaddingInfo>> paddedStructsVec;
  for (auto& paddedStruct : paddedStructs) {
    paddedStructsVec.push_back({paddedStruct.first, paddedStruct.second});
  }

  for (auto& paddedStruct : paddedStructsVec) {
    sum += paddedStruct.second.paddingSize * paddedStruct.second.instancesCnt;
  }

  paddingStatsFile << "Total Saving Opportunity: " << sum << "\n\n\n";

  std::sort(paddedStructsVec.begin(), paddedStructsVec.end(),
            [](const std::pair<std::string, PaddingInfo>& left,
               const std::pair<std::string, PaddingInfo>& right) {
              return left.second.instancesCnt * left.second.savingSize >
                     right.second.instancesCnt * right.second.savingSize;
            });

  for (auto& paddedStruct : paddedStructsVec) {
    paddingStatsFile << "Name: " << paddedStruct.first
                     << ", object size: " << paddedStruct.second.structSize
                     << ", saving size: " << paddedStruct.second.savingSize
                     << ", padding size: " << paddedStruct.second.paddingSize
                     << ", isSet size: " << paddedStruct.second.isSetSize
                     << ", instance_cnt: " << paddedStruct.second.instancesCnt
                     << "\nSaving opportunity: "
                     << paddedStruct.second.savingSize *
                            paddedStruct.second.instancesCnt
                     << " bytes\n\n"
                     << paddedStruct.second.definition << "\n\n\n";
  }
  paddingStatsFile.close();
}
