// Drives the server over its own protocol: opens one clean file and one with a missing stop,
// and checks what comes back.  Needs a built compiler (LOWENTC or ../../impl/build/lowentc).
'use strict';
const { spawn } = require('child_process');
const fs = require('fs'), os = require('os'), path = require('path');
const { pathToFileURL } = require('url');

const lowentc = process.env.LOWENTC || path.join(__dirname, '..', '..', 'impl', 'build', 'lowentc');
if (!fs.existsSync(lowentc)) { console.log(`lowent-lsp test: SKIP — no compiler at ${lowentc}`); process.exit(3); }
const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'lowlsp-'));
const good = path.join(dir, 'good.low'), bad = path.join(dir, 'bad.low');
fs.writeFileSync(good, 'module good .\nexport fn f input a u64 . output u64 . do\n  return add a. 1 . .\nend .\n');
fs.writeFileSync(bad, 'module bad .\nexport fn f input a u64 . output u64 . do\n  let k u64 add a. 1 .\n  return k. .\nend .\n');

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
    if (got['good.low'] && got['bad.low']) finish();
  }
});
function finish() {
  const b = got['bad.low'], g = got['good.low'];
  const ok = g.length === 0 && b.length === 1 && b[0].code === 'E-DOT-MISSING' && b[0].range.start.line === 3 && b[0].source === 'lowentc';
  console.log(ok ? 'lowent-lsp test: ok — a clean file gets no diagnostics; a missing stop comes back as E-DOT-MISSING on its line'
                 : `lowent-lsp test: FAIL — good=${JSON.stringify(g)} bad=${JSON.stringify(b)}`);
  say({ jsonrpc: '2.0', id: 2, method: 'shutdown' }); say({ jsonrpc: '2.0', method: 'exit' });
  fs.rmSync(dir, { recursive: true, force: true });
  process.exit(ok ? 0 : 1);
}
say({ jsonrpc: '2.0', id: 1, method: 'initialize', params: { initializationOptions: { lowentc } } });
say({ jsonrpc: '2.0', method: 'initialized', params: {} });
for (const f of [good, bad]) say({ jsonrpc: '2.0', method: 'textDocument/didOpen', params: { textDocument: { uri: pathToFileURL(f).href, languageId: 'lowent', version: 1, text: fs.readFileSync(f, 'utf8') } } });
setTimeout(() => { console.log('lowent-lsp test: FAIL — no answer within 20 s'); process.exit(1); }, 20000);
