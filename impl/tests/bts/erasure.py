"""BTS `range_erasure` — the examples translated to Lowent, and the compiler's remaining-check list read back (RFC-0157 §8-2 · §8-6 ·
W-20261116-0607).

Each example becomes one op `bts_e` (inputs = the example's params; a callee's result becomes an input `<name>_in` fed through an op whose
`ensures` is the callee's). Each check is one operation of the body, and the compiler says which checks REMAIN: `--ir` lists, per op,
`남은 자리(…): <ir index>:<op>` — the IR op names a kind (`add` · `sub` · `mul` · `neg` · `abs` → ovf, a narrowing cast → nar, `div` · `mod` →
div, `shl` · `shr` → shc, an index → idx). Judging (harness protocol §… `range_erasure`): a `remove` check must not be in the list, a `keep`
check must be in it and must trap on its `fires_on` input.
"""

TY = {'u8': 'u8', 'u16': 'u16', 'u32': 'u32', 'u64': 'u64', 'i8': 'i8', 'i16': 'i16', 'i32': 'i32', 'i64': 'i64', 'usize': 'usize',
      'slice': 'slice u8'}
BIN = {'add': 'add', 'sub': 'sub', 'mul': 'mul', 'div': 'div', 'rem': 'mod', 'and': 'bit_and', 'or': 'bit_or', 'xor': 'bit_xor',
       'shl': 'shl', 'shr': 'shr', 'lt': 'lt', 'le': 'le', 'ge': 'ge', 'gt': 'gt', 'ne0': 'ne'}
KIND = {'add': 'ovf', 'sub': 'ovf', 'mul': 'ovf', 'neg': 'ovf', 'abs': 'ovf', 'narrow': 'nar', 'div': 'div', 'rem': 'div',
        'shl': 'shc', 'idx': 'idx'}


