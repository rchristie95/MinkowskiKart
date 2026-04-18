import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent
BUILD = ROOT / "build"


def ninja_escape(path_text: str) -> str:
    if len(path_text) >= 3 and path_text[1] == ":" and path_text[2] == "/":
        return f"{path_text[0]}$:{path_text[2:]}"
    return path_text


def normalize_text(text: str) -> str:
    root_posix = ROOT.as_posix()
    build_posix = BUILD.as_posix()
    root_win = str(ROOT)
    build_win = str(BUILD)

    legacy_root_posix = root_posix.replace("MinkowskiKartGithub", "MinkowskiKartMain")
    legacy_build_posix = build_posix.replace("MinkowskiKartGithub", "MinkowskiKartMain")
    legacy_root_win = root_win.replace("MinkowskiKartGithub", "MinkowskiKartMain")
    legacy_build_win = build_win.replace("MinkowskiKartGithub", "MinkowskiKartMain")

    replacements = [
        (ninja_escape(legacy_build_posix), "."),
        (ninja_escape(build_posix), "."),
        (legacy_build_win, "."),
        (build_win, "."),
        (legacy_build_posix, "."),
        (build_posix, "."),
        (ninja_escape(legacy_root_posix), ".."),
        (ninja_escape(root_posix), ".."),
        (legacy_root_win, ".."),
        (root_win, ".."),
        (legacy_root_posix, ".."),
        (root_posix, ".."),
    ]

    for needle, replacement in replacements:
        text = text.replace(needle, replacement)

    text = re.sub(
        r'-DSUPERTUXKART_DATADIR="\\"[^"]+\\""',
        r'-DSUPERTUXKART_DATADIR="\\".\\""',
        text,
    )
    text = re.sub(r"^CMAKE_INSTALL_PREFIX:PATH=.*$", "CMAKE_INSTALL_PREFIX:PATH=.", text, flags=re.MULTILINE)
    return text


def rewrite_text_file(path: Path) -> bool:
    original = path.read_text(encoding="utf-8", errors="replace")
    updated = normalize_text(original)
    if path.name == "icon.rc":
        updated = '100 ICON "../tools/windows_installer/icon.ico"\n'
    if updated == original:
        return False
    path.write_text(updated, encoding="utf-8", newline="\n")
    return True


def main() -> None:
    print(f"Project root: {ROOT}")
    files = [
        BUILD / "build.ninja",
        BUILD / "CMakeFiles" / "rules.ninja",
        BUILD / "CMakeCache.txt",
        BUILD / "tmp" / "icon.rc",
    ]

    changed = []
    for path in files:
        if not path.exists():
            continue
        if rewrite_text_file(path):
            changed.append(path.relative_to(ROOT).as_posix())

    if changed:
        for rel_path in changed:
            print(f"rewrote {rel_path}")
    else:
        print("no changes")


if __name__ == "__main__":
    main()
