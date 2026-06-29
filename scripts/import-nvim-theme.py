#!/usr/bin/env python3
#
# Convert selected Neovim theme palettes into fyts styling YAML.

import argparse
import re
import sys
from pathlib import Path


HEX_RE = re.compile(r'([A-Za-z_][A-Za-z0-9_]*)\s*=\s*["\'](#[0-9A-Fa-f]{6})["\']')


CAPTURES = [
    (r"^diff\.plus$", "diff-added"),
    (r"^diff\.minus$", "diff-removed"),
    (r"^string\.special\.path$", "path"),
    (r"^string\.special\.key$", "key"),
    (r"^escape$", "escape"),
    (r"^embedded$", "embedded"),
    (r"^attribute$", "attribute"),
    (r"^label$", "label"),
    (r"^property$", "property"),
    (r"^boolean$", "constant"),
    (r"^comment$", "comment"),
    (r"^string$", "string"),
    (r"^number$", "constant"),
    (r"^constant(\..*)?$", "constant"),
    (r"^keyword$", "keyword"),
    (r"^conditional$", "control"),
    (r"^repeat$", "control"),
    (r"^function(\..*)?$", "function"),
    (r"^method$", "function"),
    (r"^module$", "type"),
    (r"^type$", "type"),
    (r"^class$", "type"),
    (r"^constructor$", "type"),
    (r"^variable(\..*)?$", "variable"),
    (r"^parameter$", "variable"),
    (r"^operator$", "operator"),
    (r"^delimiter$", "punctuation"),
    (r"^punctuation(\..*)?$", "punctuation"),
    (r"^spell$", "text"),
]


CATPPUCCIN = {
    "diff-added": ("green", "teal"),
    "diff-removed": ("red", "maroon"),
    "path": ("yellow", "peach"),
    "key": ("sapphire", "sky", "blue"),
    "escape": ("peach", "yellow"),
    "embedded": ("subtext1", "text"),
    "attribute": ("sky", "sapphire"),
    "label": ("pink", "mauve"),
    "property": ("sky", "sapphire"),
    "constant": ("peach", "mauve"),
    "comment": ("overlay1", "overlay2"),
    "string": ("green", "teal"),
    "keyword": ("mauve", "pink"),
    "control": ("mauve", "pink"),
    "function": ("blue", "sapphire"),
    "type": ("yellow", "peach"),
    "variable": ("text", "subtext1"),
    "operator": ("sky", "subtext1"),
    "punctuation": ("subtext1", "text"),
    "text": ("text", "subtext1"),
}


KANAGAWA = {
    "diff-added": (("autumnGreen", "springGreen"), ("lotusGreen", "lotusGreen2")),
    "diff-removed": (("waveRed", "autumnRed"), ("lotusRed", "lotusRed2")),
    "path": (("carpYellow", "boatYellow2"), ("lotusYellow3", "lotusYellow2")),
    "key": (("springBlue", "crystalBlue"), ("lotusTeal2", "lotusBlue4")),
    "escape": (("surimiOrange", "roninYellow"), ("lotusOrange", "lotusOrange2")),
    "embedded": (("oldWhite", "fujiWhite"), ("lotusGray2", "lotusInk1")),
    "attribute": (("springBlue", "crystalBlue"), ("lotusTeal2", "lotusBlue4")),
    "label": (("oniViolet", "sakuraPink"), ("lotusViolet4", "lotusPink")),
    "property": (("springBlue", "crystalBlue"), ("lotusTeal2", "lotusBlue4")),
    "constant": (("surimiOrange", "sakuraPink"), ("lotusOrange", "lotusPink")),
    "comment": (("fujiGray", "katanaGray"), ("lotusGray3", "lotusGray2")),
    "string": (("springGreen", "waveAqua2"), ("lotusGreen", "lotusAqua")),
    "keyword": (("oniViolet", "crystalBlue"), ("lotusViolet4", "lotusBlue5")),
    "control": (("oniViolet", "sakuraPink"), ("lotusViolet4", "lotusPink")),
    "function": (("crystalBlue", "springBlue"), ("lotusBlue4", "lotusTeal2")),
    "type": (("carpYellow", "boatYellow2"), ("lotusYellow3", "lotusYellow2")),
    "variable": (("fujiWhite", "oldWhite"), ("lotusInk1", "lotusGray2")),
    "operator": (("springViolet2", "oldWhite"), ("lotusViolet2", "lotusInk2")),
    "punctuation": (("oldWhite", "fujiWhite"), ("lotusInk1", "lotusGray2")),
    "text": (("fujiWhite", "oldWhite"), ("lotusInk1", "lotusGray2")),
}


VSCODE = {
    "diff-added": ("vscGitAdded", "vscLightGreen", "vscGreen"),
    "diff-removed": ("vscGitDeleted", "vscRed"),
    "path": ("vscYellow", "vscYellowOrange"),
    "key": ("vscLightBlue", "vscAccentBlue"),
    "escape": ("vscYellowOrange", "vscOrange"),
    "embedded": ("vscFront", "vscGray"),
    "attribute": ("vscLightBlue", "vscAccentBlue"),
    "label": ("vscPink", "vscViolet"),
    "property": ("vscLightBlue", "vscAccentBlue"),
    "constant": ("vscLightGreen", "vscOrange"),
    "comment": ("vscGreen", "vscGray"),
    "string": ("vscOrange", "vscGreen"),
    "keyword": ("vscBlue", "vscPink"),
    "control": ("vscPink", "vscBlue"),
    "function": ("vscYellow", "vscAccentBlue"),
    "type": ("vscBlueGreen", "vscYellow"),
    "variable": ("vscLightBlue", "vscFront"),
    "operator": ("vscFront", "vscGray"),
    "punctuation": ("vscFront", "vscGray"),
    "text": ("vscFront", "vscGray"),
}


