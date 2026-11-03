#ifndef OK_COMPILER_H
#define OK_COMPILER_H

#include "ok/ok_source.h"
#include "okast.h"
#include "okchunk.h"
#include "okfwd.h"
#include "okglobals_store.h"
#include "okindex.h"
#include "okintern.h"
#include "okmap.h"
#include "okobject.h"
#include "okobject_store.h"

#if defined(OK_PARANOID)
#define OK_DEBUG_DUMP_CODE
#endif // defined(OK_PARANOID)

#define IS_REGISTER_VALID(r) ((r.errors) == REGISTER_OK)
#define REGISTERS_MAX INDEX_MAX

typedef index_wrapper _register;
typedef enum {
  REGISTER_OK = 0,
  REGISTER_OVERFLOW,
  REGISTER_ILL_MUTATION,
  REGISTER_ALLOCATION_FAILED,
  REGISTER_UNDEFINED_LOCAL,
  REGISTER_REDEFINED_LOCAL,
} register_error;

typedef enum {
  REGISTER_FLAG_NONE = 0,               // this register has a temp
  REGISTER_FLAG_LOCAL = 1 << 0,         // this register has a local
  REGISTER_FLAG_UNINITIALIZED = 1 << 1, // this register has an uninitialized *thing*
  REGISTER_FLAG_MUTABLE = 1 << 2,       // this register has a mutable *thing*
  REGISTER_FLAG_CAPTURED = 1 << 3,      // this register has captured *thing*
  REGISTER_FLAG_DEVNULL = 1 << 4,       // this register is discarded
} register_flags;

#define IS_UPVALUE_VALID(u) ((u.errors) == UPVALUE_OK)
#define UPVALUES_MAX INDEX_MAX

typedef index_wrapper upvalue;

typedef enum {
  UPVALUE_FLAG_UP = 0,
  UPVALUE_FLAG_LOCAL = 1 << 0,
  UPVALUE_FLAG_MUTABLE = 1 << 1,
} upvalue_flags;

typedef enum {
  UPVALUE_OK = 0,
  UPVALUE_UNDEFINED,
  UPVALUE_OVERFLOW,
  UPVALUE_ALLOCATION_FAILED,
} upvalue_error;

typedef enum {
  EXPRESSION_VALUE = 0,
  EXPRESSION_LVALUE = 1 << 0,
  EXPRESSION_ACCESS = 1 << 1,
  EXPRESSION_SUBSCRIPT = 1 << 2,
} expression_type;

typedef index_wrapper location;

typedef enum {
  LOCATION_OK = 0,
  LOCATION_INVALID,
} location_error;

typedef enum {
  LOCATION_KIND_INVALID = 0,
  LOCATION_KIND_REGISTER,
  LOCATION_KIND_CONSTANT,
  LOCATION_KIND_GLOBAL,
  LOCATION_KIND_UPVALUE,
  // a field of `this`, named by a string constant. only materializes inside
  // class-body methods (and lambdas within): the freeze pass rewrites the
  // emitted NAME op to a baked slot (or a global) once the layout is known.
  LOCATION_KIND_FIELD,
  // a static field of the enclosing class, named by a string constant. the
  // class operand is resolved at emit time through the class-name variable,
  // so the op works at definition time and inside methods alike.
  LOCATION_KIND_STATIC_FIELD,
} location_kind;

typedef enum {
  LVALUE_INVALID = 0,
  LVALUE_VARIABLE,
  LVALUE_SUBSCRIPT,
  LVALUE_FIELD,  // a field of `this` (bare name or this.x)
  LVALUE_MEMBER, // obj.x: receiver materialized, resolved at runtime
  LVALUE_STATIC, // a static field of the enclosing class (bare name)
} lvalue_kind;

typedef struct {
  lvalue_kind kind;
  union {
    location variable;
    struct {
      location base;  // always materialized into a register by resolve_lvalue
      location index; // register or constant (GET/SET carry a const-index flag)
    } subscript;
    struct {
      location name; // the field's string constant (LVALUE_FIELD)
    } field;
    struct {
      location base; // the receiver, materialized into a register
      location name; // the member's string constant
    } member;
    struct {
      location name; // the static field's string constant (LVALUE_STATIC)
    } statik;
  };
} lvalue;

