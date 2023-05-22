find_package(GTest REQUIRED)
if (TARGET GTest::gmock_main)
  # Only modern version of GTest defines the gmock_main target
  set(GMOCK_MAIN_LIBS GTest::gmock_main)
else()
  # To support older version of the lib,
  # We manually locate the lib and its dependencies instead
  find_library(GMOCK_MAIN_LIB NAMES libgmock_main.a REQUIRED)
  find_library(GMOCK_LIB NAMES libgmock.a REQUIRED)
  set(GMOCK_MAIN_LIBS ${GMOCK_MAIN_LIB} ${GMOCK_LIB} GTest::GTest)
endif()
message(STATUS "Using GMockMain: ${GMOCK_MAIN_LIBS}")

enable_testing()
include(GoogleTest)

function(cpp_unittest)
  cmake_parse_arguments(
    PARSE_ARGV 0
    TEST
    "" "NAME" "SRCS;DEPS;PROPERTIES"
  )

  add_executable(
    ${TEST_NAME}
    ${TEST_SRCS}
  )

  target_link_libraries(
    ${TEST_NAME}
    ${GMOCK_MAIN_LIBS} glog
    ${TEST_DEPS}
  )

  gtest_discover_tests(${TEST_NAME} PROPERTIES ${TEST_PROPERTIES})
endfunction()

add_executable(integration_mttest
  integration_mttest.cpp
)
target_link_libraries(integration_mttest folly_headers)

add_executable(integration_sleepy
  integration_sleepy.cpp
)
target_link_libraries(integration_sleepy folly_headers)

# Unit tests
cpp_unittest(
  NAME test_parser
  SRCS test_parser.cpp
  DEPS oid_parser
)

cpp_unittest(
  NAME test_compiler
  SRCS test_compiler.cpp
  DEPS oicore
)

# Integration tests
if (WITH_FLAKY_TESTS)
  add_test(
    NAME integration_py
    COMMAND python3 ${CMAKE_CURRENT_SOURCE_DIR}/integration.py
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
  )
  set_tests_properties(integration_py PROPERTIES
    TIMEOUT 90
    ENVIRONMENT "OID=$<TARGET_FILE:oid>"
  )
endif()

include_directories("${PROJECT_SOURCE_DIR}/extern/drgn/build")
add_subdirectory("integration")

