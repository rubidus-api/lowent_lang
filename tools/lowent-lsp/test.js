// Drives the server over its own protocol: opens one clean file and one with a missing stop,
// checks the diagnostics that come back, then asks for a hover, completion, the block pair of a `do`, and quick fixes.  Needs a built compiler (LOWENTC or ../../impl/build/lowentc).
'use strict';
const { spawn } = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');
const { pathToFileURL } = require('url');

const lowentc = process.env.LOWENTC || path.join(__dirname, '..', '..', 'impl', 'build', 'lowentc');
if (!fs.existsSync(lowentc)) { console.log(`lowent-lsp test: SKIP — no compiler at ${lowentc}`); process.exit(3); }
// words.json must be what the specification's tables say today (python3 writes it; without python3 this is said, not skipped silently).
{
  const r = require('child_process').spawnSync('python3', [path.join(__dirname, 'scripts', 'gen-words.py'), '--check'], { encoding: 'utf8' });
  if (r.error) console.log('lowent-lsp test: note — python3 is not here, so words.json was not compared with the specification');
  else if (r.status !== 0) { console.log(`lowent-lsp test: FAIL — ${(r.stderr || r.stdout).trim()}`); process.exit(1); }
}
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'lowlsp-'));
const good = path.join(dir, 'good.low'), bad = path.join(dir, 'bad.low'), bad2 = path.join(dir, 'bad2.low');
fs.writeFileSync(good, 'module good .\nexport fn f input a u64 . output u64 . do\n  return add a. 1 . .\nend .\n');
fs.writeFileSync(bad, 'module bad .\nexport fn f input a u64 . output u64 . do\n  let k u64 add a. 1 .\n  return k. .\nend .\n');
fs.writeFileSync(bad2, 'module bad2 .\nexport fn f input a u64 . output u64 . do\n  return add a. 1\nend .\n');   // two stops missing — no single place

const srv = spawn(process.execPath, [path.join(__dirname, 'server.js')], { stdio: ['pipe', 'pipe', 'inherit'] });
const say = (m) => { const b = Buffer.from(JSON.stringify(m)); srv.stdin.write(`Content-Length: ${b.length}\r\n\r\n`); srv.stdin.write(b); };
const got = {};
let buf = Buffer.alloc(0);
srv.stdout.on('data', (c) => {
  buf = Buffer.concat([buf, c]);
  for (;;) {
    const h = buf.indexOf('\r\n\r\n'); if (h < 0) return;
    const len = Number(/Content-Length: *(\d+)/i.exec(buf.slice(0, h).toString())[1]);
    if (buf.length < h + 4 + len) return;
    const msg = JSON.parse(buf.slice(h + 4, h + 4 + len).toString()); buf = buf.slice(h + 4 + len);
    if (msg.method === 'textDocument/publishDiagnostics') got[path.basename(msg.params.uri)] = msg.params.diagnostics;
    if (msg.id >= 10) got[msg.id] = msg.result === undefined ? 'error' : msg.result;
    if (got['good.low'] && got['bad.low'] && got['bad2.low'] && !asked) ask();
    if ([10, 11, 12, 13, 14, 15, 16].every((i) => i in got)) finish();
  }
});
let asked = false;
function ask() {                       // good.low line 2 is `  return add a. 1 . .`
  asked = true;
  const doc = { uri: pathToFileURL(good).href };
  say({ jsonrpc: '2.0', id: 10, method: 'textDocument/hover', params: { textDocument: doc, position: { line: 2, character: 10 } } });   // on `add`
  say({ jsonrpc: '2.0', id: 11, method: 'textDocument/hover', params: { textDocument: doc, position: { line: 2, character: 13 } } });   // on `a` — a program's own name
  say({ jsonrpc: '2.0', id: 12, method: 'textDocument/hover', params: { textDocument: doc, position: { line: 2, character: 3 } } });    // on `return`
  say({ jsonrpc: '2.0', id: 13, method: 'textDocument/completion', params: { textDocument: doc, position: { line: 2, character: 10 } } });
  say({ jsonrpc: '2.0', id: 14, method: 'textDocument/documentHighlight', params: { textDocument: doc, position: { line: 1, character: 40 } } });   // on `do`
  for (const [id, f] of [[15, bad], [16, bad2]])
    say({ jsonrpc: '2.0', id, method: 'textDocument/codeAction', params: { textDocument: { uri: pathToFileURL(f).href },
      range: got[path.basename(f)][0].range, context: { diagnostics: got[path.basename(f)] } } });
}
function finish() {
  const b = got['bad.low'], g = got['good.low'];
  const diag = g.length === 0 && b.length === 1 && b[0].code === 'E-DOT-MISSING' && b[0].range.start.line === 3 && b[0].source === 'lowentc';
  const h = got[10], labels = new Set(((got[13] && got[13].items) || []).map((i) => i.label));
  const hov = h && h.contents && h.contents.value.startsWith('**add** — builtin op') && h.range.start.character === 9 && h.range.end.character === 12
    && got[11] === null && got[12] && got[12].contents.value === '**return** — keyword';
  const comp = labels.has('add') && labels.has('while') && labels.has('payload') && !labels.has('a');
  const hl = Array.isArray(got[14]) && got[14].length === 2 && got[14][1].range.start.line === 3 && got[14][1].range.start.character === 0;
  const a15 = got[15], a16 = got[16];
  const fix = Array.isArray(a15) && a15.length === 1 && a15[0].kind === 'quickfix'
    && JSON.stringify(Object.values(a15[0].edit.changes)[0][0]) === JSON.stringify({ range: { start: { line: 2, character: 22 }, end: { line: 2, character: 22 } }, newText: ' .' })
    && Array.isArray(a16) && a16.length === 0;
  const ok = diag && hov && comp && hl && fix;
  console.log(ok ? 'lowent-lsp test: ok — a clean file gets no diagnostics; a missing stop comes back as E-DOT-MISSING on its line; hover and completion answer from the specification\'s tables; `do` highlights its `end`; one missing stop gets «insert `.`» and two get none'
                 : `lowent-lsp test: FAIL — diag=${diag} hover=${hov} completion=${comp} pairs=${hl} fix=${fix} a15=${JSON.stringify(a15)} a16=${JSON.stringify(a16)} h14=${JSON.stringify(got[14])} good=${JSON.stringify(g)} bad=${JSON.stringify(b)} h10=${JSON.stringify(got[10])} h11=${JSON.stringify(got[11])} h12=${JSON.stringify(got[12])}`);
  say({ jsonrpc: '2.0', id: 2, method: 'shutdown' }); say({ jsonrpc: '2.0', method: 'exit' });
  fs.rmSync(dir, { recursive: true, force: true });
  process.exit(ok ? 0 : 1);
}
say({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { initializationOptions: { lowentc } } });
say({ jsonrpc: '2.0', method: 'initialized', params: {} });
for (const f of [good, bad, bad2]) say({ jsonrpc: '2.0', method: 'textDocument/didOpen', params: { textDocument: { uri: pathToFileURL(f).href, languageId: 'lowent', version: 1, text: fs.readFileSync(f, 'utf8') } } });
setTimeout(() => { console.log('lowent-lsp test: FAIL — no answer within 20 s'); process.exit(1); }, 20000);
