#!/usr/bin/env node
// lowent-lsp — a small language server for Lowent.
//
// What it does:
//   diagnostics  when a .low file is opened or saved, it runs the compiler
//                (`lowentc --check --diag-json <file>`) and publishes what the compiler said.
//   hover        on a keyword or a builtin op, it shows the specification's line for that word.
//   completion   it offers the keywords and the builtin ops.
//   pairs        on `do` or `end`, it highlights the other half of that block (documentHighlight).
//   quick fix    on E-DOT-MISSING, it offers «insert `.`» right after the word before the place the compiler
//                names — only when the compiler, given the text with that one stop, has nothing left to say
//                about the surface (so the place is the only one a single stop can fix). RFC-0150 §8-9.
// The compiler and the specification's tables (words.json, written by scripts/gen-words.py)
// are the only sources here: this server adds no analysis of its own.
//
// What it does not do: hover or completion for names a program defines, go-to-definition,
// or diagnostics for text that has not been saved (the compiler reads the file on disk,
// because a file names its dependencies by path). The quick fix is checked with the edited text
// on standard input (`--text-stdin`), so dependencies still resolve from the file's folder.
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

// Words with their places, skipping comments (`rem` to the end of the line), strings ("…", r"…" with "" inside),
// character literals ('…') and text literals (`text TERM` … a line that is TERM alone).  Enough to pair blocks;
// the compiler stays the judge of everything else.
function scan(text) {
  const out = [];
  const lines = text.split('\n');
  let textTerm = null;
  for (let ln = 0; ln < lines.length; ln++) {
    const s = lines[ln];
    if (textTerm !== null) { if (s === textTerm) textTerm = null; continue; }
    for (let i = 0; i < s.length;) {
      const ch = s[i];
      if (ch === '"' || ((ch === 'r') && s[i + 1] === '"' && !/[A-Za-z0-9_]/.test(s[i - 1] || ''))) {
        const raw = ch === 'r'; i += raw ? 2 : 1;
        while (i < s.length) {
          if (!raw && s[i] === '\\') { i += 2; continue; }
          if (s[i] === '"') { if (raw && s[i + 1] === '"') { i += 2; continue; } i++; break; }
          i++;
        }
        continue;
      }
      if (ch === "'") { i++; while (i < s.length && s[i] !== "'") i += s[i] === '\\' ? 2 : 1; i++; continue; }
      if (/[A-Za-z_]/.test(ch)) {
        let j = i; while (j < s.length && /[A-Za-z0-9_]/.test(s[j])) j++;
        const w = s.slice(i, j);
        if (w === 'rem') break;
        if (w === 'text') { const m = /^\s+([A-Za-z_][A-Za-z0-9_]*)/.exec(s.slice(j)); if (m) { textTerm = m[1]; break; } }
        out.push({ w, line: ln, a: i, b: j });
        i = j; continue;
      }
      if (/\S/.test(ch)) out.push({ w: ch, line: ln, a: i, b: i + 1 });
      i++;
    }
  }
  return out;
}

function pairs(uri, pos) {
  const toks = scan(texts.get(uri) || '');
  const at = toks.findIndex((t) => t.line === pos.line && t.a <= pos.character && pos.character <= t.b && (t.w === 'do' || t.w === 'end'));
  if (at < 0) return [];
  const stack = [];
  let mate = -1;
  for (let i = 0; i < toks.length && mate < 0; i++) {
    if (toks[i].w === 'do') stack.push(i);
    else if (toks[i].w === 'end' && stack.length) { const o = stack.pop(); if (o === at) mate = i; else if (i === at) mate = o; }
  }
  if (mate < 0) return [];
  const r = (t) => ({ range: { start: { line: t.line, character: t.a }, end: { line: t.line, character: t.b } }, kind: 1 });
  return [r(toks[at]), r(toks[mate])];
}

// «insert `.`» — after the last word before the place the compiler named.
function dotFixes(uri, diags, reply) {
  let file;
  try { file = fileURLToPath(uri); } catch (_) { reply([]); return; }
  const text = texts.get(uri) || '';
  const toks = scan(text);
  const lines = text.split('\n');
  const todo = (diags || []).filter((d) => d.code === 'E-DOT-MISSING');
  if (!todo.length) { reply([]); return; }
  const actions = [];
  let left = todo.length;
  const done = () => { if (--left === 0) reply(actions); };
  for (const d of todo) {
    const s = d.range.start;
    const before = toks.filter((t) => t.line < s.line || (t.line === s.line && t.b <= s.character));
    if (!before.length) { done(); continue; }
    const last = before[before.length - 1];
    const off = lines.slice(0, last.line).reduce((n, l) => n + l.length + 1, 0) + last.b;
    const edited = text.slice(0, off) + ' .' + text.slice(off);
    const child = execFile(lowentc, ['--check', '--diag-json', '--text-stdin', file], { cwd: path.dirname(file), timeout: 60000, maxBuffer: 16 << 20 },
      (err, stdout, stderr) => {
        const left_over = String(stderr).split('\n').filter((l) => l.startsWith('{')).map((l) => { try { return JSON.parse(l); } catch (_) { return {}; } })
          .filter((r) => r.rule && (r.phase === 'surface' || r.phase === 'parse' || r.phase === 'lex') && (!r.file || path.resolve(r.file) === path.resolve(file)));
        if (!(err && err.code === 'ENOENT') && left_over.length === 0)
          actions.push({ title: 'Insert `.` here', kind: 'quickfix', diagnostics: [d], isPreferred: true,
            edit: { changes: { [uri]: [{ range: { start: { line: last.line, character: last.b }, end: { line: last.line, character: last.b } }, newText: ' .' }] } } });
        done();
      });
    child.stdin.end(edited);
  }
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
        capabilities: { textDocumentSync: { openClose: true, change: 1, save: { includeText: false } }, hoverProvider: true, completionProvider: {},
                        documentHighlightProvider: true, codeActionProvider: { codeActionKinds: ['quickfix'] } },
        serverInfo: { name: 'lowent-lsp', version: '1.16.0' },
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
    case 'textDocument/documentHighlight':
      send({ jsonrpc: '2.0', id: msg.id, result: pairs(msg.params.textDocument.uri, msg.params.position) });
      break;
    case 'textDocument/codeAction':
      dotFixes(msg.params.textDocument.uri, msg.params.context && msg.params.context.diagnostics,
        (actions) => send({ jsonrpc: '2.0', id: msg.id, result: actions }));
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
