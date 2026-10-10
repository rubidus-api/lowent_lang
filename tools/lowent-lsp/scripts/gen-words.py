#!/usr/bin/env python3
"""Write words.json — what the server shows on hover and offers as completion.

Sources are the specification's own tables, so the server says what the specification says:
  keywords   Annex A.10.1 (`keyword ::= …`)
  ops        docs/spec/BUILTIN-MEANINGS.tsv (one line of meaning) joined with
             docs/spec/BUILTIN-SIGNATURES.tsv (form · takes · gives), where a row exists
`--check` fails when the written file is stale.
"""
import json, pathlib, re, sys
HERE = pathlib.Path(__file__).resolve().parent.parent
ROOT = HERE.parent.parent
OUT = HERE / 'words.json'

def rows(name):
    for l in (ROOT / 'docs' / 'spec' / name).read_text(encoding='utf-8').splitlines():
        if l and not l.startswith('#'):
            yield l.split('\t')

annex = (ROOT / 'docs' / 'spec' / 'canon' / 'annex-a-grammar.md').read_text(encoding='utf-8')
m = re.search(r'^keyword ::=(.*?)\n(?=name\s+::=)', annex, re.S | re.M)
keywords = sorted(set(re.findall(r'"([a-z_]+)"', m.group(1))))
if len(keywords) < 40:
    sys.exit(f'gen-words: only {len(keywords)} keywords found in Annex A.10.1')
sig = {r[0]: r for r in rows('BUILTIN-SIGNATURES.tsv') if len(r) >= 5}
ops = {}
for r in rows('BUILTIN-MEANINGS.tsv'):
    if len(r) < 2 or not re.fullmatch(r'[a-z_][a-z0-9_.]*', r[0]):
        continue
    e = {'meaning': r[1]}
    s = sig.get(r[0])
    if s:
        e.update({'group': s[1], 'form': s[2].strip('`'), 'takes': s[3], 'gives': s[4]})
    ops[r[0]] = e
if len(ops) < 150:
    sys.exit(f'gen-words: only {len(ops)} ops found in BUILTIN-MEANINGS.tsv')
want = json.dumps({'keywords': keywords, 'ops': dict(sorted(ops.items()))}, indent=1, ensure_ascii=False) + '\n'
if '--check' in sys.argv:
    if not OUT.exists() or OUT.read_text(encoding='utf-8') != want:
        sys.exit('gen-words: words.json is stale — run scripts/gen-words.py')
    print(f'gen-words: ok — {len(keywords)} keywords · {len(ops)} ops ({sum(1 for e in ops.values() if "form" in e)} with a signature row)')
else:
    OUT.write_text(want, encoding='utf-8')
    print(f'gen-words: wrote {OUT.name} — {len(keywords)} keywords · {len(ops)} ops')
