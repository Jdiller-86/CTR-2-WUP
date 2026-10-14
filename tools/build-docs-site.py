#!/usr/bin/env python3
"""Stage maintained Markdown, build strict Material docs and verify local links.

Install docs-requirements.txt, then run this script. Generated source/site/report
stay under ignored artifacts; repository Markdown is never rewritten.
"""
from __future__ import annotations

import argparse
from html.parser import HTMLParser
import hashlib
import importlib.metadata
import json
import os
from pathlib import Path, PurePosixPath
import re
import shutil
import subprocess
import sys
from urllib.parse import quote, unquote, urlsplit, urlunsplit

import yaml

ROOT = Path(__file__).resolve().parents[1]
ROOT_PAGES = {
    "README.md": "index.md", "STATUS.md": "status.md",
    "CONTRIBUTING.md": "contributing.md", "SECURITY.md": "security.md",
    "CHANGELOG.md": "changelog.md", "AGENTS.md": "agent-instructions.md",
    "LICENSE": "license.md", "THIRD_PARTY_NOTICES": "third-party-notices.md",
}


class RenderedPage(HTMLParser):
    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.ids: set[str] = set()
        self.links: list[str] = []
        self.text: list[str] = []
        self.config_text: list[str] = []
        self.in_config = False

    def handle_starttag(self, tag: str, attributes: list[tuple[str, str | None]]) -> None:
        attrs = dict(attributes)
        if tag == "script" and attrs.get("id") == "__config":
            self.in_config = True
        if attrs.get("id"):
            self.ids.add(attrs["id"])
        for name in ("href", "src", "poster"):
            if attrs.get(name):
                self.links.append(attrs[name])
        if attrs.get("srcset"):
            self.links.extend(item.strip().split()[0] for item in attrs["srcset"].split(",") if item.strip())

    def handle_data(self, text: str) -> None:
        self.text.append(text)
        if self.in_config:
            self.config_text.append(text)

    def handle_endtag(self, tag: str) -> None:
        if tag == "script":
            self.in_config = False


def source_url(repository: str, commit: str, relative: str, directory: bool = False) -> str:
    kind = "tree" if directory else "blob"
    return f"{repository}/{kind}/{commit}/{quote(relative, safe='/')}"


def rewrite_markdown(text: str, source: Path, destination: Path, mapping: dict[str, str],
                     root: Path, repository: str, commit: str) -> str:
    # Keep an ordinary CI link in the offline-capable staged documentation;
    # reading the guide must not depend on a network badge image.
    badge = re.compile(r"\[!\[([^]]+)\]\(" + re.escape(repository) + r"/actions/workflows/[^)]+/badge\.svg\)\]\(([^)]+)\)")
    text = badge.sub(lambda match: "[" + match.group(1) + " results](" + match.group(2) + ")", text)
    # The live chart is an ordinary link in the offline
    # documentation build. Never embed invented/static Project counts here.
    chart = '[![Project progress](https://jdiller-86.github.io/CTR-2-WUP/progress/gates.svg)](https://jdiller-86.github.io/CTR-2-WUP/progress/)'
    text = text.replace(chart, '[Live Project progress](https://github.com/users/Jdiller-86/projects/2)')
    def rewrite(target: str) -> str:
        wrapped = target.startswith("<") and target.endswith(">")
        parsed = urlsplit(target[1:-1] if wrapped else target)
        if parsed.scheme or parsed.netloc or not parsed.path:
            return target
        raw = unquote(parsed.path)
        original = (root / raw.lstrip("/") if raw.startswith("/") else source.parent / raw).resolve()
        if not original.is_relative_to(root):
            raise ValueError(f"Documentation link escapes the repository: {source.relative_to(root)}")
        relative = original.relative_to(root).as_posix()
        mapped = mapping.get(relative)
        if mapped is None and original.is_dir():
            mapped = mapping.get(relative + "/README.md") or mapping.get(relative + "/index.md")
        if mapped is not None:
            result = os.path.relpath(Path(mapped), destination.parent).replace(os.sep, "/")
            result = urlunsplit(("", "", result, parsed.query, parsed.fragment))
        elif original.exists():
            result = source_url(repository, commit, relative, original.is_dir())
            result = urlunsplit((*urlsplit(result)[:3], parsed.query, parsed.fragment))
        else:
            raise ValueError(f"Unresolved maintained-source link in {source.relative_to(root)}: {parsed.path}")
        return "<" + result + ">" if wrapped else result

    result: list[str] = []
    fence: str | None = None
    inline = re.compile(r"(\]\()(<[^>]+>|[^\s)]+)")
    reference = re.compile(r"^(\s{0,3}\[[^]]+\]:\s*)(<[^>]+>|[^\s]+)")
    html = re.compile(r"((?:href|src)=['\"])([^'\"]+)(['\"])")
    for line in text.splitlines(keepends=True):
        marker = re.match(r"^\s{0,3}(`{3,}|~{3,})", line)
        if marker:
            if fence is None:
                fence = marker.group(1)
            elif marker.group(1)[0] == fence[0] and len(marker.group(1)) >= len(fence):
                fence = None
            result.append(line)
            continue
        if fence is None:
            line = inline.sub(lambda match: match.group(1) + rewrite(match.group(2)), line)
            line = reference.sub(lambda match: match.group(1) + rewrite(match.group(2)), line)
            line = html.sub(lambda match: match.group(1) + rewrite(match.group(2)) + match.group(3), line)
        result.append(line)
    provenance = source_url(repository, commit, source.relative_to(root).as_posix())
    return "".join(result) + f"\n\n---\n\nMaintained source: [{source.relative_to(root).as_posix()}]({provenance}) · `{commit[:12]}`.\nGitHub source and issue links may require repository access.\n"


