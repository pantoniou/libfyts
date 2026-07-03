function(fyts_probe_python_yq YQ_CANDIDATE YQ_OK_VAR YQ_ERROR_VAR)
  set(_YQ_PROBE_FILE "${CMAKE_BINARY_DIR}/_yq-probe.yaml")
  file(WRITE "${_YQ_PROBE_FILE}" "- name: probe\n  progressive-safe: false\n")
  execute_process(
    COMMAND "${YQ_CANDIDATE}" -r ".[] | [.name, (if has(\"progressive-safe\") then .[\"progressive-safe\"] else true end)] | @tsv" "${_YQ_PROBE_FILE}"
    OUTPUT_VARIABLE _YQ_PROBE_OUTPUT
    RESULT_VARIABLE _YQ_PROBE_RESULT
    ERROR_VARIABLE _YQ_PROBE_ERROR
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_STRIP_TRAILING_WHITESPACE
  )
  if(_YQ_PROBE_RESULT EQUAL 0 AND _YQ_PROBE_OUTPUT STREQUAL "probe	false")
    set(${YQ_OK_VAR} TRUE PARENT_SCOPE)
  else()
    set(${YQ_OK_VAR} FALSE PARENT_SCOPE)
  endif()
  set(${YQ_ERROR_VAR} "${_YQ_PROBE_ERROR}" PARENT_SCOPE)
endfunction()

function(fyts_find_python_yq)
  find_program(YQ_EXECUTABLE
    NAMES yq
    DOC "jq-based kislyuk/yq executable"
  )
  if(NOT YQ_EXECUTABLE)
    message(FATAL_ERROR
      "Python yq not found. Install the jq-based kislyuk/yq package or configure "
      "with -DYQ_EXECUTABLE=/path/to/python-yq/bin/yq.")
  endif()

  fyts_probe_python_yq("${YQ_EXECUTABLE}" _YQ_OK _YQ_ERROR)
  if(NOT _YQ_OK AND APPLE)
    find_program(_YQ_HOMEBREW_EXECUTABLE
      NAMES yq
      PATHS
        /opt/homebrew/opt/python-yq/bin
        /usr/local/opt/python-yq/bin
      NO_DEFAULT_PATH
    )
    if(_YQ_HOMEBREW_EXECUTABLE AND NOT "${_YQ_HOMEBREW_EXECUTABLE}" STREQUAL "${YQ_EXECUTABLE}")
      fyts_probe_python_yq("${_YQ_HOMEBREW_EXECUTABLE}" _YQ_HOMEBREW_OK _YQ_HOMEBREW_ERROR)
      if(_YQ_HOMEBREW_OK)
        set(YQ_EXECUTABLE "${_YQ_HOMEBREW_EXECUTABLE}" CACHE FILEPATH "jq-based kislyuk/yq executable" FORCE)
        set(_YQ_OK TRUE)
        set(_YQ_ERROR "${_YQ_HOMEBREW_ERROR}")
      endif()
    endif()
  endif()

  if(NOT _YQ_OK)
    message(FATAL_ERROR
      "YQ_EXECUTABLE must be the jq-based Python yq from kislyuk/yq. "
      "The selected executable failed the required query syntax: ${YQ_EXECUTABLE}\n"
      "Install python-yq or configure with -DYQ_EXECUTABLE=/path/to/python-yq/bin/yq.\n"
      "Probe stderr: ${_YQ_ERROR}")
  endif()

  message(STATUS "Using Python yq: ${YQ_EXECUTABLE}")
  set(YQ_EXECUTABLE "${YQ_EXECUTABLE}" PARENT_SCOPE)
endfunction()
