/**
 * Tree-sitter grammar for Lowent, language revision 1.8.
 *
 * One rule shapes the surface: a NAME in a use position opens a form, and one
 * STOP `.` closes the innermost open form.  So an expression is read without
 * knowing how many arguments an op takes.  What still needs a table is the set
 * of positions where a name does NOT open a form (a type, a field name, a
 * defining name): statement heads, the word forms, the types, and the core ops
 * listed in shapes.json (copied from the compiler by scripts/update-shapes.py).
 *
 * The normative grammar is Annex A.10 of the specification.  This file follows
 * the compiler's surface pass (impl/src/low_closer.c) where the annex leaves a
 * shape to prose.
 */

const SHAPES = require('./shapes.json');

const sep = (rule) => repeat(rule);

// type words that take one more type after them
const T_PRE1 = ['slice', 'mut', 'owned', 'ref', 'mut_ref', 'option', 'segments', 'stack', 'set',
                'unsafe_ptr', 'nonzero', 'atomic', 'lock', 'rwlock', 'shared_read', 'view'];

const CLAUSE_WORDS = ['reduce', 'lowdoc', 'vector', 'priority', 'inplace', 'invalidates', 'absorbs',
                      'reference', 'why', 'strlen', 'link', 'variadic', 'schedule', 'access', 'parallel',
                      'tests', 'satisfies', 'effects', 'cap', 'on'];

const STAGES = ['collect', 'count', 'sort', 'take', 'skip', 'filter', 'map', 'any', 'all', 'enumerate',
                'fold', 'scan', 'zip'];

