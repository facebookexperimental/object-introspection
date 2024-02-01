# - Find LLDB
# Find the LLDB debugger library and includes
#
# LLDB_INCLUDE_DIRS - where to find LLDB.h, etc.
# LLDB_LIBRARIES - List of libraries when using LLDB.
# LLDB_FOUND - True if LLDB found.

find_path(LLDB_INCLUDE_DIRS LLDB.h
  HINTS ${LLDB_ROOT_DIR}/include
  PATH_SUFFIXES lldb/API/)
# Remove the path suffixes
cmake_path(APPEND LLDB_INCLUDE_DIRS .. ..)

find_library(LLDB_LIBRARIES lldb lldb-15 HINTS ${LLDB_ROOT_DIR}/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(LLDB DEFAULT_MSG LLDB_LIBRARIES LLDB_INCLUDE_DIRS)

mark_as_advanced(
  LLDB_LIBRARIES
  LLDB_INCLUDE_DIRS)

if(LLDB_FOUND AND NOT (TARGET LLDB))
  add_library(LLDB UNKNOWN IMPORTED)
  set_target_properties(LLDB
    PROPERTIES
      IMPORTED_LOCATION ${LLDB_LIBRARIES}
      INTERFACE_INCLUDE_DIRECTORIES ${LLDB_INCLUDE_DIRS})
endif()
