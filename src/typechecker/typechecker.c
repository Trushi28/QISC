/*
 * QISC Type Checker - Implementation
 *
 * Gradual but stricter validation for symbols, function signatures,
 * assignments, and returns. Warnings remain for soft issues; hard
 * mismatches are reported as errors and now block execution.
 */

#include "typechecker.h"
#include "qisc.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======== Diagnostics ======== */

static void tc_warn(TypeChecker *tc, int line, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "[typecheck] Warning (line %d): ", line);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
  tc->warning_count++;
}

static void tc_error(TypeChecker *tc, int line, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  fprintf(stderr, "[typecheck] Error (line %d): ", line);
  vfprintf(stderr, fmt, args);
  fprintf(stderr, "\n");
  va_end(args);
  tc->error_count++;
}

/* ======== Symbol Tables ======== */

static TcSymbol *tc_lookup_symbol(TypeChecker *tc, const char *name) {
  for (int i = tc->symbol_count - 1; i >= 0; i--) {
    if (strcmp(tc->symbols[i].name, name) == 0) {
      return &tc->symbols[i];
    }
  }
  return NULL;
}

static const char *tc_lookup(TypeChecker *tc, const char *name) {
  TcSymbol *symbol = tc_lookup_symbol(tc, name);
  return symbol ? symbol->type_name : NULL;
}

static void tc_define(TypeChecker *tc, const char *name, const char *type_name,
                      bool is_const) {
  if (tc->symbol_count >= TYPECHECKER_MAX_SYMBOLS)
    return;

  tc->symbols[tc->symbol_count].name = strdup(name);
  tc->symbols[tc->symbol_count].type_name = strdup(type_name);
  tc->symbols[tc->symbol_count].is_const = is_const;
  tc->symbol_count++;
}

static int tc_lookup_func_idx(TypeChecker *tc, const char *name) {
  for (int i = 0; i < tc->func_count; i++) {
    if (strcmp(tc->functions[i].name, name) == 0)
      return i;
  }
  return -1;
}

static int tc_define_func_signature(TypeChecker *tc, const char *name,
                                    int param_count,
                                    const char *return_type,
                                    bool is_canfail,
                                    const char *param_types[]) {
  int idx = tc_lookup_func_idx(tc, name);
  if (idx >= 0)
    return idx;

  if (tc->func_count >= TYPECHECKER_MAX_FUNCTIONS)
    return -1;

  idx = tc->func_count++;
  tc->functions[idx].name = strdup(name);
  tc->functions[idx].param_count = param_count;
  tc->functions[idx].return_type =
      strdup(return_type ? return_type : "void");
  tc->functions[idx].is_canfail = is_canfail;

  for (int i = 0; i < TYPECHECKER_MAX_PARAMS; i++) {
    tc->functions[idx].param_types[i] = NULL;
  }

  for (int i = 0; i < param_count && i < TYPECHECKER_MAX_PARAMS; i++) {
    if (param_types && param_types[i]) {
      tc->functions[idx].param_types[i] = strdup(param_types[i]);
    }
  }

  return idx;
}

static void tc_restore_symbols(TypeChecker *tc, int saved_count) {
  for (int i = saved_count; i < tc->symbol_count; i++) {
    free(tc->symbols[i].name);
    free(tc->symbols[i].type_name);
    tc->symbols[i].name = NULL;
    tc->symbols[i].type_name = NULL;
  }
  tc->symbol_count = saved_count;
}

/* ======== Type Helpers ======== */

static bool tc_is_dynamic_type(const char *type_name) {
  if (!type_name)
    return true;

  return strcmp(type_name, "any") == 0 || strcmp(type_name, "auto") == 0;
}

static bool tc_types_compatible(const char *expected, const char *actual) {
  if (!expected || !actual)
    return true;

  if (strcmp(expected, actual) == 0)
    return true;

  if (tc_is_dynamic_type(expected) || tc_is_dynamic_type(actual))
    return true;

  if (strcmp(expected, "float") == 0 && strcmp(actual, "int") == 0)
    return true;

  if (strcmp(expected, "proc") == 0 && strcmp(actual, "proc") == 0)
    return true;

  return false;
}

static bool tc_is_known_proc(TypeChecker *tc, const char *name) {
  int func_idx = tc_lookup_func_idx(tc, name);
  TcSymbol *symbol = tc_lookup_symbol(tc, name);

  if (func_idx >= 0)
    return true;

  return symbol && strcmp(symbol->type_name, "proc") == 0;
}

