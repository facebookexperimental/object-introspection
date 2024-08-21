# object-introspection
cmake_minimum_required(VERSION 3.24)
project(object-introspection)

# Lets find_program() locate SETUID binaries
cmake_policy(SET CMP0109 NEW)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED True)
set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${CMAKE_SOURCE_DIR}/cmake")

include(FetchContent)
set(FETCHCONTENT_QUIET FALSE)

include(ProcessorCount)
include(cmake/StandardProjectSettings.cmake)
include(cmake/PreventInSourceBuilds.cmake)
include(cmake/CompilerWarnings.cmake)

option(STATIC_LINK "Statically link oid" OFF)
option(ASAN "Enable address sanitizer" OFF)
option(CODE_COVERAGE "Enable code coverage" OFF)
option(WITH_TESTS "Build with tests" ON)
option(WITH_FLAKY_TESTS "Build with flaky tests" ON)
option(FORCE_BOOST_STATIC "Build with static boost" ON)
option(FORCE_LLVM_STATIC "Build with static llvm and clang" ON)

# Fetch all external projects before we enable warnings globally

### gflags (before glog)
find_package(gflags REQUIRED)

### tomlplusplus (for configuration files)
FetchContent_Declare(
  tomlplusplus
  GIT_REPOSITORY    https://github.com/marzer/tomlplusplus.git
  GIT_TAG           4b166b69f28e70a416a1a04a98f365d2aeb90de8 # v3.2.0
  GIT_PROGRESS      TRUE
  FIND_PACKAGE_ARGS
)
FetchContent_MakeAvailable(tomlplusplus)

### glog
find_package(glog)
if (NOT glog_FOUND)
  FetchContent_Declare(
    glog
    GIT_REPOSITORY    https://github.com/google/glog.git
    GIT_TAG           96a2f23dca4cc7180821ca5f32e526314395d26a
    GIT_PROGRESS      TRUE
  )
  FetchContent_MakeAvailable(glog)

  # These glog executable targets still generate warnings - disable warnings for
  # them explicitly
  target_compile_options(demangle_unittest PRIVATE "-w")
  target_compile_options(logging_unittest PRIVATE "-w")
  target_compile_options(stl_logging_unittest PRIVATE "-w")
  target_compile_options(symbolize_unittest PRIVATE "-w")
  target_compile_options(utilities_unittest PRIVATE "-w")
endif()

### GTest
# Do this in the main file so it can be fetched before setting project warnings.
# After this is fixed with FetchContent, move to test/CMakeLists.txt.
FetchContent_Declare(
  GTest
  GIT_REPOSITORY    https://github.com/google/googletest.git
  GIT_TAG           1ed6a8c67a0bd675149ece27bbec0ef1759854cf
  GIT_PROGRESS      TRUE
  FIND_PACKAGE_ARGS
)
FetchContent_MakeAvailable(GTest)

### liburing (for RocksDB)
find_package(uring REQUIRED)

### rocksdb
find_package(RocksDB 8.11 CONFIG)
if (NOT RocksDB_FOUND)
  FetchContent_Declare(
    rocksdb
    GIT_REPOSITORY https://github.com/facebook/rocksdb.git
    GIT_TAG        f32521662acf3352397d438b732144c7813bbbec # v8.5.3
    GIT_PROGRESS   TRUE
  )
  FetchContent_Populate(rocksdb)

  add_custom_target(librocksdb_build ALL
    WORKING_DIRECTORY ${rocksdb_SOURCE_DIR}
    COMMAND cmake -G Ninja -B ${rocksdb_BINARY_DIR} -DCMAKE_BUILD_TYPE=Release -DWITH_GFLAGS=Off -DWITH_LIBURING=Off -DWITH_ZSTD=On -DFAIL_ON_WARNINGS=Off
    COMMAND cmake --build ${rocksdb_BINARY_DIR} --target rocksdb
    BYPRODUCTS ${rocksdb_BINARY_DIR}/librocksdb.a
    COMMENT "Building RocksDB"
    USES_TERMINAL
  )

  ### zstd (for rocksdb)
  find_package(zstd REQUIRED)

  add_library(librocksdb INTERFACE)
  add_dependencies(librocksdb librocksdb_build)
  target_include_directories(librocksdb INTERFACE SYSTEM "${rocksdb_SOURCE_DIR}/include")
  target_link_libraries(librocksdb INTERFACE ${rocksdb_BINARY_DIR}/librocksdb.a zstd::zstd)

  add_library(RocksDB::rocksdb ALIAS librocksdb)
