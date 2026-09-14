// low_value.h — lowent-mini dynamic value model (§4).
// Compound payloads (string bytes, list items, record pairs, closures) live in the
// evaluator's value arena and are bulk-freed at run end (§8.1 pure-eval model).
#ifndef LOW_VALUE_H
#define LOW_VALUE_H

#include "proven/types.h"
#include "proven/u8str.h"

typedef enum {
    LOW_V_NONE,      // the `none` value — absence/optional (§4)
    LOW_V_UNIT,     // "no meaningful return"
    LOW_V_BOOL,
    LOW_V_INT,      // dynamic number (integer)
    LOW_V_FLOAT,    // dynamic number (float)
    LOW_V_STR,      // bytes in value arena or source
    LOW_V_LIST,
    LOW_V_RECORD,
    LOW_V_OP,       // user op = closure
    LOW_V_BUILTIN,  // prelude op
    LOW_V_CELL,     // mutable box (§12.B)
    LOW_V_RANGE,    // lazy integer range (never materialized) — O(1) for-loops
} low_vtag_t;

typedef struct { proven_i64 start, stop, step; } low_range_t;

typedef struct low_value  low_value_t;
typedef struct low_list   low_list_t;
typedef struct low_record low_record_t;
typedef struct low_op     low_op_t;
typedef struct low_eval   low_eval_t;
typedef struct low_scope  low_scope_t;

// A builtin receives already-evaluated args (soft arity handled by the caller).
typedef low_value_t (*low_builtin_fn)(low_eval_t *ev, const low_value_t *args, proven_size_t n);

struct low_value {
    low_vtag_t tag;
    union {
        bool                b;
        proven_i64          i;
        double              f;
        proven_u8str_view_t s;
        low_list_t         *list;
        low_record_t       *rec;
        low_op_t           *op;
        low_builtin_fn      bf;
        low_value_t        *cell;   // LOW_V_CELL: pointer to the boxed value (arena)
        low_range_t        *rng;    // LOW_V_RANGE: {start, stop, step}
    };
};

struct low_list   { low_value_t *items; proven_size_t len, cap; };
struct low_record { proven_u8str_view_t *keys; low_value_t *vals; proven_size_t len, cap; };
struct low_op {
    proven_u8str_view_t *params;
    proven_size_t        nparams;
    const void          *body;   // low_cst_t* (block); opaque to avoid include cycle
    low_scope_t         *env;    // captured lexical scope (closure)
};

// ── constructors (values are POD, passed by value) ──
static inline low_value_t low_none(void)       { return (low_value_t){ .tag = LOW_V_NONE }; }
static inline low_value_t low_unit(void)       { return (low_value_t){ .tag = LOW_V_UNIT }; }
static inline low_value_t low_bool(bool b)     { return (low_value_t){ .tag = LOW_V_BOOL, .b = b }; }
static inline low_value_t low_int(proven_i64 i){ return (low_value_t){ .tag = LOW_V_INT, .i = i }; }
static inline low_value_t low_float(double f)  { return (low_value_t){ .tag = LOW_V_FLOAT, .f = f }; }
static inline low_value_t low_str(proven_u8str_view_t s) { return (low_value_t){ .tag = LOW_V_STR, .s = s }; }
static inline low_value_t low_builtin(low_builtin_fn bf) { return (low_value_t){ .tag = LOW_V_BUILTIN, .bf = bf }; }

static inline bool low_is_num(low_value_t v) { return v.tag == LOW_V_INT || v.tag == LOW_V_FLOAT; }
static inline double low_as_f64(low_value_t v) { return v.tag == LOW_V_FLOAT ? v.f : (double)v.i; }

// Truthiness: none/false/0/""/empty are falsey, everything else truthy.
[[nodiscard]] bool low_truthy(low_value_t v);
[[nodiscard]] const char *low_type_name(low_vtag_t t);

#endif // LOW_VALUE_H