def read_palette(path):
    text = path.read_text(encoding="utf-8")

    return {name: value.lower() for name, value in HEX_RE.findall(text)}


def read_vscode_palette(path, dark):
    text = path.read_text(encoding="utf-8")
    marker = "if vim.o.background == 'dark' then"
    split = "else"

    if marker not in text:
        raise ValueError("vscode colors file does not contain a dark/light branch")

    text = text.split(marker, 1)[1]
    if split not in text:
        raise ValueError("vscode colors file does not contain a light branch")
    dark_text, light_text = text.split(split, 1)
    text = dark_text if dark else light_text
    return {name: value.lower() for name, value in HEX_RE.findall(text)}


def pick(palette, names):
    for name in names:
        if name in palette:
            return name
    raise ValueError("palette is missing all of: %s" % ", ".join(names))


def sgr(hex_color):
    value = hex_color.lstrip("#")
    red = int(value[0:2], 16)
    green = int(value[2:4], 16)
    blue = int(value[4:6], 16)

    return r"\e[38;2;%d;%d;%dm" % (red, green, blue)


def bg_sgr(hex_color):
    value = hex_color.lstrip("#")
    red = int(value[0:2], 16)
    green = int(value[2:4], 16)
    blue = int(value[4:6], 16)

    return r"\e[48;2;%d;%d;%dm" % (red, green, blue)


def yaml_escape_quote(text):
    return '"' + text.replace('"', '\\"') + '"'


def style_name(theme, variant, color):
    return "%s-%s-%s" % (theme, variant, color)


def emit(theme, source, dark, light, attributes):
    used = {}
    lines = []

    lines.append("# Generated by scripts/import-nvim-theme.py from %s" % source)
    lines.append("captures:")
    lines.append("  # Capture attributes are matched as POSIX extended regular expressions in order.")
    for capture, attribute in CAPTURES:
        lines.append("  - capture: %s" % capture)
        lines.append("    attribute: %s" % attribute)

    lines.append("")
    lines.append("attributes:")
    for attribute, choices in attributes.items():
        if theme == "kanagawa":
            dark_choices, light_choices = choices
        else:
            dark_choices = choices
            light_choices = choices

        dark_color = pick(dark["palette"], dark_choices)
        light_color = pick(light["palette"], light_choices)
        dark_style = style_name(theme, dark["variant"], dark_color)
        light_style = style_name(theme, light["variant"], light_color)

        used[dark_style] = dark["palette"][dark_color]
        used[light_style] = light["palette"][light_color]
        lines.append("  %s:" % attribute)
        lines.append("    dark: %s" % dark_style)
        lines.append("    light: %s" % light_style)

    lines.append("")
    lines.append("frame:")
    lines.append("  dark-background: frame-dark-background")
    lines.append("  light-background: frame-light-background")
    lines.append("  dark-conflicts: []")
    lines.append("  light-conflicts: []")
    lines.append("  dark-contrast: frame-dark-contrast")
    lines.append("  light-contrast: frame-light-contrast")
    used["frame-dark-background"] = "#000000"
    used["frame-light-background"] = "#ffffff"
    used["frame-dark-contrast"] = "#ffffff"
    used["frame-light-contrast"] = "#000000"

    lines.append("")
    lines.append("styles:")
    for name in sorted(used):
        escape = bg_sgr(used[name]) if name.endswith("-background") else sgr(used[name])
        lines.append("  %s: %s" % (name, yaml_escape_quote(escape)))
    lines.append("")
    return "\n".join(lines)


def load_catppuccin(repo):
    root = repo / "lua" / "catppuccin" / "palettes"

    return emit(
        "catppuccin",
        "catppuccin/nvim",
        {"variant": "mocha", "palette": read_palette(root / "mocha.lua")},
        {"variant": "latte", "palette": read_palette(root / "latte.lua")},
        CATPPUCCIN,
    )


def load_kanagawa(repo):
    palette = read_palette(repo / "lua" / "kanagawa" / "colors.lua")

    return emit(
        "kanagawa",
        "rebelot/kanagawa.nvim",
        {"variant": "wave", "palette": palette},
        {"variant": "lotus", "palette": palette},
        KANAGAWA,
    )


def load_vscode(repo):
    path = repo / "lua" / "vscode" / "colors.lua"

    return emit(
        "vscode",
        "Mofiqul/vscode.nvim",
        {"variant": "dark", "palette": read_vscode_palette(path, True)},
        {"variant": "light", "palette": read_vscode_palette(path, False)},
        VSCODE,
    )


def parse_args(argv):
    parser = argparse.ArgumentParser(
        description="Import selected Neovim color scheme palettes as fyts styling YAML."
    )
    parser.add_argument(
        "--theme",
        choices=("catppuccin", "kanagawa", "vscode"),
        required=True,
        help="theme profile to import",
    )
    parser.add_argument("--repo", required=True, help="path to the local theme checkout")
    parser.add_argument("--output", default="-", help="output path, or '-' for stdout")
    return parser.parse_args(argv)


def main(argv):
    args = parse_args(argv)
    repo = Path(args.repo)
    output = None
    text = None

    if not repo.exists():
        print("theme checkout does not exist: %s" % repo, file=sys.stderr)
        return 1

    try:
        if args.theme == "catppuccin":
            text = load_catppuccin(repo)
        elif args.theme == "kanagawa":
            text = load_kanagawa(repo)
        else:
            text = load_vscode(repo)
    except (OSError, ValueError) as error:
        print("import-nvim-theme: %s" % error, file=sys.stderr)
        return 1

    if args.output == "-":
        sys.stdout.write(text)
        return 0

    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(text, encoding="utf-8")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
