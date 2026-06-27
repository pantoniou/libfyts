# Project Notes

## Scope

This is a C/CMake Tree-sitter highlighter. Keep the project C-only: do not add
Rust, Python bindings, Cargo paths, or the upstream Tree-sitter CLI.

## Build

Use CMake from the repository root:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The build fetches Tree-sitter and grammar repositories with CMake
`FetchContent` using pinned commits from the selected catalogue. Fetched
sources belong under `build/_deps` and must not be committed.

Select a catalogue with:

```sh
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=minimal
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=default
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=full
```

Use SSH GitHub URLs for catalogue fetches with:

```sh
cmake -S . -B build -DGITHUB_USE_SSH=ON
```

## Style

C source follows Linux kernel style:

- tabs for indentation, 8-column tab stops
- opening braces on function/control lines per kernel style
- prefer small, direct C helpers over broad abstractions
- declare local variables at the start of the function or block; do not
  intermix declarations with statements inside loops and branches
- keep comments sparse and useful

Run `clang-format -i src/ts_highlight.c` before committing C changes. The
repository `.clang-format` is configured for this style.

## Catalogue

Language metadata lives in `catalogues/*.yaml`:

- `minimal.yaml`: small smoke-test set
- `default.yaml`: upstream `github.com/tree-sitter/*` grammars plus `diff`
- `full.yaml`: full tree-sitter-language-pack definition set

Each entry should include:

```yaml
- name: c
  display-name: C
  repo: https://github.com/tree-sitter/tree-sitter-c.git
  tag: b780e47fc780ddc8da13afa35a3f4ed5c157823d
  entrypoint: tree_sitter_c
  extensions: [ ".c", ".C" ]
```

CMake parses the catalogue with `yq`, fetches each grammar, and generates the
language table plus the embedded catalogue.

## libfyaml

Use the generic API from `libfyaml/libfyaml-generic.h` for runtime catalogue
handling. Prefer `fy_parse(gb, ...)`, `fy_emit(...)`, and the generic
convenience helpers over direct `fy_document` / `fy_node` traversal.

For generic catalogue traversal:

- use `fy_foreach(item, sequence)` instead of manually creating sequence
  handles and indexing them
- use `fy_get(mapping, "key", default_value)` for mapping lookup when the
  expected type is known
- use `fy_cast(value, default_value)` when reading scalar values from generic
  nodes
- keep temporary `fy_generic` values and scalar pointers declared before the
  first statement in the containing block

Do not use `pkg-config` for libfyaml in CMake; use `find_path()` and
`find_library()`.
