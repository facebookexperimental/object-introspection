# Add new test definition files to this list:
set(INTEGRATION_TEST_CONFIGS
  anonymous.toml
  arrays.toml
  container_enums.toml
  cycles.toml
  enums.toml
  fbstring.toml
  ignored.toml
  inheritance_access.toml
  inheritance_multiple.toml
  inheritance_polymorphic.toml
  inheritance_polymorphic_diamond.toml
  inheritance_polymorphic_non_dynamic_base.toml
  multi_arg.toml
  namespaces.toml
  packed.toml
  padding.toml
  pointers.toml
  pointers_function.toml
  pointers_incomplete.toml
  primitives.toml
  references.toml
  simple.toml
  sorted_vector_set.toml
  std_array.toml
  std_conditional.toml
  std_deque_del_allocator.toml
  std_list_del_allocator.toml
  std_deque.toml
  std_map_custom_comparator.toml
  std_multimap_custom_comparator.toml
  std_optional.toml
  std_pair.toml
  std_queue.toml
  std_reference_wrapper.toml
  std_priority_queue.toml
  std_set_custom_comparator.toml
  std_smart_ptr.toml
  std_stack.toml
  std_string.toml
  std_unordered_map.toml
  std_unordered_map_custom_operator.toml
  std_unordered_set_custom_operator.toml
  std_variant.toml
  std_vector.toml
  std_vector_del_allocator.toml
  templates.toml
  typedefs.toml
  typedefed_parent.toml
)

find_package(Thrift)
if (${THRIFT_FOUND})
  # Add test definition files requiring the Thrift compiler to this list:
  set(THRIFT_TEST_CONFIGS
    thrift_isset.toml
    thrift_isset_missing.toml
    thrift_namespaces.toml
  )
  list(APPEND INTEGRATION_TEST_CONFIGS ${THRIFT_TEST_CONFIGS})
endif()

list(TRANSFORM INTEGRATION_TEST_CONFIGS PREPEND "${CMAKE_CURRENT_SOURCE_DIR}/")

# disable position independent executables that oid can't yet handle
# todo: update to more modern cmake syntax
set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -no-pie")

set(INTEGRATION_TEST_TARGET_SRC integration_test_target.cpp)
set(INTEGRATION_TEST_RUNNER_SRC integration_test_runner.cpp)

find_program(PYTHON_CMD NAMES python3.6 python3)

set(THRIFT_TESTS ${THRIFT_TEST_CONFIGS})
list(TRANSFORM THRIFT_TESTS REPLACE ".toml$" "")

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
target_link_libraries(integration_test_target PRIVATE oil Boost::headers ${Boost_LIBRARIES})

add_executable(integration_test_runner ${INTEGRATION_TEST_RUNNER_SRC} runner_common.cpp)
target_include_directories(integration_test_runner PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
# GMOCK_MAIN_LIBS is set in test/CMakeLists.txt
target_link_libraries(integration_test_runner PRIVATE
  ${GMOCK_MAIN_LIBS}
  Boost::headers
  ${Boost_LIBRARIES}
  tomlplusplus::tomlplusplus
)
target_compile_definitions(integration_test_runner PRIVATE
  TARGET_EXE_PATH="${CMAKE_CURRENT_BINARY_DIR}/integration_test_target"
  OID_EXE_PATH="$<TARGET_FILE:oid>"
  CONFIG_FILE_PATH="${CMAKE_BINARY_DIR}/testing.oid.toml")

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
