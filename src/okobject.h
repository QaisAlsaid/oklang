#ifndef OK_OBJECT_H
#define OK_OBJECT_H

#include <stdbool.h>
#include <stdint.h>

#include "ok/ok.h"
#include "okchunk.h"
#include "okfwd.h"
#include "okindex.h"
#include "okmap.h"
#include "okobject_store.h"
#include "okstring.h"
#include "okvalue.h"

#define OBJECT_TYPE(value) object_get_type(VALUE_AS_OBJECT(value))

#define IS_VALUE_STRING(value) value_is_object_type(value, OBJ_STRING)
#define IS_VALUE_FUNCTION(value) value_is_object_type(value, OBJ_FUNCTION)
#define IS_VALUE_CLOSURE(value) value_is_object_type(value, OBJ_CLOSURE)
#define IS_VALUE_UPVALUE(value) value_is_object_type(value, OBJ_UPVALUE)
#define IS_VALUE_NATIVE_VALUE(value) value_is_object_type(value, OBJ_NATIVE_VALUE)
#define IS_VALUE_NATIVE_FUNCTION(value) value_is_object_type(value, OBJ_NATIVE_FUNCTION)
#define IS_VALUE_ARRAY(value) value_is_object_type(value, OBJ_ARRAY)
#define IS_VALUE_MAP(value) value_is_object_type(value, OBJ_MAP)
#define IS_VALUE_CLASS(value) value_is_object_type(value, OBJ_CLASS)
#define IS_VALUE_INSTANCE(value) value_is_object_type(value, OBJ_INSTANCE)
#define IS_VALUE_BOUND_METHOD(value) value_is_object_type(value, OBJ_BOUND_METHOD)
#define IS_VALUE_CORO(value) value_is_object_type(value, OBJ_CORO)

#define VALUE_AS_STRING(value) ((object_string*)VALUE_AS_OBJECT(value))
#define VALUE_AS_FUNCTION(value) ((object_function*)VALUE_AS_OBJECT(value))
#define VALUE_AS_CLOSURE(value) ((object_closure*)VALUE_AS_OBJECT(value))
#define VALUE_AS_UPVALUE(value) ((object_upvalue*)VALUE_AS_OBJECT(value))
#define VALUE_AS_NATIVE_VALUE(value) ((object_native_type*)VALUE_AS_OBJECT(value))
#define VALUE_AS_NATIVE_FUNCTION(value) ((object_native_function*)VALUE_AS_OBJECT(value))
#define VALUE_AS_ARRAY(value) ((object_array*)VALUE_AS_OBJECT(value))
#define VALUE_AS_MAP(value) ((object_map*)VALUE_AS_OBJECT(value))
#define VALUE_AS_CLASS(value) ((object_class*)VALUE_AS_OBJECT(value))
#define VALUE_AS_INSTANCE(value) ((object_instance*)VALUE_AS_OBJECT(value))
#define VALUE_AS_BOUND_METHOD(value) ((object_bound_method*)VALUE_AS_OBJECT(value))
#define VALUE_AS_CORO(value) ((object_coro*)VALUE_AS_OBJECT(value))
// the defer tag's payload is an object_closure*, but it is not an object-tagged value.
// unwrap through the payload directly (the tag check is IS_VALUE_STRICTLY_DEFER, never IS_VALUE_CLOSURE).
#define VALUE_AS_DEFER(value) ((object_closure*)(uintptr_t)(VALUE_PAYLOAD(value)))

typedef struct {
  allocators* alloc;
  object_store* store;
} object_specs;

typedef enum {
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_CLOSURE,
  OBJ_UPVALUE,
  OBJ_NATIVE_VALUE,
  OBJ_NATIVE_FUNCTION,
  OBJ_ARRAY,
  OBJ_MAP,
  OBJ_CLASS,
  OBJ_INSTANCE,
  OBJ_BOUND_METHOD,
  OBJ_CORO,
} object_type;

struct object {
  uint32_t info; // 24bit for type + 1 bit for is marked and rest of bits for later usage
  object* next;
};

