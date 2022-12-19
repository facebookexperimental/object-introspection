# - Find zstd
# Find the zstd compression library and includes
#
# zstd_INCLUDE_DIRS - where to find zstd.h, etc.
# zstd_LIBRARIES - List of libraries when using zstd.
# zstd_FOUND - True if zstd found.

find_path(zstd_INCLUDE_DIRS zstd.h HINTS ${zstd_ROOT_DIR}/include)

# Don't merge these two `find_library`! This is to give priority to the static library.
# See "Professional CMake", page 300, for more info.
find_library(zstd_LIBRARIES libzstd.a HINTS ${zstd_ROOT_DIR}/lib)
find_library(zstd_LIBRARIES zstd HINTS ${zstd_ROOT_DIR}/lib)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(zstd DEFAULT_MSG zstd_LIBRARIES zstd_INCLUDE_DIRS)

mark_as_advanced(
  zstd_LIBRARIES
  zstd_INCLUDE_DIRS)

if(zstd_FOUND AND NOT (TARGET zstd::zstd))
  add_library (zstd::zstd UNKNOWN IMPORTED)
  set_target_properties(zstd::zstd
    PROPERTIES
      IMPORTED_LOCATION ${zstd_LIBRARIES}
      INTERFACE_INCLUDE_DIRECTORIES ${zstd_INCLUDE_DIRS}
      COMPILE_DEFINITIONS -DZSTD)
endif()
