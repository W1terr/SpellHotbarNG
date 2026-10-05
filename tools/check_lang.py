"""
Checks the translations in res/lang against the texts the code translates.

The English texts in src/ui and core/Hotkeys.cpp are the keys (T("..."), F("..."), Id("..."), Help("..."), ColorOption, combo lists).
Reports keys missing from a language file, keys the code no longer uses and broken {} placeholders.
Usage: python check_lang.py
"""

import json
import re
import sys
from pathlib import Path

ROOT = Path(__file__).parent.parent
SOURCES = [ROOT / "src" / "ui" / name for name in ("Menu.cpp", "Hud.cpp", "UI.cpp")] + [ROOT / "src" / "core" / "Hotkeys.cpp"]
LANG_DIR = ROOT / "res" / "lang"

LITERAL = r'"(?:[^"\\]|\\.)*"'
LITERALS = rf"{LITERAL}(?:\s*{LITERAL})*"  # adjacent literals are one string


def joined(literals: str) -> str:
    return "".join(json.loads(lit) for lit in re.findall(LITERAL, literals))


def code_keys() -> list[str]:
    keys = []

    def add(key):
        if key not in keys:
            keys.append(key)

    for path in SOURCES:
        src = re.sub(r"//[^\n]*", "", path.read_text(encoding="utf-8"))
        for m in re.finditer(rf"\b(?:T|F|Id|Help)\(\s*({LITERALS})", src):
            add(joined(m.group(1)))
        for m in re.finditer(rf"ColorOption\(\s*({LITERALS})\s*,\s*[\w.]+\s*(?:,\s*({LITERALS}))?", src):
            add(joined(m.group(1)))
            if m.group(2):
                add(joined(m.group(2)))
        for m in re.finditer(rf"SidebarPage\{{\s*({LITERAL})", src):  # UI.cpp: the sidebar page names
            add(json.loads(m.group(1)))
        for m in re.finditer(r"k(?:Anchor|Visibility|ExtraBarMode)Names\[\]\s*=\s*\{([^}]*)\}", src):
            for lit in re.findall(LITERAL, m.group(1)):
                add(json.loads(lit))
        for m in re.finditer(r"T\(alch->[^;]*;", src):
            for lit in re.findall(LITERAL, m.group(0)):
                add(json.loads(lit))
        for m in re.finditer(rf"kGlyphHint\s*=\s*({LITERALS})", src):
            add(joined(m.group(1)))
    return keys


def main():
    keys = code_keys()
    problems = 0
    for file in sorted(LANG_DIR.glob("*.json")):
        table = json.loads(file.read_text(encoding="utf-8"))
        for key in keys:
            if key not in table:
                print(f"{file.name}: missing: {key!r}")
                problems += 1
        for key, text in table.items():
            if key not in keys:
                print(f"{file.name}: not used by the code: {key!r}")
                problems += 1
            elif text.count("{}") != key.count("{}") or re.search(r"[{}]", text.replace("{}", "")):
                print(f"{file.name}: placeholders differ from the English text: {key!r}")
                problems += 1
            elif key in ("Bindings", "Bar Layout", "Profiles") and "/" in text:
                print(f"{file.name}: page names can't contain '/': {text!r}")
                problems += 1
    print(f"{len(keys)} texts, {problems} problem(s)")
    sys.exit(1 if problems else 0)


if __name__ == "__main__":
    main()