uint32_t object_get_type(object* p_object);
bool object_is_marked(object* p_object);
void object_set_marked(object* p_object, bool p_marked);

static inline bool value_is_object_type(value p_value, object_type p_type) {
  // strict check: raw doubles alias the non strict tag bits
  return IS_VALUE_STRICTLY_OBJECT(p_value) && OBJECT_TYPE(p_value) == p_type;
}

typedef struct {
  object object;
  string string;
  hash_t hash; // FNV-1a over the bytes, cached at creation. strings are immutable,
               // so map keys (and any future hashed user) never re-walk the bytes.
  uint64_t codepoints; // the P2 len ruling: the codepoint count, lossy-cached at
                       // creation (one U+FFFD per malformed byte for runtime
                       // strings). len() answers this; the byte length stays
                       // string_get_length until an s.bytes() ever earns its keep.
} object_string;

// the single choke point (D20): EVERY ok string routes through the interner
// - literals, concatenation results, conversions, class/method names,
// host-created values. identical content is one canonical object, so ===
// is content truth and probe tables hit the pointer fast path. reaches the
// interner through the gc's watch (alloc->gc->interner, live from ok_create
// before any string can exist); the NULL fallback is the bare creator
// (reachable in principle only before ok_create, which creates no strings).
// the result is NOT rooted by the weak table: root it like any fresh object.
object_string* create_object_string(const ok_string_view p_string, object_specs* p_specs);

// the bare allocation, no interner. the interner's own miss path (a canonical
// object must not re-enter the interner) and nothing else.
object_string* create_object_string_uninterned(const ok_string_view p_string, object_specs* p_specs);

typedef struct {
  ok_index index;
  bool is_local;
} upvalue_descriptor;

ARRAY_DECLARE(upvalue_descriptor_array, upvalue_descriptor, uint16_t)

typedef struct {
  object object;
  object_string* name;
  chunk chunk;
  uint16_t arity;
  uint16_t ndefaults; // trailing defaulted params: floor = arity - ndefaults
  uint16_t registers;
  uint16_t results; // the function's result span width.
                    // 1 for every single-result function (the default); n >= 2 when every
                    // return path agrees on n. call() compares the caller's requested retc
  bool has_varargs; // the last parameter is `rest...`: argc has no ceiling, the
                    // floor drops it, and the runtime frame widens past `registers` to cover
                    // the staged arguments. set by the compiler at the prologue.
  upvalue_descriptor_array upvalues;
  // true when this proto is a class member (a method or a field initializer
  // thunk): the class's own freeze enters through it directly. the freeze
  // walk uses the flag to NOT descend into member protos of OTHER classes -
  // a class declared inside a method compiles its members as constants of the
  // ENCLOSING method's chunk, and the enclosing class's freeze must leave them
  // alone (their own OP_CLASS_END owns them). plain lambdas carry false and
  // patch for whatever class lexically encloses them.
  bool is_class_member;
  // the class whose definition freeze-patched this proto's chunk (NULL until
  // then). a second freeze by a different class means the same body defined
  // twice - the in-place patching cannot express that, so it is refused.
  struct object_class* patched_by;
  // set by the compiler when the body contains a super(...) initializer call
  // (bare or spelled). the class-definition check reads it: an init override
  // whose chain has an init must call super. this is the only compile-time
  // half of that rule - the chain itself is a runtime truth.
  bool calls_super_init;
} object_function;

object_function* create_object_function(object_string* p_name, uint16_t p_arity, object_specs* p_specs);

typedef struct object_upvalue {
  object object;
  uint32_t info; // 24 bit index into the stack + 1 bit for is_closed
  value closed;
  struct object_upvalue* next;
  // the capture's owner: the null value = the vm's own state, otherwise the
  // coro object whose state the location indexes. TRACED through the
  // upvalue, so a closure that escaped a parked coro keeps the coro (and its
  // register file) alive while its captured cells are still open - the
  // upvalue-cell read resolves the owner's array, never the running state's.
  // cleared at close (a snapshot needs no owner).
  value owner;
} object_upvalue;

