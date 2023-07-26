#pragma once

#include "gmock/gmock.h"
#include "oi/SymbolService.h"

namespace oi::detail {

class MockSymbolService : public SymbolService {
 public:
  MockSymbolService() {
  }
};

}  // namespace oi::detail