endif()

### folly
### use folly as a header only library. some features won't be supported.
find_package(folly CONFIG)
if (NOT folly_FOUND)
  FetchContent_Declare(
    folly
    GIT_REPOSITORY    https://github.com/facebook/folly.git
    GIT_TAG           c5aa5c46291a27f69acc920894d43605ceb43eba
    GIT_PROGRESS      TRUE
    PATCH_COMMAND     ${CMAKE_COMMAND} -E copy ${CMAKE_CURRENT_SOURCE_DIR}/extern/shim-folly-config.h folly/folly-config.h
  )
  FetchContent_Populate(folly)

  add_library(folly_headers INTERFACE)
  target_include_directories(folly_headers SYSTEM INTERFACE ${folly_SOURCE_DIR})
  target_link_libraries(folly_headers INTERFACE Boost::headers)
else()
  add_library(folly_headers INTERFACE)
  target_include_directories(folly_headers SYSTEM INTERFACE ${FOLLY_INCLUDE_DIR})
endif()

### range-v3
FetchContent_Declare(
  range-v3
  GIT_REPOSITORY    https://github.com/ericniebler/range-v3.git
  GIT_TAG           a81477931a8aa2ad025c6bda0609f38e09e4d7ec # 0.12.0
  GIT_PROGRESS      TRUE
  FIND_PACKAGE_ARGS
)
FetchContent_MakeAvailable(range-v3)

set_project_warnings()

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

if (DEFINED drgn_SOURCE_DIR)
    # drgn's autotools build requires source modification. in case we have a
    # readonly source (read: nix) we must copy this. do this always to avoid
    # polluting the source.
    file(COPY "${drgn_SOURCE_DIR}/" DESTINATION "${FETCHCONTENT_BASE_DIR}/drgn-src" NO_SOURCE_PERMISSIONS)
    SET(drgn_SOURCE_DIR "${FETCHCONTENT_BASE_DIR}/drgn-src")
    SET(drgn_BINARY_DIR "${CMAKE_BINARY_DIR}/_deps/drgn-build")
else()
    SET(drgn_SOURCE_DIR "${PROJECT_SOURCE_DIR}/extern/drgn")
    SET(drgn_BINARY_DIR "${drgn_SOURCE_DIR}/build")
endif()

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

        if(NOT EXISTS "${drgn_SOURCE_DIR}")
            message(FATAL_ERROR "The submodules were not downloaded! GIT_SUBMODULE was turned off or failed. Please update submodules and try again.")
        endif()
    endif()
endif()


### Select Python version
find_program(PYTHON NAMES python3.9 python3)

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

### LLVM and Clang
find_package(LLVM ${LLVM_REQUESTED_VERSION} REQUIRED CONFIG)

message(STATUS "Found LLVM ${LLVM_PACKAGE_VERSION}")
message(STATUS "Using LLVMConfig.cmake in: ${LLVM_DIR}")

if((${LLVM_VERSION_MAJOR} VERSION_LESS 15) OR (${LLVM_VERSION_MAJOR} VERSION_GREATER 16))
  message(SEND_ERROR "Object Introspection currently requires an LLVM version between 15 and 16!")
endif()

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

### drgn/elfutils dependencies
find_package(BZip2 REQUIRED)
find_package(OpenMP REQUIRED)
find_package(LibLZMA REQUIRED)
find_package(ZLIB REQUIRED)

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

set(DRGN_CONFIGURE_FLAGS "--with-libkdumpfile=no")
if (ASAN)
  list(APPEND DRGN_CONFIGURE_FLAGS "--enable-asan=yes")
endif()

add_custom_target(drgn_build ALL
  WORKING_DIRECTORY ${drgn_SOURCE_DIR}
  COMMAND unset BISON_PKGDATADIR && CFLAGS="${DRGN_CFLAGS}"
  CONFIGURE_FLAGS="${DRGN_CONFIGURE_FLAGS}" ${PYTHON} ./setup.py build --build-temp "${drgn_BINARY_DIR}"
  BYPRODUCTS ${drgn_BINARY_DIR}/.libs/libdrgnimpl.a
    ${drgn_BINARY_DIR}/velfutils/libdw/libdw.a
    ${drgn_BINARY_DIR}/velfutils/libelf/libelf.a
    ${drgn_BINARY_DIR}/velfutils/libdwelf/libdwelf.a
  COMMENT "Building drgn"
  USES_TERMINAL
)

