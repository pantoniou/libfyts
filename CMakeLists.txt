cmake_minimum_required(VERSION 3.16)
project(c_tree_sitter_highlighter C CXX)

include(FetchContent)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GITHUB_USE_SSH "Rewrite GitHub catalogue URLs from HTTPS to SSH" OFF)

set(TS_LANGUAGE_CATALOGUE_SET "minimal" CACHE STRING "Tree-sitter language catalogue set: minimal, default, or full")
set_property(CACHE TS_LANGUAGE_CATALOGUE_SET PROPERTY STRINGS minimal default full)
if(NOT TS_LANGUAGE_CATALOGUE_SET MATCHES "^(minimal|default|full)$")
  message(FATAL_ERROR "TS_LANGUAGE_CATALOGUE_SET must be one of: minimal, default, full")
endif()

set(_TS_SELECTED_LANGUAGE_CATALOGUE "${CMAKE_CURRENT_SOURCE_DIR}/catalogues/${TS_LANGUAGE_CATALOGUE_SET}.yaml")
if(NOT EXISTS "${_TS_SELECTED_LANGUAGE_CATALOGUE}")
  message(FATAL_ERROR "selected language catalogue does not exist: ${_TS_SELECTED_LANGUAGE_CATALOGUE}")
endif()

set(TS_LANGUAGE_CATALOGUE "" CACHE FILEPATH "Override Tree-sitter language catalogue YAML path")
if(TS_LANGUAGE_CATALOGUE STREQUAL "" OR
   TS_LANGUAGE_CATALOGUE MATCHES "/catalogues/(minimal|default|full|base)\\.yaml$" OR
   NOT EXISTS "${TS_LANGUAGE_CATALOGUE}")
  set(TS_LANGUAGE_CATALOGUE "${_TS_SELECTED_LANGUAGE_CATALOGUE}" CACHE FILEPATH "Override Tree-sitter language catalogue YAML path" FORCE)
endif()
message(STATUS "Using language catalogue: ${TS_LANGUAGE_CATALOGUE}")

set(TS_STYLING "${CMAKE_CURRENT_SOURCE_DIR}/stylings/default.yaml" CACHE FILEPATH "Override syntax highlighting styling YAML path")
if(NOT EXISTS "${TS_STYLING}")
  message(FATAL_ERROR "styling file does not exist: ${TS_STYLING}")
endif()
message(STATUS "Using styling: ${TS_STYLING}")

find_program(YQ_EXECUTABLE yq REQUIRED)
find_path(LIBFYAML_INCLUDE_DIR NAMES libfyaml.h)
find_library(LIBFYAML_LIBRARY NAMES fyaml)
if(NOT LIBFYAML_INCLUDE_DIR OR NOT LIBFYAML_LIBRARY)
  message(FATAL_ERROR "libfyaml not found")
endif()

FetchContent_Declare(tree-sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG cbee4672665173d1702d836353ef7648dc2b2fac
  GIT_SHALLOW TRUE
)

FetchContent_GetProperties(tree-sitter)
if(NOT tree-sitter_POPULATED)
  FetchContent_Populate(tree-sitter)
endif()

execute_process(
  COMMAND "${YQ_EXECUTABLE}" -r ".[] | [.name, .[\"display-name\"], .repo, .tag, .entrypoint, (.directory // \"-\")] | @tsv" "${TS_LANGUAGE_CATALOGUE}"
  OUTPUT_VARIABLE TS_LANGUAGE_ROWS
  RESULT_VARIABLE TS_LANGUAGE_ROWS_RESULT
  OUTPUT_STRIP_TRAILING_WHITESPACE
)
if(NOT TS_LANGUAGE_ROWS_RESULT EQUAL 0)
  message(FATAL_ERROR "failed to parse language catalogue: ${TS_LANGUAGE_CATALOGUE}")
endif()
if(TS_LANGUAGE_ROWS STREQUAL "")
  message(FATAL_ERROR "language catalogue is empty: ${TS_LANGUAGE_CATALOGUE}")
endif()

set(TS_GRAMMAR_SOURCES)
set(TS_GRAMMAR_INCLUDE_DIRS)
set(TS_LANGUAGE_DECLS)
set(TS_LANGUAGE_ENTRIES)