void object_upvalue_set_location(object_upvalue* p_upvalue, uint32_t p_location);
void object_upvalue_set_closed(object_upvalue* p_upvalue);
void object_upvalue_set_owner(object_upvalue* p_upvalue, value p_owner);
uint32_t object_upvalue_get_location(object_upvalue* p_upvalue);
bool object_upvalue_is_closed(object_upvalue* p_upvalue);
value* object_upvalue_get_value(object_upvalue* p_upvalue, value* p_frame_window);

object_upvalue* create_object_upvalue(uint32_t p_location, object_specs* p_specs);

ARRAY_DECLARE_DEFAULT(upvalue_array, object_upvalue*)

// the ok_value-keyed instantiation of the okmap core (defined in okobject.c):
// strict-null key = empty slot, cached hashes, backward-shift deletion.
MAP_DECLARE(value_map, value, value, value)

typedef struct {
  object object;
  object_function* function;
  upvalue_array upvalues;
  // the class whose body lexically produced this closure: methods get it at
  // definition time, lambdas inherit the creating frame's owner at runtime.
  // privacy checks and super resolution read it. NULL outside class bodies.
  struct object_class* owner;
} object_closure;

object_closure* create_object_closure(object_function* p_function, object_specs* p_specs);

typedef struct {
  object object;
  void* user_data;
  ok_native_destructor destructor;
  ok_native_tracer tracer; // NULL: opaque to the collector (no ok_values inside)
} object_native_value;

object_native_value* create_object_native_value(void* p_user_data,
                                                ok_native_destructor p_destructor,
                                                ok_native_tracer p_tracer,
                                                object_specs* p_specs);

typedef struct {
  object object;
  string name;
  uint16_t arity;
  ok_native_fn function;
} object_native_function;

object_native_function*
create_object_native_function(ok_string_view p_name, uint16_t p_arity, ok_native_fn p_cb, object_specs* p_specs);

typedef struct {
  object object;
  value_array values; // ARRAY_DECLARE(value_array, value, uint32_t) in okvalue.h
} object_array;

object_array* create_object_array(object_specs* p_specs);

// the surface map: the same linear-probing core the compiler's tables use
// (okmap.h), instantiated at ok_value keys AND values. the strict-null key is
// the empty-slot sentinel, which is exactly why null KEYS are illegal on the
// surface (the encoding spends them on "empty"). keys are stored as the values
// they arrived as; probes hash through map_value_hash and compare through
// map_value_key_equals, both of which must stay in agreement with perform_equal
// for the types that are legal keys (strings, numbers, bools).
//
// version: bumped by every size-changing operation (insert-new, remove;
// overwrites do not bump). the for-in ops snapshot it at loop entry and hard
// error on a mid-iteration size change, overwriting values while iterating is
// fine, inserting/removing is not.
typedef struct {
  object object;
  value_map entries;
  uint32_t version;
} object_map;

object_map* create_object_map(object_specs* p_specs);

hash_t map_value_hash(value p_key);
bool map_value_key_equals(value p_probe, value p_key);

// the v1 key whitelist: strings, numbers (int-tagged included) and booleans.
// null is the empty-slot encoding (never a legal key), arrays/maps/functions
bool map_key_is_legal(value p_key);

// insert-or-overwrite + version bookkeeping. returns false only on oom
// (the okmap core's contract), a size change bumps p_map->version so active iterations can detect it.
bool object_map_set(object_map* p_map, value p_key, value p_value);

// false = absent (no version change).
bool object_map_remove(object_map* p_map, value p_key);

// one method table entry. the table is FROZEN when the class definition
// completes: entries never move, get replaced, or die afterwards, which is
// what makes raw method_entry pointers inside inline caches sound.
typedef struct object_class object_class;

