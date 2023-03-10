# object-introspection
cmake_minimum_required(VERSION 3.19)
project(object-introspection)

# Lets find_program() locate SETUID binaries
cmake_policy(SET CMP0109 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)

add_compile_options(-ggdb3)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

add_compile_definitions(OSS_ENABLE)

include(FetchContent)
include(ProcessorCount)
include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)
include(cmake/CompilerWarnings.cmake)

option(STATIC_LINK "Statically link oid" OFF)
option(ASAN "Enable address sanitizer" OFF)
option(CODE_COVERAGE "Enable code coverage" OFF)
option(WITH_TESTS "Build with tests" Off)
option(FORCE_BOOST_STATIC "Build with static boost" On)
option(FORCE_LLVM_STATIC "Build with static llvm and clang" On)

if (ASAN)
  add_compile_options(-fsanitize=address -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address)
endif()

if (CODE_COVERAGE)
  add_compile_options(--coverage)
  add_link_options(--coverage)
endif()



## System checks
## These checks are potentially fatal so perform them first.

### (Re)download submodules
find_package(Git QUIET)

# TODO: No idea if this huge block is required, just picked from an example. There may be a short-hand.
if(GIT_FOUND AND EXISTS "${PROJECT_SOURCE_DIR}/.git")
# Update submodules as needed
    option(GIT_SUBMODULE "Check submodules during build" ON)
    if(GIT_SUBMODULE)
        message(STATUS "Submodule update")

        # This is a hack. If contents in drgn/libdrgn folder are not found, do a force checkout
        # If drgn/* is manually deleted (for whatever reason), git doesn't seem to re-pull the contents unless forced
        if (NOT EXISTS "${PROJECT_SOURCE_DIR}/extern/drgn/libdrgn")
          execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive --force
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        else()
          execute_process(COMMAND ${GIT_EXECUTABLE} submodule update --init --recursive
                        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
                        RESULT_VARIABLE GIT_SUBMOD_RESULT)
        endif()
        if(NOT GIT_SUBMOD_RESULT EQUAL "0")
            message(FATAL_ERROR "git submodule update --init failed with ${GIT_SUBMOD_RESULT}, please checkout submodules")
        endif()
    endif()
endif()

if(NOT EXISTS "${PROJECT_SOURCE_DIR}/extern/drgn")
    message(FATAL_ERROR "The submodules were not downloaded! GIT_SUBMODULE was turned off or failed. Please update submodules and try again.")
endif()

### Select Python version
find_program(PYTHON NAMES python3.8 python3)


### gflags (before glog)
find_package(gflags REQUIRED)

### tomlplusplus (for configuration files)
FetchContent_Declare(
    tomlplusplus
    GIT_REPOSITORY https://github.com/marzer/tomlplusplus.git
    GIT_TAG        4b166b69f28e70a416a1a04a98f365d2aeb90de8 # v3.2.0
)
FetchContent_MakeAvailable(tomlplusplus)

###??glog
FetchContent_Declare(
    glog
    GIT_REPOSITORY https://github.com/google/glog.git
    GIT_TAG        96a2f23dca4cc7180821ca5f32e526314395d26a
)
FetchContent_MakeAvailable(glog)

### bison & flex (for oid_parser)
find_package(BISON 3.5 REQUIRED)
find_package(FLEX)

### Boost
### Always use static linking with Boost, as some of its dependencies are not in the system's LD_LIBRARY_PATH.
if (FORCE_BOOST_STATIC)
  set(Boost_USE_STATIC_LIBS True)
endif()
find_package(Boost REQUIRED COMPONENTS
  system
  filesystem
  thread
  regex
  serialization
)
message(STATUS "Linking Boost libraries: ${Boost_LIBRARIES}")

### LLVM and Clang - Preferring Clang 12
find_package(LLVM 12 REQUIRED CONFIG)
message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

find_package(Clang REQUIRED CONFIG)
message(STATUS "Found Clang ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using ClangConfig.cmake in: ${Clang_DIR}")

