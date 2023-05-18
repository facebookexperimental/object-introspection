add_library(resources headers.cpp)

target_include_directories(resources PRIVATE ../)

function(embed_headers output)
  file(WRITE ${output} "#include \"oi/Headers.h\"\n\n")
  file(APPEND ${output} "namespace ObjectIntrospection {\n")
  file(APPEND ${output} "namespace headers {\n\n")

  set(HEADERS
    ../oi/OITraceCode.cpp
  )
  foreach(header ${HEADERS})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${header})
    get_filename_component(filename ${header} NAME)
    string(MAKE_C_IDENTIFIER ${filename} varname)
    file(READ ${header} contents)
    file(APPEND ${output} "const std::string_view ${varname} = R\"CONTENTS(${contents})CONTENTS\";\n\n")
  endforeach()

  file(APPEND ${output} "} // namespace headers\n")
  file(APPEND ${output} "} // namespace ObjectIntrospection\n")
endfunction()

embed_headers(${CMAKE_BINARY_DIR}/resources/headers.cpp)