string(REPLACE "\n" ";" TS_LANGUAGE_ROWS "${TS_LANGUAGE_ROWS}")
foreach(row IN LISTS TS_LANGUAGE_ROWS)
  string(REPLACE "\t" ";" fields "${row}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 6)
    message(FATAL_ERROR "invalid language catalogue row: ${row}")
  endif()
  list(GET fields 0 language_name)
  list(GET fields 1 language_display_name)
  list(GET fields 2 language_repo)
  list(GET fields 3 language_tag)
  list(GET fields 4 language_entrypoint)
  list(GET fields 5 language_directory)
  if(GITHUB_USE_SSH)
    string(REGEX REPLACE "^https?://github\\.com/" "ssh://git@github.com/" language_repo "${language_repo}")
  endif()

  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" language_symbol "${language_name}")
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" fetch_name "tree_sitter_grammar_${language_symbol}")
  if(NOT language_entrypoint MATCHES "^tree_sitter_[A-Za-z0-9_]+$")
    message(FATAL_ERROR "${language_name}: invalid entrypoint '${language_entrypoint}'")
  endif()

  FetchContent_Declare("${fetch_name}"
    GIT_REPOSITORY "${language_repo}"
    GIT_TAG "${language_tag}"
    GIT_SHALLOW TRUE
    GIT_SUBMODULES "" GIT_SUBMODULES_RECURSE FALSE
  )

  FetchContent_GetProperties("${fetch_name}")
  if(NOT ${fetch_name}_POPULATED)
    message(STATUS "${language_name}: fetching entrypoint ${fetch_name} from ${language_repo}")
    FetchContent_Populate("${fetch_name}")
    message(STATUS "${language_name}: fetching entrypoint ${fetch_name} OK")
  endif()

  set(grammar_dir "${${fetch_name}_SOURCE_DIR}")
  if(NOT language_directory STREQUAL "-")
    set(grammar_dir "${grammar_dir}/${language_directory}")
  endif()
  if(NOT EXISTS "${grammar_dir}/src/parser.c")
    message(FATAL_ERROR "${language_name}: missing generated parser at ${grammar_dir}/src/parser.c")
  endif()

  list(APPEND TS_GRAMMAR_SOURCES "${grammar_dir}/src/parser.c")
  if(EXISTS "${grammar_dir}/src/scanner.c")
    list(APPEND TS_GRAMMAR_SOURCES "${grammar_dir}/src/scanner.c")
  endif()
  if(EXISTS "${grammar_dir}/src/scanner.cc")
    list(APPEND TS_GRAMMAR_SOURCES "${grammar_dir}/src/scanner.cc")
  endif()
  list(APPEND TS_GRAMMAR_INCLUDE_DIRS "${grammar_dir}/src")

  if(NOT EXISTS "${grammar_dir}/queries/highlights.scm")
    message(FATAL_ERROR "${language_name}: missing highlight query at ${grammar_dir}/queries/highlights.scm")
  endif()

  string(APPEND TS_LANGUAGE_DECLS "const TSLanguage *${language_entrypoint}(void);\n")
  string(APPEND TS_LANGUAGE_ENTRIES "    {\"${language_name}\", ${language_entrypoint}, \"${grammar_dir}/queries/highlights.scm\"},\n")
endforeach()

set(TS_LANGUAGE_TABLE "${CMAKE_CURRENT_BINARY_DIR}/generated/language_catalogue.inc")
file(WRITE "${TS_LANGUAGE_TABLE}"
  "/* Generated from ${TS_LANGUAGE_CATALOGUE}. */\n"
  "${TS_LANGUAGE_DECLS}\n"
  "static const LanguageSpec LANGUAGES[] = {\n"
  "${TS_LANGUAGE_ENTRIES}"
  "};\n"
)

file(READ "${TS_LANGUAGE_CATALOGUE}" TS_CATALOGUE_HEX HEX)
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," TS_CATALOGUE_BYTES "${TS_CATALOGUE_HEX}")
set(TS_EMBEDDED_CATALOGUE "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_catalogue.inc")
file(WRITE "${TS_EMBEDDED_CATALOGUE}"
  "/* Generated from ${TS_LANGUAGE_CATALOGUE}. */\n"
  "static const unsigned char EMBEDDED_CATALOGUE[] = {\n"
  "${TS_CATALOGUE_BYTES}"
  "\n};\n"
  "static const size_t EMBEDDED_CATALOGUE_LEN = sizeof(EMBEDDED_CATALOGUE);\n"
)

file(READ "${TS_STYLING}" TS_STYLING_HEX HEX)
string(REGEX REPLACE "([0-9a-fA-F][0-9a-fA-F])" "0x\\1," TS_STYLING_BYTES "${TS_STYLING_HEX}")
set(TS_EMBEDDED_STYLING "${CMAKE_CURRENT_BINARY_DIR}/generated/embedded_styling.inc")
file(WRITE "${TS_EMBEDDED_STYLING}"
  "/* Generated from ${TS_STYLING}. */\n"
  "static const unsigned char EMBEDDED_STYLING[] = {\n"
  "${TS_STYLING_BYTES}"
  "\n};\n"
  "static const size_t EMBEDDED_STYLING_LEN = sizeof(EMBEDDED_STYLING);\n"
)

add_library(tree-sitter STATIC
  "${tree-sitter_SOURCE_DIR}/lib/src/lib.c"
)
target_include_directories(tree-sitter PUBLIC
  "${tree-sitter_SOURCE_DIR}/lib/include"
  "${tree-sitter_SOURCE_DIR}/lib/src"
)

add_library(fyts STATIC
  src/fyts.c
  ${TS_GRAMMAR_SOURCES}
)
target_include_directories(fyts PUBLIC
  "${CMAKE_CURRENT_SOURCE_DIR}/include"
  "${tree-sitter_SOURCE_DIR}/lib/include"
)
target_include_directories(fyts PRIVATE
  "${tree-sitter_SOURCE_DIR}/lib/include"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
  "${LIBFYAML_INCLUDE_DIR}"
  ${TS_GRAMMAR_INCLUDE_DIRS}
)
target_link_libraries(fyts PUBLIC tree-sitter "${LIBFYAML_LIBRARY}")

add_executable(fyts-highlight
  src/fyts_highlight.c
)
target_link_libraries(fyts-highlight PRIVATE fyts)

add_executable(fyts-stream-test
  tests/fyts_stream_test.c
)
target_link_libraries(fyts-stream-test PRIVATE fyts)

enable_testing()

function(add_highlight_test name lang fixture)
  add_test(NAME "highlight-${name}"
    COMMAND "$<TARGET_FILE:fyts-highlight>" --color on --lang "${lang}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/${fixture}"
  )
  set_tests_properties("highlight-${name}" PROPERTIES
    PASS_REGULAR_EXPRESSION "\\[[0-9;]*m"
  )
endfunction()

add_highlight_test(c c sample.c)
add_highlight_test(cpp cpp sample.cpp)
add_highlight_test(bash bash sample.sh)
add_highlight_test(python python sample.py)
add_highlight_test(diff diff sample.diff)

add_test(NAME list-languages
  COMMAND "$<TARGET_FILE:fyts-highlight>" --list-languages
)
set_tests_properties(list-languages PROPERTIES
  PASS_REGULAR_EXPRESSION "^c\ncpp\nbash\npython\ndiff\n$"
)

add_test(NAME output-catalog
  COMMAND "$<TARGET_FILE:fyts-highlight>" --output-catalog
)
set_tests_properties(output-catalog PROPERTIES
  PASS_REGULAR_EXPRESSION "entrypoint: tree_sitter_python"
)

add_test(NAME output-styling
  COMMAND "$<TARGET_FILE:fyts-highlight>" --output-styling
)
set_tests_properties(output-styling PROPERTIES
  PASS_REGULAR_EXPRESSION "captures:"
)

add_test(NAME autodetect-python
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color on "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.py"
)
set_tests_properties(autodetect-python PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[[0-9;]*m"
)

add_test(NAME color-off
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color off --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(color-off PROPERTIES
  PASS_REGULAR_EXPRESSION "puts"
  FAIL_REGULAR_EXPRESSION "\\[[0-9;]*m"
)

add_test(NAME custom-style
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color on --style "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/string-red-style.yaml" --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(custom-style PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[31m"
)

add_test(NAME color-auto-non-tty
  COMMAND "$<TARGET_FILE:fyts-highlight>" --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(color-auto-non-tty PROPERTIES
  PASS_REGULAR_EXPRESSION "puts"
  FAIL_REGULAR_EXPRESSION "\\[[0-9;]*m"
)

add_test(NAME width-fixed
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color off --width 4 --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(width-fixed PROPERTIES
  PASS_REGULAR_EXPRESSION "#inc"
  FAIL_REGULAR_EXPRESSION "stdio"
)

add_test(NAME parse-only-fallback
  COMMAND "$<TARGET_FILE:fyts-highlight>" --lang c --query "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/no-captures.scm" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(parse-only-fallback PROPERTIES
  PASS_REGULAR_EXPRESSION "puts"
)

add_test(NAME fyts-stream
  COMMAND "$<TARGET_FILE:fyts-stream-test>"
)