/* ======== Inference ======== */

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
    return node->as.struct_decl.name ? node->as.struct_decl.name : "struct";
  case AST_IDENTIFIER: {
    const char *t = tc_lookup(tc, node->as.identifier.name);
    return t ? t : "any";
  }
  case AST_ASSIGN:
    return infer_type(tc, node->as.assign.value);
  case AST_MEMBER: {
    const char *object_type = infer_type(tc, node->as.member.object);
    if (strcmp(node->as.member.member, "length") == 0)
      return "int";
    if (strcmp(object_type, "enum") == 0)
      return "int";
    return "any";
  }
  case AST_INDEX:
    return "any";
  case AST_BINARY_OP: {
    switch (node->as.binary.op) {
    case OP_EQ:
    case OP_NE:
    case OP_LT:
    case OP_GT:
    case OP_LE:
    case OP_GE:
    case OP_AND:
    case OP_OR:
    case OP_HAS:
      return "bool";
    case OP_ADD: {
      const char *left = infer_type(tc, node->as.binary.left);
      const char *right = infer_type(tc, node->as.binary.right);
      if (strcmp(left, "string") == 0 || strcmp(right, "string") == 0)
        return "string";
      if (strcmp(left, "float") == 0 || strcmp(right, "float") == 0)
        return "float";
      return "int";
    }
    case OP_DIV: {
      const char *left = infer_type(tc, node->as.binary.left);
      const char *right = infer_type(tc, node->as.binary.right);
      if (strcmp(left, "float") == 0 || strcmp(right, "float") == 0)
        return "float";
      return "int";
    }
    default:
      return "int";
    }
  }
  case AST_CALL: {
    if (node->as.call.callee && node->as.call.callee->type == AST_IDENTIFIER) {
      int idx =
          tc_lookup_func_idx(tc, node->as.call.callee->as.identifier.name);
      if (idx >= 0)
        return tc->functions[idx].return_type;

      if (tc_is_known_proc(tc, node->as.call.callee->as.identifier.name))
        return "any";
    }
    return "any";
  }
  case AST_LAMBDA:
    return "proc";
  case AST_PIPELINE:
    return infer_type(tc, node->as.binary.right);
  default:
    return "any";
  }
}

/* ======== AST Walking ======== */

static void check_node(TypeChecker *tc, AstNode *node);
static void check_block(TypeChecker *tc, AstNode *block);

static void collect_program_declarations(TypeChecker *tc, AstNode *program) {
  if (!program || program->type != AST_PROGRAM)
    return;

  for (int i = 0; i < program->as.program.declarations.count; i++) {
    AstNode *decl = program->as.program.declarations.items[i];
    if (!decl)
      continue;

    if (decl->type == AST_PROC) {
      const char *param_types[TYPECHECKER_MAX_PARAMS] = {0};
      const char *return_type = decl->as.proc.return_type
                                    ? decl->as.proc.return_type->name
                                    : "void";

      for (int p = 0; p < decl->as.proc.params.count && p < TYPECHECKER_MAX_PARAMS;
           p++) {
        AstNode *param = decl->as.proc.params.items[p];
        if (param && param->type == AST_VAR_DECL && param->as.var_decl.type_info) {
          param_types[p] = param->as.var_decl.type_info->name;
        } else {
          param_types[p] = "any";
        }
      }

      tc_define_func_signature(tc, decl->as.proc.name, decl->as.proc.params.count,
                               return_type, decl->as.proc.is_canfail,
                               param_types);

      if (!tc_lookup_symbol(tc, decl->as.proc.name)) {
        tc_define(tc, decl->as.proc.name, "proc", false);
      }
    } else if (decl->type == AST_STRUCT) {
      if (!tc_lookup_symbol(tc, decl->as.struct_decl.name)) {
        tc_define(tc, decl->as.struct_decl.name, decl->as.struct_decl.name,
                  true);
      }
    } else if (decl->type == AST_ENUM) {
      if (!tc_lookup_symbol(tc, decl->as.enum_decl.name)) {
        tc_define(tc, decl->as.enum_decl.name, "enum", true);
      }
    }
  }
}