def stage_engine_notices(source: Path) -> int:
    assets = source / "site-licenses"
    assets.mkdir()
    index = ["# Site engine licenses\n", "The site uses the pinned public MkDocs and Material packages. These notices\ncover generated theme assets; [project/runtime notices](third-party-notices.md)\nretain their separate scope.\n"]
    selected = []
    for name in ("mkdocs", "mkdocs-material"):
        distribution = importlib.metadata.distribution(name)
        matches = [path for path in distribution.files or [] if str(path).endswith(".dist-info/licenses/LICENSE")]
        if len(matches) != 1:
            raise ValueError("Installed documentation engine license is missing")
        selected.append((name, distribution.version, distribution.locate_file(matches[0])))
    material = importlib.metadata.distribution("mkdocs-material")
    for name, relative in (("Material icons", "material/templates/.icons/material/LICENSE"),
                           ("Font Awesome", "material/templates/.icons/fontawesome/LICENSE.txt"),
                           ("Octicons", "material/templates/.icons/octicons/LICENSE")):
        selected.append((name, "bundled with Material " + material.version, material.locate_file(relative)))
    for name, version, original in selected:
        if not original.is_file() or original.is_symlink() or original.stat().st_size > 256 * 1024:
            raise ValueError("Documentation asset notice must be a bounded regular file")
        destination = re.sub(r"[^a-z0-9]+", "-", name.lower()).strip("-") + ".txt"
        shutil.copyfile(original, assets / destination)
        index.append(f"- [{name} {version} notice](site-licenses/{destination})\n")
    (source / "site-licenses.md").write_text("\n".join(index), encoding="utf-8")
    return len(selected)


