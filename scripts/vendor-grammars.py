#!/usr/bin/env python3
#
# Vendor generated Tree-sitter grammar sources from a catalogue.

import argparse
import json
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path


ALLOWED_LICENSES = {
    "0BSD",
    "Apache-2.0",
    "BSD",
    "ISC",
    "MIT",
    "MPL-2.0",
    "Unlicense",
}


class SkipEntry(Exception):
    pass


def run_git(args, cwd=None):
    return subprocess.run(
        ["git"] + args,
        cwd=cwd,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )


def parse_catalogue(path):
    entries = []
    current = None
    key_re = re.compile(r"^  ([A-Za-z0-9_-]+):\s*(.*)$")

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].rstrip()
        if not line:
            continue
        if line.startswith("- "):
            if current:
                entries.append(current)
            current = {}
            line = "  " + line[2:]
        if current is None:
            continue
        match = key_re.match(line)
        if not match:
            continue
        key, value = match.groups()
        value = value.strip()
        if value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
        current[key] = value

    if current:
        entries.append(current)
    return entries


def sparse_checkout(repo, tag, directory, checkout):
    prefix = "" if directory in ("", "-") else "/" + directory
    result = None

    checkout.mkdir(parents=True)
    for args in (
        ["init"],
        ["remote", "add", "origin", repo],
        ["config", "core.sparseCheckout", "true"],
    ):
        result = run_git(args, checkout)
        if result.returncode:
            raise RuntimeError(result.stderr.strip())

    (checkout / ".git" / "info" / "sparse-checkout").write_text(
        "\n".join(
            [
                "%s/src/*" % prefix,
                "%s/src/**" % prefix,
                "%s/queries/highlights.scm" % prefix,
                "%s/queries/*" % prefix,
                "%s/queries/**" % prefix,
                "%s/queries-src/*" % prefix,
                "%s/queries-src/**" % prefix,
                "%s/tree-sitter.json" % prefix,
                "/queries/highlights.scm",
                "/queries/*",
                "/queries/**",
                "/queries-src/*",
                "/queries-src/**",
                "/tree-sitter.json",
                "/common/*",
                "/common/**",
                "/LICENSE*",
                "/COPYING*",
                "/NOTICE*",
                "%s/LICENSE*" % prefix,
                "%s/COPYING*" % prefix,
                "%s/NOTICE*" % prefix,
                "",
            ]
        ),
        encoding="utf-8",
    )

    result = run_git(["fetch", "--filter=blob:none", "--depth", "1", "origin", tag], checkout)
    if result.returncode:
        result = run_git(["fetch", "--filter=blob:none", "origin", tag], checkout)
    if result.returncode:
        raise RuntimeError(result.stderr.strip())

    result = run_git(["checkout", "--detach", "FETCH_HEAD"], checkout)
    if result.returncode:
        raise RuntimeError(result.stderr.strip())


def sparse_checkout_path(repo, tag, path, checkout):
    result = None

    checkout.mkdir(parents=True)
    for args in (
        ["init"],
        ["remote", "add", "origin", repo],
        ["config", "core.sparseCheckout", "true"],
    ):
        result = run_git(args, checkout)
        if result.returncode:
            raise RuntimeError(result.stderr.strip())

    (checkout / ".git" / "info" / "sparse-checkout").write_text("/%s\n" % path, encoding="utf-8")

    result = run_git(["fetch", "--filter=blob:none", "--depth", "1", "origin", tag], checkout)
    if result.returncode:
        result = run_git(["fetch", "--filter=blob:none", "origin", tag], checkout)
    if result.returncode:
        raise RuntimeError(result.stderr.strip())

    result = run_git(["checkout", "--detach", "FETCH_HEAD"], checkout)
    if result.returncode:
        raise RuntimeError(result.stderr.strip())


def detect_license(text):
    lower = text.lower()

    if "mozilla public license version 2.0" in lower:
        return "MPL-2.0"
    if "apache license" in lower and "version 2.0" in lower:
        return "Apache-2.0"
    if "permission is hereby granted, free of charge" in lower:
        return "MIT"
    if "redistribution and use in source and binary forms" in lower:
        return "BSD"
    if "permission to use, copy, modify, and/or distribute this software" in lower:
        return "ISC"
    if "the unlicense" in lower:
        return "Unlicense"
    if "bsd zero clause license" in lower or "0bsd" in lower:
        return "0BSD"
    return "unknown"


def license_files(root, grammar_root):
    seen = set()
    files = []

    for base in (root, grammar_root):
        for pattern in ("LICENSE*", "COPYING*", "NOTICE*"):
            for path in sorted(base.glob(pattern)):
                if path.is_file() and path not in seen:
                    files.append(path)
                    seen.add(path)
    return files


