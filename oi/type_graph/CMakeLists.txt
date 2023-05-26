add_library(type_graph
  AddChildren.cpp
  AddPadding.cpp
  AlignmentCalc.cpp
  DrgnParser.cpp
  Flattener.cpp
  NameGen.cpp
  PassManager.cpp
  Printer.cpp
  RemoveTopLevelPointer.cpp
  TopoSorter.cpp
  TypeIdentifier.cpp
  Types.cpp
)
add_dependencies(type_graph libdrgn)
target_link_libraries(type_graph
  symbol_service

  "-L${DRGN_PATH}/.libs"
  drgn
)
