# Copyright (c) Facebook, Inc. and its affiliates.

include(FBCMakeParseArgs)
include(FBPythonBinary)

find_package(FBThrift REQUIRED)

# Generate a Python library from a thrift file
function(add_fbthrift_py3_library LIB_NAME THRIFT_FILE)
  message(STATUS "#### add_fbthrift_py3_library:0(${LIB_NAME}, ${THRIFT_FILE})")
  # Parse the arguments
  set(one_value_args NAMESPACE THRIFT_INCLUDE_DIR)
  set(multi_value_args SERVICES DEPENDS OPTIONS)
  fb_cmake_parse_args(
    ARG "" "${one_value_args}" "${multi_value_args}" "${ARGN}"
  )

  if(NOT DEFINED ARG_THRIFT_INCLUDE_DIR)
    set(ARG_THRIFT_INCLUDE_DIR "include/thrift-files")
  endif()

  get_filename_component(base ${THRIFT_FILE} NAME_WE)
  set(output_dir "${CMAKE_CURRENT_BINARY_DIR}/${THRIFT_FILE}-py3")
  message(STATUS "#### add_fbthrift_py_library:1(output_dir = ${output_dir})")

  # Parse the namespace value
  if (NOT DEFINED ARG_NAMESPACE)
    set(ARG_NAMESPACE "${base}")
  endif()
  message(STATUS "#### add_fbthrift_py_library:2(ARG_NAMESPACE = ${ARG_NAMESPACE})")

  string(REPLACE "." "/" namespace_dir "${ARG_NAMESPACE}")
  message(STATUS "#### add_fbthrift_py_library:2.1(namespace_dir = ${ARG_NAMESPACE})")
  set(py_output_dir "${output_dir}/gen-python/${namespace_dir}")
  message(STATUS "#### add_fbthrift_py_library:2.2(py_output_dir = ${py_output_dir})")
  list(APPEND generated_sources
    # "${py_output_dir}/__init__.py"
    "${py_output_dir}/thrift_abstract_types.py"
    "${py_output_dir}/thrift_enums.py"
    "${py_output_dir}/thrift_metadata.py"
    # thrift_mutable_types.py
    # thrift_mutable_types.pyi
    "${py_output_dir}/thrift_types.py"
    # thrift_types.pyi
  )
  message(STATUS "#### add_fbthrift_py_library:2.3a ${ARG_SERVICES}")
  message(STATUS "#### add_fbthrift_py_library:2.3b ${generated_sources}")
  if(ARG_SERVICES)
    list(APPEND generated_sources
      "${py_output_dir}/thrift_clients.py"
      "${py_output_dir}/thrift_mutable_clients.py"
      # thrift_services.py
      # thrift_mutable_services.py
    )
  endif()
  message(STATUS "#### add_fbthrift_py_library:2.4 ${generated_sources}")
  # foreach(service IN LISTS ARG_SERVICES)
  #   list(APPEND generated_sources
  #     # ${py_output_dir}/${service}.py
  #   )
  # endforeach()

  # Define a dummy interface library to help propagate the thrift include
  # directories between dependencies.
  add_library("${LIB_NAME}.thrift_includes" INTERFACE)
  target_include_directories(
    "${LIB_NAME}.thrift_includes"
    INTERFACE
      "$<BUILD_INTERFACE:${CMAKE_SOURCE_DIR}>"
      "$<INSTALL_INTERFACE:${ARG_THRIFT_INCLUDE_DIR}>"
  )
  foreach(dep IN LISTS ARG_DEPENDS)
    target_link_libraries(
      "${LIB_NAME}.thrift_includes"
      INTERFACE "${dep}.thrift_includes"
    )
  endforeach()

  # This generator expression gets the list of include directories required
  # for all of our dependencies.
  # It requires using COMMAND_EXPAND_LISTS in the add_custom_command() call
  # below.  COMMAND_EXPAND_LISTS is only available in CMake 3.8+
  # If we really had to support older versions of CMake we would probably need
  # to use a wrapper script around the thrift compiler that could take the
  # include list as a single argument and split it up before invoking the
  # thrift compiler.
  if (NOT POLICY CMP0067)
    message(FATAL_ERROR "add_fbthrift_py3_library() requires CMake 3.8+")
  endif()
  set(
    thrift_include_options
    "-I;$<JOIN:$<TARGET_PROPERTY:${LIB_NAME}.thrift_includes,INTERFACE_INCLUDE_DIRECTORIES>,;-I;>"
  )

  # Always force generation of "new-style" python classes for Python 2
  # list(APPEND ARG_OPTIONS "new_style")
  # CMake 3.12 is finally getting a list(JOIN) function, but until then
  # treating the list as a string and replacing the semicolons is good enough.
  string(REPLACE ";" "," GEN_ARG_STR "${ARG_OPTIONS}")

  message(STATUS "#### add_fbthrift_py_library:3 - adding custom command")
  # Emit the rule to run the thrift compiler
  add_custom_command(
    OUTPUT
      ${generated_sources}
    COMMENT "#### add_fbthrift_py3_library:4 generating thrift sources for ${LIB_NAME}"
    COMMAND_EXPAND_LISTS
    COMMAND
      "${CMAKE_COMMAND}" -E make_directory "${output_dir}"
    COMMAND
      "${FBTHRIFT_COMPILER}"
      --legacy-strict
      --recurse
      --gen "mstch_python:${GEN_ARG_STR}"
      "${thrift_include_options}"
      -o "${output_dir}"
      "${CMAKE_CURRENT_SOURCE_DIR}/${THRIFT_FILE}"
    # COMMENT "#### add_fbthrift_py3_library:5 running sed ..."
    # COMMENT "                                        ... on ${generated_sources}"
    # COMMAND
    #   sed
    #   -i ""
    #   -e 's/from . import \(apache.thrift.metadata\)*.thrift_types as _fbthrift_metadata/from . import thrift_metadata as _fbthrift_metadata/'
    #   ${generated_sources}
    # COMMAND ${CMAKE_COMMAND} -E env bash -c
    #       "for f in \"\$@\"\; do \
    #          [ -f \"\$f\" ] || continue\; \
    #          sed -i'' -e 's/from . import \\(apache.thrift.metadata\\)*.thrift_types as _fbthrift_metadata/from . import thrift_metadata as _fbthrift_metadata/' \"\$f\"\; \
    #        done"
    #        ${always_generated_sources} ${sometimes_generated_sources}
    # VERBATIM
    WORKING_DIRECTORY
      "${CMAKE_BINARY_DIR}"
    MAIN_DEPENDENCY
      "${THRIFT_FILE}"
    DEPENDS
      "${FBTHRIFT_COMPILER}"
  )

  # We always want to pass the namespace as "" to this call:
  # thrift will already emit the files with the desired namespace prefix under
  # gen-py3.  We don't want add_fb_python_library() to prepend the namespace a
  # second time.
  add_fb_python_library(
    "${LIB_NAME}"
    BASE_DIR "${output_dir}/gen-python"
    NAMESPACE ""
    SOURCES ${generated_sources}
    # DEPENDS ${ARG_DEPENDS} FBThrift::thrift_py
    # DEPENDS ${ARG_DEPENDS} FBThrift::thrift_py Folly::folly_python_cpp
    DEPENDS ${ARG_DEPENDS}  Folly::folly_python_cpp
    NORMAL_DEPENDS
      Folly::folly_python_cpp
      FBThrift::thrift_python_cpp
    # thrift_python_and_py3_bindings
    # DEPENDS ${ARG_DEPENDS} fbthrift
    # FBThrift::thrift_python_and_py3_bindings 
  )

  # wrap_non_fb_python_library("Folly::folly_python_cpp")
  # wrap_non_fb_python_library("folly_python_cpp" "Folly::folly_python_cpp")
endfunction()