def copy_tree(src, dst):
    ignore = shutil.ignore_patterns("*.o", "*.a", "*.so", "*.dylib", "*.dll", "*.exe")

    if dst.exists():
        shutil.rmtree(dst)
    shutil.copytree(src, dst, ignore=ignore)


def source_references_repo_common(src):
    path = None

    for path in src.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix not in (".c", ".cc", ".h", ".hpp"):
            continue
        if re.search(r'"\.\./(?:\.\./)+common/', path.read_text(encoding="utf-8", errors="replace")):
            return True
    return False


def copy_repo_common(checkout, output):
    src = checkout / "common"
    dst = output / "common"
    ignore = shutil.ignore_patterns("*.o", "*.a", "*.so", "*.dylib", "*.dll", "*.exe")

    if not src.exists():
        return
    shutil.copytree(src, dst, dirs_exist_ok=True, ignore=ignore)


def rewrite_repo_common_includes(src):
    path = None
    text = None
    rewritten = None

    for path in src.rglob("*"):
        if not path.is_file():
            continue
        if path.suffix not in (".c", ".cc", ".h", ".hpp"):
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        rewritten = re.sub(r'"\.\./(?:\.\./)+common/', '"../common/', text)
        if rewritten != text:
            path.write_text(rewritten, encoding="utf-8")


def yaml_scalar(value):
    if value is None:
        return None
    if value.startswith("[") and value.endswith("]"):
        return value
    if re.match(r"^[A-Za-z0-9_./:+@-]+$", value):
        return value
    return json.dumps(value)


def write_yaml_mapping(path, mapping):
    lines = []

    for key, value in mapping.items():
        if value is None:
            continue
        if isinstance(value, list):
            lines.append("%s:" % key)
            for item in value:
                lines.append("  - %s" % yaml_scalar(item))
        else:
            lines.append("%s: %s" % (key, yaml_scalar(str(value))))

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def write_yaml_sequence(path, items):
    lines = []

    for item in items:
        lines.append("- name: %s" % yaml_scalar(item["name"]))
        lines.append("  repo: %s" % yaml_scalar(item["repo"]))
        lines.append("  tag: %s" % yaml_scalar(item["tag"]))
        if item.get("directory"):
            lines.append("  directory: %s" % yaml_scalar(item["directory"]))
        lines.append("  licenses:")
        for license_id in item["licenses"]:
            lines.append("    - %s" % yaml_scalar(license_id))

    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def parse_simple_yaml_mapping(path):
    mapping = {}
    current_key = None
    key_re = re.compile(r"^([A-Za-z0-9_-]+):\s*(.*)$")

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        line = raw_line.split("#", 1)[0].rstrip()
        match = key_re.match(line)

        if not line:
            continue
        if line.startswith("  - ") and current_key:
            mapping[current_key].append(line[4:].strip().strip('"'))
            continue
        if not match:
            continue

        key, value = match.groups()
        value = value.strip()
        if not value:
            mapping[key] = []
            current_key = key
            continue
        if value.startswith('"') and value.endswith('"'):
            value = value[1:-1]
        mapping[key] = value
        current_key = None

    return mapping


def read_vendor_manifest(output):
    yaml_path = output / "fyts-vendor.yaml"
    json_path = output / "fyts-vendor.json"

    if yaml_path.exists():
        return parse_simple_yaml_mapping(yaml_path)
    if json_path.exists():
        return json.loads(json_path.read_text(encoding="utf-8"))
    return None


def cached_manifest_matches(entry, vendor_dir, output, manifest):
    directory = entry.get("directory", "-")
    manifest_directory = manifest.get("directory")

    if manifest_directory in ("", "-"):
        manifest_directory = None
    if directory == "-":
        directory = None

    if manifest.get("name") != entry["name"]:
        return False
    if manifest.get("repo") != entry["repo"]:
        return False
    if manifest.get("tag") != entry["tag"]:
        return False
    if manifest_directory != directory:
        return False
    if not (output / "src" / "parser.c").exists():
        return False
    if not (output / "queries" / "highlights.scm").exists():
        return False
    if source_references_repo_common(output / "src") and not (output / "common").exists():
        return False
    return True


