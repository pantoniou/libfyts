#!/usr/bin/env python3
#
# Pack vendored Tree-sitter grammar directories as per-language tar.zst files.

import argparse
import subprocess
import sys
from pathlib import Path


def parse_catalogue(path):
    entries = []
    current = None

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].rstrip()
        if not line:
            continue
        if line.startswith("- "):
            if current:
                entries.append(current)
            current = {}
            line = "  " + line[2:]
        if current is None or not line.startswith("  ") or ":" not in line:
            continue
        key, value = line.strip().split(":", 1)
        value = value.strip()
        if value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
        current[key] = value

    if current:
        entries.append(current)
    return entries


def run(args):
    return subprocess.run(args, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE, text=True)


def archive_language(name, input_dir, output_dir, level, keep_metadata):
    source = input_dir / name
    output = output_dir / ("%s.tar.zst" % name)
    temporary = output.with_suffix(output.suffix + ".tmp")
    excludes = []
    result = None
    raw_size = 0
    archive_size = 0

    if not source.exists():
        raise RuntimeError("%s: missing vendored source at %s" % (name, source))
    if not keep_metadata:
        excludes = [
            "--exclude=%s/src/grammar.json" % name,
            "--exclude=%s/src/node-types.json" % name,
            "--exclude=%s/src/parser_abi*.c" % name,
        ]

    output_dir.mkdir(parents=True, exist_ok=True)
    if temporary.exists():
        temporary.unlink()

    result = run(
        [
            "tar",
            "--sort=name",
            "--owner=0",
            "--group=0",
            "--numeric-owner",
            "--mtime=@0",
            "-C",
            str(input_dir),
            "-I",
            "zstd -%d -T0" % level,
            "-cf",
            str(temporary),
            *excludes,
            name,
        ]
    )
    if result.returncode:
        if temporary.exists():
            temporary.unlink()
        raise RuntimeError("%s: tar failed: %s" % (name, result.stderr.strip()))

    temporary.replace(output)
    raw_size = sum(path.stat().st_size for path in source.rglob("*") if path.is_file())
    archive_size = output.stat().st_size
    return raw_size, archive_size


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Archive vendored Tree-sitter grammars per language.")

    parser.add_argument("--catalogue", default="catalogues/full.yaml")
    parser.add_argument("--input", default="vendor/grammars")
    parser.add_argument("--output", default="vendor/grammar-packs")
    parser.add_argument("--zstd-level", type=int, default=10)
    parser.add_argument("--keep-metadata", action="store_true")
    parser.add_argument(
        "--language",
        action="append",
        default=[],
        help="only archive this language name; may be passed multiple times or as a comma-separated list",
    )
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    catalogue = Path(args.catalogue)
    input_dir = Path(args.input)
    output_dir = Path(args.output)
    entries = parse_catalogue(catalogue)
    selected_languages = set()
    total_raw = 0
    total_archive = 0

    for selected in args.language:
        selected_languages.update(name.strip() for name in selected.split(",") if name.strip())

    if selected_languages:
        entries = [entry for entry in entries if entry["name"] in selected_languages]
    if not entries:
        print("no catalogue entries selected", file=sys.stderr)
        return 1

    for entry in entries:
        name = entry["name"]

        try:
            raw_size, archive_size = archive_language(
                name, input_dir, output_dir, args.zstd_level, args.keep_metadata
            )
        except RuntimeError as error:
            print("archive-grammars: %s" % error, file=sys.stderr)
            return 1
        total_raw += raw_size
        total_archive += archive_size
        print("%s: %d -> %d (%.1f%%)" % (name, raw_size, archive_size, archive_size * 100.0 / raw_size))

    print("total: %d -> %d (%.1f%%)" % (total_raw, total_archive, total_archive * 100.0 / total_raw))
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
