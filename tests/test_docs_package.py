"""Original documentation fixtures exercise publication and offline boundaries."""
import argparse
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
import zipfile

ROOT = Path(__file__).resolve().parents[1]
SPEC = importlib.util.spec_from_file_location('docs_package',ROOT/'tools/package-docs-site.py')
DOCS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(DOCS)
COMMIT = 'a' * 40
SITE_URL = 'https://example.github.io/original-docs/'


class DocsPackageTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='ctr2wup-doc-package-')
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.site = self.root/'artifacts/docs-site/site'
        (self.site/'docs/guide').mkdir(parents=True)
        (self.site/'docs/images').mkdir()
        (self.site/'search').mkdir()
        (self.site/'site-licenses').mkdir()
        progress = {'version':'0.17.0','full_milestones_accepted':0,'milestones':[{'id':str(i)} for i in range(7)]}
        (self.site/'docs/milestone-status.json').write_text(json.dumps(progress))
        (self.site/'docs/images/progress.svg').write_text('<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 100 40"><defs><linearGradient id="color"><stop stop-color="blue"/></linearGradient></defs><rect width="100" height="40" fill="url(#color)"/><text>0/7</text></svg>')
        (self.site/'site-licenses/notice.txt').write_text('Original synthetic license notice.')
        overview = f'<h1 id="goal">Windows GUI to playable WUP</h1><p>0 of 7 accepted.</p><h2 id="shared">Overview section</h2><a href="#shared">here</a><a href="docs/guide/#shared">guide</a><a href="site-licenses/notice.txt">notice</a><img src="docs/images/progress.svg" alt="progress"><a href="https://github.com/users/Jdiller-86/projects/2">Live Project</a><a href="https://github.com/example/private/blob/{COMMIT}/README.md">source</a><pre>&lt;img src="private.key"&gt;</pre>'
        guide = '<h1 id="guide">Original guide</h1><h2 id="shared">Guide section</h2><p>Independent original fixture and irregular whitespace.</p><a href="../../index.html#shared">overview</a>'
        self.page('index.html',overview)
        self.page('docs/guide/index.html',guide)
        self.page('404.html','<h1>Not found</h1>')
        (self.site/'search/search_index.json').write_text(json.dumps({'docs':[{'location':'','text':'Windows GUI'},{'location':'docs/guide/#shared','text':'Original fixture'}]}))
        self.report = {'ok':True,'strict_build':True,'all_rendered_internal_links_resolve':True,'source_commit':COMMIT,'project_version':'0.17.0','site_url':SITE_URL,'full_milestones_accepted':0,'html_pages':3}
        self.report_path = self.site.parent/'build-report.json'
        self.report_path.write_text(json.dumps(self.report))
        self.options = argparse.Namespace(source_root=self.root,site_dir=self.site,build_report=self.report_path,output=self.root/'artifacts/docs-publication',public_repository='example/visible-docs')

    def page(self,name,content):
        (self.site/name).write_text('<!doctype html><html><body><article class="md-content__inner md-typeset">'+content+'</article></body></html>')

    def test_complete_reader_bundle_and_checksums(self):
        result = DOCS.package(self.options)
        self.assertEqual(result['document_pages'],2)
        self.assertEqual(result['embedded_text_attachments'],1)
        self.assertFalse(result['external_runtime_dependencies'])
        self.assertTrue(result['zip_crc_verified'])
        text = (self.options.output/result['standalone_html']).read_text()
        self.assertIn(COMMIT,text)
        self.assertIn('0/7 full milestones accepted',text)
        self.assertIn('<svg',text)
        self.assertIn('Original synthetic license notice.',text)
        self.assertIn('&lt;img src="private.key"&gt;',text)
        self.assertNotIn('src="docs/images/progress.svg"',text)
        parsed = DOCS.LinkAudit(text)
        self.assertTrue(set(parsed.targets)<=parsed.ids)
        self.assertIn(DOCS.page_id('index.html')+'--shared',parsed.ids)
        self.assertIn(DOCS.page_id('docs/guide/index.html')+'--shared',parsed.ids)
        with zipfile.ZipFile(self.options.output/result['public_bundle']) as archive:
            self.assertIsNone(archive.testzip())
            self.assertIn('.github/workflows/pages.yml',archive.namelist())
            self.assertIn('site/'+result['standalone_html'],archive.namelist())
            for line in archive.read('SHA256SUMS.txt').decode().splitlines():
                expected,name=line.split('  ',1)
                self.assertEqual(DOCS.digest(archive.read(name)),expected)
            self.assertIn(b'no premium plan',archive.read('README.md'))
        receipt=json.loads((self.options.output/'publication-report.json').read_text())
        self.assertTrue(receipt['archive_contains_documentation_only'])
        for row in receipt['artifacts']:
            self.assertEqual(DOCS.digest((self.options.output/row['name']).read_bytes()),row['sha256'])
        with self.assertRaises(ValueError):DOCS.package(self.options)

    def test_project_link_replaces_the_required_static_progress_image(self):
        page = self.site / 'index.html'
        page.write_text(page.read_text().replace('<img src="docs/images/progress.svg" alt="progress">', ''))
        (self.site / 'docs/images/progress.svg').unlink()
        result = DOCS.package(self.options)
        reader = (self.options.output / result['standalone_html']).read_text()
        self.assertNotIn('<svg', reader)
        self.assertIn('https://github.com/users/Jdiller-86/projects/2', reader)
        self.assertFalse(result['external_runtime_dependencies'])

    def test_missing_page_or_fragment_is_rejected(self):
        for href in ['missing/','#missing']:
            self.page('index.html','<h1>Windows GUI</h1><a href="'+href+'">bad</a>')
            with self.assertRaises(ValueError):DOCS.package(self.options)
            self.assertFalse(self.options.output.exists())

    def test_external_image_and_active_link_are_rejected(self):
        for unsafe in ['<img src="https://example.org/asset.png">','<a href="javascript:alert(1)">bad</a>']:
            self.page('index.html','<h1>Windows GUI</h1>'+unsafe)
            with self.assertRaises(ValueError):DOCS.package(self.options)

    def test_source_binary_key_and_symlink_guards(self):
        for name,data in [('private.key',b'original fake key'),('program.py',b'original fake source'),('asset.png',b'MZoriginal fake executable')]:
            path=self.site/name;path.write_bytes(data)
            with self.assertRaises(ValueError):DOCS.package(self.options)
            path.unlink()
        if hasattr(Path,'symlink_to'):
            link=self.site/'alias.html'
            try:link.symlink_to(self.site/'index.html')
            except OSError:return  # Windows may not grant symlink creation to the test process.
            with self.assertRaises(ValueError):DOCS.package(self.options)

    def test_raw_crt_and_source_maps_are_excluded(self):
        folder=self.site/'docs/licenses/devkitarm-crtls';folder.mkdir(parents=True)
        (folder/'3dsx_crt0.s').write_text('original source placeholder')
        folder=self.site/'assets/javascripts';folder.mkdir(parents=True)
        (folder/'test.js').write_text('"use strict";\n//# sourceMappingURL=test.js.map\n')
        (folder/'test.js.map').write_text('{"original":"source map fixture"}')
        result=DOCS.package(self.options)
        with zipfile.ZipFile(self.options.output/result['public_bundle']) as archive:
            self.assertNotIn('site/docs/licenses/devkitarm-crtls/3dsx_crt0.s',archive.namelist())
            self.assertNotIn('site/assets/javascripts/test.js.map',archive.namelist())
            self.assertNotIn(b'sourceMappingURL',archive.read('site/assets/javascripts/test.js'))

    def test_receipt_mismatch_and_unsafe_output_are_rejected(self):
        self.report['strict_build']=False;self.report_path.write_text(json.dumps(self.report))
        with self.assertRaises(ValueError):DOCS.package(self.options)
        self.report['strict_build']=True;self.report['html_pages']=4;self.report_path.write_text(json.dumps(self.report))
        with self.assertRaises(ValueError):DOCS.package(self.options)
        self.options.output=self.root/'outside-artifacts'
        with self.assertRaises(ValueError):DOCS.package(self.options)

    def test_svg_and_navigation_targets_are_checked(self):
        with self.assertRaises(ValueError):DOCS.audit_reader('<html><a href="#absent">bad</a><script>'+DOCS.SCRIPT+'</script></html>')
        with self.assertRaises(ValueError):DOCS.audit_reader('<html><svg><rect fill="url(#absent)"/></svg><script>'+DOCS.SCRIPT+'</script></html>')
        with self.assertRaises(ValueError):DOCS.audit_reader('<html><img src="relative.png"><script>'+DOCS.SCRIPT+'</script></html>')


if __name__=='__main__':unittest.main()
