"""Download a locked build input; verify SHA-256 before it can be extracted."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import tempfile
from urllib.request import Request, urlopen

ROOT = Path(__file__).resolve().parents[2]

def download(entry, destination):
    destination = Path(destination)
    expected_size = entry.get("size")
    limit = expected_size if expected_size is not None else 32 * 1024 * 1024
    digest = hashlib.sha256()
    received = 0
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=destination.parent, delete=False) as output:
            temporary = Path(output.name)
            request = Request(entry["url"], headers={"User-Agent": "MinkowskiKart-verified-build"})
            with urlopen(request, timeout=60) as response:
                if not response.url.startswith("https://"):
                    raise ValueError("Build input redirected to an insecure URL")
                while chunk := response.read(1024 * 1024):
                    received += len(chunk)
                    if received > limit:
                        raise ValueError("Build input exceeds its locked size")
                    digest.update(chunk)
                    output.write(chunk)
            if expected_size is not None and received != expected_size:
                raise ValueError("Build input does not match its locked size")
            if digest.hexdigest() != entry["sha256"]:
                raise ValueError("Build input SHA-256 mismatch")
        os.replace(temporary, destination)
        temporary = None
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("key", help="Input name in .github/build-inputs.json")
    parser.add_argument("destination", nargs="?", help="Output path (defaults to input name)")
    args = parser.parse_args()
    manifest = json.loads((ROOT / ".github/build-inputs.json").read_text(encoding="utf-8"))
    download(manifest[args.key], args.destination or args.key)
    print(f"Verified {args.key}")

if __name__ == "__main__":
    main()
