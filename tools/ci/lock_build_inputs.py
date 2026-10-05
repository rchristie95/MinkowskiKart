"""Maintainer helper: capture reviewed upstream inputs, never run in CI."""
import hashlib
import json
from pathlib import Path
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[2]

def read_json(url):
    with urlopen(Request(url, headers={"User-Agent": "MinkowskiKart-input-lock"})) as response:
        return json.load(response)

def digest_url(url):
    digest = hashlib.sha256()
    with urlopen(Request(url, headers={"User-Agent": "MinkowskiKart-input-lock"}), timeout=60) as response:
        while chunk := response.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()

def main():
    inputs = {}
    releases = [
        ("supertuxkart/dependencies", "preview", ["dependencies-android-src.tar.xz", "ispc-cross-1.26.0.tar.xz", "mxe_static_mingw.zip", "dependencies-iphoneos.tar.xz", "dependencies-macosx.tar.xz", "dependencies-win-i686.zip", "dependencies-win-x86_64.zip", "dependencies-win-armv7.zip", "dependencies-win-aarch64.zip"]),
        ("supertuxkart/dependencies", "cctools", ["cctools-14.1.tar.xz"]),
        ("supertuxkart/stk-assets-mobile", "git", ["stk-assets-full.zip"]),
        ("mstorsjo/llvm-mingw", "20210423", ["llvm-mingw-20210423-msvcrt-ubuntu-18.04-x86_64.tar.xz"]),
        ("rainers/cv2pdb", "v0.50", ["cv2pdb-0.50.zip"]),
        ("pal1000/mesa-dist-win", "26.2.4", ["mesa3d-26.2.4-release-mingw.7z"]),
    ]
    for repo, tag, names in releases:
        release = read_json(f"https://api.github.com/repos/{repo}/releases/tags/{tag}")
        for name in names:
            asset = next(item for item in release["assets"] if item["name"] == name)
            url = asset["browser_download_url"]
            digest = asset.get("digest")
            if not digest or not digest.startswith("sha256:"):
                print(f"Hashing {name}", flush=True)
                digest = "sha256:" + digest_url(url)
            inputs[name] = {"url": url, "sha256": digest.removeprefix("sha256:"), "size": asset["size"]}
    for repo, revision, name in [
        ("MestreLion/git-tools", read_json("https://api.github.com/repos/MestreLion/git-tools/commits/main")["sha"], "git-tools.zip"),
        ("bylaws/liblinkernsbypass", "aa3975893d83ef1bc84c321ec60c65fbf1287887", "linkernsbypass.tar.gz"),
    ]:
        url = f"https://codeload.github.com/{repo}/" + (f"zip/{revision}" if name.endswith(".zip") else f"tar.gz/{revision}")
        print(f"Hashing {name} at {revision}", flush=True)
        inputs[name] = {"url": url, "sha256": digest_url(url), "revision": revision}
    (ROOT / ".github/build-inputs.json").write_text(json.dumps(inputs, indent=2) + "\n", encoding="utf-8")
    print("Wrote .github/build-inputs.json; review changes before committing.", flush=True)

if __name__ == "__main__":
    main()