static void check_block(TypeChecker *tc, AstNode *block) {
  int saved_count;

  if (!block || block->type != AST_BLOCK)
    return;

  saved_count = tc->symbol_count;
  for (int i = 0; i < block->as.block.statements.count; i++) {
    check_node(tc, block->as.block.statements.items[i]);
  }
  tc_restore_symbols(tc, saved_count);
}

static void check_proc(TypeChecker *tc, AstNode *node) {
  int saved_count = tc->symbol_count;
  const char *saved_proc_name = tc->current_proc_name;
  const char *saved_return_type = tc->current_return_type;

  tc->current_proc_name = node->as.proc.name;
  tc->current_return_type =
      node->as.proc.return_type ? node->as.proc.return_type->name : "void";

  for (int i = 0; i < node->as.proc.params.count; i++) {
    AstNode *param = node->as.proc.params.items[i];
    const char *param_type = "any";

    if (param->type == AST_VAR_DECL && param->as.var_decl.type_info) {
      param_type = param->as.var_decl.type_info->name;
    }

    tc_define(tc, param->as.var_decl.name, param_type, false);
  }

  check_block(tc, node->as.proc.body);

  tc_restore_symbols(tc, saved_count);
  tc->current_proc_name = saved_proc_name;
  tc->current_return_type = saved_return_type;
}

static void check_call(TypeChecker *tc, AstNode *node) {
  int argc;

  if (!node || node->type != AST_CALL)
    return;

  argc = node->as.call.args.count;

  if (node->as.call.callee && node->as.call.callee->type == AST_IDENTIFIER) {
    const char *fname = node->as.call.callee->as.identifier.name;
    int idx = tc_lookup_func_idx(tc, fname);

    if (strcmp(fname, "stream_reduce") == 0) {
      for (int i = 0; i < argc; i++) {
        check_node(tc, node->as.call.args.items[i]);
      }
      return;
    }

    if (idx >= 0) {
      TcFunction *fn = &tc->functions[idx];
      if (argc != fn->param_count) {
        tc_error(tc, node->line, "'%s' expects %d argument(s) but got %d",
                 fname, fn->param_count, argc);
      }

      for (int i = 0; i < argc; i++) {
        AstNode *arg = node->as.call.args.items[i];
        check_node(tc, arg);

        if (i < fn->param_count && i < TYPECHECKER_MAX_PARAMS &&
            fn->param_types[i]) {
          const char *actual = infer_type(tc, arg);
          if (!tc_types_compatible(fn->param_types[i], actual)) {
            tc_error(tc, arg->line,
                     "Argument %d to '%s' expects '%s' but got '%s'", i + 1,
                     fname, fn->param_types[i], actual);
          }
        }
      }
      return;
    }

    if (!tc_is_known_proc(tc, fname)) {
      tc_error(tc, node->line, "Undefined function or callable '%s'", fname);
    }
  } else {
    check_node(tc, node->as.call.callee);
  }

  for (int i = 0; i < argc; i++) {
    check_node(tc, node->as.call.args.items[i]);
  }
}

