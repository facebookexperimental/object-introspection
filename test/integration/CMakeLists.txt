file(GLOB_RECURSE INTEGRATION_TEST_CONFIGS
  RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
  CONFIGURE_DEPENDS
  *.toml
)

set(THRIFT_TEST_CONFIGS
  thrift_isset.toml
  thrift_isset_missing.toml
  thrift_namespaces.toml
  thrift_unions.toml
)

find_package(Thrift)
if (${THRIFT_FOUND})
  # Add test definition files requiring the Thrift compiler to this list:
  set(THRIFT_TESTS ${THRIFT_TEST_CONFIGS})
  list(TRANSFORM THRIFT_TESTS REPLACE ".toml$" "")
else()
  list(REMOVE_ITEM INTEGRATION_TEST_CONFIGS ${THRIFT_TEST_CONFIGS})
endif()

list(TRANSFORM INTEGRATION_TEST_CONFIGS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# Disable position independent executables that oid can't yet handle
add_link_options(-no-pie)

set(INTEGRATION_TEST_TARGET_SRC integration_test_target.cpp)
set(INTEGRATION_TEST_RUNNER_SRC integration_test_runner.cpp)

find_program(PYTHON_CMD NAMES python3.6 python3)

set(INTEGRATION_TEST_THRIFT_SRCS ${THRIFT_TESTS})
list(TRANSFORM INTEGRATION_TEST_THRIFT_SRCS APPEND ".thrift")

add_custom_command(
  OUTPUT
    ${INTEGRATION_TEST_TARGET_SRC}
    ${INTEGRATION_TEST_RUNNER_SRC}
    ${INTEGRATION_TEST_THRIFT_SRCS}
  COMMAND ${PYTHON_CMD}
    ${CMAKE_CURRENT_SOURCE_DIR}/gen_tests.py
    ${INTEGRATION_TEST_TARGET_SRC}
    ${INTEGRATION_TEST_RUNNER_SRC}
    ${INTEGRATION_TEST_CONFIGS}
  MAIN_DEPENDENCY gen_tests.py
  DEPENDS ${INTEGRATION_TEST_CONFIGS})

add_executable(integration_test_target
  ${INTEGRATION_TEST_TARGET_SRC}
  folly_shims.cpp)
target_compile_options(integration_test_target PRIVATE -O1)
target_link_libraries(integration_test_target PRIVATE oil_jit Boost::headers ${Boost_LIBRARIES})

add_executable(integration_test_runner ${INTEGRATION_TEST_RUNNER_SRC} runner_common.cpp)
target_include_directories(integration_test_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(integration_test_runner PRIVATE
  GTest::gmock_main
  Boost::headers
  ${Boost_LIBRARIES}
  range-v3
  toml
)
target_compile_definitions(integration_test_runner PRIVATE
  TARGET_EXE_PATH="$<TARGET_FILE:integration_test_target>"
  OID_EXE_PATH="$<TARGET_FILE:oid>"
  OILGEN_EXE_PATH="$<TARGET_FILE:oilgen>"
  CONFIG_FILE_PATH="${CMAKE_BINARY_DIR}/testing.oid.toml"

  CXX="${CMAKE_CXX_COMPILER}"
  TARGET_INCLUDE_DIRECTORIES="$<JOIN:$<TARGET_PROPERTY:integration_test_target,INCLUDE_DIRECTORIES>,:>"
)

if (${THRIFT_FOUND})
  foreach(THRIFT_TEST IN LISTS THRIFT_TESTS)
    set(THRIFT_SRC "${THRIFT_TEST}.thrift")
    set(THRIFT_TYPES_H "thrift/annotation/gen-cpp2/${THRIFT_TEST}_types.h")
    set(THRIFT_DATA_CPP "thrift/annotation/gen-cpp2/${THRIFT_TEST}_data.cpp")
    add_custom_command(
      OUTPUT
        ${THRIFT_TYPES_H}
        ${THRIFT_DATA_CPP}
      COMMAND
        ${THRIFT_COMPILER}
        -r
        --gen mstch_cpp2
        -o thrift/annotation/
        -I ${THRIFT_INCLUDE_DIRS}
        ${THRIFT_SRC}
      MAIN_DEPENDENCY ${THRIFT_SRC})

    add_custom_target(integration_test_thrift_sources_${THRIFT_TEST} DEPENDS ${THRIFT_TYPES_H})
    add_dependencies(integration_test_target integration_test_thrift_sources_${THRIFT_TEST})
    add_dependencies(integration_test_runner integration_test_thrift_sources_${THRIFT_TEST})
    target_sources(integration_test_target PRIVATE ${THRIFT_DATA_CPP})
  endforeach()

  target_include_directories(integration_test_target PRIVATE
    ${THRIFT_INCLUDE_DIRS}
    ${CMAKE_CURRENT_BINARY_DIR})
  target_link_libraries(integration_test_target PRIVATE glog::glog)
endif()

if (DEFINED ENV{CI})
  gtest_discover_tests(integration_test_runner EXTRA_ARGS "--verbose" "--preserve-on-failure")
else()
  gtest_discover_tests(integration_test_runner)
endif()
