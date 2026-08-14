# SPDX-License-Identifier: MIT
#
# The libfyts tests. Included from the top-level file, so a path that
# starts with CMAKE_CURRENT_SOURCE_DIR keeps its meaning.

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
if(TS_LANGUAGE_CATALOGUE_SET STREQUAL "minimal")
  target_compile_definitions(fyts-language-test PRIVATE FYTS_TEST_MINIMAL_CATALOGUE)
endif()
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
add_highlight_test(bash-alias sh sample.sh)
add_highlight_test(python python sample.py)
add_highlight_test(diff diff sample.diff)
add_highlight_test(json json sample.json)
add_highlight_test(yaml yaml sample.yaml)

add_test(NAME yaml-key-style
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color on --lang yaml "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.yaml"
)
set_tests_properties(yaml-key-style PROPERTIES
  PASS_REGULAR_EXPRESSION "\\[[0-9;]*mname"
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

if(TS_HAVE_MAKE_LANGUAGE)
  add_test(NAME autodetect-makefile
    COMMAND "$<TARGET_FILE:fyts-highlight>" --color on "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/Makefile"
  )
  set_tests_properties(autodetect-makefile PROPERTIES
    PASS_REGULAR_EXPRESSION "hello"
  )
endif()

if(TS_LANGUAGE_CATALOGUE_SET STREQUAL "default")
  add_test(NAME default-unmatched-captures
    COMMAND "${PYTHON3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/check-unmatched-captures.py" "$<TARGET_FILE:fyts-highlight>"
      python "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/python.py"
      c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/c.c"
      cpp "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/cpp.cpp"
      java "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/java.java"
      csharp "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/csharp.cs"
      javascript "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/javascript.js"
      typescript "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/typescript.ts"
      tsx "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/tsx.tsx"
      sql "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/sql.sql"
      r "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/r.r"
      php "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/php.php"
      rust "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/rust.rs"
      go "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/go.go"
      swift "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/swift.swift"
      ada "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/ada.ada"
      fortran "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/fortran.f90"
      perl "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/perl.pl"
      asm "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/asm.s"
      matlab "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/matlab.matlab"
      kotlin "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/kotlin.kt"
      ruby "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/ruby.rb"
      lua "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/lua.lua"
      dart "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/dart.dart"
      zig "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/zig.zig"
      scala "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/scala.scala"
      haskell "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/haskell.hs"
      julia "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/julia.jl"
      bash "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/bash.sh"
      powershell "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/powershell.ps1"
      html "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/html.html"
      css "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/css.css"
      json "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/json.json"
      yaml "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/yaml.yaml"
      xml "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/xml.xml"
      markdown "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/markdown.md"
      toml "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/toml.toml"
      ini "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/ini.ini"
      csv "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/csv.csv"
      dockerfile "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/dockerfile.dockerfile"
      make "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/make.mk"
      cmake "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/cmake.cmake"
      nix "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/nix.nix"
      regex "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/regex.regex"
      diff "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/default-captures/diff.diff"
  )
endif()

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

add_test(NAME custom-styling-long-option
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color on --styling "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/string-red-style.yaml" --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample.c"
)
set_tests_properties(custom-styling-long-option PROPERTIES
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

add_test(NAME width-wide-cjk
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color off --width 5 --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample-wide.c"
)
set_tests_properties(width-wide-cjk PROPERTIES
  PASS_REGULAR_EXPRESSION "// 日"
  FAIL_REGULAR_EXPRESSION "本|�"
)

add_test(NAME width-wide-emoji
  COMMAND "$<TARGET_FILE:fyts-highlight>" --color off --width 7 --lang c "${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/sample-wide.c"
)
set_tests_properties(width-wide-emoji PROPERTIES
  PASS_REGULAR_EXPRESSION "// 日本"
  FAIL_REGULAR_EXPRESSION "🙂|�"
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
  # LeakSanitizer was never ported to arm64 Darwin; requesting detect_leaks
  # there aborts the process outright ("detect_leaks is not supported on
  # this platform") instead of just warning.
  if(APPLE AND CMAKE_SYSTEM_PROCESSOR MATCHES "^(arm64|aarch64)")
    set(FYTS_ASAN_OPTIONS "detect_leaks=0:halt_on_error=1")
  else()
    set(FYTS_ASAN_OPTIONS "detect_leaks=1:halt_on_error=1")
  endif()
  set_property(TEST ${FYTS_TESTS} APPEND PROPERTY ENVIRONMENT
    "ASAN_OPTIONS=${FYTS_ASAN_OPTIONS}"
    "UBSAN_OPTIONS=halt_on_error=1"
  )
endif()
