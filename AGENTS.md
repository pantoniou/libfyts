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

The build fetches Tree-sitter with CMake `FetchContent` and uses pinned grammar
metadata from the selected catalogue. Grammar sources are normally checked in
as per-language archives under `vendor/grammar-packs` and expanded under the
build directory. Fetched sources belong under `build/_deps` and must not be
committed.

Select a catalogue with:

```sh
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=minimal
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=default
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=full
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=full-broken
```

Grammar sources can come from per-language archives, local expanded trees,
sparse fetches, or full fetches:

```sh
cmake -S . -B build -DTS_GRAMMAR_SOURCE_MODE=archive
cmake -S . -B build -DTS_GRAMMAR_SOURCE_MODE=vendor
cmake -S . -B build -DTS_GRAMMAR_SOURCE_MODE=sparse
cmake -S . -B build -DTS_GRAMMAR_SOURCE_MODE=fetch
```

`auto` prefers archives from `vendor/grammar-packs/*.tar.zst`, then local
expanded sources in `vendor/grammars`, then network fetches. Archive mode
unpacks only the selected catalogue entries into `build/_grammar-packs`.

Use SSH GitHub URLs for catalogue fetches with:

```sh
cmake -S . -B build -DGITHUB_USE_SSH=ON
```

Enable the sanitizer build with:

```sh
cmake -S . -B build-asan -DENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

`ENABLE_ASAN` follows libfyaml's convention and enables address sanitizer,
signed-integer-overflow sanitizer, undefined behavior sanitizer, and
`-fno-omit-frame-pointer` when the compiler accepts `-fsanitize=address`.

## Style

C source follows Linux kernel style:

- tabs for indentation, 8-column tab stops
- opening braces on function/control lines per kernel style
- prefer small, direct C helpers over broad abstractions
- declare local variables at the start of the function or block; do not
  intermix declarations with statements inside loops and branches
- keep comments sparse and useful

Run `clang-format -i src/fyts.c src/fyts_highlight.c` before committing C
changes. The repository `.clang-format` is configured for this style.

## Catalogue

Language metadata lives in `catalogues/*.yaml`:

- `minimal.yaml`: small smoke-test set
- `default.yaml`: upstream `github.com/tree-sitter/*` grammars plus `diff`
- `full.yaml`: usable full tree-sitter-language-pack definition set
- `full-broken.yaml`: all full catalogue entries, with known-broken languages
  moved to the end

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

Do not commit expanded grammar source trees. `vendor/grammars` is ignored and
only exists as local scratch or as the input to regenerate packs. If using
`TS_GRAMMAR_SOURCE_MODE=vendor`, each `vendor/grammars/<name>` tree must be
normalized to contain `src/parser.c` and `queries/highlights.scm`. Repo-local
`highlight-query` overrides in the catalogue are for fetched/sparse source
trees; do not make archive or local-vendor builds depend on those original
query paths.

Use per-language archives instead of one large compressed blob, so ordinary git
can handle updates without Git LFS:

```sh
scripts/archive-grammars.py --catalogue catalogues/full.yaml
scripts/archive-grammars.py --catalogue catalogues/default.yaml --language c,cpp,python
```

Prefer the CMake target when refreshing checked-in packs. It uses the configured
catalogue, vendors into `build/_pack-update/grammars`, and writes packs to
`vendor/grammar-packs`:

```sh
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=default
cmake --build build --target update-packs
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=default -DTS_UPDATE_PACK_LANGUAGES=c,cpp,python
cmake --build build --target update-packs
```

Use `-DTS_UPDATE_PACK_FORCE=ON` to refetch matching local update checkouts,
`-DTS_UPDATE_PACK_KEEP_METADATA=ON` to preserve bulky grammar metadata in the
archives, and `-DTS_UPDATE_PACK_ALLOW_UNKNOWN_LICENSE=ON` only when the
licensing risk has been reviewed.

The archive script omits bulky debug metadata (`src/grammar.json`,
`src/node-types.json`, and alternate `src/parser_abi*.c`) by default. Pass
`--keep-metadata` only when the archive must preserve those files for grammar
debugging.

To verify the per-language archives are self-contained, force archive mode:

```sh
cmake -S . -B build-archive-full-test -DTS_LANGUAGE_CATALOGUE_SET=full -DTS_GRAMMAR_SOURCE_MODE=archive
cmake --build build-archive-full-test
ctest --test-dir build-archive-full-test --output-on-failure
```

A successful archive-only build should report grammar sources under
`build-archive-full-test/_grammar-packs`; it must not require
`vendor/grammars`.

Generated grammar sources are compiled into one static library. Keep grammar
`src` include directories scoped per generated source file so
`tree_sitter/parser.h` from one grammar cannot shadow another. External scanner
helpers with generic names such as `serialize`, `deserialize`, `scan`, and
`scan_comment` must stay renamed per language at compile time to avoid archive
link collisions.

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
