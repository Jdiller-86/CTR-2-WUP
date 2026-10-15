#!/usr/bin/env python3
"""Verify a bounded returned diagnostic ZIP and reparse its original evidence."""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import re
import zipfile

SPEC = importlib.util.spec_from_file_location('hardware_collector', Path(__file__).with_name('collect-hardware-results.py'))
collector = importlib.util.module_from_spec(SPEC);SPEC.loader.exec_module(collector)


def analyze(path, expected):
    if not re.fullmatch('[0-9a-f]{40}', expected):raise ValueError('Require the expected tested source commit')
    with zipfile.ZipFile(path) as archive:
        names = archive.namelist()
        if len(names) != len(set(names)) or not 2 <= len(names) <= 65 or sum(i.file_size for i in archive.infolist()) > collector.MAX_TOTAL+1024*1024:
            raise ValueError('Return archive names/count/size exceed the contract')
        if 'collection.json' not in names:raise ValueError('Collection receipt missing')
        if archive.getinfo('collection.json').file_size > 1024*1024:raise ValueError('Collection receipt is oversized')
        receipt = json.loads(archive.read('collection.json'))
        version = receipt.get('version')
        _, pattern = collector.patterns(version)
        for info in archive.infolist():
            if info.filename == 'collection.json':
                if info.file_size > 1024*1024:raise ValueError('Collection receipt is oversized')
            elif not info.filename.startswith('logs/') or not pattern.fullmatch(info.filename[5:]) or info.file_size > collector.MAX_FILE:
                raise ValueError('Return archive contains an unrequested path')
        if receipt.get('kind') != 'ctr2wup-hardware-return' or version not in collector.SUPPORTED_VERSIONS or receipt.get('source_commit') != expected:
            raise ValueError('Return identity differs from the tested kit')
        rows = receipt.get('files', [])
        if not isinstance(rows, list) or len(rows) != len(names)-1 or {r['path'] for r in rows} != set(names)-{'collection.json'}:
            raise ValueError('Return manifest does not match its payload')
        summaries, payload = [], {}
        for row in rows:
            data = archive.read(row['path'])
            if row.get('size') != len(data) or row.get('sha256') != hashlib.sha256(data).hexdigest():
                raise ValueError('Returned log checksum differs')
            name = row['path'][5:]
            if not name.startswith('result-') and not collector.trace_identity_matches(name, data, expected, version):
                raise ValueError('Returned trace build/source identity differs or is incomplete')
            payload[row['path']] = data
            summaries.append(collector.result_summary(row['path'][5:], data, expected, version))
        warnings = list(receipt.get('warnings', []))
        for warning in collector.missing_trace_warnings(payload, version):
            if warning not in warnings:warnings.append(warning)
    return {'ok': True, 'transport_verified': True, 'source_commit': expected, 'version': version,
            'observations': receipt.get('observations', {}), 'warnings': warnings,
            'summaries': summaries, 'full_milestone_acceptance': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('archive', type=Path);parser.add_argument('--expected-source', required=True)
    parser.add_argument('--output', type=Path, required=True);args = parser.parse_args()
    report = analyze(args.archive, args.expected_source)
    with args.output.open('x', encoding='utf-8') as stream:json.dump(report, stream, indent=2);stream.write('\n')
    print(json.dumps({'ok': True, 'files': len(report['summaries']), 'full_milestone_acceptance': False}))


if __name__ == '__main__':main()
