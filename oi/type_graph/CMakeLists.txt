add_library(type_graph
  AddChildren.cpp
  AddPadding.cpp
  AlignmentCalc.cpp
  ClangTypeParser.cpp
  DrgnExporter.cpp
  DrgnParser.cpp
  EnforceCompatibility.cpp
  Flattener.cpp
  IdentifyContainers.cpp
  KeyCapture.cpp
  NameGen.cpp
  PassManager.cpp
  Printer.cpp
  Prune.cpp
  RemoveMembers.cpp
  RemoveTopLevelPointer.cpp
  TopoSorter.cpp
  TypeGraph.cpp
  TypeIdentifier.cpp
  Types.cpp
)
add_dependencies(type_graph libdrgn)
target_link_libraries(type_graph
  container_info
  symbol_service

  "-L${DRGN_PATH}/.libs"
  drgn
)
target_include_directories(type_graph SYSTEM PUBLIC ${LLVM_INCLUDE_DIRS} ${CLANG_INCLUDE_DIRS})