### msgpack
# msgpack v3.0.0 doesn't define the msgpackc-cxx target, but since the library is header only,
# we can locate the header dir and add it to our include directories.
# Ideally, we would use a more modern version, like v3.3.0, and directly use the msgpackc-cxx target.
find_package(msgpack REQUIRED CONFIG)
get_target_property(MSGPACK_INCLUDE_DIRS msgpackc INTERFACE_INCLUDE_DIRECTORIES)
include_directories(SYSTEM ${MSGPACK_INCLUDE_DIRS})

### folly
### use folly as a header only library. some features won't be supported.
include_directories(SYSTEM ${CMAKE_CURRENT_SOURCE_DIR}/extern/folly)

### zstd (for rocksdb)
find_package(zstd REQUIRED)

### rocksdb
add_custom_target(librocksdb ALL
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extern/rocksdb
  COMMAND cmake -G Ninja -B build/ -DCMAKE_BUILD_TYPE=Release -DWITH_GFLAGS=Off -DWITH_LIBURING=Off -DWITH_ZSTD=On
  COMMAND cmake --build build/ --target rocksdb
  BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/extern/rocksdb/build/librocksdb.a
  COMMENT "Building RocksDB"
  USES_TERMINAL
)
set(ROCKSDB_PATH "${PROJECT_SOURCE_DIR}/extern/rocksdb")
include_directories(SYSTEM "${ROCKSDB_PATH}/include")

### drgn
# The setup.py script in drgn is really meant to build drgn (python
# debugger).  It shoves the C headers/lib in a temporary directory (which
# is named 'build' below using --build-temp flag).  It may(not) make sense
# to just build libdrgn manually.  Don't know how finicky the setup.py
# might be. These are the steps to manually build lib/headers and output
# to extern/drgn/libdrgn/build directory :-
#
# cd extern/drgn/libdrgn
# autoreconf -i .
# autoreconf -i ./elfutils
# mkdir build
# cd build
# ../configure
# make
#
# Since setup.py has a single cmd to do this, just use it for now.
#
# Another extemely annoying point. drgn pretty much has to be compiled with gcc only
# clang-12 does NOT work. clang fails with the following error :-
# configure: error: gcc with GNU99 support required

set(DRGN_CONFIGURE_FLAGS "--with-libkdumpfile=no")
if (ASAN)
  list(APPEND DRGN_CONFIGURE_FLAGS "--enable-asan=yes")
endif()
add_custom_target(libdrgn ALL
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/extern/drgn
  COMMAND unset BISON_PKGDATADIR && CC=gcc CFLAGS="${DRGN_CFLAGS}" CONFIGURE_FLAGS="${DRGN_CONFIGURE_FLAGS}" ${PYTHON} ./setup.py build --build-temp build
  BYPRODUCTS ${CMAKE_CURRENT_SOURCE_DIR}/extern/drgn/build/.libs/libdrgnimpl.a
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/drgn/build/velfutils/libdw/libdw.a
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/drgn/build/velfutils/libelf/libelf.a
  ${CMAKE_CURRENT_SOURCE_DIR}/extern/drgn/build/velfutils/libdwelf/libdwelf.a
  COMMENT "Building drgn"
  USES_TERMINAL
)
set(DRGN_PATH "${PROJECT_SOURCE_DIR}/extern/drgn/build")

# Ideally drgn stuff should be together at the end. But looks like rpath needs
# to be set before add_executable() unfortunately. Maybe split libdrgn stuff
# into a separate file later.
set(CMAKE_SKIP_BUILD_RPATH FALSE)
set(CMAKE_INSTALL_RPATH "${DRGN_PATH}/.libs")
set(CMAKE_BUILD_RPATH "${DRGN_PATH}/.libs")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)

include_directories(SYSTEM "${DRGN_PATH}")



if (STATIC_LINK)
  # glog links against the `gflags` target, which is an alias for `gflags_shared`
  # For static builds, we force it to link against `gflags_static` instead
  set_property(TARGET glog PROPERTY INTERFACE_LINK_LIBRARIES "gflags_static")
endif()

# FIXME: LLVM 12's source code is not compatible with C++20.
# We should check with the compiler team if we could apply a fix to our LLVM.
# In the meantime, we can compile OICompiler with C++17.
set_source_files_properties(src/OICompiler.cpp PROPERTIES COMPILE_FLAGS -std=c++17 SKIP_PRECOMPILE_HEADERS ON)



