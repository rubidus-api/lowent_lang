; Highlights for Lowent 1.8

(line_comment) @comment
(block_comment) @comment

(string) @string
(char) @string
(text_literal) @string
(escape) @string.escape
(number) @number
(boolean) @constant.builtin
(none) @constant.builtin

(stop) @punctuation.delimiter
["(" ")"] @punctuation.bracket
(operator) @operator

[
  "module" "use" "from" "as" "package" "build"
  "def" "type" "newtype" "struct" "enum" "trait" "actor" "state" "contract" "test"
  "fn" "proc" "export" "extern" "unsafe"
  "let" "var" "set" "return" "break" "continue" "guard" "expect" "drop"
  "if" "else" "while" "for" "repeat" "range" "cycle" "next" "step"
  "match" "case" "region" "borrow" "task_group"
  "do" "end" "lit" "expr" "pipe" "try" "send" "spawn"
] @keyword

[
  "input" "output" "effects" "requires" "ensures" "errors" "tests" "satisfies"
  "comptime" "assume" "static" "debug" "keep"
] @keyword.modifier

[ "field" "method" "payload" "call_builtin" "alloc_bytes" "pop" "view" "stack_new" "channel" ] @function.builtin
(core_op) @function.builtin

(module_declaration name: (name) @module)
(use_declaration name: (name) @module)

(op_declaration name: (_) @function)
(extern_declaration name: (_) @function)
(signature name: (_) @function)
(test_declaration name: (name) @function)

(type_declaration name: (name) @type.definition)
(struct_declaration name: (name) @type.definition)
(enum_declaration name: (name) @type.definition)
(trait_declaration name: (name) @type.definition)
(actor_declaration name: (name) @type.definition)
(contract_declaration name: (name) @type.definition)

(type_name) @type
(applied_type) @type

(field_declaration name: (name) @property)
(field_init name: (name) @property)
(variant name: (name) @constant)

(binding name: (name) @variable)
(clause name: (name) @variable.parameter)

; a form with arguments is a call; a name closed at once is a value
(call head: (_) @function.call (_))
(call head: (name) @variable . (stop))
(call head: (path) @constant . (stop))

(send_form handler: (name) @function.method)
(method_form op: (name) @function.method)
(builtin_call builtin: (name) @function.builtin)
(stage stage: _ @function.builtin)