add_library(drgn INTERFACE)
add_dependencies(drgn drgn_build)
target_link_libraries(drgn INTERFACE
  ${drgn_BINARY_DIR}/.libs/libdrgnimpl.a
  ${drgn_BINARY_DIR}/velfutils/libdw/libdw.a
  ${drgn_BINARY_DIR}/velfutils/libelf/libelf.a
  ${drgn_BINARY_DIR}/velfutils/libdwelf/libdwelf.a

  BZip2::BZip2
  LibLZMA::LibLZMA
  OpenMP::OpenMP_CXX
  ZLIB::ZLIB
)
target_include_directories(drgn SYSTEM INTERFACE "${drgn_SOURCE_DIR}" "${drgn_BINARY_DIR}")

if (STATIC_LINK)
  # glog links against the `gflags` target, which is an alias for `gflags_shared`
  # For static builds, we force it to link against `gflags_static` instead
  set_property(TARGET glog PROPERTY INTERFACE_LINK_LIBRARIES "gflags_static")
endif()



## OI Dependencies (linked to by output libraries and executables)
### OI Language Parser
BISON_TARGET(Parser oi/OIParser.yy ${CMAKE_CURRENT_BINARY_DIR}/OIParser.tab.cpp
  DEFINES_FILE ${CMAKE_CURRENT_BINARY_DIR}/OIParser.tab.hh)
FLEX_TARGET(Lexer oi/OILexer.l ${CMAKE_CURRENT_BINARY_DIR}/OILexer.cpp)

ADD_FLEX_BISON_DEPENDENCY(Lexer Parser)

add_library(oid_parser STATIC ${BISON_Parser_OUTPUTS} ${FLEX_Lexer_OUTPUTS})
target_compile_options(oid_parser PRIVATE "-w") # Disable warnings for generated code
target_link_libraries(oid_parser glog::glog)

### Core OI

include_directories(${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
add_subdirectory(oi)
add_subdirectory(resources)
add_library(oicore
  oi/Config.cpp
  oi/Descs.cpp
  oi/Metrics.cpp
  oi/OICache.cpp
  oi/OICompiler.cpp
  oi/PaddingHunter.cpp
  oi/Serialize.cpp
)
target_include_directories(oicore SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
target_compile_definitions(oicore PRIVATE ${LLVM_DEFINITIONS})
target_include_directories(oicore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

llvm_map_components_to_libnames(llvm_libs core native mcjit x86disassembler)
target_link_libraries(oicore
  codegen

  ${Boost_LIBRARIES}
  Boost::headers
  glog::glog
  range-v3
  resources
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
# linker order
if (FORCE_LLVM_STATIC)
  target_link_libraries(oicore ${llvm_libs})
else()
  target_link_libraries(oicore LLVM)
endif()

target_link_libraries(oicore
  drgn
  dw
  pthread
)

### TreeBuilder
add_library(treebuilder
  oi/TreeBuilder.cpp
  oi/exporters/TypeCheckingWalker.cpp
)
target_link_libraries(treebuilder
  RocksDB::rocksdb
  oicore # overkill but it does need a lot of stuff
)



## OI Outputs
### Object Introspection as a Library (OIL)
add_library(oil
  oi/IntrospectionResult.cpp
  oi/exporters/ParsedData.cpp
)
target_link_libraries(oil folly_headers)
target_include_directories(oil PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_library(oil_jit
  oi/OILibrary.cpp
  oi/OILibraryImpl.cpp
)
target_link_libraries(oil_jit oicore oil)

### Object Introspection as a Library Generator (OILGen)
add_executable(oilgen
  tools/OILGen.cpp
  oi/OIGenerator.cpp
)
target_link_libraries(oilgen
  drgn_utils
  oicore
  clangTooling
)

### Object Introspection cache Printer (OIP)
add_executable(oip tools/OIP.cpp)
target_link_libraries(oip oicore)

### Object Introspection RocksDB Printer (OIRP)
add_executable(oirp tools/OIRP.cpp)
target_link_libraries(oirp
  RocksDB::rocksdb
  msgpackc
)

### Object Introspection Tree Builder (OITB)
add_executable(oitb tools/OITB.cpp)
target_link_libraries(oitb oicore treebuilder)

### Object Introspection Debugger (OID)
add_executable(oid oi/OID.cpp oi/OIDebugger.cpp)

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

# Add a hook for custom CMake rules
if (DEFINED ENV{CMAKE_HOOK})
  include($ENV{CMAKE_HOOK})
endif()

install(TARGETS oid DESTINATION ${CMAKE_INSTALL_BINDIR})

