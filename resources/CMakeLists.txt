add_library(resources headers.cpp)

target_include_directories(resources PRIVATE ../)

function(embed_headers output)
  file(WRITE ${output} "#include \"oi/Headers.h\"\n\n")
  file(APPEND ${output} "namespace oi::detail::headers {\n")

  set(HEADERS
    ../include/oi/IntrospectionResult-inl.h
    ../include/oi/IntrospectionResult.h
    ../include/oi/exporters/ParsedData.h
    ../include/oi/exporters/inst.h
    ../include/oi/result/Element.h
    ../include/oi/types/dy.h
    ../include/oi/types/st.h
    ../oi/OITraceCode.cpp
  )
  foreach(header ${HEADERS})
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${header})

    file(REAL_PATH ${header} header_path)
    file(REAL_PATH ${CMAKE_SOURCE_DIR}/include include_path)
    file(REAL_PATH ${CMAKE_SOURCE_DIR} base_path)

    cmake_path(IS_PREFIX include_path ${header_path} NORMALIZE in_include)
    if (${in_include})
      file(RELATIVE_PATH header_rel_path ${include_path} ${header_path})
    else()
      file(RELATIVE_PATH header_rel_path ${base_path} ${header_path})
    endif()

    string(MAKE_C_IDENTIFIER ${header_rel_path} varname)
    get_filename_component(filename ${header} NAME)

    file(READ ${header} contents)
    file(APPEND ${output} "const std::string_view ${varname} = R\"CONTENTS(${contents})CONTENTS\";\n\n")
  endforeach()

  file(APPEND ${output} "} // namespace oi::detail::headers\n")
endfunction()

embed_headers(${CMAKE_BINARY_DIR}/resources/headers.cpp)