def write_catalogue(path, entries):
    lines = ["# generated by scripts/vendor-grammars.py"]

    for entry in entries:
        lines.append("- name: %s" % yaml_scalar(entry["name"]))
        lines.append("  display-name: %s" % yaml_scalar(entry.get("display-name", entry["name"])))
        lines.append("  repo: %s" % yaml_scalar(entry["repo"]))
        lines.append("  tag: %s" % yaml_scalar(entry["tag"]))
        lines.append("  entrypoint: %s" % yaml_scalar(entry["entrypoint"]))
        lines.append("  progressive-safe: %s" % str(entry.get("progressive-safe", True)).lower())
        if entry.get("highlight-query"):
            lines.append("  highlight-query: %s" % yaml_scalar(entry["highlight-query"]))
        if entry.get("highlight-query-repo"):
            lines.append("  highlight-query-repo: %s" % yaml_scalar(entry["highlight-query-repo"]))
        if entry.get("highlight-query-tag"):
            lines.append("  highlight-query-tag: %s" % yaml_scalar(entry["highlight-query-tag"]))
        if entry.get("highlight-query-path"):
            lines.append("  highlight-query-path: %s" % yaml_scalar(entry["highlight-query-path"]))
        if entry.get("directory") not in (None, "", "-"):
            lines.append("  directory: %s" % yaml_scalar(entry["directory"]))
        if entry.get("filenames"):
            lines.append("  filenames: %s" % entry["filenames"])
        if entry.get("extensions"):
            lines.append("  extensions: %s" % entry["extensions"])
        lines.append("")

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("\n".join(lines), encoding="utf-8")


def tree_sitter_json_paths(path, name, directory):
    highlights = []
    data = {}
    value = None

    if not path.exists():
        return []

    data = json.loads(path.read_text(encoding="utf-8"))
    value = data.get("highlights")
    if isinstance(value, str):
        highlights.append(value)
    elif isinstance(value, list):
        highlights.extend(item for item in value if isinstance(item, str))

    value = data.get("queries")
    if isinstance(value, dict):
        value = value.get("highlights")
        if isinstance(value, str):
            highlights.append(value)
        elif isinstance(value, list):
            highlights.extend(item for item in value if isinstance(item, str))

    value = data.get("grammars")
    if isinstance(value, list):
        for grammar in value:
            grammar_path = None
            grammar_name = None

            if not isinstance(grammar, dict):
                continue
            grammar_path = grammar.get("path")
            grammar_name = grammar.get("name")
            if directory not in ("", "-") and grammar_path not in (None, directory):
                continue
            if directory in ("", "-") and grammar_name not in (None, name):
                continue

            value = grammar.get("highlights")
            if isinstance(value, str):
                highlights.append(value)
            elif isinstance(value, list):
                highlights.extend(item for item in value if isinstance(item, str))

            value = grammar.get("queries")
            if not isinstance(value, dict):
                continue
            value = value.get("highlights")
            if isinstance(value, str):
                highlights.append(value)
            elif isinstance(value, list):
                highlights.extend(item for item in value if isinstance(item, str))

    return highlights


def query_candidates(entry, checkout, grammar_root, name, directory):
    candidates = []
    query = entry.get("highlight-query")

    if query:
        path = Path(query)
        candidates.append(path if path.is_absolute() else checkout / path)

    for manifest_root in (grammar_root, checkout):
        for query in tree_sitter_json_paths(manifest_root / "tree-sitter.json", name, directory):
            path = Path(query)
            candidates.append(path if path.is_absolute() else manifest_root / path)

    candidates.append(grammar_root / "queries" / "highlights.scm")
    if directory not in ("", "-"):
        candidates.append(checkout / "queries" / "highlights.scm")
    return candidates


def external_query(entry, temp_root):
    repo = entry.get("highlight-query-repo")
    tag = entry.get("highlight-query-tag")
    path = entry.get("highlight-query-path")
    checkout = temp_root / ("%s-query" % entry["name"])

    if not repo and not tag and not path:
        return None
    if not repo or not tag or not path:
        raise RuntimeError("%s: incomplete external highlight query source" % entry["name"])

    sparse_checkout_path(repo, tag, path, checkout)
    path = checkout / path
    return path if path.exists() else None


