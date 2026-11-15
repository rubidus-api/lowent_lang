"""BTS `programs` — the shared program notation (refmodel/progmodel.py) translated to Lowent source (RFC-0157 §8-6 · W-20261115-1055).

Each example is one op `bts_prog` in its own module, after a fixed prelude that gives the notation's external functions a Lowent body.
`translate(example)` gives (source, None), or (None, (skip status, reason)) when the notation names something Lowent does not have:

  language-policy   decided not to have it — `secret` types and `declassify` (RFC-0156 §8-11: the time classes are omitted);
                    borrowing one field of a struct (canon §8.4 (4): a borrow is of the whole value, E-BORROW-FIELD)
  not-implemented   decided but not built, or not yet given a spelling — raw pointers and their operations (`*T`, `offset`,
                    `diff`, `with_addr`, `device_ptr`, `deref` of a pointer, `uaddr`: canon §6.9.4 `raw` · `addr`), arenas whose
                    reset waits for their blocks (`Arena`)
  target-unavailable  the example names a target other than the one this run is for (harness protocol §2)

How the rest maps (one choice each; the adapter never decides a verdict — the compiler does):
  param that is written or borrowed for writing   →  `input x0 T .` + `var x T x0. .`   (a Lowent input cannot be written)
  owned_ptr(u8,static)                            →  `owned box` (`type box u8 .`) · a local owned value is a `var`
  File                                            →  `owned xfile`; `open` returns one, `close` takes it and returns a `result`
                                                     (canon §8.5 (4): a value whose ending can fail needs completion)
  ref T · mutref T                                →  `ref T` · `mut_ref T`;  ["ref", x] → `ref x .` · ["mutref", x] → `mut_ref x .`
  slice(T) · slice_mut(T)                         →  `slice T` · `mut slice T`; ["slice", a] → `a.`; ["store", s, i, v] → `set idx s. i. . v. .`
  array(u8,n) param                               →  a `var` array of that length (so that a write view can be taken)
  ["set", a, <array literal>]                     →  `set idx a. 0 . 0 .` — a write to the array (Lowent has no whole-array assignment)
  ["do", call]                                    →  `let _dN <result type> call .` (a `result` is then dropped: `drop _dN. .`)
  ops: lt le eq add sub mul shl, disposition wrap → `wrap_*`, trap or none → the plain word (which stops on overflow)
  struct P {packed} → `layout packed .`
"""
import json

PRELUDE = '''type box u8 .
struct xfile do fd u32 . end .
enum xerr do bad . end .
fn x_consume_box input b owned box . output u32 . do drop b. . return 0 . end .
fn x_open output owned xfile . do return lit xfile do fd 1 . end . . end .
fn x_close input f owned xfile . output result u32 xerr . do drop f. . return ok 0 . . end .
fn x_use_ref input r ref u8 . output u32 . do return widen u32 deref r. . . . end .
fn x_use_mut input r mut_ref u8 . output u32 . do return widen u32 deref r. . . . end .
fn x_send input v u32 . output u32 . do return v. . end .
fn x_use_u8 input v u8 . output u32 . do return widen u32 v. . . end .
fn x_sum input s slice u8 . output u32 . do
  var t u32 0 .
  for x s. do set t. wrap_add t. widen u32 x. . . . end .
  return t. .
end .
proc x_store_masked input s mut slice u8 . input v vec 16 u8 . input n usize . output u32 . effects none . do
  guard gt len s. . 0 . else return 0 . .
  set idx s. 0 . 0 .
  return 0 .
end .'''

EXTERN = {  # notation name: (Lowent op, result type)
    'consume_box': ('x_consume_box', 'u32'), 'open': ('x_open', 'owned xfile'), 'close': ('x_close', 'result u32 xerr'),
    'use_ref': ('x_use_ref', 'u32'), 'use_mut': ('x_use_mut', 'u32'), 'send': ('x_send', 'u32'), 'use_u8': ('x_use_u8', 'u32'),
    'sum': ('x_sum', 'u32'), 'store_masked': ('x_store_masked', 'u32'),
}
OPS = {'lt': 'lt', 'le': 'le', 'gt': 'gt', 'ge': 'ge', 'eq': 'eq', 'ne': 'ne', 'add': 'add', 'sub': 'sub', 'mul': 'mul', 'shl': 'shl'}


class Untranslatable(Exception):
    def __init__(self, status, why):
        self.status, self.why = status, why