typedef struct {
  expression_type type;
  location value;
  lvalue lvalue;
  uint32_t count;
} expression_result;

typedef enum {
  VARIABLE_DECLRATION_FLAGS_NONE = 0,
  VARIABLE_DECLARATION_FLAGS_MUTABLE = 1 << 0,
  VARIABLE_DECLARATION_FLAGS_GLOBAL = 1 << 1,
} variable_declaration_flags;
typedef uint8_t variable_declaration_flags_t;

typedef struct {
  ok_string_view identifier;
  variable_declaration_flags_t flags;
} variable_declaration;

ARRAY_DECLARE_DEFAULT(uint16_array, uint16_t)
MAP_DECLARE(scope_registers, string, uint16_t /* index to the scope's registers array */, ok_string_view)
ARRAY_DECLARE_DEFAULT(registers, _register)
ARRAY_DECLARE_DEFAULT(upvalues, upvalue)

// a binder's receipt: where a local was declared. the scope map stores only
// the register index, so these ride in a parallel array.
// the "note: previously declared here" lane reads them at error time
// (the token's .start points into the source, alive for the whole compile pass).
typedef struct {
  uint16_t register_index;
  token declared;
} scope_declaration;

ARRAY_DECLARE_DEFAULT(declarations, scope_declaration)

// one pending defer: which register holds its tagged thunk,
// and whether it is a deferr (error path only).
typedef struct {
  uint16_t slot;
  bool is_err;
} defer_slot_entry;

ARRAY_DECLARE_DEFAULT(defer_slots, defer_slot_entry)

typedef struct {
  scope_registers registers_map; // indices into the scope's registers
  uint16_array registers;        // indices into the function's registers
  declarations declarations;     // binder receipts, parallel to the map's values
  uint32_t registers_checkpoint;
  bool captures_any;
  // the scope's pending defers, in declaration order (cleanup emits them in reverse (lifo)).
  // one entry per defer statement that executed or may execute in this scope instance:
  // slot = the hidden local holding the tagged thunk,
  // is_err = deferr (normal exits kill the slot. only the unwind scan runs it).
  defer_slots defers;
} scope;

ARRAY_DECLARE_DEFAULT(scopes, scope)

typedef struct {
  uint32_t scope_depth;
  uint32_t continue_target;
  uint32_t break_target;
  uint16_array continues;
  uint16_array breaks;
  bool continue_forward;
  // the handler depth at loop entry: break/continue jumps leaving try regions
  // behind must pop their records first
  // (a stale record would hijack a later error into this frame's dead window).
  uint32_t handlers_at_entry;
} loop_context;

ARRAY_DECLARE_DEFAULT(loop_stack, loop_context)

typedef enum {
  FUNCTION_NONE = 0,
  FUNCTION_SCRIPT,
  FUNCTION_FUNCTION,
  FUNCTION_METHOD, // receiver ('this') rides register 0 instead of the name
  FUNCTION_STATIC, // receiver ('this') rides register 0 and is the class value
} function_type;

typedef struct {
  object_function* function;
  function_type type;
} compile_function;

typedef struct {
  registers registers;
  allocators* alloc;
  uint32_t local_top;
  uint32_t temp_top;
  uint32_t registers_count;
} register_allocator;

typedef struct {
  scopes scopes;
  upvalues upvalues;
  // binder receipts for upvalues, by position in `upvalues`
  // (the captured local's declaration token, when the root was resolvable).
  // the immutable-upvalue-mutation note reads it.
  declarations upvalue_declarations;
  loop_stack loop_stack;
  compile_function function;
  register_allocator register_allocator;
  // live handler records this function has emitted (TRY_ENTERs minus TRY_EXITs):
  // what break/continue pop and what the rethrow context reads.
  uint32_t active_handlers;
  // the catch context: while compiling a catch body, a bare `throw;`
  // re-emits the parked error value from this register.
  bool in_catch;
  uint16_t catch_park_reg;
  // for inference: the agreed result width of the function being compiled.
  // 0 = no explicit return seen yet, 1 = single (the classic path);
  // n >= 2 = every return path agreed on n. first_return_author points at the
  // first return's AST node for the mismatch error's "at line N" half.
  uint16_t return_span;
  ast_node* first_return_author;
  // unresolved bare names may be fields of `this` (methods and anything
  // lexically nested inside them). propagated to nested contexts.
  bool sees_fields;
  // unresolved bare names may be statics of the enclosing class (static
  // methods, the class body itself, anything nested inside those). the class
  // operand is the class-name variable, whose view this carries.
  bool sees_statics;
  ok_string_view statics_of_class;
} compile_context;

