#!/usr/bin/env python3
"""Write syntaxes/lowent.tmLanguage.json from the repository's own word lists.

The keyword list is the specification's (Annex A.10.1, `keyword ::= …`) and the core-op
list is docs/spec/BUILTIN-MEANINGS.tsv, so the editor colours the words the compiler knows.
`--check` fails when the written file is stale.
"""
import json, pathlib, re, sys
HERE = pathlib.Path(__file__).resolve().parent.parent
ROOT = HERE.parent.parent
OUT = HERE / 'syntaxes' / 'lowent.tmLanguage.json'

annex = (ROOT / 'docs' / 'spec' / 'canon' / 'annex-a-grammar.md').read_text(encoding='utf-8')
m = re.search(r'^keyword ::=(.*?)\n(?=name\s+::=)', annex, re.S | re.M)
keywords = sorted(set(re.findall(r'"([a-z_]+)"', m.group(1))))
if len(keywords) < 40:
    sys.exit(f'gen-syntax: only {len(keywords)} keywords found in Annex A.10.1')
ops = sorted({l.split('\t')[0] for l in (ROOT / 'docs' / 'spec' / 'BUILTIN-MEANINGS.tsv').read_text(encoding='utf-8').splitlines()
              if l and not l.startswith('#') and re.fullmatch(r'[a-z_][a-z0-9_]*', l.split('\t')[0])} - set(keywords))
clause = ['input', 'output', 'using', 'effects', 'requires', 'ensures', 'errors', 'tests', 'comptime', 'from', 'as',
          'assume', 'static', 'debug', 'into', 'with', 'capacity', 'order']
types = ['bool', 'u8', 'i8', 'u16', 'i16', 'u32', 'i32', 'u64', 'i64', 'usize', 'isize', 'f32', 'f64', 'void', 'self',
         'slice', 'array', 'matrix', 'option', 'result', 'vec', 'segments', 'bitset', 'bits', 'mask', 'cap', 'mut', 'owned',
         'mut_ref', 'ref', 'unsafe_ptr', 'stack', 'set']
constants = ['true', 'false', 'none']
control = [k for k in keywords if k in ('if', 'else', 'while', 'for', 'repeat', 'range', 'cycle', 'next', 'step', 'match',
                                         'case', 'return', 'break', 'continue', 'guard', 'do', 'end', 'try')]
other = [k for k in keywords if k not in control and k not in constants]
alt = lambda ws: '|'.join(sorted(ws, key=lambda w: (-len(w), w)))
NAME = r'[A-Za-z_][A-Za-z0-9_]*'
grammar = {
    '$schema': 'https://raw.githubusercontent.com/martinring/tmlanguage/master/tmlanguage.json',
    'name': 'Lowent',
    'scopeName': 'source.lowent',
    'fileTypes': ['low'],
    'patterns': [
        {'name': 'comment.block.lowent', 'begin': r'\bnote[ \t]+(' + NAME + r')\b.*$', 'end': r'^\1[ \t]*$'},
        {'name': 'comment.line.rem.lowent', 'match': r'\brem(?:[ \t].*)?$'},
        {'name': 'string.unquoted.text.lowent', 'begin': r'\btext[ \t]+(' + NAME + r')\b.*$', 'end': r'^\1[ \t]*$'},
        {'name': 'string.quoted.double.lowent', 'begin': r'[uU]?"', 'end': r'"|$',
         'patterns': [{'name': 'constant.character.escape.lowent',
                       'match': r'\\(?:x[0-9A-Fa-f]{2}|u[0-9A-Fa-f]{4}|U[0-9A-Fa-f]{8}|[\\"\'abfnrtv0])'}]},
        {'name': 'string.quoted.single.lowent', 'match': r"[uU]?'(?:\\(?:x[0-9A-Fa-f]{2}|u[0-9A-Fa-f]{4}|U[0-9A-Fa-f]{8}|.)|[^'\\])'"},
        {'name': 'constant.numeric.lowent',
         'match': r'(?<![A-Za-z0-9_.])[+-]?(?:0[xX][0-9A-Fa-f_]+(?:\.[0-9A-Fa-f_]+)?(?:[pP][+-]?[0-9_]+)?|0[bB][01_]+|[0-9][0-9_]*(?:\.[0-9][0-9_]*)?(?:[eE][+-]?[0-9_]+)?)(?![A-Za-z0-9_])'},
        {'match': r'\b(fn|proc)\s+(' + NAME + r'(?:\.' + NAME + r')*)',
         'captures': {'1': {'name': 'keyword.other.lowent'}, '2': {'name': 'entity.name.function.lowent'}}},
        {'match': r'\b(module|use|struct|enum|trait|actor|contract|test|type|newtype)\s+(' + NAME + r')',
         'captures': {'1': {'name': 'keyword.other.lowent'}, '2': {'name': 'entity.name.type.lowent'}}},
        {'match': r'\b(let|var)\s+(' + NAME + r')',
         'captures': {'1': {'name': 'keyword.other.lowent'}, '2': {'name': 'variable.other.definition.lowent'}}},
        {'name': 'constant.language.lowent', 'match': r'\b(?:' + alt(constants) + r')\b'},
        {'name': 'keyword.control.lowent', 'match': r'\b(?:' + alt(control) + r')\b'},
        {'name': 'keyword.other.lowent', 'match': r'\b(?:' + alt(other) + r')\b'},
        {'name': 'keyword.other.clause.lowent', 'match': r'\b(?:' + alt(clause) + r')\b'},
        {'name': 'storage.type.lowent', 'match': r'\b(?:' + alt(types) + r')\b(?!\.)'},
        {'name': 'support.function.core.lowent', 'match': r'\b(?:' + alt(ops) + r')\b(?!\.)'},
        {'match': r'\b(' + NAME + r')(\.)(?=\s|$|\))',
         'captures': {'1': {'name': 'variable.other.lowent'}, '2': {'name': 'punctuation.terminator.stop.lowent'}}},
        {'name': 'entity.name.namespace.path.lowent', 'match': r'\b' + NAME + r'(?:\.[A-Za-z0-9_]+)+'},
        {'name': 'punctuation.terminator.stop.lowent', 'match': r'(?<=\s|^)\.(?=\s|$|\))'},
        {'name': 'keyword.operator.lowent', 'match': r'(?<=\s)[+\-*/](?=\s)'},
    ],
}
want = json.dumps(grammar, indent=2, ensure_ascii=False) + '\n'
if '--check' in sys.argv:
    if not OUT.exists() or OUT.read_text(encoding='utf-8') != want:
        sys.exit('gen-syntax: syntaxes/lowent.tmLanguage.json is stale — run scripts/gen-syntax.py')
    print(f'gen-syntax: ok — {len(keywords)} keywords · {len(ops)} core ops')
else:
    OUT.write_text(want, encoding='utf-8')
    print(f'gen-syntax: wrote {OUT.name} — {len(keywords)} keywords · {len(ops)} core ops')
