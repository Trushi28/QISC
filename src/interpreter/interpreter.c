/*
 * QISC Tree-Walk Interpreter Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "interpreter.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======== Value Operations ======== */

Value value_none(void) {
  Value v = {.type = VAL_NONE};
  return v;
}

Value value_int(int64_t val) {
  Value v = {.type = VAL_INT};
  v.as.int_val = val;
  return v;
}

Value value_float(double val) {
  Value v = {.type = VAL_FLOAT};
  v.as.float_val = val;
  return v;
}

Value value_bool(bool val) {
  Value v = {.type = VAL_BOOL};
  v.as.bool_val = val;
  return v;
}

Value value_string(const char *str, int length) {
  Value v = {.type = VAL_STRING};
  v.as.string_val.str = malloc(length + 1);
  memcpy(v.as.string_val.str, str, length);
  v.as.string_val.str[length] = '\0';
  v.as.string_val.length = length;
  return v;
}

Value value_error(const char *message) {
  Value v = {.type = VAL_ERROR};
  v.as.error_val.message = strdup(message);
  return v;
}

void value_free(Value *val) {
  if (val->type == VAL_STRING) {
    free(val->as.string_val.str);
  } else if (val->type == VAL_ERROR) {
    free(val->as.error_val.message);
  }
}

void value_print(Value *val) {
  switch (val->type) {
  case VAL_NONE:
    printf("none");
    break;
  case VAL_INT:
    printf("%lld", (long long)val->as.int_val);
    break;
  case VAL_FLOAT:
    printf("%g", val->as.float_val);
    break;
  case VAL_BOOL:
    printf("%s", val->as.bool_val ? "true" : "false");
    break;
  case VAL_STRING:
    printf("%s", val->as.string_val.str);
    break;
  case VAL_PROC:
    printf("<proc>");
    break;
  case VAL_ARRAY:
    printf("[array]");
    break;
  case VAL_ERROR:
    printf("Error: %s", val->as.error_val.message);
    break;
  }
}

bool value_is_truthy(Value *val) {
  switch (val->type) {
  case VAL_NONE:
    return false;
  case VAL_BOOL:
    return val->as.bool_val;
  case VAL_INT:
    return val->as.int_val != 0;
  case VAL_FLOAT:
    return val->as.float_val != 0.0;
  case VAL_STRING:
    return val->as.string_val.length > 0;
  default:
    return true;
  }
}

/* ======== Environment Operations ======== */

Environment *env_new(Environment *parent) {
  Environment *env = calloc(1, sizeof(Environment));
  env->parent = parent;
  env->capacity = 8;
  env->names = calloc(env->capacity, sizeof(char *));
  env->values = calloc(env->capacity, sizeof(Value));
  return env;
}

void env_free(Environment *env) {
  for (int i = 0; i < env->count; i++) {
    free(env->names[i]);
    value_free(&env->values[i]);
  }
  free(env->names);
  free(env->values);
  free(env);
}

void env_define(Environment *env, const char *name, Value value) {
  /* Check if already exists in current scope */
  for (int i = 0; i < env->count; i++) {
    if (strcmp(env->names[i], name) == 0) {
      value_free(&env->values[i]);
      env->values[i] = value;
      return;
    }
  }

  /* Add new */
  if (env->count >= env->capacity) {
    env->capacity *= 2;
    env->names = realloc(env->names, env->capacity * sizeof(char *));
    env->values = realloc(env->values, env->capacity * sizeof(Value));
  }
  env->names[env->count] = strdup(name);
  env->values[env->count] = value;
  env->count++;
}

Value *env_get(Environment *env, const char *name) {
  for (int i = 0; i < env->count; i++) {
    if (strcmp(env->names[i], name) == 0) {
      return &env->values[i];
    }
  }
  if (env->parent) {
    return env_get(env->parent, name);
  }
  return NULL;
}

bool env_set(Environment *env, const char *name, Value value) {
  for (int i = 0; i < env->count; i++) {
    if (strcmp(env->names[i], name) == 0) {
      value_free(&env->values[i]);
      env->values[i] = value;
      return true;
    }
  }
  if (env->parent) {
    return env_set(env->parent, name, value);
  }
  return false;
}

/* ======== Built-in Functions ======== */

