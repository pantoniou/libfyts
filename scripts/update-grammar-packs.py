#!/usr/bin/env python3
#
# Refresh Tree-sitter grammar packs by vendoring into a local work directory
# and archiving the selected languages.

import argparse
import subprocess
import sys
from pathlib import Path


def run(args):
    print("+ %s" % " ".join(str(arg) for arg in args), flush=True)
    return subprocess.run(args).returncode


def add_languages(command, languages):
    language = None

    for language in languages:
        command.extend(["--language", language])


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Update generated Tree-sitter grammar packs.")

    parser.add_argument("--catalogue", default="catalogues/full.yaml")
    parser.add_argument("--work-dir", default="build/_pack-update")
    parser.add_argument("--output", default="vendor/grammar-packs")
    parser.add_argument("--zstd-level", type=int, default=10)
    parser.add_argument("--keep-metadata", action="store_true")
    parser.add_argument("--allow-unknown-license", action="store_true")
    parser.add_argument("--force", action="store_true")
    parser.add_argument(
        "--language",
        action="append",
        default=[],
        help="only update this language name; may be passed multiple times or as a comma-separated list",
    )
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    script_dir = Path(__file__).resolve().parent
    work_dir = Path(args.work_dir)
    vendor_dir = work_dir / "grammars"
    catalogue_output = work_dir / "catalogue.yaml"
    vendor_command = [
        sys.executable,
        str(script_dir / "vendor-grammars.py"),
        "--catalogue",
        args.catalogue,
        "--output",
        str(vendor_dir),
        "--catalogue-output",
        str(catalogue_output),
    ]
    archive_command = [
        sys.executable,
        str(script_dir / "archive-grammars.py"),
        "--catalogue",
        args.catalogue,
        "--input",
        str(vendor_dir),
        "--output",
        args.output,
        "--zstd-level",
        str(args.zstd_level),
    ]
    result = 0

    work_dir.mkdir(parents=True, exist_ok=True)

    if args.allow_unknown_license:
        vendor_command.append("--allow-unknown-license")
    if args.force:
        vendor_command.append("--force")
    if args.keep_metadata:
        archive_command.append("--keep-metadata")

    add_languages(vendor_command, args.language)
    add_languages(archive_command, args.language)

    result = run(vendor_command)
    if result:
        return result

    result = run(archive_command)
    if result:
        return result

    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
