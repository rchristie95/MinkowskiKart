from pathlib import Path


ROOT = Path(__file__).resolve().parent
BUILD_DIR = ROOT / "build-dev"

RENAMES = {
    "src/items/bowling.cpp": "src/items/black_hole.cpp",
    "src/items/bowling.hpp": "src/items/black_hole.hpp",
    "src/items/plunger.cpp": "src/items/photon.cpp",
    "src/items/plunger.hpp": "src/items/photon.hpp",
    "src/items/rubber_ball.cpp": "src/items/wormhole.cpp",
    "src/items/rubber_ball.hpp": "src/items/wormhole.hpp",
}


def rewrite(path: Path) -> bool:
    if not path.exists():
        return False

    original = path.read_text(encoding="utf-8", errors="replace")
    updated = original

    for old, new in RENAMES.items():
        old_obj = old + ".obj"
        new_obj = new + ".obj"
        updated = updated.replace(old_obj, new_obj)
        updated = updated.replace(old.replace("/", "\\"), new.replace("/", "\\"))
        updated = updated.replace(old, new)

    if updated == original:
        return False

    path.write_text(updated, encoding="utf-8", newline="\n")
    return True


def main() -> None:
    files = [
        BUILD_DIR / "build.ninja",
        BUILD_DIR / "CMakeFiles" / "rules.ninja",
    ]
    changed = [path for path in files if rewrite(path)]
    if changed:
        for path in changed:
            print(f"refreshed {path.relative_to(ROOT)}")
    else:
        print("development build names already current")


if __name__ == "__main__":
    main()
