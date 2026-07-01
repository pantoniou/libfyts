cmake_minimum_required(VERSION 3.16)
project(c_tree_sitter_highlighter VERSION 0.1.0 LANGUAGES C CXX)

include(GNUInstallDirs)
include(FetchContent)
include(CheckCCompilerFlag)
include(CheckCSourceCompiles)
include(CMakePackageConfigHelpers)
find_package(Git REQUIRED)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

option(GITHUB_USE_SSH "Rewrite GitHub catalogue URLs from HTTPS to SSH" OFF)
option(ENABLE_ASAN "Enable ASAN support" OFF)
option(BUILD_SHARED_LIBS "Build shared libraries" ON)
option(TS_GRAMMAR_FETCH_SPARSE "Fetch only generated grammar paths from language repositories" ON)
set(TS_GRAMMAR_SOURCE_MODE "auto" CACHE STRING "Grammar source mode: auto, archive, vendor, sparse, or fetch")
set_property(CACHE TS_GRAMMAR_SOURCE_MODE PROPERTY STRINGS auto archive vendor sparse fetch)
set(TS_GRAMMAR_VENDOR_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/grammars" CACHE PATH "Vendored grammar source directory")
set(TS_GRAMMAR_ARCHIVE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/vendor/grammar-packs" CACHE PATH "Per-language grammar archive directory")
set(TS_UPDATE_PACK_LANGUAGES "" CACHE STRING "Comma-separated language names for update-packs; empty updates the configured catalogue")
set(TS_UPDATE_PACK_ZSTD_LEVEL "10" CACHE STRING "zstd compression level for update-packs")
option(TS_UPDATE_PACK_FORCE "Force update-packs to refetch matching grammar checkouts" OFF)
option(TS_UPDATE_PACK_KEEP_METADATA "Keep bulky grammar metadata in update-packs archives" OFF)
option(TS_UPDATE_PACK_ALLOW_UNKNOWN_LICENSE "Allow update-packs to vendor grammars with unknown licenses" OFF)
if(NOT TS_GRAMMAR_SOURCE_MODE MATCHES "^(auto|archive|vendor|sparse|fetch)$")
  message(FATAL_ERROR "TS_GRAMMAR_SOURCE_MODE must be one of: auto, archive, vendor, sparse, fetch")
endif()

set(TS_LANGUAGE_CATALOGUE_SET "minimal" CACHE STRING "Tree-sitter language catalogue set: minimal, default, full, or full-broken")
set_property(CACHE TS_LANGUAGE_CATALOGUE_SET PROPERTY STRINGS minimal default full full-broken)
if(NOT TS_LANGUAGE_CATALOGUE_SET MATCHES "^(minimal|default|full|full-broken)$")
  message(FATAL_ERROR "TS_LANGUAGE_CATALOGUE_SET must be one of: minimal, default, full, full-broken")
endif()

set(_TS_SELECTED_LANGUAGE_CATALOGUE "${CMAKE_CURRENT_SOURCE_DIR}/catalogues/${TS_LANGUAGE_CATALOGUE_SET}.yaml")
if(NOT EXISTS "${_TS_SELECTED_LANGUAGE_CATALOGUE}")
  message(FATAL_ERROR "selected language catalogue does not exist: ${_TS_SELECTED_LANGUAGE_CATALOGUE}")
endif()

set(TS_LANGUAGE_CATALOGUE "" CACHE FILEPATH "Override Tree-sitter language catalogue YAML path")
if(TS_LANGUAGE_CATALOGUE STREQUAL "" OR
   TS_LANGUAGE_CATALOGUE MATCHES "/catalogues/(minimal|default|full|full-broken|base)\\.yaml$" OR
   NOT EXISTS "${TS_LANGUAGE_CATALOGUE}")
  set(TS_LANGUAGE_CATALOGUE "${_TS_SELECTED_LANGUAGE_CATALOGUE}" CACHE FILEPATH "Override Tree-sitter language catalogue YAML path" FORCE)
endif()
message(STATUS "Using language catalogue: ${TS_LANGUAGE_CATALOGUE}")

set(TS_STYLING "${CMAKE_CURRENT_SOURCE_DIR}/stylings/default.yaml" CACHE FILEPATH "Override syntax highlighting styling YAML path")
if(NOT EXISTS "${TS_STYLING}")
  message(FATAL_ERROR "styling file does not exist: ${TS_STYLING}")
endif()
message(STATUS "Using styling: ${TS_STYLING}")

file(GLOB TS_BUILTIN_CATALOGUE_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/catalogues/*.yaml"
)
file(GLOB TS_BUILTIN_STYLING_FILES CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/stylings/*.yaml"
)
file(GLOB TS_GRAMMAR_ARCHIVE_FILES CONFIGURE_DEPENDS
  "${TS_GRAMMAR_ARCHIVE_DIR}/*.tar.zst"
)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${TS_LANGUAGE_CATALOGUE}"
  "${TS_STYLING}"
  ${TS_BUILTIN_CATALOGUE_FILES}
  ${TS_BUILTIN_STYLING_FILES}
  ${TS_GRAMMAR_ARCHIVE_FILES}
)

find_program(YQ_EXECUTABLE yq REQUIRED)
find_program(PYTHON3_EXECUTABLE python3 REQUIRED)
find_program(TAR_EXECUTABLE tar)
find_program(ZSTD_EXECUTABLE zstd)
find_path(LIBFYAML_INCLUDE_DIR NAMES libfyaml.h)
find_library(LIBFYAML_LIBRARY NAMES fyaml)
if(NOT LIBFYAML_INCLUDE_DIR OR NOT LIBFYAML_LIBRARY)
  message(FATAL_ERROR "libfyaml not found")
endif()
if(NOT TARGET libfyaml::libfyaml)
  add_library(libfyaml::libfyaml UNKNOWN IMPORTED)
  set_target_properties(libfyaml::libfyaml PROPERTIES
    IMPORTED_LOCATION "${LIBFYAML_LIBRARY}"
    INTERFACE_INCLUDE_DIRECTORIES "${LIBFYAML_INCLUDE_DIR}"
  )
endif()

set(TS_UPDATE_PACK_ARGS
  "${PYTHON3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/scripts/update-grammar-packs.py"
  "--catalogue" "${TS_LANGUAGE_CATALOGUE}"
  "--work-dir" "${CMAKE_BINARY_DIR}/_pack-update"
  "--output" "${TS_GRAMMAR_ARCHIVE_DIR}"
  "--zstd-level" "${TS_UPDATE_PACK_ZSTD_LEVEL}"
)
if(NOT TS_UPDATE_PACK_LANGUAGES STREQUAL "")
  list(APPEND TS_UPDATE_PACK_ARGS "--language" "${TS_UPDATE_PACK_LANGUAGES}")
endif()
if(TS_UPDATE_PACK_FORCE)
  list(APPEND TS_UPDATE_PACK_ARGS "--force")
endif()
if(TS_UPDATE_PACK_KEEP_METADATA)
  list(APPEND TS_UPDATE_PACK_ARGS "--keep-metadata")
endif()
if(TS_UPDATE_PACK_ALLOW_UNKNOWN_LICENSE)
  list(APPEND TS_UPDATE_PACK_ARGS "--allow-unknown-license")
endif()

add_custom_target(update-packs
  COMMAND ${TS_UPDATE_PACK_ARGS}
  WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}"
  COMMENT "Updating Tree-sitter grammar packs for ${TS_LANGUAGE_CATALOGUE}"
  VERBATIM
)

FetchContent_Declare(tree-sitter
  GIT_REPOSITORY https://github.com/tree-sitter/tree-sitter.git
  GIT_TAG 41d3e16eac821845ca95f75af5ca00a17d22e4f8
  GIT_SHALLOW FALSE
)

FetchContent_GetProperties(tree-sitter)
if(NOT tree-sitter_POPULATED)
  FetchContent_Populate(tree-sitter)
endif()

execute_process(
  COMMAND "${YQ_EXECUTABLE}" -r ".[] | [.name, .[\"display-name\"], .repo, .tag, .entrypoint, (.directory // \"-\"), (.[\"highlight-query\"] // \"-\")] | @tsv" "${TS_LANGUAGE_CATALOGUE}"
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
set(TS_GRAMMAR_QUERY_ROOT_ARGS)
set(TS_LANGUAGE_DECLS)
set(TS_LANGUAGE_ENTRIES)
set(FYTS_C_FLAGS)
set(ASAN_C_FLAGS)
set(HAVE_ASAN 0)

if(ENABLE_ASAN)
  set(CMAKE_REQUIRED_FLAGS_SAVE "${CMAKE_REQUIRED_FLAGS}")
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS} -fsanitize=address")
  check_c_source_compiles("
    int main(void) { return 0; }
  " ASAN_WORKS)
  set(CMAKE_REQUIRED_FLAGS "${CMAKE_REQUIRED_FLAGS_SAVE}")
  if(ASAN_WORKS)
    set(HAVE_ASAN 1)
    set(ASAN_C_FLAGS
      -fsanitize=address,signed-integer-overflow,undefined
      -fno-omit-frame-pointer
    )
  else()
    message(WARNING "ENABLE_ASAN requested but compiler does not support -fsanitize=address")
  endif()
endif()
message(STATUS "HAVE_ASAN: ${HAVE_ASAN}")

if(NOT MSVC)
  list(APPEND FYTS_C_FLAGS -Wall -Wsign-compare)
  if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    list(APPEND FYTS_C_FLAGS -Wextra)
  endif()

  set(FYTS_USE_STD 0)
  if(NOT FYTS_USE_STD)
    check_c_compiler_flag(-std=gnu2x FYTS_COMPILER_SUPPORTS_GNU2X)
    if(FYTS_COMPILER_SUPPORTS_GNU2X)
      list(APPEND FYTS_C_FLAGS -std=gnu2x)
      set(FYTS_USE_STD 1)
    endif()
  endif()
  if(NOT FYTS_USE_STD)
    check_c_compiler_flag(-std=c2x FYTS_COMPILER_SUPPORTS_C2X)
    if(FYTS_COMPILER_SUPPORTS_C2X)
      list(APPEND FYTS_C_FLAGS -std=c2x)
      set(FYTS_USE_STD 1)
    endif()
  endif()
  if(NOT FYTS_USE_STD)
    check_c_compiler_flag(-std=gnu11 FYTS_COMPILER_SUPPORTS_GNU11)
    if(FYTS_COMPILER_SUPPORTS_GNU11)
      list(APPEND FYTS_C_FLAGS -std=gnu11)
    endif()
  endif()
endif()

function(ts_git command_result)
  execute_process(
    COMMAND "${GIT_EXECUTABLE}" ${ARGN}
    RESULT_VARIABLE result
    OUTPUT_QUIET
    ERROR_QUIET
  )
  set(${command_result} "${result}" PARENT_SCOPE)
endfunction()

function(ts_try_populate_grammar_sparse out_var repo tag directory fetch_name)
  set(source_dir "${CMAKE_BINARY_DIR}/_deps/${fetch_name}-src")
  set(prefix "")
  set(marker "${source_dir}/.fyts-sparse-tag")
  set(result 0)
  set(fetch_result 0)
  set(checkout_result 0)

  if(EXISTS "${marker}")
    file(READ "${marker}" current_tag)
    if(current_tag STREQUAL "${tag}\n")
      if(NOT directory STREQUAL "-")
        set(source_dir "${source_dir}/${directory}")
      endif()
      set(${out_var} "${source_dir}" PARENT_SCOPE)
      return()
    endif()
  endif()

  file(REMOVE_RECURSE "${source_dir}")
  file(MAKE_DIRECTORY "${source_dir}")

  if(NOT directory STREQUAL "-")
    set(prefix "/${directory}")
  endif()

  ts_git(result -C "${source_dir}" init)
  if(NOT result EQUAL 0)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  ts_git(result -C "${source_dir}" remote add origin "${repo}")
  if(NOT result EQUAL 0)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  ts_git(result -C "${source_dir}" config core.sparseCheckout true)
  if(NOT result EQUAL 0)
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  file(WRITE "${source_dir}/.git/info/sparse-checkout"
    "${prefix}/src/*\n"
    "${prefix}/src/**\n"
    "${prefix}/queries/highlights.scm\n"
    "${prefix}/queries/*\n"
    "${prefix}/queries/**\n"
    "${prefix}/queries-src/*\n"
    "${prefix}/queries-src/**\n"
    "${prefix}/tree-sitter.json\n"
    "/queries/highlights.scm\n"
    "/queries/*\n"
    "/queries/**\n"
    "/queries-src/*\n"
    "/queries-src/**\n"
    "/tree-sitter.json\n"
    "/common/*\n"
    "/common/**\n"
  )

  ts_git(fetch_result -C "${source_dir}" fetch --filter=blob:none --depth 1 origin "${tag}")
  if(NOT fetch_result EQUAL 0)
    ts_git(fetch_result -C "${source_dir}" fetch --filter=blob:none origin "${tag}")
  endif()
  if(NOT fetch_result EQUAL 0)
    file(REMOVE_RECURSE "${source_dir}")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  ts_git(checkout_result -C "${source_dir}" checkout --detach FETCH_HEAD)
  if(NOT checkout_result EQUAL 0)
    file(REMOVE_RECURSE "${source_dir}")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  file(WRITE "${marker}" "${tag}\n")
  if(NOT directory STREQUAL "-")
    set(source_dir "${source_dir}/${directory}")
  endif()
  set(${out_var} "${source_dir}" PARENT_SCOPE)
endfunction()

function(ts_try_populate_grammar_vendor out_var language_name)
  set(source_dir "${TS_GRAMMAR_VENDOR_DIR}/${language_name}")

  if(EXISTS "${source_dir}/src/parser.c" AND
     EXISTS "${source_dir}/queries/highlights.scm")
    set(${out_var} "${source_dir}" PARENT_SCOPE)
  else()
    set(${out_var} "" PARENT_SCOPE)
  endif()
endfunction()

function(ts_try_populate_grammar_archive out_var language_name)
  set(archive "${TS_GRAMMAR_ARCHIVE_DIR}/${language_name}.tar.zst")
  set(unpack_root "${CMAKE_BINARY_DIR}/_grammar-packs/${language_name}")
  set(source_dir "${unpack_root}/${language_name}")
  set(result 0)

  if(NOT EXISTS "${archive}")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()
  if(NOT TAR_EXECUTABLE OR NOT ZSTD_EXECUTABLE)
    message(FATAL_ERROR "${language_name}: archive grammar source requires tar and zstd")
  endif()

  if(EXISTS "${source_dir}/src/parser.c" AND
     EXISTS "${source_dir}/queries/highlights.scm")
    set(${out_var} "${source_dir}" PARENT_SCOPE)
    return()
  endif()

  file(REMOVE_RECURSE "${unpack_root}")
  file(MAKE_DIRECTORY "${unpack_root}")
  execute_process(
    COMMAND "${TAR_EXECUTABLE}" "--use-compress-program=${ZSTD_EXECUTABLE}" -xf "${archive}" -C "${unpack_root}"
    RESULT_VARIABLE result
  )
  if(NOT result EQUAL 0 OR
     NOT EXISTS "${source_dir}/src/parser.c" OR
     NOT EXISTS "${source_dir}/queries/highlights.scm")
    file(REMOVE_RECURSE "${unpack_root}")
    set(${out_var} "" PARENT_SCOPE)
    return()
  endif()

  set(${out_var} "${source_dir}" PARENT_SCOPE)
endfunction()

string(REPLACE "\n" ";" TS_LANGUAGE_ROWS "${TS_LANGUAGE_ROWS}")
foreach(row IN LISTS TS_LANGUAGE_ROWS)
  string(REPLACE "\t" ";" fields "${row}")
  list(LENGTH fields field_count)
  if(NOT field_count EQUAL 7)
    message(FATAL_ERROR "invalid language catalogue row: ${row}")
  endif()
  list(GET fields 0 language_name)
  list(GET fields 1 language_display_name)
  list(GET fields 2 language_repo)
  list(GET fields 3 language_tag)
  list(GET fields 4 language_entrypoint)
  list(GET fields 5 language_directory)
  list(GET fields 6 language_highlight_query)
  if(GITHUB_USE_SSH)
    string(REGEX REPLACE "^https?://github\\.com/" "ssh://git@github.com/" language_repo "${language_repo}")
  endif()

  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" language_symbol "${language_name}")
  string(REGEX REPLACE "[^A-Za-z0-9_]" "_" fetch_name "tree_sitter_grammar_${language_symbol}")
  if(NOT language_entrypoint MATCHES "^tree_sitter_[A-Za-z0-9_]+$")
    message(FATAL_ERROR "${language_name}: invalid entrypoint '${language_entrypoint}'")
  endif()

  set(grammar_dir "")
  set(grammar_source_kind "")
  if(TS_GRAMMAR_SOURCE_MODE STREQUAL "auto" OR TS_GRAMMAR_SOURCE_MODE STREQUAL "archive")
    ts_try_populate_grammar_archive(grammar_dir "${language_name}")
    if(NOT grammar_dir STREQUAL "")
      set(grammar_source_kind "archive")
      message(STATUS "${language_name}: using archived grammar source from ${grammar_dir}")
    elseif(TS_GRAMMAR_SOURCE_MODE STREQUAL "archive")
      message(FATAL_ERROR "${language_name}: archived grammar source not found in ${TS_GRAMMAR_ARCHIVE_DIR}")
    endif()
  endif()

  if(TS_GRAMMAR_SOURCE_MODE STREQUAL "auto" OR TS_GRAMMAR_SOURCE_MODE STREQUAL "vendor")
    if(grammar_dir STREQUAL "")
      ts_try_populate_grammar_vendor(grammar_dir "${language_name}")
      if(NOT grammar_dir STREQUAL "")
        set(grammar_source_kind "vendor")
        message(STATUS "${language_name}: using vendored grammar source from ${grammar_dir}")
      elseif(TS_GRAMMAR_SOURCE_MODE STREQUAL "vendor")
        message(FATAL_ERROR "${language_name}: vendored grammar source not found in ${TS_GRAMMAR_VENDOR_DIR}")
      endif()
    endif()
  endif()

  if(grammar_dir STREQUAL "" AND
     (TS_GRAMMAR_SOURCE_MODE STREQUAL "auto" OR TS_GRAMMAR_SOURCE_MODE STREQUAL "sparse") AND
     TS_GRAMMAR_FETCH_SPARSE)
    message(STATUS "${language_name}: fetching generated grammar paths from ${language_repo}")
    ts_try_populate_grammar_sparse(grammar_dir "${language_repo}" "${language_tag}" "${language_directory}" "${fetch_name}")
    if(NOT grammar_dir STREQUAL "")
      set(grammar_source_kind "sparse")
    endif()
  endif()

  if(grammar_dir STREQUAL "" AND
     NOT TS_GRAMMAR_SOURCE_MODE STREQUAL "archive" AND
     NOT TS_GRAMMAR_SOURCE_MODE STREQUAL "vendor")
    FetchContent_Declare("${fetch_name}"
      GIT_REPOSITORY "${language_repo}"
      GIT_TAG "${language_tag}"
      GIT_SHALLOW FALSE
      GIT_SUBMODULES "" GIT_SUBMODULES_RECURSE FALSE
    )

    FetchContent_GetProperties("${fetch_name}")
    if(NOT ${fetch_name}_POPULATED)
      message(STATUS "${language_name}: fetching entrypoint ${fetch_name} from ${language_repo}")
      FetchContent_Populate("${fetch_name}")
      message(STATUS "${language_name}: fetching entrypoint ${fetch_name} OK")
    endif()

    set(grammar_dir "${${fetch_name}_SOURCE_DIR}")
    set(grammar_source_kind "fetch")
    if(NOT language_directory STREQUAL "-")
      set(grammar_dir "${grammar_dir}/${language_directory}")
    endif()
  elseif(grammar_source_kind STREQUAL "sparse")
    message(STATUS "${language_name}: fetching generated grammar paths OK")
  endif()

  if(NOT EXISTS "${grammar_dir}/src/parser.c")
    message(FATAL_ERROR "${language_name}: missing generated parser at ${grammar_dir}/src/parser.c")
  endif()

  set(grammar_sources "${grammar_dir}/src/parser.c")
  set(grammar_scanner_sources)
  if(EXISTS "${grammar_dir}/src/scanner.c")
    list(APPEND grammar_scanner_sources "${grammar_dir}/src/scanner.c")
    list(APPEND grammar_sources "${grammar_dir}/src/scanner.c")
  endif()
  if(EXISTS "${grammar_dir}/src/scanner.cc")
    list(APPEND grammar_scanner_sources "${grammar_dir}/src/scanner.cc")
    list(APPEND grammar_sources "${grammar_dir}/src/scanner.cc")
  endif()
  set_source_files_properties(${grammar_sources} PROPERTIES INCLUDE_DIRECTORIES "${grammar_dir}/src")
  if(grammar_scanner_sources)
    set(grammar_scanner_definitions
      "serialize=fyts_${language_symbol}_serialize"
      "deserialize=fyts_${language_symbol}_deserialize"
      "scan=fyts_${language_symbol}_scan"
      "scan_comment=fyts_${language_symbol}_scan_comment"
    )
    set_source_files_properties(${grammar_scanner_sources} PROPERTIES
      COMPILE_DEFINITIONS "${grammar_scanner_definitions}"
    )
  endif()
  list(APPEND TS_GRAMMAR_SOURCES ${grammar_sources})
  list(APPEND TS_GRAMMAR_QUERY_ROOT_ARGS "--root" "${grammar_dir}")

  set(query_path "${grammar_dir}/queries/highlights.scm")
  if(NOT grammar_source_kind MATCHES "^(archive|vendor)$" AND NOT language_highlight_query STREQUAL "-")
    if(language_directory STREQUAL "-")
      set(repo_root "${grammar_dir}")
      set(query_path "${repo_root}/${language_highlight_query}")
    else()
      get_filename_component(repo_root "${grammar_dir}" DIRECTORY)
      set(query_path "${repo_root}/${language_highlight_query}")
    endif()
  endif()
  if(NOT EXISTS "${query_path}" AND NOT language_directory STREQUAL "-")
    get_filename_component(repo_root "${grammar_dir}" DIRECTORY)
    set(query_path "${repo_root}/queries/highlights.scm")
  endif()
  if(NOT EXISTS "${query_path}")
    message(FATAL_ERROR "${language_name}: missing highlight query at ${grammar_dir}/queries/highlights.scm")
  endif()

  string(APPEND TS_LANGUAGE_DECLS "const TSLanguage *${language_entrypoint}(void);\n")
  string(APPEND TS_LANGUAGE_ENTRIES "    {\"${language_name}\", ${language_entrypoint}, \"${query_path}\"},\n")
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

set(FYTS_VERSION_SCRIPT "${CMAKE_CURRENT_BINARY_DIR}/generated/fyts.map")
file(WRITE "${FYTS_VERSION_SCRIPT}"
  "{\n"
  "  global:\n"
  "    fyts_*;\n"
  "  local:\n"
  "    *;\n"
  "};\n"
)

add_library(tree-sitter STATIC
  "${tree-sitter_SOURCE_DIR}/lib/src/lib.c"
)
set_target_properties(tree-sitter PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)
target_include_directories(tree-sitter PUBLIC
  "${tree-sitter_SOURCE_DIR}/lib/include"
  "${tree-sitter_SOURCE_DIR}/lib/src"
)
if(HAVE_ASAN)
  target_compile_options(tree-sitter PRIVATE ${ASAN_C_FLAGS})
endif()

add_library(fyts
  src/fyts.c
  ${TS_GRAMMAR_SOURCES}
)
set_target_properties(fyts PROPERTIES
  EXPORT_NAME fyts
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON
)
add_library(libfyts::fyts ALIAS fyts)
set_source_files_properties(src/fyts.c PROPERTIES COMPILE_OPTIONS "${FYTS_C_FLAGS}")
if(HAVE_ASAN)
  target_compile_options(fyts PRIVATE ${ASAN_C_FLAGS})
  if(BUILD_SHARED_LIBS)
    target_link_options(fyts PRIVATE ${ASAN_C_FLAGS})
  endif()
endif()
target_include_directories(fyts PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(fyts PRIVATE
  "${tree-sitter_SOURCE_DIR}/lib/include"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
if(NOT BUILD_SHARED_LIBS)
  target_sources(fyts PRIVATE "${tree-sitter_SOURCE_DIR}/lib/src/lib.c")
  target_include_directories(fyts PRIVATE "${tree-sitter_SOURCE_DIR}/lib/src")
endif()
if(BUILD_SHARED_LIBS AND NOT APPLE AND NOT WIN32)
  target_link_options(fyts PRIVATE "LINKER:--version-script=${FYTS_VERSION_SCRIPT}")
endif()
target_link_libraries(fyts PUBLIC libfyaml::libfyaml)
if(BUILD_SHARED_LIBS)
  target_link_libraries(fyts PRIVATE tree-sitter)
endif()

add_library(fyts_static STATIC
  "${tree-sitter_SOURCE_DIR}/lib/src/lib.c"
  src/fyts.c
  ${TS_GRAMMAR_SOURCES}
)
set(FYTS_STATIC_OUTPUT_NAME fyts_static)
if(BUILD_SHARED_LIBS AND NOT WIN32)
  set(FYTS_STATIC_OUTPUT_NAME fyts)
endif()
set_target_properties(fyts_static PROPERTIES
  EXPORT_NAME fyts_static
  OUTPUT_NAME ${FYTS_STATIC_OUTPUT_NAME}
  C_VISIBILITY_PRESET hidden
  CXX_VISIBILITY_PRESET hidden
  VISIBILITY_INLINES_HIDDEN ON
  POSITION_INDEPENDENT_CODE ON
)
set_source_files_properties(src/fyts.c PROPERTIES COMPILE_OPTIONS "${FYTS_C_FLAGS}")
if(HAVE_ASAN)
  target_compile_options(fyts_static PRIVATE ${ASAN_C_FLAGS})
endif()
target_include_directories(fyts_static PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)
target_include_directories(fyts_static PRIVATE
  "${tree-sitter_SOURCE_DIR}/lib/include"
  "${tree-sitter_SOURCE_DIR}/lib/src"
  "${CMAKE_CURRENT_BINARY_DIR}/generated"
)
target_link_libraries(fyts_static PUBLIC libfyaml::libfyaml)

add_executable(fyts-highlight
  src/fyts_highlight.c
)
set_source_files_properties(src/fyts_highlight.c PROPERTIES COMPILE_OPTIONS "${FYTS_C_FLAGS}")
target_link_libraries(fyts-highlight PRIVATE fyts)
if(HAVE_ASAN)
  target_compile_options(fyts-highlight PRIVATE ${ASAN_C_FLAGS})
  target_link_options(fyts-highlight PRIVATE ${ASAN_C_FLAGS})
endif()

add_executable(fyts-stream-test
  tests/fyts_stream_test.c
)
set_source_files_properties(tests/fyts_stream_test.c PROPERTIES COMPILE_OPTIONS "${FYTS_C_FLAGS}")
target_link_libraries(fyts-stream-test PRIVATE fyts)
if(HAVE_ASAN)
  target_compile_options(fyts-stream-test PRIVATE ${ASAN_C_FLAGS})
  target_link_options(fyts-stream-test PRIVATE ${ASAN_C_FLAGS})
endif()

add_executable(fyts-generic-style-test
  tests/fyts_generic_style_test.c
)
set_source_files_properties(tests/fyts_generic_style_test.c PROPERTIES COMPILE_OPTIONS "${FYTS_C_FLAGS}")
target_link_libraries(fyts-generic-style-test PRIVATE fyts)
if(HAVE_ASAN)
  target_compile_options(fyts-generic-style-test PRIVATE ${ASAN_C_FLAGS})
  target_link_options(fyts-generic-style-test PRIVATE ${ASAN_C_FLAGS})
endif()

add_executable(fyts-language-test
  tests/fyts_language_test.c
)
set_source_files_properties(tests/fyts_language_test.c PROPERTIES COMPILE_OPTIONS "${FYTS_C_FLAGS}")
target_link_libraries(fyts-language-test PRIVATE fyts)
if(HAVE_ASAN)
  target_compile_options(fyts-language-test PRIVATE ${ASAN_C_FLAGS})
  target_link_options(fyts-language-test PRIVATE ${ASAN_C_FLAGS})
endif()

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
add_highlight_test(json json sample.json)
add_highlight_test(yaml yaml sample.yaml)

add_test(NAME yaml-key-style
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color on --lang yaml "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.yaml"
)
set_tests_properties(yaml-key-style PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[38;2;135;175;175mname"
)

add_test(NAME extract-captures
  COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/extract-captures.py" ${TS_GRAMMAR_QUERY_ROOT_ARGS} --format yaml
)
set_tests_properties(extract-captures PROPERTIES
  PASS_REGULAR_EXPRESSION "string\\.special\\.key"
)

add_test(NAME import-tokyonight
  COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/scripts/import-tokyonight.py" --repo "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/tokyonight.nvim" --variant night --light-variant moon
)
set_tests_properties(import-tokyonight PROPERTIES
  PASS_REGULAR_EXPRESSION "tokyonight-night-blue"
)

add_test(NAME stream-cli
  COMMAND "$<TARGET_FILE:fyts-highlight>" --stream --color on --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(stream-cli PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[[0-9;]*m"
)

add_test(NAME light-background
  COMMAND "$<TARGET_FILE:fyts-highlight>" --background light --color on --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(light-background PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[30m"
)

add_test(NAME line-frame
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color off --lang c --prolog "BEGIN:" --epilog ":END" --line-prefix "| " --line-suffix " |" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(line-frame PROPERTIES
  PASS_REGULAR_EXPRESSION "BEGIN:\\|"
)

add_test(NAME reverse-bubble
  COMMAND "$<TARGET_FILE:fyts-highlight>" --background dark --reverse --color on --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(reverse-bubble PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[47m.*\\[30m"
)

add_test(NAME reverse-dark-bubble
  COMMAND "$<TARGET_FILE:fyts-highlight>" --background light --reverse --color on --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(reverse-dark-bubble PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[40m.*\\[37m"
)

add_test(NAME reverse-tabs
  COMMAND "$<TARGET_FILE:fyts-highlight>" --background dark --reverse --color on --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample-tabs.c"
)
set_tests_properties(reverse-tabs PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[47m        "
  FAIL_REGULAR_EXPRESSION "\t"
)

add_test(NAME list-languages
  COMMAND "$<TARGET_FILE:fyts-highlight>" --list-languages
)
set_tests_properties(list-languages PROPERTIES
  PASS_REGULAR_EXPRESSION "(^|\n)python\n"
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

add_test(NAME debug-captures
  COMMAND "$<TARGET_FILE:fyts-highlight>" --debug-captures --color off --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(debug-captures PROPERTIES
  PASS_REGULAR_EXPRESSION "<function>main</>"
  FAIL_REGULAR_EXPRESSION "\\[[0-9;]*m|<constant>main</>"
)

add_test(NAME report-unmatched-captures
  COMMAND "$<TARGET_FILE:fyts-highlight>" --report-unmatched-captures --color off --style "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/string-only-attribute-style.yaml" --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(report-unmatched-captures PROPERTIES
  PASS_REGULAR_EXPRESSION "keyword\n.*function"
  FAIL_REGULAR_EXPRESSION "hello c|\\[[0-9;]*m"
)

add_test(NAME custom-style
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color on --style "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/string-red-style.yaml" --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(custom-style PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[31m"
)

add_test(NAME solarized-dark-style
  COMMAND "$<TARGET_FILE:fyts-highlight>" --background dark --color on --style "${CMAKE_CURRENT_SOURCE_DIR}/stylings/solarized.yaml" --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(solarized-dark-style PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[1;38;2;38;139;210m"
)

add_test(NAME solarized-light-style
  COMMAND "$<TARGET_FILE:fyts-highlight>" --background light --color on --style "${CMAKE_CURRENT_SOURCE_DIR}/stylings/solarized.yaml" --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(solarized-light-style PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[38;2;101;123;131m"
)

add_test(NAME vscode-dark-style
  COMMAND "$<TARGET_FILE:fyts-highlight>" --background dark --color on --style "${CMAKE_CURRENT_SOURCE_DIR}/stylings/vscode.yaml" --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(vscode-dark-style PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[38;2;86;156;214m"
)

add_test(NAME vscode-light-style
  COMMAND "$<TARGET_FILE:fyts-highlight>" --background light --color on --style "${CMAKE_CURRENT_SOURCE_DIR}/stylings/vscode.yaml" --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(vscode-light-style PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[38;2;0;0;255m"
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

add_test(NAME width-framed
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color off --width 6 --lang c --line-prefix "| " --line-suffix " |" "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(width-framed PROPERTIES
  PASS_REGULAR_EXPRESSION "\\| #i \\|"
  FAIL_REGULAR_EXPRESSION "#in"
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

add_test(NAME fyts-generic-style
  COMMAND "$<TARGET_FILE:fyts-generic-style-test>"
)

add_test(NAME fyts-language-supported
  COMMAND "$<TARGET_FILE:fyts-language-test>"
)

if(HAVE_ASAN)
  get_property(FYTS_TESTS DIRECTORY PROPERTY TESTS)
  set_property(TEST ${FYTS_TESTS} APPEND PROPERTY ENVIRONMENT
    "ASAN_OPTIONS=detect_leaks=0:halt_on_error=1"
    "UBSAN_OPTIONS=halt_on_error=1"
  )
endif()

install(TARGETS fyts fyts-highlight
  EXPORT libfyts-targets
  RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)
if(BUILD_SHARED_LIBS)
  install(TARGETS fyts_static
    EXPORT libfyts-targets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
  )
endif()
install(FILES include/fyts/fyts.h
  DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/fyts
)

set(LIBFYTS_INSTALL_CMAKEDIR "${CMAKE_INSTALL_LIBDIR}/cmake/libfyts")

install(EXPORT libfyts-targets
  FILE libfyts-targets.cmake
  NAMESPACE libfyts::
  DESTINATION ${LIBFYTS_INSTALL_CMAKEDIR}
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/libfyts-config.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/libfyts-config.cmake"
  INSTALL_DESTINATION ${LIBFYTS_INSTALL_CMAKEDIR}
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/libfyts-config-version.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY AnyNewerVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/libfyts-config.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/cmake/libfyts-config-version.cmake"
  DESTINATION ${LIBFYTS_INSTALL_CMAKEDIR}
)
