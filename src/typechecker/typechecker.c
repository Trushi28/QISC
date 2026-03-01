/*
 * QISC Type Checker - Implementation
 *
 * Basic type validation: return types, variable types, arity, undefined vars.
 * Reports as warnings (gradual typing).
 */

#define _GNU_SOURCE
#include "typechecker.h"
#include "qisc.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======== Helpers ======== */

static void tc_warn(TypeChecker *tc, int line, const char *fmt, ...) {
  (void)tc;
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "[typecheck] Warning (line %d): ", line);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
  tc->warning_count++;
}

/* Look up a symbol's type by name */
static const char *tc_lookup(TypeChecker *tc, const char *name) {
  for (int i = tc->symbol_count - 1; i >= 0; i--) {
    if (strcmp(tc->symbols[i].name, name) == 0) {
      return tc->symbols[i].type_name;
    }
  }
  return NULL;
}

/* Register a symbol */
static void tc_define(TypeChecker *tc, const char *name, const char *type_name,
                      bool is_const) {
  if (tc->symbol_count >= 512)
    return;
  int idx = tc->symbol_count++;
  tc->symbols[idx].name = strdup(name);
  tc->symbols[idx].type_name = strdup(type_name);
  tc->symbols[idx].is_const = is_const;
}

/* Register a function signature */
static void tc_define_func(TypeChecker *tc, const char *name, int param_count,
                           const char *return_type, bool is_canfail) {
  if (tc->func_count >= 256)
    return;
  int idx = tc->func_count++;
  tc->functions[idx].name = strdup(name);
  tc->functions[idx].param_count = param_count;
  tc->functions[idx].return_type =
      return_type ? strdup(return_type) : strdup("void");
  tc->functions[idx].is_canfail = is_canfail;
}

/* Look up function by name */
static int tc_lookup_func_idx(TypeChecker *tc, const char *name) {
  for (int i = 0; i < tc->func_count; i++) {
    if (strcmp(tc->functions[i].name, name) == 0)
      return i;
  }
  return -1;
}

/* Infer expression type (best-effort) */
static const char *infer_type(TypeChecker *tc, AstNode *node) {
  if (!node)
    return "void";

  switch (node->type) {
  case AST_INT_LITERAL:
    return "int";
  case AST_FLOAT_LITERAL:
    return "float";
  case AST_STRING_LITERAL:
    return "string";
  case AST_BOOL_LITERAL:
    return "bool";
  case AST_NONE_LITERAL:
    return "none";
  case AST_ARRAY_LITERAL:
    return "array";
  case AST_STRUCT_LITERAL:
    return "struct";
  case AST_IDENTIFIER: {
    const char *t = tc_lookup(tc, node->as.identifier.name);
    return t ? t : "any";
  }
  case AST_BINARY_OP: {
    /* Arithmetic returns number types, comparisons return bool */
    switch (node->as.binary.op) {
    case OP_EQ:
    case OP_NE:
    case OP_LT:
    case OP_GT:
    case OP_LE:
    case OP_GE:
    case OP_AND:
    case OP_OR:
      return "bool";
    case OP_ADD: {
      /* string + string = string */
      const char *left = infer_type(tc, node->as.binary.left);
      if (strcmp(left, "string") == 0)
        return "string";
      return "int";
    }
    default:
      return "int";
    }
  }
  case AST_CALL: {
    /* Look up function return type */
    if (node->as.call.callee && node->as.call.callee->type == AST_IDENTIFIER) {
      int idx =
          tc_lookup_func_idx(tc, node->as.call.callee->as.identifier.name);
      if (idx >= 0)
        return tc->functions[idx].return_type;
    }
    return "any";
  }
  case AST_LAMBDA:
    return "proc";
  default:
    return "any";
  }
}

/* ======== AST Walking ======== */

static void check_node(TypeChecker *tc, AstNode *node);
static void check_block(TypeChecker *tc, AstNode *block);

static void check_block(TypeChecker *tc, AstNode *block) {
  if (!block || block->type != AST_BLOCK)
    return;
  for (int i = 0; i < block->as.block.statements.count; i++) {
    check_node(tc, block->as.block.statements.items[i]);
  }
}

static void check_proc(TypeChecker *tc, AstNode *node) {
  const char *name = node->as.proc.name;
  int param_count = node->as.proc.params.count;
  const char *return_type = NULL;

  if (node->as.proc.return_type) {
    /* TypeInfo → type name string */
    return_type = node->as.proc.return_type->name;
  }

  tc_define_func(tc, name, param_count, return_type, node->as.proc.is_canfail);

  /* Register as a callable symbol too */
  tc_define(tc, name, "proc", false);

  /* Register parameters in symbol table */
  int saved_count = tc->symbol_count;
  for (int i = 0; i < param_count; i++) {
    AstNode *p = node->as.proc.params.items[i];
    if (p->type == AST_VAR_DECL) {
      const char *ptype = "any";
      if (p->as.var_decl.type_info)
        ptype = p->as.var_decl.type_info->name;
      tc_define(tc, p->as.var_decl.name, ptype, false);
    }
  }

  /* Check body */
  check_block(tc, node->as.proc.body);

  /* Restore scope (simple approach for now) */
  /* Free param symbols */
  for (int i = saved_count; i < tc->symbol_count; i++) {
    free(tc->symbols[i].name);
    free(tc->symbols[i].type_name);
  }
  tc->symbol_count = saved_count;
  /* Re-add function itself */
  tc_define(tc, name, "proc", false);
}