## OI Dependencies (linked to by output libraries and executables)
### OI Language Parser
BISON_TARGET(Parser src/OIParser.yy ${CMAKE_CURRENT_BINARY_DIR}/OIParser.tab.cpp
  DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/OIParser.tab.hh)
FLEX_TARGET(Lexer src/OILexer.l ${CMAKE_CURRENT_BINARY_DIR}/OILexer.cpp)

ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

add_library(oid_parser STATIC ${BISON_Parser_OUTPUTS} ${FLEX_Lexer_OUTPUTS})
target_link_libraries(oid_parser glog::glog)

### Core OI
add_subdirectory(src)

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/src)
add_library(oicore
  src/Descs.cpp
  src/Metrics.cpp
  src/OICache.cpp
  src/OICompiler.cpp
  src/OIUtils.cpp
  src/PaddingHunter.cpp
  src/Serialize.cpp
)
add_dependencies(oicore libdrgn)
set_project_warnings(oicore)
target_include_directories(oicore SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(oicore PRIVATE ${LLVM_DEFINITIONS})

llvm_map_components_to_libnames(llvm_libs core native mcjit x86disassembler)
target_link_libraries(oicore
  codegen

  ${Boost_LIBRARIES}
  Boost::headers
  glog::glog
)
if (FORCE_LLVM_STATIC)
  target_link_libraries(oicore
    clangCodeGen
    clangFrontend
  )
else()
  target_link_libraries(oicore
    clang-cpp
  )
endif()
# link the llvm_libs last as they must come after the clang dependencies in the
#??linker order
if (FORCE_LLVM_STATIC)
  target_link_libraries(oicore ${llvm_libs})
else()
  target_link_libraries(oicore LLVM)
endif()

target_link_libraries(oicore
  "-L${DRGN_PATH}/.libs"
  drgn
  dw
  pthread
)

###??TreeBuilder
add_library(treebuilder src/TreeBuilder.cpp)
add_dependencies(treebuilder librocksdb)
target_link_libraries(treebuilder
  ${ROCKSDB_PATH}/build/librocksdb.a
  oicore # overkill but it does need a lot of stuff
  zstd::zstd
)



## OI Outputs
### Object Introspection as a Library (OIL)
add_library(oil src/OILibrary.cpp src/OILibraryImpl.cpp)
target_include_directories(oil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(oil oicore)

###??Object Introspection as a Library Generator (OILGen)
add_executable(oilgen
  tools/OILGen.cpp
  src/OIGenerator.cpp
)
target_link_libraries(oilgen
  drgn_utils
  oicore
)

### Object Introspection cache Printer (OIP)
add_executable(oip tools/OIP.cpp)
set_project_warnings(oip)
target_link_libraries(oip oicore)

### Object Introspection Tree Builder (OITB)
add_executable(oitb tools/OITB.cpp)
set_project_warnings(oitb)
target_link_libraries(oitb oicore treebuilder)

### Object Introspection Debugger (OID)
add_executable(oid src/OID.cpp src/OIDebugger.cpp)
set_project_warnings(oid)

target_link_libraries(oid oicore oid_parser treebuilder)
if (STATIC_LINK)
  target_link_libraries(oid gflags_static)
else()
  target_link_libraries(oid gflags_shared)
endif()
target_link_libraries(oid oicore treebuilder)

### Object Introspection Tests
if (WITH_TESTS)
  add_subdirectory(test)
endif()



### Custom link options
if (STATIC_LINK)
  target_link_libraries(oicore -static)
  target_link_libraries(oil -static)
  target_link_libraries(oip -static)
  target_link_libraries(oid -static)
  target_link_libraries(oitb -static)
endif()



## Performance improvements
### Precompile headers
target_precompile_headers(oicore
  PUBLIC <fstream>
  PUBLIC <iostream>
  PUBLIC <map>
  PUBLIC <memory>
  PUBLIC <set>
  PUBLIC <string>
  PUBLIC <vector>
  PUBLIC <glog/logging.h>
)

foreach(TARGET oil oip oid oitb)
  target_precompile_headers(${TARGET} REUSE_FROM oicore)
endforeach()

# Add a hook for custom CMake rules
if (DEFINED ENV{CMAKE_HOOK})
  include($ENV{CMAKE_HOOK})
endif()
