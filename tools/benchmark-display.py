#!/usr/bin/env python3
"""Compare original display workloads; host timings are not Wii U performance."""
import argparse
import hashlib
import json
from pathlib import Path
import platform
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cc', default='cc')
    parser.add_argument('--frames', type=int, default=60, choices=range(1, 1001), metavar='1..1000')
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    binary = args.output.resolve()/'benchmark-scale'
    sources = ['tests/report/benchmark_scale.c', 'runtime/wiiu/image_scale.c', 'runtime/wiiu/viewport.c']
    command = [args.cc, '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror', '-Iruntime/wiiu', *sources, '-o', str(binary)]
    subprocess.run(command, cwd=ROOT, check=True)
    result = subprocess.run([str(binary), str(args.frames)], check=True, capture_output=True, text=True)
    report = json.loads(result.stdout)
    report.update(source_commit=subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
                  source_dirty=bool(subprocess.check_output(['git', 'status', '--porcelain'], cwd=ROOT)),
                  compiler=subprocess.check_output([args.cc, '--version'], text=True).splitlines()[0],
                  host=platform.platform(), compile_command=command,
                  sources={p: hashlib.sha256((ROOT/p).read_bytes()).hexdigest() for p in sources})
    for case in report['cases']:
        if case['cached_publish_bytes'] > case['baseline_publish_bytes'] or case['cached_scaled_pixels'] > case['baseline_scaled_pixels']:
            raise ValueError('Cached work exceeds full-surface work')
        if case['full_updates'] != 1 or (case['scenario'] == 'static' and case['reused_updates'] != args.frames-1):
            raise ValueError('Cold/static cache invariant differs')
    (args.output/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
    print(json.dumps({'ok': True, 'exact_pixels': True, 'cases': len(report['cases']), 'console_verified': False}))


if __name__ == '__main__':main()