def vendor_entry(entry, vendor_dir, temp_root, allow_unknown, force):
    name = entry["name"]
    repo = entry["repo"]
    tag = entry["tag"]
    directory = entry.get("directory", "-")
    checkout = temp_root / name
    grammar_root = checkout if directory in ("", "-") else checkout / directory
    output = vendor_dir / name
    query_src = None
    licenses = []
    detected = []
    manifest = {}

    if not force and output.exists():
        manifest = read_vendor_manifest(output)
        if manifest and cached_manifest_matches(entry, vendor_dir, output, manifest):
            if (output / "fyts-vendor.json").exists():
                (output / "fyts-vendor.json").unlink()
            write_yaml_mapping(output / "fyts-vendor.yaml", manifest)
            return manifest

    sparse_checkout(repo, tag, directory, checkout)
    if not (grammar_root / "src" / "parser.c").exists():
        if output.exists():
            shutil.rmtree(output)
        raise SkipEntry("%s: missing src/parser.c" % name)
    for candidate in query_candidates(entry, checkout, grammar_root, name, directory):
        if candidate.exists():
            query_src = candidate
            break
    if query_src is None:
        query_src = external_query(entry, temp_root)
    if query_src is None:
        if output.exists():
            shutil.rmtree(output)
        raise SkipEntry("%s: missing queries/highlights.scm" % name)

    licenses = license_files(checkout, grammar_root)
    for path in licenses:
        detected.append(detect_license(path.read_text(encoding="utf-8", errors="replace")))
    detected = sorted(set(detected))
    if not detected:
        detected = ["unknown"]
    if "unknown" in detected and not allow_unknown:
        raise RuntimeError("%s: unknown license; rerun with --allow-unknown-license to vendor anyway" % name)
    for license_id in detected:
        if license_id not in ALLOWED_LICENSES and license_id != "unknown":
            raise RuntimeError("%s: unsupported license %s" % (name, license_id))

    if output.exists():
        shutil.rmtree(output)
    (output / "queries").mkdir(parents=True)
    copy_tree(grammar_root / "src", output / "src")
    if source_references_repo_common(output / "src"):
        copy_repo_common(checkout, output)
        rewrite_repo_common_includes(output / "src")
    shutil.copy2(query_src, output / "queries" / "highlights.scm")

    if licenses:
        (output / "licenses").mkdir()
        for path in licenses:
            shutil.copy2(path, output / "licenses" / path.name)

    manifest = {
        "name": name,
        "repo": repo,
        "tag": tag,
        "directory": directory if directory != "-" else None,
        "licenses": detected,
    }
    write_yaml_mapping(output / "fyts-vendor.yaml", manifest)
    return manifest


def parse_args(argv):
    parser = argparse.ArgumentParser(description="Vendor generated Tree-sitter grammar sources.")
    parser.add_argument("--catalogue", default="catalogues/minimal.yaml")
    parser.add_argument("--output", default="vendor/grammars")
    parser.add_argument(
        "--catalogue-output",
        default=None,
        help="write pruned catalogue path (default: <output>/catalogue.yaml)",
    )
    parser.add_argument("--allow-unknown-license", action="store_true")
    parser.add_argument("--force", action="store_true", help="refetch even when a matching vendored checkout exists")
    parser.add_argument(
        "--language",
        action="append",
        default=[],
        help="only vendor this language name; may be passed multiple times or as a comma-separated list",
    )
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    catalogue = Path(args.catalogue)
    vendor_dir = Path(args.output)
    catalogue_output = Path(args.catalogue_output) if args.catalogue_output else vendor_dir / "catalogue.yaml"
    entries = parse_catalogue(catalogue)
    manifests = []
    vendored_entries = []
    selected_languages = set()
    selected = None

    for selected in args.language:
        selected_languages.update(name.strip() for name in selected.split(",") if name.strip())

    if not entries:
        print("no catalogue entries found: %s" % catalogue, file=sys.stderr)
        return 1
    if selected_languages:
        entries = [entry for entry in entries if entry["name"] in selected_languages]
        if not entries:
            print("no selected catalogue entries found", file=sys.stderr)
            return 1

    vendor_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="fyts-vendor-") as temp:
        temp_root = Path(temp)
        for entry in entries:
            print(
                "vendoring language: %s from %s"
                % (entry.get("display-name", entry["name"]), entry["repo"]),
                flush=True,
            )
            try:
                manifest = vendor_entry(entry, vendor_dir, temp_root, args.allow_unknown_license, args.force)
            except SkipEntry as error:
                print("vendor-grammars: %s" % error, file=sys.stderr)
                continue
            except (OSError, RuntimeError) as error:
                print("vendor-grammars: %s" % error, file=sys.stderr)
                return 1
            manifests.append(manifest)
            vendored_entries.append(entry)
            print("%s: %s" % (manifest["name"], ", ".join(manifest["licenses"])))

    if selected_languages:
        return 0

    if (vendor_dir / "manifest.json").exists():
        (vendor_dir / "manifest.json").unlink()
    write_yaml_sequence(vendor_dir / "manifest.yaml", manifests)
    write_catalogue(catalogue_output, vendored_entries)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