def verify_site(site: Path, site_url: str) -> dict:
    pages: dict[Path, RenderedPage] = {}
    for path in sorted(site.rglob("*.html")):
        parser = RenderedPage()
        parser.feed(path.read_text(encoding="utf-8"))
        pages[path.resolve()] = parser
    base = urlsplit(site_url)
    prefix = base.path.rstrip("/") + "/"
    checked = external = 0

    def verify(source: Path, target: str) -> None:
        nonlocal checked, external
        parsed = urlsplit(target)
        if target == 'https://jdiller-86.github.io/CTR-2-WUP/progress/':
            # A separate Project workflow publishes this live endpoint. It is
            # HTTP-verified during hosting, outside the static docs artifact.
            external += 1
            return
        if parsed.scheme or parsed.netloc:
            if (parsed.scheme, parsed.netloc) != (base.scheme, base.netloc):
                external += 1
                return
        raw = unquote(parsed.path)
        if raw.startswith("/"):
            if prefix != "/" and not raw.startswith(prefix) and raw != prefix.rstrip("/"):
                raise ValueError(f"Rendered link escapes the Pages base path: {target}")
            path = site / raw.removeprefix(prefix).lstrip("/")
        else:
            path = source.parent / raw if raw else source
        path = path.resolve()
        if not path.is_relative_to(site):
            raise ValueError(f"Rendered link escapes the site: {target}")
        if path.is_dir():
            path /= "index.html"
        if not path.is_file():
            raise ValueError(f"Broken rendered link in {source.relative_to(site)}: {target}")
        if parsed.fragment and path.suffix == ".html" and unquote(parsed.fragment) not in pages[path].ids:
            raise ValueError(f"Broken rendered anchor in {source.relative_to(site)}: {target}")
        checked += 1

    for source, parser in pages.items():
        for target in parser.links:
            verify(source, target)
        if parser.config_text:
            configuration = json.loads("".join(parser.config_text))
            if configuration.get("search"):
                verify(source, configuration["search"])
    css_links = re.compile(r"url\(\s*['\"]?([^)'\"]+)['\"]?\s*\)")
    for path in sorted(site.rglob("*.css")):
        for target in css_links.findall(path.read_text(encoding="utf-8")):
            verify(path.resolve(), target.strip())
    index_path = site / "search/search_index.json"
    search = json.loads(index_path.read_text(encoding="utf-8"))
    if not search.get("docs"):
        raise ValueError("Search index is empty")
    for row in search["docs"]:
        verify(site / "index.html", row["location"])
    project_url = "https://github.com/users/Jdiller-86/projects/2"
    if project_url not in pages[(site / "index.html").resolve()].links:
        raise ValueError("The overview must link the authoritative live Project")
    return {"html_pages": len(pages), "checked_internal_links_and_assets": checked,
            "external_links_not_network_checked": external, "search_entries": len(search["docs"]),
            "authoritative_project_link_present": True, "all_rendered_internal_links_resolve": True}