def ty(t):
    t = t.strip()
    if 'secret' in t:
        raise Untranslatable('language-policy', 'secret types are omitted (RFC-0156 §8-11)')
    if '@program' in t:
        raise Untranslatable('target-unavailable', 'program-space references are the AVR target\'s')
    if t.startswith('*') or t == 'uaddr':
        raise Untranslatable('not-implemented', 'raw pointers and addresses: `raw` · `addr` are decided but not built (canon §6.9.4)')
    if t == 'Arena' or 'arena)' in t:
        raise Untranslatable('not-implemented', 'an arena whose reset waits for its blocks has no Lowent spelling yet')
    if t in ('u8', 'u16', 'u32', 'u64', 'i8', 'i16', 'i32', 'i64', 'bool', 'usize', 'idiff'):
        return t
    if t.startswith('ref '):
        return 'ref ' + ty(t[4:])
    if t.startswith('mutref '):
        return 'mut_ref ' + ty(t[7:])
    if t.startswith('owned_ptr('):
        return 'owned box'
    if t == 'File':
        return 'owned xfile'
    if t.startswith('slice_mut('):
        return 'mut slice ' + ty(t[10:-1])
    if t.startswith('slice('):
        return 'slice ' + ty(t[6:-1])
    if t.startswith('array('):
        e, n = t[6:-1].rsplit(',', 1)
        return f'array {n.strip()} {ty(e)}'
    if t.startswith('simd('):
        e, n = t[5:-1].rsplit(',', 1)
        return f'vec {n.strip()} {ty(e)}'
    if t.startswith('struct '):
        return t[7:]
    raise Untranslatable('not-implemented', f'type `{t}` has no mapping in this adapter')


