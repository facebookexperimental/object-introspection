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

add_executable(test_type_graph
  main.cpp
  test_add_children.cpp
  test_add_padding.cpp
  test_alignment_calc.cpp
  test_codegen.cpp
  test_drgn_parser.cpp
  test_flattener.cpp
  test_name_gen.cpp
  test_node_tracker.cpp
  test_remove_ignored.cpp
  test_remove_top_level_pointer.cpp
  test_topo_sorter.cpp
  test_type_identifier.cpp
  type_graph_utils.cpp
  TypeGraphParser.cpp
)
add_dependencies(test_type_graph integration_test_target)
target_compile_definitions(test_type_graph PRIVATE
  TARGET_EXE_PATH="${CMAKE_CURRENT_BINARY_DIR}/integration/integration_test_target"
)
target_link_libraries(test_type_graph
  codegen
  container_info
  type_graph

  ${GMOCK_MAIN_LIBS}
)
include(GoogleTest)
gtest_discover_tests(test_type_graph)

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

cpp_unittest(
  NAME test_container_info
  SRCS test_container_info.cpp
  DEPS oicore
)

cpp_unittest(
  NAME types_static_test
  SRCS ../oi/types/test/StaticTest.cpp
  DEPS oicore
)

cpp_unittest(
  NAME type_checking_walker_test
  SRCS ../oi/exporters/test/TypeCheckingWalkerTest.cpp
  DEPS treebuilder
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
