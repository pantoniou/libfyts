#!/usr/bin/env python3
#
# Scan fetched Tree-sitter grammar checkouts and report highlight captures.

import argparse
import re
import sys
from pathlib import Path


CAPTURE_RE = re.compile(r"@([A-Za-z0-9_.-]+)")


def strip_line_comment(line):
    escaped = False
    in_string = False

    for index, char in enumerate(line):
        if escaped:
            escaped = False
            continue
        if char == "\\":
            escaped = True
            continue
        if char == '"':
            in_string = not in_string
            continue
        if char == ";" and not in_string:
            return line[:index]
    return line


def captures_from_query(path):
    captures = set()

    with path.open("r", encoding="utf-8", errors="replace") as query:
        for line in query:
            line = strip_line_comment(line)
            captures.update(CAPTURE_RE.findall(line))
    return captures


def language_name(root, query):
    source = None

    for parent in query.parents:
        if parent.parent == root and parent.name.endswith("-src"):
            source = parent
            break

    if source is None and query.parent.parent == root:
        return root.name
    if source is None:
        return str(query.parent.parent.relative_to(root))

    name = source.name[:-4]
    if name.startswith("tree_sitter_grammar_"):
        name = name[len("tree_sitter_grammar_"):]

    grammar_root = query.parent.parent
    if grammar_root != source:
        suffix = grammar_root.relative_to(source)
        name = "%s/%s" % (name, suffix)
    return name


def discover(root, query_name):
    result = {}

    for query in sorted(root.rglob(query_name)):
        captures = captures_from_query(query)
        if not captures:
            continue
        result[language_name(root, query)] = captures
    return result


def yaml_quote(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def emit_yaml(languages):
    all_captures = sorted({capture for captures in languages.values() for capture in captures})

    print("captures:")
    for capture in all_captures:
        print("  - %s" % yaml_quote(capture))
    print("languages:")
    for language in sorted(languages):
        print("  - name: %s" % yaml_quote(language))
        print("    captures:")
        for capture in sorted(languages[language]):
            print("      - %s" % yaml_quote(capture))


def emit_text(languages):
    all_captures = sorted({capture for captures in languages.values() for capture in captures})

    print("captures:")
    for capture in all_captures:
        print(capture)
    print()
    print("languages:")
    for language in sorted(languages):
        captures = ", ".join(sorted(languages[language]))
        print("%s: %s" % (language, captures))


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Extract Tree-sitter highlight capture names from fetched grammars."
    )
    parser.add_argument(
        "--root",
        action="append",
        default=None,
        help="directory containing grammar checkouts; may be repeated (default: build/_deps)",
    )
    parser.add_argument(
        "--query",
        default="highlights.scm",
        help="query filename to scan (default: highlights.scm)",
    )
    parser.add_argument(
        "--format",
        choices=("text", "yaml"),
        default="text",
        help="output format (default: text)",
    )
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    roots = [Path(root) for root in (args.root or ["build/_deps"])]
    languages = {}

    for root in roots:
        if not root.exists():
            print("capture scan root does not exist: %s" % root, file=sys.stderr)
            return 1
        languages.update(discover(root, args.query))

    if not languages:
        print("no %s files found under %s" % (args.query, ", ".join(str(root) for root in roots)), file=sys.stderr)
        return 1

    if args.format == "yaml":
        emit_yaml(languages)
    else:
        emit_text(languages)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