typedef struct method_entry {
  object_closure* closure; // script method. NULL for a native entry
  ok_native_fn native;     // native method (receiver staged as argv[0])
  object_string* name;     // the method name (native diagnostics reuse it)
  uint16_t arity;          // declared arity (natives count the receiver)
  object_class* owner;     // the class whose body declared it (privacy + super)
  uint8_t is_native;
  uint8_t pub;
} method_entry;

// name-keyed method table: object_string* keys. all keys are interner
// canonicals now (D20), so probes hit on the pointer; the content compare
// stays as the belt for any pre-create fallback string (there are none in
// practice). cached hashes.
MAP_DECLARE(method_map, object_string*, method_entry, const object_string*)

// the overloadable operator surface, as seen by the dispatch arms. each
// binary takes one argument, unary minus and the subscript-get take none
// beyond the receiver, the subscript-store takes two, the call operator's
// count is the call's own. arity is checked at dispatch (unary minus and
// binary minus share the "operator -" name and split by arity).
typedef enum {
  CLASS_OP_ADD = 0,
  CLASS_OP_SUBTRACT,
  CLASS_OP_MULTIPLY,
  CLASS_OP_DIVIDE,
  CLASS_OP_MODULUS,
  CLASS_OP_EQUALS,
  CLASS_OP_LESS,
  CLASS_OP_LESS_EQUAL,
  CLASS_OP_GREATER,
  CLASS_OP_GREATER_EQUAL,
  CLASS_OP_NEGATE,
  CLASS_OP_SUBSCRIPT_GET,
  CLASS_OP_SUBSCRIPT_SET,
  CLASS_OP_CALL,
  CLASS_OP_COUNT,
} class_operator;

// the canonical method-table spelling of an operator ("operator +" etc.).
const char* class_operator_spelling(class_operator p_op);

// the seven builtin classes (C10): one per existing value lane, all under
// Object, all created at startup and sealed by ok_define_builtin_surfaces
// once their native method tables are mounted (str/function/num/bool/nil
// carry no methods in v0.5.0 - nothing existed as a function builtin to
// mirror, and the ruling is no new stdlib). int and float share
// `num` (the box detail is not a class difference); `nil` is null's class
// and null is its sole instance. `coro` (R20-R24) joins them as the eighth:
// the coroutine lane's class value, constructed through its native static
// `operator ()`.
typedef enum {
  OK_BUILTIN_STR = 0,
  OK_BUILTIN_NUM,
  OK_BUILTIN_BOOL,
  OK_BUILTIN_ARRAY,
  OK_BUILTIN_MAP,
  OK_BUILTIN_FUNCTION,
  OK_BUILTIN_NIL,
  OK_BUILTIN_CORO,
  OK_BUILTIN_COUNT,
} builtin_class_id;

// the singleton's name ("str", "num", ...), also the `as` target spelling
// and the mangled-name suffix of an `operator str`-style conversion.
const char* builtin_class_name(builtin_class_id p_id);

// one flat-layout field: its absolute slot in the instance array, mutability,
// and visibility. pub (patch 51) opens the member ring: a pub field is
// readable (and, with mut, writable) from outside the defining hierarchy,
// module boundaries included - the getter-free surface. private stays the
// default, exactly as before.
typedef struct {
  uint16_t slot;
  uint8_t mut;
  uint8_t pub;
} field_entry;

MAP_DECLARE(field_map, object_string*, field_entry, const object_string*)

ARRAY_DECLARE_DEFAULT(object_string_array, object_string*)
ARRAY_DECLARE_DEFAULT(uint8_array, uint8_t)
ARRAY_DECLARE_DEFAULT(object_closure_array, object_closure*)