ARRAY_DECLARE_DEFAULT(compile_contexts, compile_context)

// OR-1 modules: one pub top-level export of the TU being compiled. the
// compiler records name + the local's register; the epilogue it emits stores
// the register to the hidden global " module:<canon>:<name>" after the user's
// top level ran, and the loader reads it back to fill the namespace object.
typedef struct {
  ok_string_view name;   // the export's display name (the namespace field name)
  uint16_t local;        // the top-level local's register index
  global_id hidden;      // the mangled hidden global (allocated at the epilogue)
} module_export;

ARRAY_DECLARE_DEFAULT(module_exports, module_export)

// one resolved import binding, prepared by the module system (okmodules.c)
// BEFORE the importing TU compiles - the compiler never resolves anything,
// it only binds what the table hands it, in statement order (M4: the whole
// graph is compile-time; by the time this TU compiles every imported module
// is initialized and its exports are live values). the values are borrowed:
// the module registry (or the pending-roots window) roots them.
typedef struct {
  ok_string_view name;  // the local binding: the root (or alias), or the symbol
  value value;          // the root namespace / module value / export value
  bool merge;           // a path import whose root is already bound in this scope
                        // (M5 root merging): no redeclaration, no code
} ok_import_binding;

typedef struct {
  const ok_import_binding* data;
  uint32_t count;
} ok_import_bindings;

struct compiler {
  ok_source* source;
  allocators* alloc;
  object_store* objects_store;
  globals_store* globals_store;
  // the string interner: every compiler-made string constant goes through
  // it, so member/class/method names canonicalize and runtime probes hit
  // the pointer fast path. the table outlives the compiler (ok owns both).
  string_interner* interner;
  compile_contexts contexts;
  bool had_error;
  bool panic;
  // OR-1 modules: the resolved import bindings for the TU being compiled
  // (borrowed from compile_specs; consumed in statement order).
  ok_import_bindings imports;
  uint32_t imports_next;
  // OR-1: this TU is a module - pub top-level declarations become exports,
  // and an early top-level return is refused (the epilogue must run).
  bool is_module;
  ok_string_view module_path;
  module_exports exports;
};

typedef enum {
  COMPILE_OK,
  COMPILE_ERROR,
} compile_status;

typedef struct {
  compile_status status;
  object_function* function;
  // OR-1 modules: valid when the specs said is_module; the driver reads the
  // hidden globals after the run and builds the namespace object from it.
  // the array is the caller's to deinit (module_exports_deinit) -
  // compiler_compile always leaves it initialized.
  module_exports exports;
} compile_result;

ARRAY_DECLARE_DEFAULT(top_level_declarations, variable_declaration);

typedef struct {
  allocators* alloc;
  object_store* objects_store;
  globals_store* globals_store;
  string_interner* interner;
} compiler_specs;

typedef struct {
  ok_source* source;
  ast_root* root;
  top_level_declarations top_level_declarations;
  // OR-1: the import bindings, in statement order (may be empty/NULL for a
  // TU with no imports - the common single-file case pays nothing).
  ok_import_bindings imports;
  // OR-1: module mode - this TU is an imported module (pub exports + the
  // epilogue + the no-early-return rule). the entry file is not one.
  bool is_module;
  // the module's canonical path (module mode only): the hidden-global
  // mangling prefix " module:<canon>:<name>".
  ok_string_view module_path;
} compile_specs;

void compiler_init(compiler* compiler, compiler_specs p_specs);
void compiler_deinit(compiler* p_compiler);
compile_result compiler_compile(compiler* p_compiler, compile_specs p_specs);

#endif // OK_COMPILER_H
