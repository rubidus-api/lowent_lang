// VS Code client for Lowent: starts tools/lowent-lsp/server.js and hands it the compiler's path.
'use strict';
const path = require('path');
const vscode = require('vscode');
const { LanguageClient, TransportKind } = require('vscode-languageclient/node');

let client;

function activate(context) {
  const cfg = vscode.workspace.getConfiguration('lowent');
  const server = cfg.get('serverPath') || context.asAbsolutePath(path.join('..', 'lowent-lsp', 'server.js'));
  client = new LanguageClient('lowent', 'Lowent',
    { command: process.execPath, args: [server], transport: TransportKind.stdio, options: { env: { ...process.env, ELECTRON_RUN_AS_NODE: '1' } } },
    { documentSelector: [{ scheme: 'file', language: 'lowent' }],
      initializationOptions: { lowentc: cfg.get('compilerPath') || 'lowentc' } });
  client.start();
}

function deactivate() { return client ? client.stop() : undefined; }

module.exports = { activate, deactivate };