struct object_class {
  object object;
  object_string* name;
  struct object_class* parent; // NULL only for the Object singleton
  uint16_t nfields;            // total layout width: parent's fields + own
  uint16_t nown;               // own field declarations (diagnostics)
  object_string_array own_names; // own field names, declaration order
  uint8_array own_muts;        // parallel mut flags
  uint8_array own_pubs;        // parallel pub flags (patch 51: fields join the ring)
  object_closure_array own_inits; // parallel initializer thunks (NULL = none)
  value_array own_defaults;    // parallel C-side initial values (patch 49: the
                               // native-class lane, always null for script
                               // fields - their per-instance initializer is
                               // the thunk. a reference-typed default is
                               // shared by every instance, the documented
                               // footgun of the static shape)
  field_map fields;            // flat name -> slot, built at freeze
  method_map methods;          // name -> entry, frozen at definition end
  method_map statics;          // static methods: name -> entry. the receiver is
                               // the class value itself, never an instance
  object_string_array own_static_names; // own static fields, declaration order
  uint8_array own_static_muts; // parallel mut flags
  uint8_array own_static_pubs; // parallel pub flags (patch 51)
  value_array static_values;   // own statics' storage: one slot each, never
                               // copied down the chain (shared by lookup)
  field_map static_fields;     // own statics only: name -> {slot, mut}
  // the class-resident operator cache: one entry pointer per dispatchable
  // operator, filled lazily on first dispatch and sound forever after - the
  // tables are frozen (C15), so find_method is a pure function of (class,
  // name) and the entry can never dangle while this class is reachable (the
  // parent chain roots every owner). shared by EVERY dispatch site in the
  // vm: one slot per (class, operator) beats per-site monomorphic state and
  // costs no instruction words.
  const struct method_entry* op_cache[CLASS_OP_COUNT];
  bool frozen;
  // OR-1 modules: this class describes a module namespace (or a root
  // namespace). its instances are the module values: field reads are PUBLIC
  // (the visibility ring does not apply - the exports are the surface), and
  // field writes from script are refused (namespaces are immutable once
  // sealed; the loader fills the slots from C before anyone sees the value).
  // set only by the module system (okmodules.c) on freshly created classes.
  bool module_namespace;
};

object_class* create_object_class(object_string* p_name, object_class* p_parent, object_specs* p_specs);

// definition-time (C side): append one native method entry. the arity COUNTS
// the receiver (`a.len()` is arity 1) - the builtin handlers were already
// receiver-first functions, which is what makes one C entry serve the
// function and method spellings. OK_NATIVE_ARITY_ANY validates inside the
// callback. false = OOM, the class is frozen (sealed, C15), a same-name
// redeclaration in the body, a private override of a pub ancestor method,
// or an override whose dispatch arity disagrees with the ancestor's (a
// native's declared arity and a closure's parameter count both express "the
// argc this entry dispatches at" - native counts the receiver, closure
// does not - and the two must agree for virtual dispatch to be honest).
bool object_class_define_native_method(object_class* p_class,
                                       ok_string_view p_name,
                                       uint16_t p_arity,
                                       ok_native_fn p_fn,
                                       bool p_pub,
                                       object_specs* p_specs);

// the static-table sibling: append one native STATIC method (the receiver is
// the class value itself). the same freeze seal and signature contract the
// methods table carries, one table over. the coro constructor
// (`coro(fu)`, the overridden static `operator ()`) rides this door.
bool object_class_define_native_static_method(object_class* p_class,
                                              ok_string_view p_name,
                                              uint16_t p_arity,
                                              ok_native_fn p_fn,
                                              bool p_pub,
                                              object_specs* p_specs);

// definition-time: append one own field. false = OOM or a redeclaration
// (p_previous_owner names the ancestor that already declared it). p_default
// is the C-side initial value (patch 49): every instance's slot starts as
// this value instead of null. the script lane always passes null - its
// per-instance initializer is the thunk, which re-evaluates per
// construction; a C default is evaluated once at definition time. p_pub
// (patch 51) marks the field readable outside the defining hierarchy.
bool object_class_add_field(object_class* p_class,
                            const object_string* p_name,
                            bool p_mut,
                            bool p_pub,
                            object_closure* p_init_thunk,
                            value p_default,
                            const object_string** p_previous_owner);

// definition end: build the flat field map over the parent chain and freeze
// both tables. false = OOM. after this, add_field/define_method are refused.
bool object_class_freeze(object_class* p_class);