static void check_node(TypeChecker *tc, AstNode *node) {
  if (!node)
    return;

  switch (node->type) {
  case AST_PROGRAM:
    for (int i = 0; i < node->as.program.declarations.count; i++) {
      check_node(tc, node->as.program.declarations.items[i]);
    }
    break;

  case AST_PROC:
    check_proc(tc, node);
    break;

  case AST_VAR_DECL: {
    const char *declared_type = "auto";
    if (node->as.var_decl.type_info)
      declared_type = node->as.var_decl.type_info->name;

    const char *init_type = "none";
    if (node->as.var_decl.initializer) {
      init_type = infer_type(tc, node->as.var_decl.initializer);

      /* Type mismatch check */
      if (node->as.var_decl.type_info && strcmp(declared_type, "auto") != 0 &&
          strcmp(init_type, "any") != 0 && strcmp(init_type, "none") != 0 &&
          strcmp(declared_type, init_type) != 0) {
        tc_warn(
            tc, node->line,
            "Type mismatch: '%s' declared as '%s' but initialized with '%s'",
            node->as.var_decl.name, declared_type, init_type);
      }
    }

    /* Register variable */
    const char *effective_type =
        node->as.var_decl.type_info ? declared_type : init_type;
    tc_define(tc, node->as.var_decl.name, effective_type,
              node->as.var_decl.is_const);
    break;
  }

  case AST_ASSIGN: {
    /* Check if target exists */
    if (node->as.assign.target &&
        node->as.assign.target->type == AST_IDENTIFIER) {
      const char *name = node->as.assign.target->as.identifier.name;
      const char *t = tc_lookup(tc, name);
      if (!t) {
        tc_warn(tc, node->line, "Assignment to undeclared variable '%s'", name);
      }
    }
    break;
  }

  case AST_CALL: {
    /* Check function arity */
    if (node->as.call.callee && node->as.call.callee->type == AST_IDENTIFIER) {
      const char *fname = node->as.call.callee->as.identifier.name;
      int argc = node->as.call.args.count;

      int idx = tc_lookup_func_idx(tc, fname);
      if (idx >= 0) {
        int expected = tc->functions[idx].param_count;
        if (argc != expected) {
          tc_warn(tc, node->line, "'%s' expects %d argument(s) but got %d",
                  fname, expected, argc);
        }
      }
      /* Don't warn on unknown functions — they might be builtins */
    }

    /* Check arguments recursively */
    for (int i = 0; i < node->as.call.args.count; i++) {
      check_node(tc, node->as.call.args.items[i]);
    }
    break;
  }

  case AST_IF:
    check_node(tc, node->as.if_stmt.condition);
    check_node(tc, node->as.if_stmt.then_branch);
    check_node(tc, node->as.if_stmt.else_branch);
    break;

  case AST_WHILE:
    check_node(tc, node->as.while_stmt.condition);
    check_block(tc, node->as.while_stmt.body);
    break;

  case AST_FOR:
    /* Register loop variable */
    if (node->as.for_stmt.var_name)
      tc_define(tc, node->as.for_stmt.var_name, "any", false);
    check_block(tc, node->as.for_stmt.body);
    break;

  case AST_BLOCK:
    check_block(tc, node);
    break;

  case AST_GIVE:
    /* Could check return type matches proc declaration */
    break;

  case AST_TRY:
    check_block(tc, node->as.try_stmt.try_block);
    for (int i = 0; i < node->as.try_stmt.catches.count; i++) {
      check_node(tc, node->as.try_stmt.catches.items[i]);
    }
    break;

  case AST_WHEN:
    /* Check subject expression */
    check_node(tc, node->as.when_stmt.value);
    break;

  case AST_ENUM:
  case AST_EXTEND:
  case AST_STRUCT:
  case AST_NONE_LITERAL:
  case AST_PRAGMA:
    /* No type checking needed */

  default:
    /* Literals, identifiers, etc. — no action needed */
    break;
  }
}

/* ======== Public API ======== */

void typechecker_init(TypeChecker *tc) {
  memset(tc, 0, sizeof(TypeChecker));

  /* Register builtins */
  tc_define(tc, "print", "proc", false);
  tc_define(tc, "str", "proc", false);
  tc_define(tc, "len", "proc", false);
  tc_define(tc, "push", "proc", false);
  tc_define(tc, "pop", "proc", false);
  tc_define(tc, "typeof", "proc", false);
  tc_define(tc, "sizeof", "proc", false);
  tc_define(tc, "input", "proc", false);
  tc_define(tc, "int_parse", "proc", false);
  tc_define(tc, "float_parse", "proc", false);
  tc_define(tc, "abs", "proc", false);
  tc_define(tc, "min", "proc", false);
  tc_define(tc, "max", "proc", false);
  tc_define(tc, "range", "proc", false);
  tc_define(tc, "filter", "proc", false);
  tc_define(tc, "map", "proc", false);
  tc_define(tc, "reduce", "proc", false);
  tc_define(tc, "collect", "proc", false);
}

int typecheck(TypeChecker *tc, AstNode *program) {
  check_node(tc, program);
  return tc->warning_count + tc->error_count;
}

void typechecker_report(TypeChecker *tc) {
  if (tc->warning_count > 0 || tc->error_count > 0) {
    fprintf(stderr, "[typecheck] %d warning(s), %d error(s)\n",
            tc->warning_count, tc->error_count);
  }
  /* Cleanup */
  for (int i = 0; i < tc->symbol_count; i++) {
    free(tc->symbols[i].name);
    free(tc->symbols[i].type_name);
  }
  for (int i = 0; i < tc->func_count; i++) {
    free(tc->functions[i].name);
    free(tc->functions[i].return_type);
  }
}