module.exports = grammar({
  name: 'lowent',

  extras: $ => [/[ \t\r\n]+/, $.line_comment, $.block_comment],

  externals: $ => [$.block_comment, $.text_literal, $._error_sentinel],

  conflicts: $ => [[$.clause, $.asm_statement]],


  rules: {
    source_file: $ => repeat($._top),

    _top: $ => choice(
      $.module_declaration, $.use_declaration, $.package_declaration, $.build_declaration,
      $._declaration, $.binding,
    ),

    // ── comments ────────────────────────────────────────────────────────────
    line_comment: _ => token(seq('rem', optional(seq(/[ \t]/, /[^\n]*/)))),

    // ── names and literals ──────────────────────────────────────────────────
    name: _ => /[A-Za-z_][A-Za-z0-9_]*/,
    path: _ => /[A-Za-z_][A-Za-z0-9_]*(\.[A-Za-z0-9_]+)+/,
    number: _ => token(seq(optional(/[+-]/), choice(
      /0[xX][0-9A-Fa-f](_?[0-9A-Fa-f])*(\.[0-9A-Fa-f](_?[0-9A-Fa-f])*)?[pP][+-]?[0-9](_?[0-9])*/,
      /0[xX][0-9A-Fa-f](_?[0-9A-Fa-f])*/,
      /0[bB][01](_?[01])*/,
      /[0-9](_?[0-9])*\.[0-9](_?[0-9])*([eE][+-]?[0-9](_?[0-9])*)?/,
      /[0-9](_?[0-9])*[eE][+-]?[0-9](_?[0-9])*/,
      /[0-9](_?[0-9])*/,
    ))),
    escape: _ => token.immediate(/\\(x[0-9A-Fa-f]{2}|u[0-9A-Fa-f]{4}|U[0-9A-Fa-f]{8}|[\\"'abfnrtv0])/),
    string: $ => seq(
      choice('"', 'u"', 'U"'),
      repeat(choice(token.immediate(prec(1, /[^"\\\n]+/)), $.escape)),
      token.immediate('"'),
    ),
    char: $ => seq(
      choice("'", "u'", "U'"),
      repeat1(choice(token.immediate(prec(1, /[^'\\\n]+/)), $.escape)),   // more than one character is refused later (E-CHAR-WIDTH)
      token.immediate("'"),
    ),
    boolean: _ => choice('true', 'false'),
    none: _ => 'none',
    stop: _ => '.',

    _word: $ => choice($.name, $.path, $.number, $.string, $.char),

    // ── top level ───────────────────────────────────────────────────────────
    module_declaration: $ => seq('module', field('name', $.name), $.stop),
    use_declaration: $ => seq('use', field('name', $.name),
      optional(seq('from', $.string)), optional(seq('as', $.name)), $.stop),
    package_declaration: $ => seq('package', repeat1($._word), $.stop),
    build_declaration: $ => seq('build', repeat1($._word), $.stop),

    _declaration: $ => choice(
      $.type_declaration, $.struct_declaration, $.enum_declaration,
      $.op_declaration, $.extern_declaration, $.actor_declaration, $.trait_declaration,
      $.contract_declaration, $.test_declaration,
    ),
    modifier: $ => choice('export', 'extern', seq('unsafe', optional(seq('target', $.name)))),

    type_declaration: $ => seq(repeat($.modifier), 'def', choice('type', 'newtype'),
      field('name', $.name), $._type, $.stop),
    struct_declaration: $ => seq(repeat($.modifier), 'def', 'struct', field('name', $.name),
      'do', repeat(choice($.struct_attribute, $.field_declaration)), 'end', $.stop),
    field_declaration: $ => seq(field('name', $.name), $._type, repeat($._word), $.stop),
    struct_attribute: $ => seq(choice('layout', 'align', 'mmio', 'storage', 'satisfies'), repeat1($._word), $.stop),
    enum_declaration: $ => seq(repeat($.modifier), 'def', 'enum', field('name', $.name),
      'do', repeat($.variant), 'end', $.stop),
    variant: $ => seq(field('name', $.name), repeat(seq($.name, $._type)), $.stop),

    op_declaration: $ => seq(repeat($.modifier), choice('fn', 'proc'),
      field('name', choice($.name, $.path)), repeat($.clause), $.block, $.stop),
    extern_declaration: $ => seq(repeat($.modifier), choice('fn', 'proc'),
      field('name', choice($.name, $.path)), 'do', repeat1($.clause), 'end', $.stop),

    clause: $ => choice(
      seq('input', optional('comptime'), field('name', $.name), $._type, repeat($._word), $.stop),
      seq('using', field('name', $.name), $._type, $.stop),
      seq('output', optional(field('name', $.name)), $._type, $.stop),
      seq(choice('requires', 'ensures'), optional(choice('assume', 'static', 'debug')), $._term, $.stop),
      seq('errors', field('name', choice($.name, $.path)), optional($._term), $.stop),
      prec.right(seq('asm', repeat(choice($._word, $.stop, '(', ')')))),
      seq(choice(...CLAUSE_WORDS), repeat(choice($._word, $.group, $.text_literal)), $.stop),
    ),
    group: $ => seq('(', repeat(choice($._word, $.group)), ')'),

    actor_declaration: $ => seq(repeat($.modifier), 'actor', field('name', $.name), 'do',
      repeat(choice($.state_declaration, $.clause, $.op_declaration,
                    seq(choice('failure', 'mailbox'), repeat1($._word), $.stop))),
      'end', $.stop),
    state_declaration: $ => seq('state', 'do', repeat($.field_declaration), 'end', $.stop),
    trait_declaration: $ => seq(repeat($.modifier), 'trait', field('name', $.name), 'do',
      repeat($.signature), 'end', $.stop),
    signature: $ => prec.right(seq(field('name', choice($.name, $.path)), repeat($.clause))),
    contract_declaration: $ => seq(repeat($.modifier), 'contract', field('name', $.name), 'do',
      repeat($.clause), 'end', $.stop),
    test_declaration: $ => seq('test', field('name', $.name),
      optional(seq('schedule', repeat($._word), $.stop)), $.block, $.stop),

    // ── types — they end by their own words, no stop ────────────────────────
    _type: $ => choice($.type_name, $.applied_type, $.paren_type),
    type_name: $ => choice($.name, $.path),
    paren_type: $ => seq('(', repeat1(choice($._type, $.number)), ')'),
    applied_type: $ => prec.right(choice(
      seq(choice(...T_PRE1), $._type),
      seq('result', optional(seq($._type, optional($._type)))),
      seq(choice('array', 'vec'), choice($._type, $.number), choice($.name, $.path, $.number, $.paren_type)),
      prec(-1, seq('matrix', $._type, $.number, repeat1($.number))),   // element type, number of dimensions, one size each
      seq(choice('bitset', 'mask', 'bits'), optional($.number)),
      seq(choice('cap', 'region'), $.name),
      seq('range', optional($.type_name), $.number, $.number),
    )),

    // ── statements ──────────────────────────────────────────────────────────
    block: $ => seq('do', repeat($._statement), 'end'),

    _statement: $ => choice(
      $.binding, $.guard_statement, $.set_statement, $.return_statement, $.jump_statement,
      $.expect_statement, $.if_statement, $.while_statement, $.for_statement,
      $.repeat_statement, $.range_statement, $.cycle_statement, $.match_statement,
      $.region_statement, $.borrow_statement, $.task_group, $.drop_statement,
      $.op_declaration, $.asm_statement, $._term,
    ),
    asm_statement: $ => seq('asm', repeat($._word), $.stop, $.text_literal, $.stop),

    binding: $ => seq(choice('let', 'var'), field('name', $.name),
      optional(seq(choice('use', 'keep'), $.name, $.stop)),
      choice(seq($._type, $._term), $.struct_literal, $.fill_literal, $.list_literal),
      optional($.fail_clause), $.stop),
    fail_clause: $ => choice(
      seq('else', $._leave, $.stop),
      seq('else', $.block),
      seq('else', 'error', $.name, $.block),
    ),
    _leave: $ => choice($.return_leave, seq(choice('break', 'continue'), optional($.number)), seq('panic', $._term)),
    return_leave: $ => prec.right(seq('return', optional($._term))),

    guard_statement: $ => seq('guard', $._term, 'else', choice(seq($._leave, $.stop), $.block), $.stop),
    set_statement: $ => seq('set', $._term, $._term, $.stop),
    return_statement: $ => seq('return', optional($._term), $.stop),
    jump_statement: $ => seq(choice('break', 'continue'), optional($.number), $.stop),   // `break 2 .` leaves two loops
    expect_statement: $ => seq('expect', $._term, $.stop),
    drop_statement: $ => seq('drop', $.name, $.stop, $.stop),

    if_statement: $ => seq('if', $._term, $.block,
      repeat(seq('else', 'if', $._term, $.block)), optional(seq('else', $.block)), $.stop),
    while_statement: $ => seq('while', $._term, $.block, $.stop),
    for_statement: $ => seq('for', field('name', $.name), optional('mut'), $._term, optional($.filter), $.block, $.stop),
    repeat_statement: $ => seq('repeat', field('name', $.name), $._type, $._term, optional($.filter), $.block, $.stop),
    range_statement: $ => seq('range', field('name', $.name), $._type, $._term, $._term,
      optional(seq('step', $._term)), optional($.filter), $.block, $.stop),
    cycle_statement: $ => seq('cycle', field('name', $.name), $._type, $._term,
      'while', $._term, 'next', $._term, optional($.filter), $.block, $.stop),
    filter: $ => seq('if', $._term),

    match_statement: $ => seq('match', optional('comptime'), $._term, 'do',
      repeat(choice($.case_arm, seq('else', $.block))), 'end',
      optional(seq('else', $.block)), $.stop),
    case_arm: $ => seq('case', $.pattern, $.block, $.stop),
    pattern: $ => repeat1(choice($.name, $.path, $.number, $.char, $.string, $.boolean, $.none,
                                 seq('(', $.pattern, ')'))),

    region_statement: $ => seq('region', field('name', $.name), $.name, $.block, $.stop),
    borrow_statement: $ => seq('borrow', field('name', $.name), $._term, $.block, $.stop),
    task_group: $ => seq('task_group', optional($.name), $.block, $.stop),

    // ── terms — a name opens, a stop closes ─────────────────────────────────
    _term: $ => choice(
      $.number, $.string, $.char, $.text_literal, $.boolean, $.none,
      $.call, $.paren_term,
      $.struct_literal, $.fill_literal, $.list_literal,
      $.island, $.pipe_form,
      $.field_form, $.method_form, $.payload_form, $.send_form, $.spawn_form, $.try_form,
      $.builtin_call, $.alloc_form, $.pop_form, $.view_form, $.word_list_form, $.shaped_form,
      $.word_value,
    ),

    // a contextual head word used as an ordinary name: `method.` `order.` `pipe.`
    word_value: $ => seq(alias(choice('order', 'method', 'field', 'payload', 'pipe', 'pop', 'alloc_bytes', 'call_builtin',
                                      'stack_new', 'channel', 'capacity', 'into', 'with'), $.name), $.stop),

    call: $ => seq(field('head', choice($.name, $.path)), repeat(choice($._term, $.order_mark)), $.stop),
    order_mark: $ => seq('order', $.name),   // atomic ops: `… order relaxed .`
    paren_term: $ => seq('(', $._term, ')'),

    struct_literal: $ => seq('lit', $._type, 'do', repeat($.field_init), 'end', $.stop),
    field_init: $ => seq(field('name', choice($.name, '_')), $._term, $.stop),
    fill_literal: $ => seq('lit', 'array', $._type, choice($.name, $.path, $.number),
      'do', repeat($.fill_item), 'end', $.stop),
    fill_item: $ => seq(choice($.number, $.name, '_'), $._term, $.stop),
    list_literal: $ => seq('lit', choice(
      seq(choice('array', 'vec'), $._type, choice($.name, $.path, $.number)),
      seq('matrix', $._type),                 // the dimensions and sizes read as the first numbers of the list
      seq('slice', $._type),
    ), repeat(choice($._term, '_')), $.stop),

    island: $ => seq('expr', $._island_operand, repeat(seq($.operator, $._island_operand)), $.stop),
    // a leading operator is read here and refused later (E-EXPR-UNARY)
    _island_operand: $ => seq(repeat(alias(choice('+', '-', '*', '/'), $.operator)), $._island_item),
    _island_item: $ => choice($._term, $.island_group),
    island_group: $ => prec(1, seq('(', $._island_operand, repeat1(seq($.operator, $._island_operand)), ')')),
    operator: _ => choice('+', '-', '*', '/', 'eq', 'ne', 'lt', 'le', 'gt', 'ge', 'and', 'or'),

    pipe_form: $ => seq('pipe', $._term, 'do', repeat($.stage), 'end', $.stop),
    stage: $ => seq(choice(
        seq(field('stage', choice('count', 'sort'))),
        seq(field('stage', choice('take', 'skip')), $._term),
        seq(field('stage', 'collect'), 'into', $._term),
        seq(field('stage', choice('filter', 'map', 'any', 'all', 'enumerate')), field('op', choice($.name, $.path))),
        seq(field('stage', choice('fold', 'scan', 'zip')), $._term, field('op', choice($.name, $.path))),
      ), repeat($._term), optional(seq('with', $._term)), $.stop),

    field_form: $ => seq('field', $._term, repeat1(field('segment', choice($.name, $.number))), $.stop),
    method_form: $ => seq('method', $._term, repeat(field('segment', choice($.name, $.number))),
      field('op', $.name), $.stop, repeat($._term), $.stop),
    payload_form: $ => seq('payload', $._term, field('variant', $.name), field('field', $.name), $.stop),
    send_form: $ => seq('send', $._term, field('handler', $.name), repeat($._term), $.stop),
    spawn_form: $ => seq('spawn', choice(
      seq('actor', choice($.name, $.path), $.stop),
      seq($.send_form, $.stop),
      seq(field('op', choice($.name, $.path)), repeat($._term), $.stop),
    )),
    try_form: $ => seq('try', $._term, repeat($.name), $.stop),
    builtin_call: $ => seq('call_builtin', field('builtin', $.name), repeat($._term), $.stop),
    alloc_form: $ => seq('alloc_bytes', optional($._term), 'capacity', $._term, $.stop),
    pop_form: $ => seq('pop', $._term, optional(seq('into', $._term)), $.stop),
    view_form: $ => seq('view', choice($.stop, seq(choice($.name, $.path), optional($._term), $.stop))),
    word_list_form: $ => seq(choice('stack_new', 'channel'), repeat1($._word), $.stop),

    // core ops with a word slot (shapes.json — copied from the compiler's table)
    shaped_form: $ => choice(...Object.entries(SHAPES)
      .filter(([_, shape]) => shape.includes('W'))
      .map(([head, shape]) => seq(
        field('head', alias(head, $.core_op)),
        choice($.stop, seq(
          ...[...shape].map(c => c === 'V' ? $._term : field('word', choice($.name, $.path, $.number))),
          $.stop))))),
  },
});