static void check_node(TypeChecker *tc, AstNode *node) {
  if (!node)
    return;

  switch (node->type) {
  case AST_PROGRAM:
    collect_program_declarations(tc, node);
    for (int i = 0; i < node->as.program.declarations.count; i++) {
      check_node(tc, node->as.program.declarations.items[i]);
    }
    break;

  case AST_PROC:
    check_proc(tc, node);
    break;

  case AST_VAR_DECL: {
    const char *declared_type =
        node->as.var_decl.type_info ? node->as.var_decl.type_info->name : "auto";
    const char *effective_type = declared_type;

    if (node->as.var_decl.initializer) {
      const char *init_type;
      check_node(tc, node->as.var_decl.initializer);
      init_type = infer_type(tc, node->as.var_decl.initializer);

      if (node->as.var_decl.type_info) {
        if (!tc_types_compatible(declared_type, init_type) &&
            strcmp(init_type, "none") != 0) {
          tc_error(tc, node->line,
                   "Type mismatch: '%s' declared as '%s' but initialized with '%s'",
                   node->as.var_decl.name, declared_type, init_type);
        }
      } else {
        effective_type = init_type;
      }
    } else if (!node->as.var_decl.type_info) {
      effective_type = "any";
    }

    tc_define(tc, node->as.var_decl.name, effective_type,
              node->as.var_decl.is_const);
    break;
  }

  case AST_ASSIGN: {
    check_node(tc, node->as.assign.value);

    if (node->as.assign.target &&
        node->as.assign.target->type == AST_IDENTIFIER) {
      const char *value_type = infer_type(tc, node->as.assign.value);
      TcSymbol *symbol =
          tc_lookup_symbol(tc, node->as.assign.target->as.identifier.name);

      if (!symbol) {
        tc_error(tc, node->line, "Assignment to undeclared variable '%s'",
                 node->as.assign.target->as.identifier.name);
        break;
      }

      if (symbol->is_const) {
        tc_error(tc, node->line, "Cannot assign to const variable '%s'",
                 symbol->name);
      }

      if (!tc_types_compatible(symbol->type_name, value_type) &&
          strcmp(value_type, "none") != 0) {
        tc_error(tc, node->line,
                 "Cannot assign value of type '%s' to '%s' of type '%s'",
                 value_type, symbol->name, symbol->type_name);
      }
    } else {
      check_node(tc, node->as.assign.target);
    }
    break;
  }

  case AST_IDENTIFIER:
    if (strcmp(node->as.identifier.name, "_") == 0)
      break;
    if (!tc_lookup_symbol(tc, node->as.identifier.name) &&
        tc_lookup_func_idx(tc, node->as.identifier.name) < 0) {
      tc_error(tc, node->line, "Undefined identifier '%s'",
               node->as.identifier.name);
    }
    break;

  case AST_CALL:
    check_call(tc, node);
    break;

  case AST_BINARY_OP:
    check_node(tc, node->as.binary.left);
    check_node(tc, node->as.binary.right);
    break;

  case AST_UNARY_OP:
    check_node(tc, node->as.unary.operand);
    break;

  case AST_MEMBER:
    check_node(tc, node->as.member.object);
    break;

  case AST_INDEX: {
    const char *index_type;
    check_node(tc, node->as.index.object);
    check_node(tc, node->as.index.index);
    index_type = infer_type(tc, node->as.index.index);
    if (!tc_types_compatible("int", index_type)) {
      tc_error(tc, node->line, "Index expression must be 'int', got '%s'",
               index_type);
    }
    break;
  }

  case AST_ARRAY_LITERAL:
    for (int i = 0; i < node->as.array_literal.elements.count; i++) {
      check_node(tc, node->as.array_literal.elements.items[i]);
    }
    break;

  case AST_STRUCT_LITERAL:
    for (int i = 0; i < node->as.struct_decl.fields.count; i++) {
      AstNode *field = node->as.struct_decl.fields.items[i];
      if (field && field->type == AST_ASSIGN) {
        check_node(tc, field->as.assign.value);
      }
    }
    break;

  case AST_IF:
    check_node(tc, node->as.if_stmt.condition);
    check_node(tc, node->as.if_stmt.then_branch);
    check_node(tc, node->as.if_stmt.else_branch);
    break;

  case AST_WHILE:
    check_node(tc, node->as.while_stmt.condition);
    check_block(tc, node->as.while_stmt.body);
    break;

  case AST_FOR: {
    int saved_count = tc->symbol_count;
    if (node->as.for_stmt.iterable) {
      check_node(tc, node->as.for_stmt.iterable);
    }
    if (node->as.for_stmt.var_name) {
      tc_define(tc, node->as.for_stmt.var_name, "any", false);
    }
    check_block(tc, node->as.for_stmt.body);
    tc_restore_symbols(tc, saved_count);
    break;
  }

  case AST_BLOCK:
    check_block(tc, node);
    break;

  case AST_GIVE:
    if (node->as.give_stmt.value) {
      const char *value_type;
      check_node(tc, node->as.give_stmt.value);
      value_type = infer_type(tc, node->as.give_stmt.value);
      if (tc->current_return_type &&
          !tc_types_compatible(tc->current_return_type, value_type) &&
          strcmp(value_type, "none") != 0) {
        tc_error(tc, node->line,
                 "Procedure '%s' returns '%s' but give statement produced '%s'",
                 tc->current_proc_name ? tc->current_proc_name : "<anon>",
                 tc->current_return_type, value_type);
      }
    }
    break;

  case AST_TRY:
    check_block(tc, node->as.try_stmt.try_block);
    for (int i = 0; i < node->as.try_stmt.catches.count; i++) {
      AstNode *catch_node = node->as.try_stmt.catches.items[i];
      int saved_count = tc->symbol_count;
      if (catch_node && catch_node->type == AST_IF &&
          catch_node->as.if_stmt.condition &&
          catch_node->as.if_stmt.condition->type == AST_IDENTIFIER) {
        tc_define(tc, catch_node->as.if_stmt.condition->as.identifier.name,
                  "string", false);
        check_node(tc, catch_node->as.if_stmt.then_branch);
        tc_restore_symbols(tc, saved_count);
      }
    }
    break;

  case AST_WHEN:
    check_node(tc, node->as.when_stmt.value);
    for (int i = 0; i < node->as.when_stmt.cases.count; i++) {
      check_node(tc, node->as.when_stmt.cases.items[i]);
    }
    break;

  case AST_EXTEND:
    for (int i = 0; i < node->as.extend_decl.methods.count; i++) {
      check_node(tc, node->as.extend_decl.methods.items[i]);
    }
    break;

  case AST_PIPELINE:
    check_node(tc, node->as.binary.left);
    check_node(tc, node->as.binary.right);
    break;

  case AST_LAMBDA: {
    int saved_count = tc->symbol_count;
    const char *saved_proc_name = tc->current_proc_name;
    const char *saved_return_type = tc->current_return_type;
    tc->current_proc_name = "<lambda>";
    tc->current_return_type =
        (node->as.lambda.body && node->as.lambda.body->type != AST_BLOCK)
            ? infer_type(tc, node->as.lambda.body)
            : "any";
    for (int i = 0; i < node->as.lambda.params.count; i++) {
      AstNode *param = node->as.lambda.params.items[i];
      if (param && param->type == AST_VAR_DECL) {
        tc_define(tc, param->as.var_decl.name, "any", false);
      }
    }
    check_node(tc, node->as.lambda.body);
    tc_restore_symbols(tc, saved_count);
    tc->current_proc_name = saved_proc_name;
    tc->current_return_type = saved_return_type;
    break;
  }

  case AST_ENUM:
  case AST_STRUCT:
  case AST_MODULE:
  case AST_IMPORT:
  case AST_PRAGMA:
  case AST_NONE_LITERAL:
  case AST_INT_LITERAL:
  case AST_FLOAT_LITERAL:
  case AST_STRING_LITERAL:
  case AST_BOOL_LITERAL:
  case AST_BREAK:
  case AST_CONTINUE:
  case AST_FAIL:
    break;

  default:
    tc_warn(tc, node->line, "Skipped unsupported node type %d during checking",
            node->type);
    break;
  }
}