static Value builtin_print(Interpreter *interp, Value *args, int argc) {
  (void)interp;
  for (int i = 0; i < argc; i++) {
    value_print(&args[i]);
    if (i < argc - 1)
      printf(" ");
  }
  printf("\n");
  return value_none();
}

static Value builtin_len(Interpreter *interp, Value *args, int argc) {
  (void)interp;
  if (argc != 1) {
    return value_error("len() takes exactly 1 argument");
  }
  if (args[0].type == VAL_STRING) {
    return value_int(args[0].as.string_val.length);
  }
  if (args[0].type == VAL_ARRAY) {
    return value_int(args[0].as.array_val.count);
  }
  return value_error("len() requires string or array");
}

static Value builtin_int(Interpreter *interp, Value *args, int argc) {
  (void)interp;
  if (argc != 1)
    return value_error("int() takes exactly 1 argument");
  switch (args[0].type) {
  case VAL_INT:
    return args[0];
  case VAL_FLOAT:
    return value_int((int64_t)args[0].as.float_val);
  case VAL_BOOL:
    return value_int(args[0].as.bool_val ? 1 : 0);
  case VAL_STRING:
    return value_int(strtoll(args[0].as.string_val.str, NULL, 10));
  default:
    return value_error("Cannot convert to int");
  }
}

static Value builtin_float(Interpreter *interp, Value *args, int argc) {
  (void)interp;
  if (argc != 1)
    return value_error("float() takes exactly 1 argument");
  switch (args[0].type) {
  case VAL_INT:
    return value_float((double)args[0].as.int_val);
  case VAL_FLOAT:
    return args[0];
  case VAL_STRING:
    return value_float(strtod(args[0].as.string_val.str, NULL));
  default:
    return value_error("Cannot convert to float");
  }
}

static Value builtin_str(Interpreter *interp, Value *args, int argc) {
  (void)interp;
  if (argc != 1)
    return value_error("str() takes exactly 1 argument");
  char buf[64];
  switch (args[0].type) {
  case VAL_INT:
    snprintf(buf, sizeof(buf), "%lld", (long long)args[0].as.int_val);
    return value_string(buf, strlen(buf));
  case VAL_FLOAT:
    snprintf(buf, sizeof(buf), "%g", args[0].as.float_val);
    return value_string(buf, strlen(buf));
  case VAL_BOOL:
    return value_string(args[0].as.bool_val ? "true" : "false",
                        args[0].as.bool_val ? 4 : 5);
  case VAL_STRING:
    return args[0];
  default:
    return value_error("Cannot convert to string");
  }
}

/* Register built-ins */
static void register_builtins(Interpreter *interp) {
  /* Built-ins are handled specially in call evaluation */
  (void)interp;
}

/* ======== Interpreter Core ======== */

void interpreter_init(Interpreter *interp) {
  interp->global = env_new(NULL);
  interp->current = interp->global;
  interp->had_error = false;
  interp->error_message[0] = '\0';
  interp->returning = false;
  interp->return_value = value_none();
  interp->breaking = false;
  interp->continuing = false;

  register_builtins(interp);
}

void interpreter_free(Interpreter *interp) {
  /* Free all environments */
  Environment *env = interp->current;
  while (env && env != interp->global) {
    Environment *parent = env->parent;
    env_free(env);
    env = parent;
  }
  if (interp->global) {
    env_free(interp->global);
  }
}

static Value runtime_error(Interpreter *interp, const char *format, ...) {
  va_list args;
  va_start(args, format);
  vsnprintf(interp->error_message, sizeof(interp->error_message), format, args);
  va_end(args);
  interp->had_error = true;
  return value_error(interp->error_message);
}

/* Forward declaration */
static Value evaluate(Interpreter *interp, AstNode *node);
static void execute(Interpreter *interp, AstNode *node);

/* ======== Expression Evaluation ======== */