// flat-layout lookup used by the freeze patch pass and runtime guards.
const field_entry* object_class_find_field(const object_class* p_class, const object_string* p_name);

// method lookup walking the parent chain. NULL = not found.
const method_entry* object_class_find_method(const object_class* p_class, const object_string* p_name);

// is p_class p_maybe_ancestor or one of its descendants.
bool object_class_is_a(const object_class* p_class, const object_class* p_maybe_ancestor);

// definition-time: append one own static field with its (already evaluated)
// value. false = OOM or a redeclaration anywhere in the chain (statics are
// never shadowed: one name, one slot, the defining class owns it). the
// previous owner names the ancestor that already declared it. p_pub (patch
// 51) opens the ring for the static, the fields' rule one table over.
bool object_class_add_static_field(object_class* p_class,
                                    const object_string* p_name,
                                    bool p_mut,
                                    bool p_pub,
                                    value p_value,
                                    const object_string** p_previous_owner);

// static field lookup walking the parent chain. NULL = not found; p_owner
// (when given) receives the defining class whose storage holds the slot.
const field_entry* object_class_find_static(const object_class* p_class,
                                             const object_string* p_name,
                                             const object_class** p_owner);

// static method lookup walking the parent chain. NULL = not found.
const method_entry* object_class_find_static_method(const object_class* p_class, const object_string* p_name);

// the operator dispatch probe: the cached entry when one exists, else a
// chain walk on the canonical spelling that (for frozen classes) fills the
// cache. NULL = the chain has no such operator. p_name must be the
// caller-held canonical object_string (the vm caches one per operator).
const method_entry* object_class_find_operator(object_class* p_class, class_operator p_op, const object_string* p_name);

typedef struct {
  object object;
  object_class* klass; // the dispatch key, doubles as the type tag
  value fields[];      // inline, one allocation, nfields wide
} object_instance;

object_instance* create_object_instance(object_class* p_class, object_specs* p_specs);

// `obj.method` as a value: the receiver plus the found entry. calling it
// stages the receiver at the call window base and dispatches the entry - the
// slow lane next to INVOKE's fused fast lane, never instead of it.
typedef struct {
  object object;
  value receiver;
  const method_entry* entry; // points into the class's frozen method table
} object_bound_method;

object_bound_method* create_object_bound_method(value p_receiver, const method_entry* p_entry, object_specs* p_specs);

// the coroutine lifecycle, as status() spells it
typedef enum {
  CORO_READY = 0,     // constructed, never resumed: the body has not started
  CORO_SUSPENDED,     // parked at a yield: frames alive, resumable
  CORO_RUNNING,       // inside a resume call right now: resuming again errors
  CORO_DEAD,          // returned or died by error: resume answers null
} coro_status;

// a coroutine: a closure plus, once first resumed, its own execution state
// (register file, call stack, handler stack - the same shape the vm's main
// state has). the state is allocated lazily at the first resume and released
// at death, so a created-but-never-run coro costs one object. while parked the
// object roots the state: the collector traces it like a live vm
// (see mark_execution_state).
struct execution_state; // okvm.h

typedef struct object_coro {
  object object;
  object_closure* body;
  struct execution_state* state; // NULL until the first resume, NULL again after death
  uint16_t resume_target;         // the parked yield's result register (frame-relative)
  uint8_t status;                 // coro_status
} object_coro;

object_coro* create_object_coro(object_closure* p_body, object_specs* p_specs);

// the status word status() spells and print shows ("ready" .. "dead")
const char* coro_status_name(uint8_t p_status);

// release a coroutine's execution state (its arrays and its own allocation).
// defined in okvm.c where the state's layout lives; declared here so the
// object teardown can call it without including okvm.h.
void okvm_execution_state_dispose(struct execution_state* p_state, allocators* p_alloc);

void object_dispatch_deinit(object* p_object, object_specs* p_specs);
void objects_list_deinit(object* p_head, object_specs* p_specs);

void object_debug_print(object* p_object);
#endif // OK_OBJECT_H
