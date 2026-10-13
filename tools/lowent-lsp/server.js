#!/usr/bin/env node
// lowent-lsp — a small language server for Lowent.
//
// What it does:
//   diagnostics  when a .low file is opened or saved, it runs the compiler
//                (`lowentc --check --diag-json <file>`) and publishes what the compiler said.
//   hover        on a keyword or a builtin op, it shows the specification's line for that word.
//   completion   it offers the keywords and the builtin ops.
// The compiler and the specification's tables (words.json, written by scripts/gen-words.py)
// are the only sources here: this server adds no analysis of its own.
//
// What it does not do: hover or completion for names a program defines, go-to-definition,
// or diagnostics for text that has not been saved (the compiler reads the file on disk,
// because a file names its dependencies by path).
//
// The compiler is found, in this order: the `lowentc` initialization option,
// the LOWENTC environment variable, `lowentc` on PATH.
// No dependencies: the JSON-RPC framing of the protocol is written out below.
'use strict';
const { execFile } = require('child_process');
const path = require('path');
const { fileURLToPath } = require('url');
const words = require('./words.json');

let lowentc = process.env.LOWENTC || 'lowentc';
let buffer = Buffer.alloc(0);
const texts = new Map();                                              // uri -> the editor's current text

function send(msg) {
  const body = Buffer.from(JSON.stringify(msg), 'utf8');
  process.stdout.write(`Content-Length: ${body.length}\r\n\r\n`);
  process.stdout.write(body);
}
function log(text) { send({ jsonrpc: '2.0', method: 'window/logMessage', params: { type: 3, message: `lowent-lsp: ${text}` } }); }

// One compiler record -> one editor diagnostic.  Lines and columns are 1-based in the
// record and 0-based in the protocol; column 0 means "the whole line".
function toDiagnostic(rec) {
  const line = Math.max(0, ((rec.span && rec.span.line) || 1) - 1);
  const col = (rec.span && rec.span.col) || 0;
  const range = col > 0
    ? { start: { line, character: col - 1 }, end: { line, character: col } }
    : { start: { line, character: 0 }, end: { line, character: 1000 } };
  return {
    range,
    severity: rec.sev === 'warning' ? 2 : 1,
    code: rec.rule,
    source: 'lowentc',
    message: rec.repair ? `${rec.msg}  [${rec.repair}]` : rec.msg,
  };
}

function check(uri) {
  let file;
  try { file = fileURLToPath(uri); } catch (_) { return; }
  if (!file.endsWith('.low')) return;
  execFile(lowentc, ['--check', '--diag-json', file], { cwd: path.dirname(file), timeout: 60000, maxBuffer: 16 << 20 },
    (err, stdout, stderr) => {
      if (err && err.code === 'ENOENT') {
        log(`cannot run the compiler (${lowentc}) — set the "lowentc" option or the LOWENTC variable`);
        return;
      }
      const diagnostics = [];
      for (const line of String(stderr).split('\n')) {
        if (!line.startsWith('{')) continue;
        let rec;
        try { rec = JSON.parse(line); } catch (_) { continue; }
        if (!rec.rule) continue;                                      // the closing {"result":…} line
        if (rec.file && path.resolve(rec.file) !== path.resolve(file)) continue;   // a dependency's diagnostic
        diagnostics.push(toDiagnostic(rec));
      }
      send({ jsonrpc: '2.0', method: 'textDocument/publishDiagnostics', params: { uri, diagnostics } });
    });
}

// The word under a position.  A name's stop is glued on (`a.`) and is not part of the word.
function wordAt(uri, pos) {
  const line = (texts.get(uri) || '').split('\n')[pos.line] || '';
  const isW = (ch) => ch !== undefined && /[A-Za-z0-9_]/.test(ch);
  let a = pos.character, b = pos.character;
  if (!isW(line[a]) && isW(line[a - 1])) { a--; b--; }
  if (!isW(line[a])) return null;
  while (isW(line[a - 1])) a--;
  while (isW(line[b])) b++;
  return { word: line.slice(a, b), range: { start: { line: pos.line, character: a }, end: { line: pos.line, character: b } } };
}

