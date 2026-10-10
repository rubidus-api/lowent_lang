# Editor tools for Lowent

Three tools, all built on the language's one surface rule — *a name opens, a stop closes* — and
all taking their word lists from the compiler and the specification rather than keeping their own.

| folder | what it is | checked by |
|---|---|---|
| [`tree-sitter-lowent/`](tree-sitter-lowent/) | A tree-sitter grammar: a syntax tree for highlighting, folding and structural editing | `scripts/check-corpus.py` — every `.low` file the compiler reads at the surface parses with no `ERROR` / `MISSING` node (935 of 935 today), and every file the compiler refuses there is refused by the grammar too (21 of 21) |
| [`lowent-lsp/`](lowent-lsp/) | A language server: on open and save it runs `lowentc --check --diag-json` and shows what the compiler said; on a keyword or a builtin op it shows the specification's line for that word (hover) and offers those words (completion) | `node test.js` — drives the server over the protocol, and compares `words.json` with the specification's tables |
| [`vscode-lowent/`](vscode-lowent/) | A VS Code extension: regular-expression colours (`syntaxes/lowent.tmLanguage.json`) and a client for the server | `scripts/gen-syntax.py --check` — the word lists match the specification |

## What they do not do

- The language server's diagnostics are **only what the compiler reports**, and only for the file on
  disk (a file names its dependencies by path, so unsaved text is not checked). Hover and completion
  know the 49 keywords and the 231 builtin ops — not the names a program defines. The hover text is
  the specification's own line, which is in Korean. There is no go-to-definition.
- The tree-sitter grammar is looser than the compiler in places — it is a reader for editors, not a
  second definition of the language. The normative grammar is Annex A.10 of the specification.
- Nothing here is published to an extension marketplace or a package registry.

## Using them

```sh
# tree-sitter: generate the parser and parse a file
cd tools/tree-sitter-lowent && npm install && npx tree-sitter generate && npx tree-sitter parse ../../lib/str.low

# language server: any editor that speaks LSP can start it
LOWENTC=/path/to/lowentc node tools/lowent-lsp/server.js

# VS Code: open tools/vscode-lowent in VS Code, run `npm install`, press F5 (Extension Development Host)
```

## Keeping them in step with the language

```sh
python3 tools/tree-sitter-lowent/scripts/update-shapes.py   # after the compiler's shape table changes
python3 tools/vscode-lowent/scripts/gen-syntax.py           # after the keyword or core-op list changes
python3 tools/tree-sitter-lowent/scripts/check-corpus.py    # the grammar against the whole corpus
```