def build(options: argparse.Namespace) -> dict:
    root = options.source_root.resolve()
    output = (options.output or root / "artifacts/docs-site").resolve()
    if not output.is_relative_to((root / "artifacts").resolve()) or output == (root / "artifacts").resolve():
        raise ValueError("Generated documentation must stay beneath ignored artifacts/")
    marker = output / ".ctr2wup-docs"
    if output.exists() and any(output.iterdir()) and not marker.is_file():
        raise ValueError("Refusing to replace an unrelated output directory")
    output.mkdir(parents=True, exist_ok=True)
    marker.write_text("CTR2WUP generated documentation\n", encoding="utf-8")
    if (output / "source").is_symlink() or (output / "site").is_symlink():
        raise ValueError("Generated source/site directories must not be symbolic links")
    dependencies = {}
    for line in (root / "docs-requirements.txt").read_text(encoding="utf-8").splitlines():
        if not line.strip() or line.startswith("#"):
            continue
        name, expected = line.split("==", 1)
        actual = importlib.metadata.version(name)
        if actual != expected:
            raise ValueError(f"Documentation dependency does not match its tested pin: {name}")
        dependencies[name] = actual
    commit = subprocess.check_output(["git", "-C", str(root), "rev-parse", "HEAD"], text=True).strip()
    if not re.fullmatch(r"[0-9a-f]{40}", commit):
        raise ValueError("Expected an exact source commit")
    configuration = yaml.safe_load((root / "mkdocs.yml").read_text(encoding="utf-8"))
    repository = configuration["repo_url"].rstrip("/")
    site_url = options.site_url or configuration["site_url"]
    url = urlsplit(site_url)
    if url.scheme != "https" or not url.netloc or url.query or url.fragment:
        raise ValueError("Documentation site URL must be an absolute HTTPS base URL")
    site_url = site_url.rstrip("/") + "/"
    source = output / "source"
    if source.exists():
        shutil.rmtree(source)
    source.mkdir()
    files = subprocess.check_output(["git", "-C", str(root), "ls-files", "-z", "docs"], text=True).split("\0")
    # The private development ledger contains issue/PR relationships. Publish
    # the policy page and safe aggregate dashboard, not this management input.
    files = [name for name in files if name and name not in {"docs/features.json", "docs/project-reviews.json"}]
    if (root / "docs/stylesheets/extra.css").is_file() and "docs/stylesheets/extra.css" not in files:
        files.append("docs/stylesheets/extra.css")
    mapping = dict(ROOT_PAGES)
    for relative in files:
        mapping[relative] = "docs/index.md" if relative == "docs/README.md" else relative
    hashes = []
    for relative, staged in sorted(mapping.items()):
        path = root / relative
        if path.is_symlink() or not path.is_file() or path.stat().st_size > 8 * 1024 * 1024:
            raise ValueError("Documentation sources must be bounded regular files")
        if any(part.lower() in {"private", "artifacts", ".git", ".env"} for part in PurePosixPath(relative).parts) or path.suffix.lower() in {".cia", ".cxi", ".cci", ".3ds", ".3dsx", ".key", ".keys", ".sav", ".snapshot", ".app", ".h3", ".tik", ".tmd", ".cert", ".rpx", ".wuhb", ".elf", ".exe", ".dll", ".bin", ".zip"}:
            raise ValueError("Private/game/key/build payloads cannot enter the documentation site")
        destination = source / staged
        destination.parent.mkdir(parents=True, exist_ok=True)
        data = path.read_bytes()
        hashes.append({"source": relative, "sha256": hashlib.sha256(data).hexdigest()})
        if path.suffix == ".md" or relative in ROOT_PAGES:
            text = data.decode("utf-8")
            if relative == "LICENSE":
                text = "# License\n\n" + text
            elif relative == "THIRD_PARTY_NOTICES":
                text = "# Third-party notices\n\n" + text
            destination.write_text(rewrite_markdown(text, path, Path(staged), mapping, root, repository, commit), encoding="utf-8")
        else:
            destination.write_bytes(data)
    notice_count = stage_engine_notices(source)
    configuration.update(docs_dir=str(source), site_dir=str(output / "site"), site_url=site_url)
    config = output / "mkdocs.generated.yml"
    config.write_text(yaml.safe_dump(configuration, sort_keys=False, allow_unicode=True), encoding="utf-8")
    command = [sys.executable, "-m", "mkdocs", "build", "--strict", "--config-file", str(config)]
    result = subprocess.run(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
    (output / "build.log").write_text(result.stdout, encoding="utf-8")
    if result.returncode:
        raise ValueError("Strict documentation build failed; see " + str(output / "build.log"))
    verification = verify_site(output / "site", site_url)
    status = json.loads((root / "docs/milestone-status.json").read_text(encoding="utf-8"))
    home = RenderedPage()
    home.feed((output / "site/index.html").read_text(encoding="utf-8"))
    text = " ".join(" ".join(home.text).split())
    gates = f"{status['full_milestones_accepted']} of {len(status['milestones'])}"
    if "Windows GUI" not in text or "Early development" not in text:
        raise ValueError("Homepage lost the maintained product goal or development status")
    report = {"ok": True, "source_commit": commit, "site_url": site_url,
              "project_version": status["version"], "dependency_versions": dependencies,
              "site_directory": str(output / "site"), "strict_build": True,
              "project_markdown_from_maintained_sources": True, "sources": hashes,
              "generated_site_engine_notices": notice_count,
              "full_milestones_accepted": status["full_milestones_accepted"],
              "console_execution_claimed": False, **verification}
    (output / "build-report.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--site-url", help="Override the canonical HTTPS Pages base URL")
    try:
        report = build(parser.parse_args())
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"Documentation build failed: {error}\n")
    print(json.dumps({key: report[key] for key in ("ok", "source_commit", "site_url", "html_pages", "checked_internal_links_and_assets", "search_entries")}))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
