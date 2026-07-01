# libfyts

`libfyts` is a C library and command line highlighter built on
[Tree-sitter](https://tree-sitter.github.io/tree-sitter/). It embeds a selected
catalogue of generated Tree-sitter grammars, runs each grammar's highlight
query, and renders ANSI-highlighted source text.

The repository is C/CMake only. It does not use the upstream Tree-sitter Rust
CLI, Cargo, Python bindings, or Python runtime paths for highlighting.

## Features

- C library API with one-shot and line-buffered streaming highlighters
- `fyts-highlight` CLI for terminal highlighting
- automatic language detection from file extensions
- configurable catalogues: `minimal`, `default`, `full`, and `full-broken`
- checked-in per-language grammar packs under `vendor/grammar-packs`
- YAML styling files with capture-to-attribute and attribute-to-ANSI mappings
- terminal color modes, width clipping, reverse/background framing, and capture
  debugging helpers

## Dependencies

Build-time tools and libraries:

- CMake 3.16 or newer
- C and C++ compilers
- Git
- `yq`
- Python 3
- `tar` and `zstd` when using grammar archives
- `libfyaml`

Tree-sitter itself is fetched by CMake `FetchContent`. Grammar sources normally
come from the checked-in archives in `vendor/grammar-packs`.

## Build

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

The default catalogue is `minimal`, which keeps first builds small. Select a
larger catalogue at configure time:

```sh
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=default
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=full
```

Force the build to use only checked-in grammar packs:

```sh
cmake -S . -B build -DTS_GRAMMAR_SOURCE_MODE=archive
```

Useful configure options:

- `TS_LANGUAGE_CATALOGUE_SET=minimal|default|full|full-broken`
- `TS_LANGUAGE_CATALOGUE=/path/to/catalogue.yaml`
- `TS_STYLING=/path/to/styling.yaml`
- `TS_GRAMMAR_SOURCE_MODE=auto|archive|vendor|sparse|fetch`
- `GITHUB_USE_SSH=ON`
- `ENABLE_ASAN=ON`

By default the build follows libfyaml's library layout: it builds the normal
`fyts` library from `BUILD_SHARED_LIBS` and also builds a static archive named
`libfyts.a`.

Install the library, CLI, header, and static archive with:

```sh
cmake --install build --prefix /usr/local
```

CMake consumers can use the installed package:

```cmake
find_package(libfyts REQUIRED CONFIG)
target_link_libraries(app PRIVATE libfyts::fyts)
```

When the package was installed from a shared build, the static archive is also
available as `libfyts::fyts_static`.

## CLI

The build produces `fyts-highlight`:

```sh
build/fyts-highlight tests/fixtures/sample.c
build/fyts-highlight --lang python tests/fixtures/sample.py
build/fyts-highlight --list-languages
```

If `--lang` is omitted, the CLI chooses a language from the source file
extension using the selected catalogue.

Common options:

```sh
build/fyts-highlight --color auto|off|on file.c
build/fyts-highlight --background auto|dark|light file.c
build/fyts-highlight --width auto file.c
build/fyts-highlight --width 100 file.c
build/fyts-highlight --style stylings/tokyonight.yaml file.c
build/fyts-highlight --reverse file.c
build/fyts-highlight --stream file.c
build/fyts-highlight --debug-captures file.c
build/fyts-highlight --report-unmatched-captures file.c
```

Terminal output probes the tty width by default. `--width 100` clips visible
source lines to a fixed width, while `--width auto` or `--width 0` probes the
tty and leaves output unbounded when stdout is not a terminal.

Catalogue and styling YAML can be emitted from the embedded build data:

```sh
build/fyts-highlight --output-catalog
build/fyts-highlight --output-styling
```

`--debug-captures` renders capture names instead of ANSI styles, for example
`<keyword>if</>`. `--report-unmatched-captures` reports captures that fired but
did not match the active styling rules.

## Library API

Public declarations live in `include/fyts/fyts.h`.

One-shot highlighting:

```c
#include <fyts/fyts.h>

struct fyts_config config = {
	.lang = "c",
	.color_mode = FYTS_COLOR_ON,
	.write = fyts_write_file,
	.write_user = stdout,
};

fyts_highlight_source(&config, source, source_len);
```

Streaming highlighting:

```c
struct fyts_ctx *ctx;
char *out;
size_t out_len;

ctx = fyts_ctx_create(&config);
fyts_ctx_feed(ctx, chunk, chunk_len, &out, &out_len);
fyts_ctx_finish(ctx, &out, &out_len);
fyts_ctx_destroy(ctx);
```

The streaming API buffers input until complete lines are available and returns
newly rendered output through `out`/`out_len`. The caller owns returned buffers
and should free them.

Additional helpers:

- `fyts_list_languages()`
- `fyts_language_supported()`
- `fyts_detect_language_for_path()`
- `fyts_output_catalogue()`
- `fyts_output_styling()`

## Catalogues

Language catalogues are YAML files in `catalogues/`. Each entry gives the
language name, repository, pinned tag, C entrypoint, extensions, and optional
source layout overrides:

```yaml
- name: c
  display-name: C
  repo: https://github.com/tree-sitter/tree-sitter-c.git
  tag: b780e47fc780ddc8da13afa35a3f4ed5c157823d
  entrypoint: tree_sitter_c
  extensions: [ ".c", ".C" ]
```

Built-in catalogues:

- `minimal.yaml`: small smoke-test set
- `default.yaml`: upstream `tree-sitter` GitHub grammars plus diff/patch
- `full.yaml`: usable full language-pack set
- `full-broken.yaml`: full catalogue including known-broken entries

## Styling

Styling files live in `stylings/`. They map Tree-sitter capture names to
semantic attributes, then map attributes to ANSI styles. Capture rules are POSIX
extended regular expressions matched in file order.

Available bundled themes include:

- `default.yaml`
- `solarized.yaml`
- `tokyonight.yaml`
- `catppuccin.yaml`
- `kanagawa.yaml`
- `vscode.yaml`
- `vscode-nvim.yaml`

## Grammar Packs

Expanded grammar repositories are not committed. The repository keeps compact
per-language archives in `vendor/grammar-packs`, and the build expands selected
languages under the build directory.

Refresh packs for the configured catalogue:

```sh
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=default
cmake --build build --target update-packs
```

Refresh selected packs:

```sh
cmake -S . -B build -DTS_LANGUAGE_CATALOGUE_SET=default -DTS_UPDATE_PACK_LANGUAGES=c,cpp,python
cmake --build build --target update-packs
```

Use `-DTS_UPDATE_PACK_FORCE=ON` to refetch matching local update checkouts.

## Development

Run the normal build and tests before committing:

```sh
cmake -S . -B build
cmake --build build
ctest --test-dir build --output-on-failure
```

For sanitizer checks:

```sh
cmake -S . -B build-asan -DENABLE_ASAN=ON
cmake --build build-asan
ctest --test-dir build-asan --output-on-failure
```

C source follows Linux kernel style. Format C changes with:

```sh
clang-format -i src/fyts.c src/fyts_highlight.c
```
