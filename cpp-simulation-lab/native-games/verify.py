#!/usr/bin/env python3
"""Build, run tests, and record only checks executed in this invocation."""
import datetime
import json
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parent

def command(args):
    result = subprocess.run(args, cwd=ROOT, text=True, capture_output=True)
    if result.returncode:
        sys.stderr.write(result.stdout + result.stderr)
        raise RuntimeError(f'Command failed: {args}')
    return result.stdout

def main():
    command(['make', 'all'])
    command(['make', 'test'])
    command(['make', 'samples'])
    reports = [json.loads((ROOT / 'build' / f'{slug}-tests.json').read_text())
               for slug in ('veil', 'chronicle', 'tempo')]
    smoke = []
    def check(label, condition):
        if not condition:
            raise AssertionError(label)
        smoke.append({'name': label, 'passed': True})
    for slug, steps in [('veil', 12), ('chronicle', 24), ('tempo', 96)]:
        args = [f'./build/{slug}', '--seed', '42', '--steps', str(steps), '--json']
        raw = command(args)
        data = json.loads(raw)
        check(f'{slug}: seeded CLI repeatability', raw == command(args))
        check(f'{slug}: trajectory envelope',
              all(k in data for k in ('project', 'seed', 'mode', 'summary', 'frames')) and data['seed'] == 42)
        check(f'{slug}: nonempty frames', bool(data['frames']))
        initial = json.loads(command([f'./build/{slug}', '--seed', '42', '--steps', '0', '--json']))
        check(f'{slug}: zero-step initial frame', bool(initial['frames']))
    observation = json.loads(command(['./build/veil', '--seed', '42', '--observation', '0']))
    rejected = subprocess.run(['./build/veil', '--seed', '42', '--phase', 'ballot', '--action',
                               '{"agent":0,"round":0,"type":"vote","target":0}'],
                              cwd=ROOT, text=True, capture_output=True)
    response = json.loads(rejected.stdout)
    check('veil: CLI rejected action exit status', rejected.returncode == 2 and response['accepted'] is False)
    check('veil: CLI rejection preserves private view',
          response['observation']['suspects'] == observation['suspects'] and
          response['observation']['alive'] == observation['alive'])
    responses = '\n'.join(json.dumps(a) for a in [
        {'agent': 0, 'round': 1, 'type': 'message', 'target': -1, 'text': 'Let us compare ballots.'},
        {'agent': 0, 'round': 1, 'type': 'offer', 'target': 1},
        {'agent': 0, 'round': 1, 'type': 'vote', 'target': 1},
    ]) + '\n'
    stream = subprocess.run(['./build/veil', '--seed', '42', '--steps', '1', '--external-agent', '0', '--json'],
                            cwd=ROOT, text=True, capture_output=True, input=responses)
    lines = [json.loads(line) for line in stream.stdout.splitlines()]
    check('veil: streaming external delegate accepted', stream.returncode == 0 and len(lines) == 4)
    check('veil: streaming requests carry only chosen private view',
          all(row.get('kind') == 'request' and row['observation']['agent'] == 0 and
              'roles' not in row['observation'] for row in lines[:-1]))
    check('veil: streaming terminal trajectory', lines[-1]['summary']['rounds'] == 1)
    source_files = sorted(str(p.relative_to(ROOT)) for p in ROOT.rglob('*')
                          if p.is_file() and 'build' not in p.parts and p.name != 'verification-report.json')
    total = sum(int(r.get('checks', 0)) for r in reports)
    report = {
        'schemaVersion': 1,
        'generatedAt': datetime.datetime.now(datetime.timezone.utc).isoformat(),
        'compiler': command(['g++', '--version']).splitlines()[0],
        'language': 'C++17',
        'commandsExecuted': ['make all', 'make test', 'make samples'],
        'nativeBuildVerified': True,
        'wasmBuildVerified': False,
        'wasmNote': 'No WebAssembly compiler is available; no WASM build is claimed.',
        'nativeSuites': reports,
        'nativeAssertionChecks': total,
        'cliSmokeChecks': smoke,
        'cliSmokeChecksCount': len(smoke),
        'performanceMeasured': False,
        'providerIntegration': 'none; local heuristics and fixed template narration',
        'sourceFiles': source_files,
    }
    (ROOT / 'verification-report.json').write_text(json.dumps(report, indent=2) + '\n')
    print(json.dumps({'passed': True, 'nativeAssertionChecks': total,
                      'cliSmokeChecks': len(smoke), 'report': 'verification-report.json'}))

if __name__ == '__main__':
    main()