class Fn:
    def __init__(self, fn):
        self.fn, self.n = fn, 0
        self.types = {p[0]: p[1] for p in fn.get('params', [])}

    def place(self, p):
        if '.' in p:
            raise Untranslatable('language-policy', 'a borrow is of the whole value — one field is not borrowed apart (canon §8.4 (4))')
        return p

    def expr(self, e):
        k = e[0]
        if k == 'var':
            return f'{e[1]}.'
        if k == 'lit':
            if e[2].startswith('array('):
                raise Untranslatable('not-implemented', 'an array literal value')
            return e[1]
        if k == 'op':
            name, disp, args = e[1], e[2], e[3:]
            if name not in OPS:
                raise Untranslatable('not-implemented', f'operation `{name}`')
            w = OPS[name]
            if disp == 'wrap' and name in ('add', 'sub', 'mul', 'shl'):
                w = 'wrap_' + w
            return f'{w} ' + ' '.join(self.expr(a) for a in args) + ' .'
        if k == 'index':
            return f'idx {self.expr(e[1])} {self.expr(e[2])} .'
        if k == 'field':
            return f'field {self.expr(e[1])} {e[2]} .'
        if k == 'ref':
            return f'ref {self.place(e[1])} .'
        if k == 'mutref':
            return f'mut_ref {self.place(e[1])} .'
        if k == 'slice':
            return f'{self.place(e[1])}.'
        if k == 'slice_mut':
            return f'{self.place(e[1])}.'
        if k == 'deref':
            if self.types.get(e[1][1], '').startswith('*') if e[1][0] == 'var' else False:
                raise Untranslatable('not-implemented', 'deref of a raw pointer (canon §6.9.4)')
            return f'deref {self.expr(e[1])} .'
        if k == 'call':
            if e[1] not in EXTERN:
                raise Untranslatable('not-implemented', f'external `{e[1]}`')
            op = EXTERN[e[1]][0]
            return f'{op} ' + ' '.join(self.expr(a) for a in e[2:]) + ' .' if len(e) > 2 else f'{op} .'
        if k == 'declassify':
            raise Untranslatable('language-policy', 'secret types are omitted (RFC-0156 §8-11)')
        if k in ('offset', 'diff', 'with_addr', 'device_ptr', 'addr'):
            raise Untranslatable('not-implemented', 'raw pointers and addresses: `raw` · `addr` are decided but not built (canon §6.9.4)')
        if k == 'cast':
            ty(e[2])
            return f'cast {ty(e[2])} {self.expr(e[1])} .'
        if k == 'query':
            raise Untranslatable('target-unavailable', 'the query example is for another target')
        raise Untranslatable('not-implemented', f'expression `{k}`')

    def stmt(self, s, out, ind):
        k = s[0]
        pad = '  ' * ind
        if k == 'let':
            t = ty(s[2])
            kw = 'var' if (t.startswith('owned') or s[1] in self.written) else 'let'
            self.types[s[1]] = s[2]
            out.append(f'{pad}{kw} {s[1]} {t} {self.expr(s[3])} .')
        elif k == 'set':
            if s[2][0] == 'lit' and s[2][2].startswith('array('):
                out.append(f'{pad}set idx {s[1]}. 0 . 0 .')
            else:
                out.append(f'{pad}set {s[1]}. {self.expr(s[2])} .')
        elif k == 'store':
            out.append(f'{pad}set idx {self.expr(s[1])} {self.expr(s[2])} . {self.expr(s[3])} .')
        elif k == 'do':
            if s[1][0] != 'call':
                raise Untranslatable('not-implemented', 'a statement that is not a call')
            self.n += 1
            rt = EXTERN.get(s[1][1], (None, 'u32'))[1]
            out.append(f'{pad}let _d{self.n} {rt} {self.expr(s[1])} .')
            if rt.startswith('result'):
                out.append(f'{pad}drop _d{self.n}. .')
        elif k == 'return':
            out.append(f'{pad}return {self.expr(s[1])} .')
        elif k == 'if':
            out.append(f'{pad}if {self.expr(s[1])} do')
            self.block(s[2], out, ind + 1)
            if s[3]:
                out.append(f'{pad}end else do')
                self.block(s[3], out, ind + 1)
            out.append(f'{pad}end .')
        elif k == 'while':
            out.append(f'{pad}while {self.expr(s[1])} do')
            self.block(s[2], out, ind + 1)
            out.append(f'{pad}end .')
        elif k == 'unsafe':
            raise Untranslatable('not-implemented', 'an `unsafe` block around raw-pointer or packed-field operations (canon §6.9.4)')
        else:
            raise Untranslatable('not-implemented', f'statement `{k}`')

    def block(self, body, out, ind):
        for s in body:
            self.stmt(s, out, ind)

    def written_names(self):
        w = set()

        def walk(x):
            if isinstance(x, list) and x:
                if x[0] == 'set' and isinstance(x[1], str):
                    w.add(x[1].split('.')[0])
                if x[0] in ('mutref', 'slice_mut') and isinstance(x[1], str):
                    w.add(x[1].split('.')[0])
                if x[0] == 'let' and x[2].startswith(('slice_mut(', 'mutref ')):
                    w.add(x[1])
                for y in x:
                    walk(y)
        walk(self.fn.get('body', []))
        return w

    def source(self):
        self.written = self.written_names()
        lines = []
        for st in self.fn.get('structs', []):
            lines.append(f'struct {st["name"]} do')
            if st.get('packed'):
                lines.append('  layout packed .')
            for fname, ft in st['fields']:
                lines.append(f'  {fname} {ty(ft)} .')
            lines.append('end .')
        head, pre = [], []
        for name, t in self.fn.get('params', []):
            lt = ty(t)
            if lt.startswith('array '):           # a local array — a write view can be taken from it
                pre.append(f'  var {name} lit {lt} _ . .')
                continue
            if name in self.written and not lt.startswith(('mut ', 'mut_ref', 'owned')):
                head.append(f'input {name}0 {lt} .'); pre.append(f'  var {name} {lt} {name}0. .')
            else:
                head.append(f'input {name} {lt} .')
        ret = ty(self.fn['ret']) if self.fn.get('ret') else 'u32'
        body = []
        self.block(self.fn.get('body', []), body, 1)
        tail = [] if (self.fn.get('body') and self.fn['body'][-1][0] == 'return') else ['  return 0 .']
        lines.append(f'proc bts_prog {" ".join(head)} output {ret} . effects none . do'.replace('  ', ' '))
        lines += pre + body + tail + ['end .']
        return '\n'.join(lines)


def translate(ex, target):
    if ex.get('target') and ex['target'] != target:
        return None, ('target-unavailable', f'the example is for {ex["target"]}')
    try:
        return PRELUDE + '\n' + Fn(ex['fn']).source(), None
    except Untranslatable as u:
        return None, (u.status, u.why)


if __name__ == '__main__':      # print every translation (for reading)
    import pathlib, sys
    doc = json.loads((pathlib.Path(__file__).parent / 'conformance/vectors/programs.json').read_text())
    for ex in doc['examples']:
        src, why = translate(ex, 'x86_64')
        print(f'── {ex["name"]} ({ex["verdict"]}) ──')
        print(src.split(PRELUDE + '\n', 1)[1] if src else f'   skip {why[0]}: {why[1]}')
