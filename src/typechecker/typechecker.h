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

/* Type checker state */
typedef struct {
  int warning_count;
  int error_count;

  /* Symbol table for tracking declared variables */
  struct {
    char *name;
    char *type_name; /* "int", "float", "string", "bool", "auto", "any" */
    bool is_const;
  } symbols[512];
  int symbol_count;

  /* Function signatures */
  struct {
    char *name;
    int param_count;
    char *return_type;
    bool is_canfail;
  } functions[256];
  int func_count;
} TypeChecker;

/* Initialize type checker */
void typechecker_init(TypeChecker *tc);

/* Run type checking on parsed AST. Returns number of warnings+errors. */
int typecheck(TypeChecker *tc, AstNode *program);

/* Report summary of type checking results */
void typechecker_report(TypeChecker *tc);

#endif /* QISC_TYPECHECKER_H */