function hover(uri, pos) {
  const w = wordAt(uri, pos);
  if (!w) return null;
  const op = Object.prototype.hasOwnProperty.call(words.ops, w.word) ? words.ops[w.word] : null;
  let value;
  if (op) {
    value = `**${w.word}** — builtin op${op.group ? ` (${op.group})` : ''}\n\n${op.meaning}`;
    if (op.form) value += `\n\n- form: \`${op.form}\`\n- takes: ${op.takes}\n- gives: ${op.gives}`;
  } else if (words.keywords.includes(w.word)) {
    value = `**${w.word}** — keyword`;
  } else return null;
  return { contents: { kind: 'markdown', value }, range: w.range };
}

let completions = null;
function completion() {
  if (!completions) {
    completions = words.keywords.map((k) => ({ label: k, kind: 14 }))
      .concat(Object.keys(words.ops).map((k) => ({ label: k, kind: 3, detail: words.ops[k].form || undefined, documentation: words.ops[k].meaning })));
  }
  return { isIncomplete: false, items: completions };
}

function handle(msg) {
  switch (msg.method) {
    case 'initialize': {
      const opt = (msg.params && msg.params.initializationOptions) || {};
      if (opt.lowentc) lowentc = opt.lowentc;
      send({ jsonrpc: '2.0', id: msg.id, result: {
        capabilities: { textDocumentSync: { openClose: true, change: 1, save: { includeText: false } }, hoverProvider: true, completionProvider: {} },
        serverInfo: { name: 'lowent-lsp', version: '1.11.0' },
      } });
      break;
    }
    case 'textDocument/didOpen':
      texts.set(msg.params.textDocument.uri, msg.params.textDocument.text || '');
      check(msg.params.textDocument.uri);
      break;
    case 'textDocument/didChange': {                                  // full-text sync: the last change is the whole text
      const ch = msg.params.contentChanges;
      if (ch && ch.length) texts.set(msg.params.textDocument.uri, ch[ch.length - 1].text);
      break;
    }
    case 'textDocument/didSave': check(msg.params.textDocument.uri); break;
    case 'textDocument/hover':
      send({ jsonrpc: '2.0', id: msg.id, result: hover(msg.params.textDocument.uri, msg.params.position) });
      break;
    case 'textDocument/completion':
      send({ jsonrpc: '2.0', id: msg.id, result: completion() });
      break;
    case 'textDocument/didClose':
      texts.delete(msg.params.textDocument.uri);
      send({ jsonrpc: '2.0', method: 'textDocument/publishDiagnostics', params: { uri: msg.params.textDocument.uri, diagnostics: [] } });
      break;
    case 'shutdown': send({ jsonrpc: '2.0', id: msg.id, result: null }); break;
    case 'exit': process.exit(0); break;
    default:
      if (msg.id !== undefined && msg.method) send({ jsonrpc: '2.0', id: msg.id, error: { code: -32601, message: 'not supported' } });
  }
}

process.stdin.on('data', (chunk) => {
  buffer = Buffer.concat([buffer, chunk]);
  for (;;) {
    const head = buffer.indexOf('\r\n\r\n');
    if (head < 0) return;
    const m = /Content-Length: *(\d+)/i.exec(buffer.slice(0, head).toString('ascii'));
    if (!m) { buffer = buffer.slice(head + 4); continue; }
    const len = Number(m[1]);
    if (buffer.length < head + 4 + len) return;
    const body = buffer.slice(head + 4, head + 4 + len).toString('utf8');
    buffer = buffer.slice(head + 4 + len);
    try { handle(JSON.parse(body)); } catch (e) { log(`bad message: ${e.message}`); }
  }
});