static Value eval_binary(Interpreter *interp, AstNode *node) {
  Value left = evaluate(interp, node->as.binary.left);
  if (interp->had_error)
    return left;

  Value right = evaluate(interp, node->as.binary.right);
  if (interp->had_error)
    return right;

  BinaryOp op = node->as.binary.op;

  /* Handle string concatenation */
  if (op == OP_ADD && left.type == VAL_STRING && right.type == VAL_STRING) {
    int new_len = left.as.string_val.length + right.as.string_val.length;
    char *new_str = malloc(new_len + 1);
    memcpy(new_str, left.as.string_val.str, left.as.string_val.length);
    memcpy(new_str + left.as.string_val.length, right.as.string_val.str,
           right.as.string_val.length);
    new_str[new_len] = '\0';
    Value result = value_string(new_str, new_len);
    free(new_str);
    return result;
  }

  /* Numeric operations */
  if (left.type == VAL_INT && right.type == VAL_INT) {
    int64_t l = left.as.int_val;
    int64_t r = right.as.int_val;
    switch (op) {
    case OP_ADD:
      return value_int(l + r);
    case OP_SUB:
      return value_int(l - r);
    case OP_MUL:
      return value_int(l * r);
    case OP_DIV:
      if (r == 0)
        return runtime_error(interp, "Division by zero");
      return value_int(l / r);
    case OP_MOD:
      if (r == 0)
        return runtime_error(interp, "Modulo by zero");
      return value_int(l % r);
    case OP_LT:
      return value_bool(l < r);
    case OP_GT:
      return value_bool(l > r);
    case OP_LE:
      return value_bool(l <= r);
    case OP_GE:
      return value_bool(l >= r);
    case OP_EQ:
      return value_bool(l == r);
    case OP_NE:
      return value_bool(l != r);
    case OP_BIT_AND:
      return value_int(l & r);
    case OP_BIT_OR:
      return value_int(l | r);
    case OP_BIT_XOR:
      return value_int(l ^ r);
    case OP_LSHIFT:
      return value_int(l << r);
    case OP_RSHIFT:
      return value_int(l >> r);
    default:
      break;
    }
  }

  /* Float operations */
  if ((left.type == VAL_FLOAT || left.type == VAL_INT) &&
      (right.type == VAL_FLOAT || right.type == VAL_INT)) {
    double l =
        left.type == VAL_FLOAT ? left.as.float_val : (double)left.as.int_val;
    double r =
        right.type == VAL_FLOAT ? right.as.float_val : (double)right.as.int_val;
    switch (op) {
    case OP_ADD:
      return value_float(l + r);
    case OP_SUB:
      return value_float(l - r);
    case OP_MUL:
      return value_float(l * r);
    case OP_DIV:
      return value_float(l / r);
    case OP_LT:
      return value_bool(l < r);
    case OP_GT:
      return value_bool(l > r);
    case OP_LE:
      return value_bool(l <= r);
    case OP_GE:
      return value_bool(l >= r);
    case OP_EQ:
      return value_bool(l == r);
    case OP_NE:
      return value_bool(l != r);
    default:
      break;
    }
  }

  /* Logical operations */
  if (op == OP_AND) {
    return value_bool(value_is_truthy(&left) && value_is_truthy(&right));
  }
  if (op == OP_OR) {
    return value_bool(value_is_truthy(&left) || value_is_truthy(&right));
  }

  return runtime_error(interp, "Invalid operands for binary operator");
}

static Value eval_unary(Interpreter *interp, AstNode *node) {
  /* For increment/decrement, we need special handling */
  if (node->as.unary.op == OP_INC || node->as.unary.op == OP_DEC) {
    /* Operand must be an identifier for increment/decrement */
    if (node->as.unary.operand->type != AST_IDENTIFIER) {
      return runtime_error(interp, "Increment/decrement requires a variable");
    }
    char *name = node->as.unary.operand->as.identifier.name;
    Value *val = env_get(interp->current, name);
    if (!val) {
      return runtime_error(interp, "Undefined variable: %s", name);
    }
    if (val->type != VAL_INT) {
      return runtime_error(interp, "Increment/decrement requires an integer");
    }

    /* Get current value (for postfix, we return original) */
    int64_t original = val->as.int_val;

    /* Update variable in place */
    if (node->as.unary.op == OP_INC) {
      val->as.int_val++;
    } else {
      val->as.int_val--;
    }

    /* Return original value (postfix behavior) */
    return value_int(original);
  }

  Value operand = evaluate(interp, node->as.unary.operand);
  if (interp->had_error)
    return operand;

  switch (node->as.unary.op) {
  case OP_NEG:
    if (operand.type == VAL_INT)
      return value_int(-operand.as.int_val);
    if (operand.type == VAL_FLOAT)
      return value_float(-operand.as.float_val);
    return runtime_error(interp, "Cannot negate non-number");
  case OP_NOT:
    return value_bool(!value_is_truthy(&operand));
  case OP_BIT_NOT:
    if (operand.type == VAL_INT)
      return value_int(~operand.as.int_val);
    return runtime_error(interp, "Cannot bitwise-not non-integer");
  default:
    return runtime_error(interp, "Unknown unary operator");
  }
}

