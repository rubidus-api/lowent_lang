#!/usr/bin/env python3
"""Regenerate shapes.json from the compiler's shape table (impl/src/low_arity.h).

The grammar needs to know which core ops take a bare WORD (a type name, a field
name, an option name) in a fixed position, because such a word does not open a
form and takes no stop.  That list is the compiler's, not ours: this script
copies it.  Run it after the compiler's table changes; `--check` fails if
shapes.json is stale.
"""
import json, pathlib, re, sys
HERE = pathlib.Path(__file__).resolve().parent.parent
ARITY = HERE.parent.parent / 'impl' / 'src' / 'low_arity.h'
OUT = HERE / 'shapes.json'

def table():
    t = ARITY.read_text(encoding='utf-8')
    shapes = {}
    for macro in ('LOW_SHAPES', 'LOW_NAMED_SHAPES', 'LOW_NAMED_SHAPES_KNOWN'):
        m = re.search(r'#define ' + macro + r'\(\w\)(.*?)\n\n', t, re.S)
        if not m:
            sys.exit(f'update-shapes: {macro} not found in {ARITY}')
        for name, shape in re.findall(r'[XY]\((\w+),\s*"([^"]*)"\)', m.group(1)):
            if set(shape) - {'V'}:
                shapes[name] = shape
    # forms the surface pass reads by hand (low_closer.c) — they are written out in grammar.js
    for hand in ('field', 'payload', 'view', 'channel'):
        shapes.pop(hand, None)
    return dict(sorted(shapes.items()))

want = json.dumps(table(), indent=2, ensure_ascii=False) + '\n'
if '--check' in sys.argv:
    have = OUT.read_text(encoding='utf-8') if OUT.exists() else ''
    if have != want:
        sys.exit('update-shapes: shapes.json is stale — run scripts/update-shapes.py')
    print(f'update-shapes: ok — {len(json.loads(want))} shaped ops')
else:
    OUT.write_text(want, encoding='utf-8')
    print(f'update-shapes: wrote {OUT.name} — {len(json.loads(want))} shaped ops')
