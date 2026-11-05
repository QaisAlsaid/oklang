#ifndef OK_PARSER_H
#define OK_PARSER_H

#include <stdbool.h>
#include <stdint.h>

#include "ok/ok_source.h"
#include "okarena.h"
#include "okast.h"
#include "oklexer.h"
#include "oktoken.h"

// P5 hardening limits. the depth cap is the stack-overflow vaccine: every
// recursive descent (expressions, statements, f-string regions) funnels
// through one shared counter, so a hostile `((((`-chain lands as a located
// error instead of a segfault - and the compiler's own recursive walk over
// the finished tree inherits the same bound for free. 200 is the lua-shaped
// number (LUAI_MAXCCALLS): deep enough that no honest program notices, and
// cheap enough that the C stack never notices either. the report cap keeps a
// garbage file from flooding the console - after it trips the parse goes
// silent and unwinds.
#define OK_PARSER_MAX_DEPTH 200u
#define OK_PARSER_MAX_REPORTED 25u

typedef enum {
  PREC_NONE = (int)0,
  PREC_ASSIGNMENT,
  PREC_CONDITIONAL,
  PREC_OR,
  PREC_AND,
  PREC_BITWISE_OR,
  PREC_BITWISE_XOR,
  PREC_BITWISE_AND,
  PREC_EQUALITY,
  PREC_COMPARISION,
  PREC_SHIFT,
  PREC_SUM,
  PREC_PRODUCT,
  PREC_EXPONENT,
  PREC_AS,
  PREC_PREFIX,
  PREC_POSTFIX,
  PREC_CALL,
  PREC_ACCESS,
  PREC_SUBSCRIPT,
} precedence;

typedef struct {
  ok_source* source;
  allocators* alloc; // the arena view: every parse-time allocation rides it
  ok_arena arena;    // P5: owns every AST byte; moves into the result
  allocators arena_view; // storage for the view (parser-scoped)
  lexer lexer;
  token current;
  token previous;
  bool panic;
  bool had_error;
  bool bail; // P5: the report cap tripped - stop reporting, unwind
  uint16_t depth;      // P5: the shared recursion depth (expressions + statements)
  uint16_t report_count; // P5: diagnostics actually rendered
  uint32_t defer_serial;
} parser;

typedef enum { PARSE_OK, PARSE_ERROR } parse_status;

typedef struct {
  parse_status status;
  ast_root* root;
  // P5: the parse's whole heap. deinit frees it in one block walk - the
  // tree needs no recursive teardown, and a failed parse's orphan nodes
  // die with it. root is kept for the successful reads only.
  ok_arena arena;
} parse_result;

void parse_result_deinit(parse_result* parse_result);

typedef struct {
  ok_source* source;
  allocators* alloc;
} parser_specs;

void parser_init(parser* p_parser, parser_specs p_specs);
void parser_deinit(parser* p_parser);
parse_result parser_parse(parser* p_parser);

#endif // OK_PARSER_H
