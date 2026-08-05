#!/usr/bin/env python3
"""Validate the static publication surface without third-party packages."""

from __future__ import annotations

import argparse
from html.parser import HTMLParser
import json
from pathlib import Path
import struct
import sys
from urllib.parse import unquote, urlparse
from urllib.request import Request, urlopen
import xml.etree.ElementTree as ET


CANONICAL_ROOT = "https://rchristie95.github.io/MinkowskiKart/"
PUBLIC_PAGES = {
    "index.html": CANONICAL_ROOT,
    "download.html": CANONICAL_ROOT + "download.html",
    "physics.html": CANONICAL_ROOT + "physics.html",
    "about.html": CANONICAL_ROOT + "about.html",
    "faq.html": CANONICAL_ROOT + "faq.html",
    "404.html": CANONICAL_ROOT + "404.html",
}
REQUIRED_META = {
    "description",
    "twitter:card",
    "twitter:title",
    "twitter:description",
    "twitter:image",
}
REQUIRED_OG = {"og:title", "og:description", "og:url", "og:image", "og:image:alt"}
EXPECTED_HTTP_404 = {PUBLIC_PAGES["404.html"]}


class DocumentParser(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.title_parts: list[str] = []
        self.in_title = False
        self.meta: dict[str, str] = {}
        self.links: list[tuple[str, str]] = []
        self.images: list[dict[str, str]] = []
        self.ids: set[str] = set()
        self.h1_count = 0
        self.canonicals: list[str] = []
        self.json_ld: list[str] = []
        self.in_json_ld = False
        self.json_parts: list[str] = []

    def handle_starttag(self, tag: str, attrs: list[tuple[str, str | None]]) -> None:
        values = {key: value or "" for key, value in attrs}
        if "id" in values:
            self.ids.add(values["id"])
        if tag == "title":
            self.in_title = True
        elif tag == "h1":
            self.h1_count += 1
        elif tag == "meta":
            key = values.get("name") or values.get("property")
            if key:
                self.meta[key] = values.get("content", "").strip()
        elif tag == "link":
            href = values.get("href", "")
            self.links.append(("href", href))
            if values.get("rel") == "canonical":
                self.canonicals.append(href)
        elif tag == "a":
            self.links.append(("href", values.get("href", "")))
        elif tag == "img":
            self.images.append(values)
            self.links.append(("src", values.get("src", "")))
        elif tag == "script" and values.get("type") == "application/ld+json":
            self.in_json_ld = True
            self.json_parts = []

    def handle_endtag(self, tag: str) -> None:
        if tag == "title":
            self.in_title = False
        elif tag == "script" and self.in_json_ld:
            self.in_json_ld = False
            self.json_ld.append("".join(self.json_parts).strip())

    def handle_data(self, data: str) -> None:
        if self.in_title:
            self.title_parts.append(data)
        if self.in_json_ld:
            self.json_parts.append(data)

    @property
    def title(self) -> str:
        return "".join(self.title_parts).strip()


def local_target(site: Path, page: Path, raw_url: str) -> tuple[Path, str] | None:
    if not raw_url or raw_url.startswith(("mailto:", "tel:", "data:")):
        return None
    parsed = urlparse(raw_url)
    if parsed.scheme or parsed.netloc:
        return None
    url_path = unquote(parsed.path)
    if not url_path:
        return page, parsed.fragment
    if url_path.startswith("/MinkowskiKart/"):
        target = site / url_path.removeprefix("/MinkowskiKart/")
    elif url_path.startswith("/"):
        return None
    else:
        target = page.parent / url_path
    if url_path.endswith("/"):
        target /= "index.html"
    return target.resolve(), parsed.fragment


def check_png_dimensions(path: Path, expected: tuple[int, int]) -> str | None:
    with path.open("rb") as handle:
        header = handle.read(24)
    if header[:8] != b"\x89PNG\r\n\x1a\n":
        return f"{path}: not a PNG"
    width, height = struct.unpack(">II", header[16:24])
    if (width, height) != expected:
        return f"{path}: expected {expected[0]}x{expected[1]}, got {width}x{height}"
    return None


def check_external(url: str) -> str | None:
    request = Request(url, method="HEAD", headers={"User-Agent": "MinkowskiKart-publication-check/1.0"})
    try:
        with urlopen(request, timeout=20) as response:
            if response.status >= 400:
                return f"{url}: HTTP {response.status}"
    except Exception as head_error:
        try:
            request = Request(url, headers={"User-Agent": "MinkowskiKart-publication-check/1.0", "Range": "bytes=0-0"})
            with urlopen(request, timeout=30) as response:
                if response.status >= 400:
                    return f"{url}: HTTP {response.status}"
        except Exception as get_error:
            return f"{url}: {get_error} (HEAD also failed: {head_error})"
    return None


def validate(site: Path, external: bool) -> list[str]:
    errors: list[str] = []
    documents: dict[Path, DocumentParser] = {}
    titles: dict[str, str] = {}
    external_urls: set[str] = set()

    for name, expected_canonical in PUBLIC_PAGES.items():
        page = site / name
        if not page.is_file():
            errors.append(f"missing public page: {page}")
            continue
        parser = DocumentParser()
        parser.feed(page.read_text(encoding="utf-8"))
        documents[page.resolve()] = parser

        if not parser.title:
            errors.append(f"{name}: missing title")
        elif parser.title in titles:
            errors.append(f"{name}: duplicate title also used by {titles[parser.title]}")
        else:
            titles[parser.title] = name
        if parser.h1_count != 1:
            errors.append(f"{name}: expected exactly one h1, found {parser.h1_count}")
        if parser.canonicals != [expected_canonical]:
            errors.append(f"{name}: canonical should be exactly {expected_canonical!r}")

        missing_meta = sorted(key for key in REQUIRED_META if not parser.meta.get(key))
        missing_og = sorted(key for key in REQUIRED_OG if not parser.meta.get(key))
        if missing_meta:
            errors.append(f"{name}: missing meta fields: {', '.join(missing_meta)}")
        if missing_og:
            errors.append(f"{name}: missing Open Graph fields: {', '.join(missing_og)}")
        for image in parser.images:
            if not image.get("alt", "").strip():
                errors.append(f"{name}: image {image.get('src', '<unknown>')} has empty alt text")
        for raw_json in parser.json_ld:
            try:
                json.loads(raw_json)
            except json.JSONDecodeError as error:
                errors.append(f"{name}: invalid JSON-LD: {error}")

    # A linked HTML document can be parsed lazily while this loop runs.
    # Iterate over a snapshot so adding that parser cannot resize the mapping.
    for page, parser in list(documents.items()):
        for attribute, raw_url in parser.links:
            if not raw_url:
                errors.append(f"{page.name}: empty {attribute}")
                continue
            parsed = urlparse(raw_url)
            if parsed.scheme in {"http", "https"}:
                external_url = raw_url.split("#", 1)[0]
                if external_url not in EXPECTED_HTTP_404:
                    external_urls.add(external_url)
                continue
            target_info = local_target(site, page, raw_url)
            if target_info is None:
                continue
            target, fragment = target_info
            if not target.exists():
                errors.append(f"{page.name}: broken local link {raw_url}")
                continue
            if fragment and target.suffix.lower() == ".html":
                target_parser = documents.get(target.resolve())
                if target_parser is None:
                    target_parser = DocumentParser()
                    target_parser.feed(target.read_text(encoding="utf-8"))
                    documents[target.resolve()] = target_parser
                if fragment not in target_parser.ids:
                    errors.append(f"{page.name}: missing anchor #{fragment} in {target.name}")

    try:
        manifest = json.loads((site / "manifest.webmanifest").read_text(encoding="utf-8"))
        if manifest.get("name") != "Minkowski Kart":
            errors.append("manifest.webmanifest: unexpected name")
    except (OSError, json.JSONDecodeError) as error:
        errors.append(f"manifest.webmanifest: {error}")

    try:
        root = ET.parse(site / "sitemap.xml").getroot()
        namespace = {"sm": "http://www.sitemaps.org/schemas/sitemap/0.9"}
        locations = {element.text for element in root.findall("sm:url/sm:loc", namespace)}
        expected = {value for key, value in PUBLIC_PAGES.items() if key != "404.html"}
        if locations != expected:
            errors.append(f"sitemap.xml: URLs differ; missing={sorted(expected-locations)}, extra={sorted(locations-expected)}")
    except (OSError, ET.ParseError) as error:
        errors.append(f"sitemap.xml: {error}")

    robots = (site / "robots.txt").read_text(encoding="utf-8")
    if CANONICAL_ROOT + "sitemap.xml" not in robots:
        errors.append("robots.txt: canonical sitemap URL missing")

    social = site / "assets" / "minkowski-kart-social-preview.png"
    if not social.is_file():
        errors.append(f"missing social preview: {social}")
    else:
        dimension_error = check_png_dimensions(social, (1280, 640))
        if dimension_error:
            errors.append(dimension_error)

    if external:
        for url in sorted(external_urls):
            error = check_external(url)
            if error:
                errors.append(error)

    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--site", type=Path, default=Path(__file__).resolve().parents[1] / "docs")
    parser.add_argument("--external", action="store_true", help="also check HTTP(S) links")
    args = parser.parse_args()
    errors = validate(args.site.resolve(), args.external)
    if errors:
        print("Publication validation failed:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("Publication validation passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