class Tr:
    def __init__(self, ex):
        self.ex = ex
        self.types = {n: t for n, t in ex['params']}
        for n, t, _ in ex.get('calls', []):
            self.types[n] = t
        self.n = 0
        self.alias = {}          # a `let` that rebinds a name gets a fresh Lowent name (the namespace has no shadowing)

    def ty_of(self, e):
        if isinstance(e, str):
            return self.types.get(e, 'u64')
        k = e[0]
        if k == 'lit':
            return e[2]
        if k == 'widen':
            return e[1]
        if k == 'narrow':
            return e[2]
        if k in ('lt', 'le', 'ge', 'gt', 'ne0'):
            return 'bool'
        if k == 'len':
            return 'usize'
        if k == 'idx':
            return 'u8'
        args = [a for a in e[1:] if not (isinstance(a, str) and a[:1] in 'cs' and a[1:].isdigit())]
        return self.ty_of(args[0])

    def expr(self, e):
        if isinstance(e, str):
            return f'{self.alias.get(e, e)}.'
        k = e[0]
        if k == 'lit':
            return str(e[1])
        if k == 'len':
            return f'len {e[1]}. .'
        if k == 'widen':
            return f'widen {e[1]} {self.expr(e[2])} .'
        if k == 'not':
            return f'bit_not {self.expr(e[1])} .'
        if k in ('neg', 'abs'):
            return f'{k} {self.expr(e[2])} .'
        if k == 'narrow':
            return f'narrow {e[2]} {self.expr(e[3])} .'
        if k == 'idx':
            return f'idx {self.expr(e[2])} {self.expr(e[3])} .'
        if k in BIN:
            args = [a for a in e[1:] if not (isinstance(a, str) and a[:1] in 'cs' and a[1:].isdigit())]
            if k == 'ne0':
                args = args[:1] + [0 if not isinstance(args[1], list) else args[1]]
            return f'{BIN[k]} ' + ' '.join(self.expr(a) if not isinstance(a, int) else str(a) for a in args) + ' .'
        raise ValueError(k)

    def stmt(self, s, out, pad, last):
        k = s[0]
        if k == 'eval':
            t = self.ty_of(s[1])
            if last:
                out.append(f'{pad}return {self.expr(s[1])} .')
                return t
            self.n += 1
            out.append(f'{pad}let _v{self.n} {TY.get(t, t)} {self.expr(s[1])} .')
        elif k == 'let':
            t = self.ty_of(s[2])
            rhs = self.expr(s[2])                       # the right side reads the old binding
            nm = s[1] if s[1] not in self.types else f'{s[1]}_{self.n + 1}'
            self.n += 1
            self.types[s[1]] = t
            self.alias[s[1]] = nm
            out.append(f'{pad}let {nm} {TY.get(t, t)} {rhs} .')
        elif k == 'guard':
            out.append(f'{pad}guard {self.expr(s[1])} else return {self.zero} . .')
        elif k == 'for':
            out.append(f'{pad}repeat {s[1]} usize {self.expr(s[2])} do')
            for q, b in enumerate(s[3]):
                self.stmt(b, out, pad + '  ', False)
            out.append(f'{pad}end .')
        return None

    def source(self):
        ex = self.ex
        lines = []
        head = [f'input {n} {TY[t]} .' for n, t in ex['params']]
        pre = []
        for i, (n, t, ens) in enumerate(ex.get('calls', [])):
            cl = ' '.join(f'ensures {c[0]} ret. {c[2]} . .' for c in ens)
            bound = max(c[2] for c in ens)
            lines.append(f'fn bts_src{i} input v {TY[t]} . output {TY[t]} . {cl} do return min v. {bound} . . end .')
            head.append(f'input {n}_in {TY[t]} .')
            pre.append(f'  let {n} {TY[t]} bts_src{i} {n}_in. . .')
        req = []
        for r in ex.get('requires', []):
            if r[0] == 'mask':
                req.append(f'requires eq bit_and {r[1]}. {r[2][0]} . {r[2][1]} . .')
            elif self.types.get(r[1]) == 'slice':
                req.append(f'requires {r[0]} len {r[1]}. . {r[2]} . .')
            else:
                req.append(f'requires {r[0]} {r[1]}. {r[2]} . .')
        body = ex['body']
        # the op's result is the last eval's type
        last = [s for s in body if s[0] == 'eval'][-1] if any(s[0] == 'eval' for s in body) else None
        out_t = self.ty_of(last[1]) if last else 'u8'
        self.zero = '0'
        out = []
        for j, s in enumerate(body):
            if s[0] == 'for':
                self.stmt(s, out, '  ', False)
            else:
                self.stmt(s, out, '  ', s is last and j == len(body) - 1)
        if not (body and body[-1][0] == 'eval'):
            out.append('  return 0 .')
        txt = '\n'.join(out)
        lines.append(f'fn bts_e {" ".join(head)} output {TY.get(out_t, out_t)} . ' + ' '.join(req) + ' do')
        lines += pre + [txt, 'end .']
        return '\n'.join(lines)


def translate(ex):
    return Tr(ex).source()


def kinds_left(ir_text):
    """the kinds of the checks the compiler kept in op `bts_e` — read from `--ir`'s remaining-check line"""
    import re
    left = []
    for line in ir_text.splitlines():
        m = re.search(r'남은 자리\([^)]*\): (.*)$', line)
        if not m:
            continue
        for item in m.group(1).split(','):
            it = item.strip().split(':', 1)[-1]
            left.append(it.rsplit('/', 1)[-1] if '/' in it else it.split('.', 1)[0])
    return left


def kind_of_irop(op):
    if op in ('ovf', 'div', 'nar', 'shc', 'idx'):
        return op
    if op in ('add', 'sub', 'mul', 'neg', 'abs'):
        return 'ovf'
    if op in ('cast', 'narrow'):
        return 'nar'
    if op in ('div', 'mod', 'rem'):
        return 'div'
    if op in ('shl', 'shr'):
        return 'shc'
    if op in ('index', 'idx', 'load', 'elem'):
        return 'idx'
    return op
