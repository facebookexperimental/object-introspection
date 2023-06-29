add_library(resources headers.cpp)

target_include_directories(resources PRIVATE ../)

function(embed_headers output)
  file(WRITE ${output} "#include \"oi/Headers.h\"\n\n")
  file(APPEND ${output} "namespace ObjectIntrospection {\n")
  file(APPEND ${output} "namespace headers {\n\n")

  set(HEADERS
    ../include/oi/types/st.h
    ../oi/OITraceCode.cpp
  )
  foreach(header ${HEADERS})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${header})

    file(REAL_PATH ${header} file_real_path)
    set(include_path ${CMAKE_CURRENT_SOURCE_DIR}/../include)

    cmake_path(IS_PREFIX include_path ${file_real_path} NORMALIZE in_include)
    if (${in_include})
      file(RELATIVE_PATH file_rel_path ${include_path} ${file_real_path})
    else()
      file(RELATIVE_PATH file_rel_path ${CMAKE_CURRENT_SOURCE_DIR}/.. ${file_real_path})
    endif()

    string(MAKE_C_IDENTIFIER ${file_rel_path} varname)
    get_filename_component(filename ${header} NAME)

    file(READ ${header} contents)
    file(APPEND ${output} "const std::string_view ${varname} = R\"CONTENTS(${contents})CONTENTS\";\n\n")
  endforeach()

  file(APPEND ${output} "} // namespace headers\n")
  file(APPEND ${output} "} // namespace ObjectIntrospection\n")
endfunction()

embed_headers(${CMAKE_BINARY_DIR}/resources/headers.cpp)
