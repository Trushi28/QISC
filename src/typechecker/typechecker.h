/*
 * QISC Type Checker - Header
 *
 * Basic type validation pass between parsing and interpretation.
 * Reports warnings for type mismatches (gradual typing — does not block
 * execution).
 */

#ifndef QISC_TYPECHECKER_H
#define QISC_TYPECHECKER_H

#include "../parser/ast.h"

#define TYPECHECKER_MAX_SYMBOLS 512
#define TYPECHECKER_MAX_FUNCTIONS 256
#define TYPECHECKER_MAX_PARAMS 16

typedef struct {
  char *name;
  char *type_name; /* "int", "float", "string", "bool", "auto", "any" */
  bool is_const;
} TcSymbol;

typedef struct {
  char *name;
  int param_count;
  char *return_type;
  bool is_canfail;
  char *param_types[TYPECHECKER_MAX_PARAMS];
} TcFunction;

/* Type checker state */
typedef struct {
  int warning_count;
  int error_count;

  /* Symbol table for tracking declared variables */
  TcSymbol symbols[TYPECHECKER_MAX_SYMBOLS];
  int symbol_count;

  /* Function signatures */
  TcFunction functions[TYPECHECKER_MAX_FUNCTIONS];
  int func_count;

  const char *current_proc_name;
  const char *current_return_type;
} TypeChecker;

/* Initialize type checker */
void typechecker_init(TypeChecker *tc);

/* Run type checking on parsed AST. Returns number of warnings+errors. */
int typecheck(TypeChecker *tc, AstNode *program);

/* Report summary of type checking results */
void typechecker_report(TypeChecker *tc);

#endif /* QISC_TYPECHECKER_H */
