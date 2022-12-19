# - Try to find Thrift
#
# Defines the following variables:
#
#  THRIFT_FOUND - system has Thrift
#  THRIFT_COMPILER - The thrift compiler executable
#  THRIFT_INCLUDE_DIRS - The Thrift include directory

find_program(THRIFT_COMPILER thrift)

find_path(THRIFT_INCLUDE_DIRS
  NAMES
    thrift/annotation/cpp.thrift
  HINTS
    ${THRIFT_ROOT})

include (FindPackageHandleStandardArgs)

# handle the QUIETLY and REQUIRED arguments and set THRIFT_FOUND to TRUE if all listed variables are TRUE
FIND_PACKAGE_HANDLE_STANDARD_ARGS(Thrift "Thrift not found"
  THRIFT_COMPILER
  THRIFT_INCLUDE_DIRS)

mark_as_advanced(THRIFT_COMPILER THRIFT_INCLUDE_DIRS)