static Value eval_call(Interpreter *interp, AstNode *node) {
  /* Handle built-in functions by name */
  if (node->as.call.callee->type == AST_IDENTIFIER) {
    char *name = node->as.call.callee->as.identifier.name;

    /* Evaluate arguments */
    int argc = node->as.call.args.count;
    Value *args = malloc(argc * sizeof(Value));
    for (int i = 0; i < argc; i++) {
      args[i] = evaluate(interp, node->as.call.args.items[i]);
      if (interp->had_error) {
        free(args);
        return value_error(interp->error_message);
      }
    }

    Value result;

    /* Built-in functions */
    if (strcmp(name, "print") == 0) {
      result = builtin_print(interp, args, argc);
    } else if (strcmp(name, "len") == 0) {
      result = builtin_len(interp, args, argc);
    } else if (strcmp(name, "int") == 0) {
      result = builtin_int(interp, args, argc);
    } else if (strcmp(name, "float") == 0) {
      result = builtin_float(interp, args, argc);
    } else if (strcmp(name, "str") == 0) {
      result = builtin_str(interp, args, argc);
    } else {
      /* Look up user-defined function */
      Value *proc_val = env_get(interp->current, name);
      if (!proc_val || proc_val->type != VAL_PROC) {
        free(args);
        return runtime_error(interp, "Undefined function: %s", name);
      }

      /* Create new environment for function call */
      AstNode *proc = proc_val->as.proc_val.proc;
      Environment *func_env = env_new(interp->global);

      /* Bind parameters */
      for (int i = 0; i < proc->as.proc.params.count && i < argc; i++) {
        AstNode *param = proc->as.proc.params.items[i];
        env_define(func_env, param->as.var_decl.name, args[i]);
      }

      /* Execute function body */
      Environment *prev_env = interp->current;
      interp->current = func_env;
      interp->returning = false;

      execute(interp, proc->as.proc.body);

      result = interp->return_value;
      interp->return_value = value_none();
      interp->returning = false;

      interp->current = prev_env;
      env_free(func_env);
    }

    free(args);
    return result;
  }

  return runtime_error(interp, "Expression is not callable");
}

static Value evaluate(Interpreter *interp, AstNode *node) {
  if (!node)
    return value_none();

  switch (node->type) {
  case AST_INT_LITERAL:
    return value_int(node->as.int_literal.value);

  case AST_FLOAT_LITERAL:
    return value_float(node->as.float_literal.value);

  case AST_STRING_LITERAL:
    return value_string(node->as.string_literal.value,
                        node->as.string_literal.length);

  case AST_BOOL_LITERAL:
    return value_bool(node->as.bool_literal.value);

  case AST_NONE_LITERAL:
    return value_none();

  case AST_IDENTIFIER: {
    char *name = node->as.identifier.name;
    Value *val = env_get(interp->current, name);
    if (!val) {
      return runtime_error(interp, "Undefined variable: %s", name);
    }
    return *val;
  }

  case AST_BINARY_OP:
    return eval_binary(interp, node);

  case AST_UNARY_OP:
    return eval_unary(interp, node);

  case AST_CALL:
    return eval_call(interp, node);

  default:
    return runtime_error(interp, "Cannot evaluate node type: %d", node->type);
  }
}

/* ======== Statement Execution ======== */

static void exec_block(Interpreter *interp, AstNode *node) {
  Environment *prev = interp->current;
  interp->current = env_new(prev);

  for (int i = 0; i < node->as.block.statements.count; i++) {
    execute(interp, node->as.block.statements.items[i]);
    if (interp->had_error || interp->returning || interp->breaking ||
        interp->continuing) {
      break;
    }
  }

  Environment *block_env = interp->current;
  interp->current = prev;
  env_free(block_env);
}