/* ======== Public API ======== */

void typechecker_init(TypeChecker *tc) {
  memset(tc, 0, sizeof(TypeChecker));
  const char *stream_reduce_params[3] = {"any", "proc", "any"};

  /* Builtins remain dynamic, but should still count as known callables. */
  tc_define(tc, "print", "proc", false);
  tc_define(tc, "str", "proc", false);
  tc_define(tc, "stdin_text", "proc", false);
  tc_define(tc, "stdin_lines", "proc", false);
  tc_define(tc, "stream_range", "proc", false);
  tc_define(tc, "stream_file_lines", "proc", false);
  tc_define(tc, "stream_take", "proc", false);
  tc_define(tc, "stream_skip", "proc", false);
  tc_define(tc, "stream_map", "proc", false);
  tc_define(tc, "stream_filter", "proc", false);
  tc_define(tc, "stream_reduce", "proc", false);
  tc_define_func_signature(tc, "stream_reduce", 3, "any", false,
                           stream_reduce_params);
  tc_define(tc, "stream_count", "proc", false);
  tc_define(tc, "stream_first", "proc", false);
  tc_define(tc, "stream_collect", "proc", false);
  tc_define(tc, "stdout_write", "proc", false);
  tc_define(tc, "stderr_write", "proc", false);
  tc_define(tc, "read_file", "proc", false);
  tc_define(tc, "file_lines", "proc", false);
  tc_define(tc, "len", "proc", false);
  tc_define(tc, "take", "proc", false);
  tc_define(tc, "skip", "proc", false);
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

  for (int i = 0; i < tc->symbol_count; i++) {
    free(tc->symbols[i].name);
    free(tc->symbols[i].type_name);
  }

  for (int i = 0; i < tc->func_count; i++) {
    free(tc->functions[i].name);
    free(tc->functions[i].return_type);
    for (int j = 0; j < TYPECHECKER_MAX_PARAMS; j++) {
      free(tc->functions[i].param_types[j]);
    }
  }
}
