#!/usr/bin/env python3
"""Publish only validated documentation: a standalone reader and static-site ZIP.

Uses only the Python standard library. The standalone file embeds article text,
images and notices, and uses local DOM search without fetch, workers or a server.
"""
from __future__ import annotations

import argparse
import base64
import hashlib
from html import escape
from html.parser import HTMLParser
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
from urllib.parse import unquote, urlsplit
import zipfile

ROOT = Path(__file__).resolve().parents[1]
VOID = {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param", "source", "track", "wbr"}
FORBIDDEN = {".3ds", ".cia", ".cxi", ".cci", ".3dsx", ".rom", ".key", ".keys", ".sav", ".snapshot", ".app", ".h3", ".tik", ".tmd", ".cert", ".rpx", ".wuhb", ".elf", ".exe", ".dll", ".o", ".py", ".c", ".h", ".rs", ".bin", ".zip"}
EXCLUDED_SOURCE = {"docs/licenses/devkitarm-crtls/3dsx_crt0.s", "docs/licenses/devkitarm-crtls/3dsx.ld", "docs/licenses/devkitarm-crtls/3dsx.specs"}
TEXT_LICENSES = {"COPYING3", "COPYING.RUNTIME", "COPYING.NEWLIB", "COPYING.NEWLIB.ARM", "cryptography-LICENSE.BSD"}
STATIC_SUFFIXES = {".html", ".css", ".js", ".svg", ".png", ".jpg", ".jpeg", ".webp", ".ico", ".xml", ".txt", ".json"}


class Node:
    def __init__(self, tag: str, attrs: list[tuple[str, str | None]] | None = None):
        self.tag = tag
        self.attrs = dict(attrs or [])
        self.children: list[Node | str] = []


class Document(HTMLParser):
    def __init__(self, text: str):
        super().__init__(convert_charrefs=True)
        self.root = Node("root")
        self.stack = [self.root]
        self.feed(text)

    def handle_starttag(self, tag, attrs):
        node = Node(tag, attrs)
        self.stack[-1].children.append(node)
        if tag not in VOID:
            self.stack.append(node)

    def handle_startendtag(self, tag, attrs):
        self.stack[-1].children.append(Node(tag, attrs))

    def handle_endtag(self, tag):
        for index in range(len(self.stack) - 1, 0, -1):
            if self.stack[index].tag == tag:
                del self.stack[index:]
                break

    def handle_data(self, text):
        self.stack[-1].children.append(text)


def nodes(node: Node):
    yield node
    for child in node.children:
        if isinstance(child, Node):
            yield from nodes(child)


def plain(node: Node) -> str:
    return " ".join(child if isinstance(child, str) else plain(child) for child in node.children)


def digest(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def read_file(path: Path, limit: int = 8 * 1024 * 1024) -> bytes:
    if path.is_symlink() or not path.is_file() or path.stat().st_size > limit:
        raise ValueError("Expected a bounded regular documentation file")
    data = path.read_bytes()
    if data.startswith((b"\x7fELF", b"MZ", b"WUHB")):
        raise ValueError("Executable payloads cannot enter documentation packages")
    return data


def static_files(site: Path) -> tuple[dict[str, bytes], list[str]]:
    files = {}; excluded = []; total = 0
    for path in sorted(site.rglob("*")):
        if path.is_symlink():
            raise ValueError("Documentation trees cannot contain symlinks")
        if not path.is_file():
            continue
        relative = path.relative_to(site).as_posix()
        parts = path.relative_to(site).parts
        if any(part.lower() in {"private", ".git", ".github", ".env", "artifacts", "build", "obj", "bin"} for part in parts) or path.suffix.lower() in FORBIDDEN:
            raise ValueError("Private/source/game/key/build payload in the rendered documentation")
        if relative in EXCLUDED_SOURCE or relative == "sitemap.xml.gz" or relative.startswith("assets/") and path.suffix.lower() == ".map":
            excluded.append(relative)
            continue
        if path.suffix.lower() not in STATIC_SUFFIXES and not (relative.startswith("docs/licenses/") and path.name in TEXT_LICENSES):
            raise ValueError("Unexpected static documentation asset: " + relative)
        if path.suffix.lower() == ".json" and relative not in {"search/search_index.json", "docs/milestone-status.json"}:
            raise ValueError("Unexpected documentation JSON payload")
        data = read_file(path)
        if path.suffix in {".js", ".css"}:
            text = data.decode("utf-8")
            text = re.sub(r"(?m)^\s*//[#@]\s*sourceMappingURL=[^\n]*$", "", text)
            text = re.sub(r"/\*[#@]\s*sourceMappingURL=[^*]*\*/", "", text)
            data = text.encode()
        total += len(data)
        if len(files) >= 20000 or total > 64 * 1024 * 1024:
            raise ValueError("Documentation package exceeds its safety bounds")
        files[relative] = data
    return files, excluded


def local_target(site: Path, page: Path, target: str, site_url: str) -> tuple[Path, str] | None:
    parsed = urlsplit(target)
    base = urlsplit(site_url)
    if parsed.scheme or parsed.netloc:
        if (parsed.scheme, parsed.netloc) != (base.scheme, base.netloc):
            if parsed.scheme not in {"https", "http", "mailto", "tel"}:
                raise ValueError("Unsafe documentation link scheme")
            return None
    raw = unquote(parsed.path)
    if raw.startswith("/"):
        prefix = base.path.rstrip("/") + "/"
        if not raw.startswith(prefix) and raw != prefix.rstrip("/"):
            raise ValueError("Documentation link escapes the site base URL")
        path = site / raw.removeprefix(prefix).lstrip("/")
    else:
        path = page.parent / raw if raw else page
    path = path.resolve()
    if not path.is_relative_to(site):
        raise ValueError("Documentation link escapes the site directory")
    if path.is_dir():
        path /= "index.html"
    if not path.is_file():
        raise ValueError("Missing documentation link target")
    return path, unquote(parsed.fragment)


def page_id(path: str) -> str:
    return "page-" + digest(path.encode())[:16]


class Reader:
    def __init__(self, site: Path, files: dict[str, bytes], site_url: str):
        self.site = site
        self.files = files
        self.site_url = site_url
        self.pages = {}
        self.attachments: dict[str, tuple[str, bytes]] = {}
        self.images = 0
        for relative, data in files.items():
            if not relative.endswith(".html") or relative == "404.html":
                continue
            document = Document(data.decode("utf-8"))
            articles = [node for node in nodes(document.root) if node.tag == "article" and "md-content__inner" in node.attrs.get("class", "").split()]
            if len(articles) != 1:
                raise ValueError("Expected one rendered maintained article per page")
            headings = [node for node in nodes(articles[0]) if node.tag == "h1"]
            title = plain(headings[0]).strip().rstrip("¶").strip() if headings else relative
            self.pages[relative] = {"article": articles[0], "document": document, "title": title, "id": page_id(relative)}
        if "index.html" not in self.pages:
            raise ValueError("Rendered overview is missing")

    def order(self) -> list[str]:
        order = ["index.html"]
        for node in nodes(self.pages["index.html"]["document"].root):
            if node.tag != "a" or not node.attrs.get("href") or not {"md-nav__link", "md-tabs__link"}.intersection(node.attrs.get("class", "").split()):
                continue
            resolved = local_target(self.site, self.site / "index.html", node.attrs["href"], self.site_url)
            if resolved:
                relative = resolved[0].relative_to(self.site).as_posix()
                if relative in self.pages and relative not in order:
                    order.append(relative)
        return order + [name for name in self.pages if name not in order]

    def href(self, page: str, target: str) -> str:
        resolved = local_target(self.site, self.site / page, target, self.site_url)
        if resolved is None:
            return target
        relative = resolved[0].relative_to(self.site).as_posix()
        if relative in self.pages:
            return "#" + self.pages[relative]["id"] + ("--" + resolved[1] if resolved[1] else "")
        if relative not in self.files:
            raise ValueError("A reader link targets an excluded source file")
        if resolved[1]:
            raise ValueError("Unsupported non-page documentation fragment")
        if Path(relative).suffix.lower() not in {".txt", ".json", ".svg"} and Path(relative).name not in TEXT_LICENSES:
            raise ValueError("Unexpected linked offline document")
        identifier = "asset-" + digest(relative.encode())[:16]
        self.attachments[identifier] = (relative, self.files[relative])
        return "#" + identifier

    def image(self, page: str, target: str, alt: str) -> str:
        resolved = local_target(self.site, self.site / page, target, self.site_url)
        if resolved is None:
            raise ValueError("Offline documentation cannot depend on an external image")
        relative = resolved[0].relative_to(self.site).as_posix()
        if relative not in self.files:
            raise ValueError("Offline image is not a selected static documentation asset")
        data = self.files[relative]
        suffix = Path(relative).suffix.lower()
        self.images += 1
        if suffix == ".svg":
            document = Document(data.decode("utf-8"))
            svg = [node for node in nodes(document.root) if node.tag == "svg"]
            if len(svg) != 1:
                raise ValueError("Expected one bounded documentation SVG")
            identifier = self.pages[page]["id"] + "--image-" + str(self.images)
            return self.render(svg[0], page, namespace=identifier)
        mime = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg", ".webp": "image/webp", ".ico": "image/x-icon"}.get(suffix)
        if mime is None:
            raise ValueError("Unsupported offline image format")
        return f'<img alt="{escape(alt, quote=True)}" src="data:{mime};base64,{base64.b64encode(data).decode()}">'

    def render(self, node: Node | str, page: str, namespace: str | None = None) -> str:
        if isinstance(node, str):
            return escape(node, quote=False)
        if node.tag in {"script", "iframe", "object", "embed", "style", "link", "base", "form"}:
            return ""
        namespace = namespace or self.pages[page]["id"]
        if node.tag == "img":
            return self.image(page, node.attrs.get("src", ""), node.attrs.get("alt", ""))
        attrs = {}
        for key, value in node.attrs.items():
            if key.startswith("on") or key in {"src", "srcset", "action", "formaction", "target"}:
                continue
            if value is None:
                attrs[key] = None
                continue
            if key in {"id", "name"}:
                value = namespace + "--" + value
            elif key in {"for", "aria-labelledby", "aria-describedby"}:
                value = " ".join(namespace + "--" + part for part in value.split())
            elif key in {"href", "xlink:href"}:
                if node.tag != "a" and value.startswith("#"):
                    value = "#" + namespace + "--" + value[1:]
                else:
                    value = self.href(page, value)
            elif "url(" in value:
                value = re.sub(r"url\(#([^)]+)\)", lambda match: "url(#" + namespace + "--" + match.group(1) + ")", value)
                if re.search(r"url\((?!#)", value):
                    raise ValueError("Offline SVG/style has an external resource")
            attrs[key] = value
        if node.tag == "a" and attrs.get("href", "").startswith(("https://", "http://")):
            attrs.update(rel="noopener noreferrer", target="_blank")
        attributes = "".join(" " + key + ("" if value is None else '="' + escape(value, quote=True) + '"') for key, value in attrs.items())
        opening = "<" + node.tag + attributes + ">"
        if node.tag in VOID:
            return opening
        rendered = opening + "".join(self.render(child, page, namespace) for child in node.children) + "</" + node.tag + ">"
        if node.tag == 'table':
            rendered = '<div class="table-wrap" tabindex="0" role="region" aria-label="Scrollable documentation table">' + rendered + '</div>'
        return rendered

    def html(self, report: dict, progress: dict) -> str:
        order = self.order()
        navigation = []; sections = []
        for relative in order:
            page = self.pages[relative]
            navigation.append(f'<a class="page-link" href="#{page["id"]}">{escape(page["title"])}</a>')
            sections.append(f'<section class="guide-page" id="{page["id"]}" data-title="{escape(page["title"], quote=True)}">' + self.render(page["article"], relative) + "</section>")
        for identifier, (relative, data) in self.attachments.items():
            if relative.endswith(".svg"):
                content = self.image("index.html", "/" + urlsplit(self.site_url).path.strip("/") + "/" + relative, relative)
            else:
                content = "<pre>" + escape(data.decode("utf-8"), quote=False) + "</pre>"
            sections.append(f'<section class="guide-page attachment" id="{identifier}" data-title="{escape(relative, quote=True)}"><h1>{escape(relative)}</h1>{content}</section>')
            navigation.append(f'<a class="page-link" href="#{identifier}">{escape(relative)}</a>')
        version = escape(report["project_version"])
        commit = escape(report["source_commit"])
        gates = progress["full_milestones_accepted"]
        count = len(progress["milestones"])
        header = f'<header><div><strong>CTR2WUP documentation</strong><span>Version {version} · {gates}/{count} full milestones accepted</span></div><label for="guide-search">Search this guide</label><input id="guide-search" type="search" placeholder="Search topics and article text" autocomplete="off"><button id="theme-toggle" type="button">Light / dark</button><button id="print-guide" type="button">Print</button></header>'
        banner = f'<p class="scope">The planned product is a Windows GUI that injects an owned 3DS game into a playable Wii U WUP. Current releases are original diagnostics; commercial games and the GUI remain unfinished. Reading and search work offline. External source/issue links need internet and may require repository access. Source commit: <code>{commit}</code>.</p>'
        return '<!doctype html><html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width, initial-scale=1"><title>CTR2WUP documentation ' + version + '</title><style>' + CSS + '</style></head><body>' + header + '<div class="layout"><aside aria-label="Document navigation"><p id="search-status" role="status" aria-live="polite"></p><nav>' + "".join(navigation) + '</nav></aside><main>' + banner + "".join(sections) + '</main></div><script>' + SCRIPT + '</script></body></html>'


CSS = """
:root { color-scheme: light; --bg:#f3f6fb; --panel:#fff; --text:#17243b; --muted:#53627a; --line:#d6deeb; --link:#2854b5; --code:#eef2f8; }
@media(prefers-color-scheme:dark) { :root { color-scheme:dark; --bg:#111827; --panel:#1b2537; --text:#edf2fb; --muted:#b4c1d5; --line:#3b4960; --link:#9bb9ff; --code:#121b2c; } }
body[data-theme=light] { color-scheme:light; --bg:#f3f6fb; --panel:#fff; --text:#17243b; --muted:#53627a; --line:#d6deeb; --link:#2854b5; --code:#eef2f8; }
body[data-theme=dark] { color-scheme:dark; --bg:#111827; --panel:#1b2537; --text:#edf2fb; --muted:#b4c1d5; --line:#3b4960; --link:#9bb9ff; --code:#121b2c; }
* { box-sizing:border-box; } html { scroll-behavior:smooth; scroll-padding-top:7rem; } body { margin:0; background:var(--bg); color:var(--text); font:16px/1.65 system-ui,-apple-system,"Segoe UI",sans-serif; }
header { position:sticky; top:0; z-index:2; display:flex; gap:.75rem; align-items:center; flex-wrap:wrap; padding:1rem max(1rem,calc((100vw - 1440px)/2)); background:var(--panel); border-bottom:1px solid var(--line); }
header div { margin-right:auto; } header strong { display:block; font-size:1.1rem; } header span { color:var(--muted); font-size:.85rem; } header label { position:absolute; width:1px; height:1px; overflow:hidden; clip-path:inset(50%); }
input,button { font:inherit; color:inherit; background:var(--panel); border:1px solid var(--line); border-radius:.5rem; padding:.45rem .7rem; } input { width:min(28rem,100%); } button { cursor:pointer; }
.layout { display:grid; grid-template-columns:17rem minmax(0,1fr); gap:1.4rem; max-width:1440px; margin:auto; padding:1.2rem; } aside { position:sticky; top:6.2rem; align-self:start; max-height:calc(100vh - 7rem); overflow:auto; }
main,aside,nav,.guide-page,.scope { min-width:0; } main,nav a,.guide-page,.scope { overflow-wrap:anywhere; }
nav { display:flex; flex-direction:column; gap:.25rem; } nav a { padding:.35rem .7rem; text-decoration:none; border-radius:.4rem; } nav a:hover,nav a[aria-current] { background:var(--code); } a { color:var(--link); text-underline-offset:.18em; } :focus-visible { outline:3px solid var(--link); outline-offset:3px; }
#search-status { color:var(--muted); font-size:.85rem; } .scope,.guide-page { padding:1.5rem; background:var(--panel); border:1px solid var(--line); border-radius:.8rem; margin:0 0 1.3rem; } .scope { border-left:4px solid var(--link); font-size:.95rem; }
h1 { line-height:1.25; font-size:1.9rem; letter-spacing:-.02em; } h2,h3 { line-height:1.35; } h2 { margin-top:2rem; } img,svg { max-width:100%; height:auto; } svg { display:inline-block; } .headerlink { font-size:.7em; padding-left:.25rem; opacity:.65; }
code,pre { font-family:ui-monospace,Consolas,"SFMono-Regular",monospace; } code { background:var(--code); padding:.1rem .25rem; border-radius:.2rem; } pre { background:var(--code); padding:1rem; border-radius:.5rem; overflow:auto; font-size:.86rem; line-height:1.5; } pre code { padding:0; } .highlight { overflow:auto; }
table { border-collapse:collapse; width:100%; margin:1rem 0; font-size:.9rem; } td,th { border:1px solid var(--line); padding:.55rem; text-align:left; vertical-align:top; } th { background:var(--code); } .table-wrap,.md-typeset__scrollwrap { max-width:100%; min-width:0; overflow-x:auto; } .table-wrap:focus-visible { outline-offset:-3px; } pre,.highlight { max-width:100%; min-width:0; overflow-wrap:normal; } blockquote { border-left:3px solid var(--line); margin-left:0; padding-left:1rem; color:var(--muted); } .admonition,details { border:1px solid var(--line); padding:.8rem; border-radius:.5rem; } summary { cursor:pointer; } [hidden] { display:none!important; }
@media(max-width:850px) { header { position:static; } .layout { grid-template-columns:minmax(0,1fr); padding:.7rem; } aside { position:static; max-height:28vh; border-bottom:1px solid var(--line); } .guide-page,.scope { padding:1rem; } h1 { font-size:1.6rem; } html { scroll-padding-top:1rem; } }
@media print { :root,body { color-scheme:light; --bg:white; --panel:white; --text:black; --muted:#333; --line:#aaa; --link:black; --code:#f5f5f5; } header { position:static; padding:0; } header input,header label,header button,aside,.scope { display:none!important; } .layout { display:block; padding:0; } .guide-page,.guide-page[hidden] { display:block!important; break-before:page; border:0; padding:0; } .guide-page:first-of-type { break-before:auto; } pre { white-space:pre-wrap; overflow:visible; overflow-wrap:anywhere; } .table-wrap { overflow:visible; } tr,img,svg { break-inside:avoid; } .headerlink { display:none; } a { text-decoration:none; } }
"""

SCRIPT = """
(()=>{'use strict';
 const search=document.getElementById('guide-search');
 const pages=[...document.querySelectorAll('.guide-page')];
 const links=[...document.querySelectorAll('.page-link')];
 const content=new Map(pages.map(page=>[page.id,(page.dataset.title+' '+page.textContent).toLocaleLowerCase()]));
 function filter(){
   const terms=search.value.trim().toLocaleLowerCase().split(/\\s+/).filter(Boolean);
   let count=0;
   for(const page of pages){const match=terms.every(term=>content.get(page.id).includes(term));page.hidden=!match;if(match)count++;}
   for(const link of links){const page=document.getElementById(link.getAttribute('href').slice(1));link.hidden=page.hidden;}
   document.getElementById('search-status').textContent=terms.length?count+' matching documents. Clear search to show all.':pages.length+' documents available offline.';
 }
 function reveal(){
   let id;try{id=decodeURIComponent(location.hash.slice(1));}catch(_){return;}
   const target=document.getElementById(id);if(!target)return;
   const page=target.closest('.guide-page');if(page&&page.hidden){search.value='';filter();}
   for(const link of links){if(page&&link.getAttribute('href')==='#'+page.id)link.setAttribute('aria-current','page');else link.removeAttribute('aria-current');}
   target.scrollIntoView();
 }
 search.addEventListener('input',filter);window.addEventListener('hashchange',reveal);
 document.getElementById('theme-toggle').addEventListener('click',()=>{const current=document.body.dataset.theme||(matchMedia('(prefers-color-scheme:dark)').matches?'dark':'light');document.body.dataset.theme=current==='dark'?'light':'dark';});
 let saved='',details=[];
 window.addEventListener('beforeprint',()=>{saved=search.value;search.value='';filter();details=[...document.querySelectorAll('details')].map(node=>[node,node.open]);for(const [node] of details)node.open=true;});
 window.addEventListener('afterprint',()=>{search.value=saved;filter();for(const [node,open] of details)node.open=open;});
 document.getElementById('print-guide').addEventListener('click',()=>window.print());filter();reveal();
})();
"""


class LinkAudit(HTMLParser):
    def __init__(self, text):
        super().__init__(convert_charrefs=True);self.ids=set();self.targets=[];self.resources=[];self.scripts=0
        self.feed(text)

    def handle_starttag(self, tag, attributes):
        attrs=dict(attributes)
        if attrs.get("id"):
            if attrs["id"] in self.ids:
                raise ValueError("Duplicate standalone document anchor")
            self.ids.add(attrs["id"])
        if attrs.get("href", "").startswith("#"):
            self.targets.append(unquote(attrs["href"][1:]))
        elif attrs.get("href") and urlsplit(attrs["href"]).scheme not in {"https", "http", "mailto", "tel"}:
            raise ValueError("Standalone navigation must use anchors or explicit external links")
        if attrs.get("xlink:href", "").startswith("#"):
            self.targets.append(attrs["xlink:href"][1:])
        for value in attrs.values():
            if value:
                self.targets.extend(re.findall(r"url\(#([^)]+)\)",value))
        for name in ("src", "srcset", "poster"):
            if attrs.get(name):
                self.resources.append(attrs[name])
        if tag=="script":self.scripts+=1


def audit_reader(text: str) -> dict:
    audit=LinkAudit(text)
    if any(target not in audit.ids for target in audit.targets):
        raise ValueError("Unresolved standalone internal anchor")
    if any(not resource.startswith("data:image/") for resource in audit.resources):
        raise ValueError("Standalone reader depends on an external/local resource")
    if audit.scripts!=1 or re.search(r"\b(?:fetch|XMLHttpRequest|Worker|importScripts)\s*\(",SCRIPT):
        raise ValueError("Standalone reader must use only its local DOM script")
    return {"internal_anchors_verified":len(audit.targets),"unique_ids":len(audit.ids),"external_runtime_dependencies":False}


def audit_static(site: Path, files: dict[str, bytes], site_url: str) -> int:
    parsed={name:Document(data.decode('utf-8')) for name,data in files.items() if name.endswith('.html')}
    ids={name:{node.attrs['id'] for node in nodes(doc.root) if node.attrs.get('id')} for name,doc in parsed.items()}
    checked=0
    def check(page,target):
        nonlocal checked
        uri=urlsplit(target)
        if uri.scheme in {'javascript','data'}:return
        resolved=local_target(site,site/page,target,site_url)
        if resolved is None:return
        relative=resolved[0].relative_to(site).as_posix()
        if relative not in files:raise ValueError('A public site link targets an excluded file')
        if resolved[1] and relative.endswith('.html') and resolved[1] not in ids[relative]:
            raise ValueError('A public site anchor does not resolve')
        checked+=1
    for page,doc in parsed.items():
        for node in nodes(doc.root):
            for name in ('href','src','poster'):
                if node.attrs.get(name):check(page,node.attrs[name])
            if node.tag=='script' and node.attrs.get('id')=='__config':
                configuration=json.loads(plain(node))
                if configuration.get('search'):check(page,configuration['search'])
    for name,data in files.items():
        if name.endswith('.css'):
            for target in re.findall(r'url\(\s*[\'\"]?([^\)\'\"]+)[\'\"]?\s*\)',data.decode('utf-8')):
                check(name,target.strip())
    for entry in json.loads(files['search/search_index.json'])['docs']:
        check('index.html',entry['location'])
    return checked


PAGES_WORKFLOW = """name: Publish documentation
on:
  push:
    branches: [main]
  workflow_dispatch:
permissions:
  contents: read
concurrency:
  group: pages
  cancel-in-progress: false
jobs:
  publish:
    if: github.ref == 'refs/heads/main'
    runs-on: ubuntu-24.04
    timeout-minutes: 15
    permissions:
      contents: read
      pages: write
      id-token: write
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    steps:
      - uses: actions/checkout@3d3c42e5aac5ba805825da76410c181273ba90b1 # v7.0.1
        with:
          persist-credentials: false
      - name: Configure public documentation Pages
        uses: actions/configure-pages@45bfe0192ca1faeb007ade9deae92b16b8254a0d # v6.0.0
        with:
          enablement: false
      - name: Upload generated documentation only
        uses: actions/upload-pages-artifact@fc324d3547104276b827a68afc52ff2a11cc49c9 # v5.0.0
        with:
          path: site
      - name: Deploy free public Pages site
        id: deployment
        uses: actions/deploy-pages@368f82528645a54fb793d4d04e342629a3f51346 # v5.0.1
"""


def package(options: argparse.Namespace) -> dict:
    root=options.source_root.resolve();site=options.site_dir.resolve();output=options.output.resolve()
    if not site.is_relative_to((root/'artifacts').resolve()) or site==(root/'artifacts').resolve() or 'private' in {part.lower() for part in site.parts}:
        raise ValueError('Publish only validated documentation under ignored artifacts/')
    if output.is_relative_to(site):
        raise ValueError('Publication output must not modify the validated input site')
    if not output.is_relative_to((root/"artifacts").resolve()) or output==(root/"artifacts").resolve():
        raise ValueError("Documentation packages must remain under ignored artifacts/")
    if output.exists():
        raise ValueError("Documentation publication output must not already exist")
    report=json.loads(read_file(options.build_report,2*1024*1024))
    if report.get("ok") is not True or report.get("strict_build") is not True or report.get("all_rendered_internal_links_resolve") is not True:
        raise ValueError("Require the passing strict documentation build report")
    version=report.get("project_version","");commit=report.get("source_commit","")
    if not re.fullmatch(r"[0-9]+\.[0-9]+\.[0-9]+",version) or not re.fullmatch(r"[0-9a-f]{40}",commit):
        raise ValueError("Documentation version/source identity is missing")
    if not re.fullmatch(r"[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+",options.public_repository):
        raise ValueError("Expected a public documentation repository name")
    public_url="https://"+options.public_repository.split('/')[0].lower()+".github.io/"+options.public_repository.split('/')[1]+"/"
    files,excluded=static_files(site)
    progress=json.loads(files["docs/milestone-status.json"])
    if progress.get("version")!=version or progress.get("full_milestones_accepted")!=report.get("full_milestones_accepted"):
        raise ValueError("Documentation progress differs from its validated build")
    reader=Reader(site,files,report["site_url"])
    if len(reader.pages)+("404.html" in files)!=report.get("html_pages"):
        raise ValueError("Rendered document set differs from the validated build")
    standalone=reader.html(report,progress)
    if len(standalone.encode())>16*1024*1024:
        raise ValueError("Standalone guide exceeds its bounded size")
    audit=audit_reader(standalone)
    if commit not in standalone or "Windows GUI" not in standalone or f"{progress['full_milestones_accepted']}/{len(progress['milestones'])}" not in standalone or "<svg" not in standalone:
        raise ValueError("Standalone guide lost source identity, goal, progress or its SVG")
    html_name=f"CTR2WUP-Documentation-{version}.html";zip_name=f"CTR2WUP-Public-Docs-{version}.zip"
    old_url=report["site_url"].rstrip('/')+'/';rebased={}
    for relative,data in files.items():
        if Path(relative).suffix in {".html",".xml",".json"}:
            data=data.decode('utf-8').replace(old_url,public_url).encode()
        rebased[relative]=data
    rebased[html_name]=standalone.encode()
    public_links=audit_static(site,files,report['site_url'])
    instructions=f"""# CTR2WUP public documentation

This repository contains generated documentation only. Application source remains
in the private CTR2WUP repository. No ROMs, keys, saves, game assets, translated
game code, SDK or application build binaries belong here.

Website: {public_url}
Source commit: `{commit}` · project version `{version}`.
CTR2WUP is in early development: the Windows GUI and playable commercial-game
WUP conversion remain unfinished. See the maintained progress and acceptance pages.

## Free publishing

1. Create/use the public repository `{options.public_repository}` without changing
   the private application repository's visibility.
2. Extract this bundle into its root and push the generated files to `main`.
3. In Settings → Pages, choose **GitHub Actions** as the source. Initial setup may
   require the repository owner; no premium plan is needed for public Pages.
4. The included native workflow publishes only `site/`. If setup or access is
   denied, retain the generated files and report the actual GitHub error.

## Read without a website

Open `site/{html_name}` in any modern browser. Reading, navigation and search work
offline with no server or downloads. Printing includes all articles even if a
search filter is active. External source/issue links require internet and may need
repository access.

## Update from the authorized source checkout

```sh
python3 -m venv artifacts/docs-venv
artifacts/docs-venv/bin/python -m pip install -r docs-requirements.txt
artifacts/docs-venv/bin/python tools/build-docs-site.py
python3 tools/package-docs-site.py --output artifacts/docs-publication-NEW
```

Copy the new generated `site/`, this README and provenance/checksum files here;
preserve repository settings and the Pages workflow. Do not edit the published
articles independently: maintain the original Markdown and regenerate. If the
public repository name changes, pass `--public-repository OWNER/NAME` when packaging.

Project/runtime and site-engine licenses are preserved inside the site. The public
bundle excludes application source and the raw ARM startup assembly/linker files;
the maintained notice links back to their original source provenance. Generated
Material JavaScript/CSS remains part of the documentation site, not game code.
"""
    output.parent.mkdir(parents=True,exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.docs-publication-',dir=output.parent) as temporary:
        stage=Path(temporary)
        (stage/html_name).write_text(standalone,encoding='utf-8')
        receipt={"schema_version":1,"ok":True,"version":version,"source_commit":commit,"public_repository":options.public_repository,"public_site_url":public_url,"standalone_html":html_name,"public_bundle":zip_name,"document_pages":len(reader.pages),"embedded_text_attachments":len(reader.attachments),"inline_images":reader.images,"excluded_source_files":excluded,"all_maintained_article_pages_retained":True,"public_site_internal_links_verified":public_links,"archive_contains_documentation_only":True,"application_source_bundled":False,"private_payloads_bundled":False,"progress_milestones_accepted":progress['full_milestones_accepted'],"console_acceptance_claimed":False,**audit}
        bundle={"README.md":instructions.encode(),".github/workflows/pages.yml":PAGES_WORKFLOW.encode(),"documentation-provenance.json":json.dumps(receipt,indent=2).encode()}
        bundle.update({"site/"+name:data for name,data in rebased.items()})
        sums="".join(digest(data)+"  "+name+"\n" for name,data in sorted(bundle.items()))
        bundle["SHA256SUMS.txt"]=sums.encode()
        with zipfile.ZipFile(stage/zip_name,'x',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as archive:
            for name,data in sorted(bundle.items()):
                entry=zipfile.ZipInfo(name,date_time=(2026,10,12,0,0,0));entry.compress_type=zipfile.ZIP_DEFLATED;entry.external_attr=0o100644<<16
                archive.writestr(entry,data)
        with zipfile.ZipFile(stage/zip_name) as archive:
            if archive.testzip() is not None or len(archive.namelist())!=len(set(archive.namelist())):
                raise ValueError("Public documentation ZIP failed CRC/entry verification")
            for name,data in bundle.items():
                if archive.read(name)!=data:
                    raise ValueError("Public documentation ZIP byte verification failed")
        receipt['zip_crc_verified']=True
        receipt['artifacts']=[{'name':path.name,'size':path.stat().st_size,'sha256':digest(path.read_bytes())} for path in sorted(stage.iterdir())]
        (stage/'publication-report.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
        external=[*receipt['artifacts'],{'name':'publication-report.json','sha256':digest((stage/'publication-report.json').read_bytes())}]
        (stage/'SHA256SUMS.txt').write_text(''.join(row['sha256']+'  '+row['name']+'\n' for row in external),encoding='utf-8')
        output.mkdir()
        try:
            for path in stage.iterdir():os.replace(path,output/path.name)
        except BaseException:
            shutil.rmtree(output);raise
    return receipt


def main() -> int:
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-root',type=Path,default=ROOT)
    parser.add_argument('--site-dir',type=Path,default=ROOT/'artifacts/docs-site/site')
    parser.add_argument('--build-report',type=Path,default=ROOT/'artifacts/docs-site/build-report.json')
    parser.add_argument('--output',type=Path,default=ROOT/'artifacts/docs-publication')
    parser.add_argument('--public-repository',default='Jdiller-86/CTR-2-WUP-docs')
    try:report=package(parser.parse_args())
    except (ValueError,OSError,KeyError,TypeError,zipfile.BadZipFile) as error:parser.exit(1,f'Documentation packaging failed: {error}\n')
    print(json.dumps({key:report[key] for key in ('ok','standalone_html','public_bundle','document_pages','internal_anchors_verified','public_site_url')}));return 0


if __name__=='__main__':raise SystemExit(main())