static void exec_if(Interpreter *interp, AstNode *node) {
  Value cond = evaluate(interp, node->as.if_stmt.condition);
  if (interp->had_error)
    return;

  if (value_is_truthy(&cond)) {
    execute(interp, node->as.if_stmt.then_branch);
  } else if (node->as.if_stmt.else_branch) {
    execute(interp, node->as.if_stmt.else_branch);
  }
}

static void exec_while(Interpreter *interp, AstNode *node) {
  while (true) {
    Value cond = evaluate(interp, node->as.while_stmt.condition);
    if (interp->had_error)
      return;

    if (!value_is_truthy(&cond))
      break;

    execute(interp, node->as.while_stmt.body);

    if (interp->breaking) {
      interp->breaking = false;
      break;
    }
    if (interp->continuing) {
      interp->continuing = false;
      continue;
    }
    if (interp->returning || interp->had_error)
      break;
  }
}

static void exec_var_decl(Interpreter *interp, AstNode *node) {
  Value val = value_none();
  if (node->as.var_decl.initializer) {
    val = evaluate(interp, node->as.var_decl.initializer);
    if (interp->had_error)
      return;
  }
  env_define(interp->current, node->as.var_decl.name, val);
}

static void exec_assign(Interpreter *interp, AstNode *node) {
  Value val = evaluate(interp, node->as.assign.value);
  if (interp->had_error)
    return;

  if (node->as.assign.target->type == AST_IDENTIFIER) {
    char *name = node->as.assign.target->as.identifier.name;
    if (!env_set(interp->current, name, val)) {
      /* If not found, define it */
      env_define(interp->current, name, val);
    }
  } else {
    runtime_error(interp, "Invalid assignment target");
  }
}

static void exec_give(Interpreter *interp, AstNode *node) {
  if (node->as.give_stmt.value) {
    interp->return_value = evaluate(interp, node->as.give_stmt.value);
  } else {
    interp->return_value = value_none();
  }
  interp->returning = true;
}

static void exec_proc(Interpreter *interp, AstNode *node) {
  Value proc_val = {.type = VAL_PROC};
  proc_val.as.proc_val.proc = node;
  proc_val.as.proc_val.closure = interp->current;
  env_define(interp->current, node->as.proc.name, proc_val);
}

static void execute(Interpreter *interp, AstNode *node) {
  if (!node || interp->had_error)
    return;

  switch (node->type) {
  case AST_BLOCK:
    exec_block(interp, node);
    break;

  case AST_IF:
    exec_if(interp, node);
    break;

  case AST_WHILE:
    exec_while(interp, node);
    break;

  case AST_VAR_DECL:
    exec_var_decl(interp, node);
    break;

  case AST_ASSIGN:
    exec_assign(interp, node);
    break;

  case AST_GIVE:
    exec_give(interp, node);
    break;

  case AST_BREAK:
    interp->breaking = true;
    break;

  case AST_CONTINUE:
    interp->continuing = true;
    break;

  case AST_PROC:
    exec_proc(interp, node);
    break;

  case AST_EXPR_STMT:
  case AST_INT_LITERAL:
  case AST_FLOAT_LITERAL:
  case AST_STRING_LITERAL:
  case AST_BOOL_LITERAL:
  case AST_IDENTIFIER:
  case AST_BINARY_OP:
  case AST_UNARY_OP:
  case AST_CALL:
    /* Expression as statement - evaluate and discard */
    evaluate(interp, node);
    break;

  case AST_PROGRAM:
    for (int i = 0; i < node->as.program.declarations.count; i++) {
      execute(interp, node->as.program.declarations.items[i]);
      if (interp->had_error || interp->returning)
        break;
    }
    break;

  default:
    runtime_error(interp, "Cannot execute node type: %d", node->type);
    break;
  }
}

/* ======== Public API ======== */

Value interpreter_run(Interpreter *interp, AstNode *program) {
  execute(interp, program);

  /* If there's a main() function, call it */
  Value *main_fn = env_get(interp->global, "main");
  if (main_fn && main_fn->type == VAL_PROC) {
    AstNode *call = ast_new_call(ast_new_identifier("main", 1, 1), 1, 1);
    Value result = eval_call(interp, call);
    ast_free(call);
    return result;
  }

  return interp->return_value;
}

Value interpreter_eval(Interpreter *interp, AstNode *expr) {
  return evaluate(interp, expr);
}
