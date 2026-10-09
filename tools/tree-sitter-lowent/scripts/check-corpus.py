#!/usr/bin/env python3
"""Parse every tracked .low file with the tree-sitter grammar and hold it against the compiler.

The claim checked:  if `lowentc` finds no lexical or surface diagnostic in a file,
the tree-sitter parser reads that file with no ERROR and no MISSING node.

Files the compiler refuses at the surface (the deliberate mistakes among the manual's
examples and the fixtures) are counted and reported, not required to parse.

    python3 tools/tree-sitter-lowent/scripts/check-corpus.py [--lowentc PATH]

Needs the tree-sitter CLI (`npm install` in tools/tree-sitter-lowent) and a built compiler.
Exits 0 when the claim holds, 1 when it does not, 3 when a tool is missing.
"""
import concurrent.futures as cf
import pathlib, shutil, subprocess, sys, tempfile

HERE = pathlib.Path(__file__).resolve().parent.parent
ROOT = HERE.parent.parent
cli = HERE / 'node_modules' / '.bin' / 'tree-sitter'
if not cli.exists():
    cli = shutil.which('tree-sitter')
lowentc = ROOT / 'impl' / 'build' / 'lowentc'
if '--lowentc' in sys.argv:
    lowentc = pathlib.Path(sys.argv[sys.argv.index('--lowentc') + 1])
if not cli or not pathlib.Path(lowentc).exists():
    print(f'check-corpus: SKIP — tree-sitter CLI ({cli}) or lowentc ({lowentc}) is missing')
    sys.exit(3)

files = subprocess.run(['git', '-C', str(ROOT), 'ls-files', '*.low'], capture_output=True, text=True).stdout.split()
if len(files) < 500:
    print(f'check-corpus: only {len(files)} .low files found — refusing to call that a corpus')
    sys.exit(1)

def surface_clean(rel):
    r = subprocess.run([str(lowentc), '--check', str(ROOT / rel)], capture_output=True, text=True,
                       stdin=subprocess.DEVNULL, cwd=str(ROOT / 'impl'))
    out = r.stdout + r.stderr
    return '== lex diagnostics' not in out and '== surface diagnostics' not in out

with cf.ThreadPoolExecutor(8) as ex:
    clean = dict(zip(files, ex.map(surface_clean, files)))

with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
    f.write('\n'.join(str(ROOT / x) for x in files) + '\n')
r = subprocess.run([str(cli), 'parse', '-q', '--paths', f.name], capture_output=True, text=True, cwd=str(HERE))
bad = set()
for line in (r.stdout + r.stderr).splitlines():
    if 'ERROR' in line or 'MISSING' in line:
        p = line.split('\t')[0].strip()
        try:
            bad.add(str(pathlib.Path(p).resolve().relative_to(ROOT.resolve())))
        except ValueError:
            pass

broken = sorted(x for x in files if clean[x] and x in bad)          # the compiler reads it, the grammar does not
lenient = sorted(x for x in files if not clean[x] and x not in bad)  # the compiler refuses it, the grammar reads it
refused = sum(1 for x in files if not clean[x])
print(f'check-corpus: {len(files)} files · the compiler reads the surface of {len(files) - refused} · refuses {refused}')
if broken:
    print(f'check-corpus: ★ {len(broken)} file(s) the compiler reads but the grammar does not:')
    for x in broken[:40]:
        print('   ' + x)
    sys.exit(1)
tail = (f'and the grammar refuses all {refused} files the compiler refuses' if not lenient else
        f'{len(lenient)} of the {refused} files the compiler refuses do parse — the grammar is looser than the compiler there')
print(f'check-corpus: ok — every file the compiler reads parses with no ERROR / MISSING node; {tail}')
