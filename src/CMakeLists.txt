add_library(drgn_utils DrgnUtils.cpp)
target_link_libraries(drgn_utils
  glog::glog

  "-L${DRGN_PATH}/.libs"
  drgn
)
add_dependencies(drgn_utils libdrgn)

add_library(symbol_service
  Descs.cpp
  SymbolService.cpp
)
target_link_libraries(symbol_service
  drgn_utils

  Boost::headers
  ${Boost_LIBRARIES}
  glog::glog

  dw
)

add_library(codegen
  ContainerInfo.cpp
  Features.cpp
  FuncGen.cpp
  OICodeGen.cpp
)
target_link_libraries(codegen
  symbol_service

  Boost::headers
  ${Boost_LIBRARIES}
  folly_headers
  glog::glog
  tomlplusplus::tomlplusplus
)
