/*
 * QISC Tree-Walk Interpreter Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "interpreter.h"
#include "../runtime/qisc_stream.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
  STREAM_KIND_UNKNOWN = 0,
  STREAM_KIND_INT = 1,
  STREAM_KIND_STRING = 2,
} StreamKind;

typedef enum {
  INFER_KIND_UNKNOWN = 0,
  INFER_KIND_INT = 1,
  INFER_KIND_STRING = 2,
  INFER_KIND_BOOL = 3,
} InferKind;

typedef enum {
  STREAM_OP_TAKE = 1,
  STREAM_OP_SKIP = 2,
  STREAM_OP_MAP = 3,
  STREAM_OP_FILTER = 4,
} StreamOpKind;

typedef struct StreamOp {
  StreamOpKind kind;
  int64_t amount;
  int64_t progress;
  StreamKind result_kind;
  Value proc;
  struct StreamOp *next;
} StreamOp;

typedef struct QiscLazyStream {
  QiscStream *handle;
  StreamKind source_kind;
  StreamKind kind;
  bool consumed;
  StreamOp *ops_head;
  StreamOp *ops_tail;
} QiscLazyStream;

typedef struct {
  const char *name;
  InferKind kind;
} InferLocal;

static Value value_stream(QiscStream *handle, StreamKind kind) {
  Value v = {.type = VAL_STREAM};
  QiscLazyStream *stream = malloc(sizeof(QiscLazyStream));
  if (!stream) {
    return value_none();
  }
  stream->handle = handle;
  stream->source_kind = kind;
  stream->kind = kind;
  stream->consumed = (handle == NULL);
  stream->ops_head = NULL;
  stream->ops_tail = NULL;
  v.as.stream_val = stream;
  return v;
}

static QiscLazyStream *value_stream_get(Value *val) {
  if (!val || val->type != VAL_STREAM || !val->as.stream_val)
    return NULL;
  if (val->as.stream_val->consumed || !val->as.stream_val->handle)
    return NULL;
  return val->as.stream_val;
}

static Value call_value(Interpreter *interp, Value *fn, Value *args, int argc);
static Value runtime_error(Interpreter *interp, const char *format, ...);

static StreamKind stream_kind_from_infer_kind(InferKind kind) {
  if (kind == INFER_KIND_INT)
    return STREAM_KIND_INT;
  if (kind == INFER_KIND_STRING)
    return STREAM_KIND_STRING;
  return STREAM_KIND_UNKNOWN;
}

static InferKind infer_kind_from_value_type(ValueType type) {
  if (type == VAL_INT)
    return INFER_KIND_INT;
  if (type == VAL_STRING)
    return INFER_KIND_STRING;
  if (type == VAL_BOOL)
    return INFER_KIND_BOOL;
  return INFER_KIND_UNKNOWN;
}

static InferKind infer_merge_kind(InferKind left, InferKind right) {
  if (left == INFER_KIND_UNKNOWN)
    return right;
  if (right == INFER_KIND_UNKNOWN)
    return left;
  if (left == right)
    return left;
  return INFER_KIND_UNKNOWN;
}

static InferKind infer_lookup_local(InferLocal *locals, int count,
                                    const char *name) {
  for (int i = count - 1; i >= 0; i--) {
    if (locals[i].name && name && strcmp(locals[i].name, name) == 0)
      return locals[i].kind;
  }
  return INFER_KIND_UNKNOWN;
}

static void infer_add_local(InferLocal *locals, int *count, const char *name,
                            InferKind kind) {
  if (!locals || !count || !name || *count >= 64)
    return;
  locals[*count].name = name;
  locals[*count].kind = kind;
  (*count)++;
}

static InferKind infer_lambda_node_kind(AstNode *node, const char *param_name,
                                        InferKind param_kind,
                                        Environment *closure,
                                        InferLocal *locals, int local_count);

static StreamKind stream_proc_result_kind(Value *proc, StreamKind param_stream_kind) {
  AstNode *node;
  InferLocal locals[64];
  int local_count = 0;
  const char *param_name = NULL;
  InferKind param_kind =
      param_stream_kind == STREAM_KIND_INT
          ? INFER_KIND_INT
          : param_stream_kind == STREAM_KIND_STRING ? INFER_KIND_STRING
                                                    : INFER_KIND_UNKNOWN;
  InferKind inferred;

  if (!proc || proc->type != VAL_PROC)
    return STREAM_KIND_UNKNOWN;

  node = proc->as.proc_val.proc;
  if (!node)
    return STREAM_KIND_UNKNOWN;

  if (node->type == AST_PROC) {
    if (!node->as.proc.return_type)
      return STREAM_KIND_UNKNOWN;
    if (strcmp(node->as.proc.return_type->name, "int") == 0)
      return STREAM_KIND_INT;
    if (strcmp(node->as.proc.return_type->name, "string") == 0)
      return STREAM_KIND_STRING;
    return STREAM_KIND_UNKNOWN;
  }

  if (node->type != AST_LAMBDA || node->as.lambda.params.count != 1 ||
      !node->as.lambda.body)
    return STREAM_KIND_UNKNOWN;

  AstNode *param = node->as.lambda.params.items[0];
  if (param->type == AST_VAR_DECL)
    param_name = param->as.var_decl.name;
  else if (param->type == AST_IDENTIFIER)
    param_name = param->as.identifier.name;

  if (param_name)
    infer_add_local(locals, &local_count, param_name, param_kind);

  inferred = infer_lambda_node_kind(node->as.lambda.body, param_name,
                                    param_kind, proc->as.proc_val.closure,
                                    locals, local_count);
  return stream_kind_from_infer_kind(inferred);
}

static InferKind infer_lambda_node_kind(AstNode *node, const char *param_name,
                                        InferKind param_kind,
                                        Environment *closure,
                                        InferLocal *locals, int local_count) {
  if (!node)
    return INFER_KIND_UNKNOWN;

  switch (node->type) {
  case AST_INT_LITERAL:
    return INFER_KIND_INT;
  case AST_STRING_LITERAL:
    return INFER_KIND_STRING;
  case AST_BOOL_LITERAL:
    return INFER_KIND_BOOL;
  case AST_IDENTIFIER: {
    if (param_name && strcmp(node->as.identifier.name, param_name) == 0)
      return param_kind;
    InferKind local_kind =
        infer_lookup_local(locals, local_count, node->as.identifier.name);
    if (local_kind != INFER_KIND_UNKNOWN)
      return local_kind;
    if (closure) {
      Value *captured = env_get(closure, node->as.identifier.name);
      if (captured)
        return infer_kind_from_value_type(captured->type);
    }
    return INFER_KIND_UNKNOWN;
  }
  case AST_UNARY_OP:
    if (node->as.unary.op == OP_NOT)
      return INFER_KIND_BOOL;
    return infer_lambda_node_kind(node->as.unary.operand, param_name,
                                  param_kind, closure, locals, local_count);
  case AST_BINARY_OP: {
    if (node->as.binary.op == OP_EQ || node->as.binary.op == OP_NE ||
        node->as.binary.op == OP_LT || node->as.binary.op == OP_GT ||
        node->as.binary.op == OP_LE || node->as.binary.op == OP_GE ||
        node->as.binary.op == OP_AND || node->as.binary.op == OP_OR)
      return INFER_KIND_BOOL;
    InferKind left = infer_lambda_node_kind(node->as.binary.left, param_name,
                                            param_kind, closure, locals,
                                            local_count);
    InferKind right = infer_lambda_node_kind(node->as.binary.right, param_name,
                                             param_kind, closure, locals,
                                             local_count);
    if (node->as.binary.op == OP_ADD &&
        (left == INFER_KIND_STRING || right == INFER_KIND_STRING))
      return INFER_KIND_STRING;
    if (left == INFER_KIND_INT && right == INFER_KIND_INT)
      return INFER_KIND_INT;
    return INFER_KIND_UNKNOWN;
  }
  case AST_CALL:
    if (node->as.call.callee && node->as.call.callee->type == AST_IDENTIFIER) {
      const char *name = node->as.call.callee->as.identifier.name;
      if (strcmp(name, "str") == 0)
        return INFER_KIND_STRING;
      if (strcmp(name, "len") == 0 || strcmp(name, "int_parse") == 0 ||
          strcmp(name, "abs") == 0 || strcmp(name, "min") == 0 ||
          strcmp(name, "max") == 0)
        return INFER_KIND_INT;
      if (strcmp(name, "stdin_text") == 0 || strcmp(name, "read_file") == 0)
        return INFER_KIND_STRING;
      if (closure) {
        Value *callee = env_get(closure, name);
        if (callee && callee->type == VAL_PROC) {
          StreamKind callee_kind =
              stream_proc_result_kind(callee, STREAM_KIND_UNKNOWN);
          if (callee_kind == STREAM_KIND_INT)
            return INFER_KIND_INT;
          if (callee_kind == STREAM_KIND_STRING)
            return INFER_KIND_STRING;
        }
      }
    }
    return INFER_KIND_UNKNOWN;
  case AST_GIVE:
    return node->as.give_stmt.value
               ? infer_lambda_node_kind(node->as.give_stmt.value, param_name,
                                        param_kind, closure, locals, local_count)
               : INFER_KIND_UNKNOWN;
  case AST_VAR_DECL: {
    InferKind init_kind = INFER_KIND_UNKNOWN;
    if (node->as.var_decl.initializer)
      init_kind = infer_lambda_node_kind(node->as.var_decl.initializer,
                                         param_name, param_kind, closure,
                                         locals, local_count);
    return init_kind;
  }
  case AST_ASSIGN:
    return infer_lambda_node_kind(node->as.assign.value, param_name, param_kind,
                                  closure, locals, local_count);
  case AST_BLOCK: {
    InferLocal scoped_locals[64];
    int scoped_count = local_count;
    InferKind result = INFER_KIND_UNKNOWN;
    memcpy(scoped_locals, locals, sizeof(scoped_locals));
    for (int i = 0; i < node->as.block.statements.count; i++) {
      AstNode *stmt = node->as.block.statements.items[i];
      if (!stmt)
        continue;
      if (stmt->type == AST_VAR_DECL) {
        InferKind init_kind = INFER_KIND_UNKNOWN;
        if (stmt->as.var_decl.initializer) {
          init_kind = infer_lambda_node_kind(
              stmt->as.var_decl.initializer, param_name, param_kind, closure,
              scoped_locals, scoped_count);
        }
        infer_add_local(scoped_locals, &scoped_count, stmt->as.var_decl.name,
                        init_kind);
        continue;
      }
      result = infer_merge_kind(
          result, infer_lambda_node_kind(stmt, param_name, param_kind, closure,
                                         scoped_locals, scoped_count));
    }
    return result;
  }
  case AST_IF: {
    InferKind then_kind = INFER_KIND_UNKNOWN;
    InferKind else_kind = INFER_KIND_UNKNOWN;
    if (node->as.if_stmt.then_branch)
      then_kind = infer_lambda_node_kind(node->as.if_stmt.then_branch,
                                         param_name, param_kind, closure, locals,
                                         local_count);
    if (node->as.if_stmt.else_branch)
      else_kind = infer_lambda_node_kind(node->as.if_stmt.else_branch,
                                         param_name, param_kind, closure, locals,
                                         local_count);
    return infer_merge_kind(then_kind, else_kind);
  }
  case AST_WHILE:
    return node->as.while_stmt.body
               ? infer_lambda_node_kind(node->as.while_stmt.body, param_name,
                                        param_kind, closure, locals, local_count)
               : INFER_KIND_UNKNOWN;
  case AST_FOR: {
    InferLocal scoped_locals[64];
    int scoped_count = local_count;
    memcpy(scoped_locals, locals, sizeof(scoped_locals));
    if (node->as.for_stmt.init && node->as.for_stmt.init->type == AST_VAR_DECL) {
      InferKind init_kind = INFER_KIND_UNKNOWN;
      if (node->as.for_stmt.init->as.var_decl.initializer) {
        init_kind = infer_lambda_node_kind(
            node->as.for_stmt.init->as.var_decl.initializer, param_name,
            param_kind, closure, scoped_locals, scoped_count);
      }
      infer_add_local(scoped_locals, &scoped_count,
                      node->as.for_stmt.init->as.var_decl.name, init_kind);
    }
    if (node->as.for_stmt.var_name)
      infer_add_local(scoped_locals, &scoped_count, node->as.for_stmt.var_name,
                      INFER_KIND_UNKNOWN);
    return node->as.for_stmt.body
               ? infer_lambda_node_kind(node->as.for_stmt.body, param_name,
                                        param_kind, closure, scoped_locals,
                                        scoped_count)
               : INFER_KIND_UNKNOWN;
  }
  case AST_LAMBDA:
    return INFER_KIND_UNKNOWN;
  default:
    return INFER_KIND_UNKNOWN;
  }
}

static StreamOp *stream_op_new(StreamOpKind kind) {
  StreamOp *op = calloc(1, sizeof(StreamOp));
  if (!op)
    return NULL;
  op->kind = kind;
  return op;
}

static Value value_stream_with_op(QiscLazyStream *src, StreamOp *op,
                                  StreamKind result_kind) {
  Value v = {.type = VAL_STREAM};
  QiscLazyStream *stream;

  if (!src || !src->handle || !op)
    return value_none();

  stream = malloc(sizeof(QiscLazyStream));
  if (!stream) {
    free(op);
    return value_none();
  }

  stream->handle = src->handle;
  stream->source_kind = src->source_kind;
  stream->kind = src->kind;
  stream->consumed = false;
  stream->ops_head = src->ops_head;
  stream->ops_tail = src->ops_tail;

  if (stream->ops_tail) {
    stream->ops_tail->next = op;
  } else {
    stream->ops_head = op;
  }
  stream->ops_tail = op;
  stream->kind = result_kind;

  src->handle = NULL;
  src->consumed = true;
  src->ops_head = NULL;
  src->ops_tail = NULL;

  v.as.stream_val = stream;
  return v;
}

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
  } else if (val->type == VAL_STREAM) {
    /* Stream wrappers are shared by shallow copies; leave cleanup to process exit
     * or explicit terminal operations to avoid double-freeing active pipelines. */
  } else if (val->type == VAL_ARRAY) {
    free(val->as.array_val.items);
  } else if (val->type == VAL_STRUCT) {
    for (int i = 0; i < val->as.struct_val.field_count; i++)
      free(val->as.struct_val.field_names[i]);
    free(val->as.struct_val.field_names);
    free(val->as.struct_val.field_values);
    free(val->as.struct_val.type_name);
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
  case VAL_STREAM:
    printf("<stream>");
    break;
  case VAL_ARRAY:
    printf("[");
    for (int i = 0; i < val->as.array_val.count; i++) {
      if (i > 0)
        printf(", ");
      value_print(&val->as.array_val.items[i]);
    }
    printf("]");
    break;
  case VAL_STRUCT:
    printf("%s { ", val->as.struct_val.type_name ? val->as.struct_val.type_name
                                                 : "struct");
    for (int i = 0; i < val->as.struct_val.field_count; i++) {
      if (i > 0)
        printf(", ");
      printf("%s: ", val->as.struct_val.field_names[i]);
      value_print(&val->as.struct_val.field_values[i]);
    }
    printf(" }");
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
  case VAL_STREAM:
    return val->as.stream_val && !val->as.stream_val->consumed &&
           val->as.stream_val->handle != NULL;
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
  env->is_const = calloc(env->capacity, sizeof(bool));
  return env;
}

void env_free(Environment *env) {
  for (int i = 0; i < env->count; i++) {
    free(env->names[i]);
  }
  free(env->names);
  free(env->values);
  free(env->is_const);
  free(env);
}

static void env_define_ex(Environment *env, const char *name, Value value,
                          bool is_const_var) {
  /* Check if already exists in current scope */
  for (int i = 0; i < env->count; i++) {
    if (strcmp(env->names[i], name) == 0) {
      value_free(&env->values[i]);
      env->values[i] = value;
      env->is_const[i] = is_const_var;
      return;
    }
  }

  /* Add new */
  if (env->count >= env->capacity) {
    env->capacity *= 2;
    env->names = realloc(env->names, env->capacity * sizeof(char *));
    env->values = realloc(env->values, env->capacity * sizeof(Value));
    env->is_const = realloc(env->is_const, env->capacity * sizeof(bool));
  }
  env->names[env->count] = strdup(name);
  env->values[env->count] = value;
  env->is_const[env->count] = is_const_var;
  env->count++;
}

void env_define(Environment *env, const char *name, Value value) {
  env_define_ex(env, name, value, false);
}

void env_define_const(Environment *env, const char *name, Value value) {
  env_define_ex(env, name, value, true);
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
      if (env->is_const[i]) {
        fprintf(stderr, "Error: Cannot reassign const variable '%s'\n", name);
        return false;
      }
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

static char *read_all_stream(FILE *fp) {
  size_t capacity = 4096;
  size_t length = 0;
  char *buffer;

  if (!fp)
    return NULL;

  buffer = malloc(capacity);
  if (!buffer)
    return NULL;

  while (!feof(fp)) {
    size_t remaining = capacity - length;
    if (remaining < 1024) {
      char *grown;
      capacity *= 2;
      grown = realloc(buffer, capacity);
      if (!grown) {
        free(buffer);
        return NULL;
      }
      buffer = grown;
      remaining = capacity - length;
    }

    size_t nread = fread(buffer + length, 1, remaining - 1, fp);
    length += nread;
    if (ferror(fp)) {
      free(buffer);
      return NULL;
    }
  }

  buffer[length] = '\0';
  return buffer;
}

static Value read_lines_stream(FILE *fp) {
  Value arr = {.type = VAL_ARRAY};
  char *line = NULL;
  size_t capacity = 0;
  ssize_t nread;

  arr.as.array_val.count = 0;
  arr.as.array_val.capacity = 8;
  arr.as.array_val.items = malloc(arr.as.array_val.capacity * sizeof(Value));
  if (!arr.as.array_val.items) {
    arr.as.array_val.capacity = 0;
    return value_none();
  }

  while ((nread = getline(&line, &capacity, fp)) != -1) {
    if (nread > 0 && line[nread - 1] == '\n') {
      nread--;
      line[nread] = '\0';
    }
    if (nread > 0 && line[nread - 1] == '\r') {
      nread--;
      line[nread] = '\0';
    }

    if (arr.as.array_val.count >= arr.as.array_val.capacity) {
      int new_capacity = arr.as.array_val.capacity * 2;
      Value *grown =
          realloc(arr.as.array_val.items, new_capacity * sizeof(Value));
      if (!grown) {
        free(line);
        free(arr.as.array_val.items);
        return value_none();
      }
      arr.as.array_val.items = grown;
      arr.as.array_val.capacity = new_capacity;
    }

    arr.as.array_val.items[arr.as.array_val.count++] =
        value_string(line, (int)nread);
  }

  free(line);
  return arr;
}

static Value builtin_stdin_text(Interpreter *interp, Value *args, int argc) {
  char *text;
  (void)interp;
  (void)args;
  if (argc != 0)
    return value_error("stdin_text() takes no arguments");

  text = read_all_stream(stdin);
  if (!text)
    return value_string("", 0);

  Value result = value_string(text, (int)strlen(text));
  free(text);
  return result;
}

static Value builtin_read_file(Interpreter *interp, Value *args, int argc) {
  FILE *fp;
  char *text;
  (void)interp;
  if (argc != 1 || args[0].type != VAL_STRING)
    return value_none();

  fp = fopen(args[0].as.string_val.str, "rb");
  if (!fp)
    return value_none();

  text = read_all_stream(fp);
  fclose(fp);
  if (!text)
    return value_none();

  Value result = value_string(text, (int)strlen(text));
  free(text);
  return result;
}

static Value builtin_stdin_lines(Interpreter *interp, Value *args, int argc) {
  (void)interp;
  (void)args;
  if (argc != 0)
    return value_error("stdin_lines() takes no arguments");
  return read_lines_stream(stdin);
}

static Value builtin_file_lines(Interpreter *interp, Value *args, int argc) {
  FILE *fp;
  (void)interp;
  if (argc != 1 || args[0].type != VAL_STRING)
    return value_none();

  fp = fopen(args[0].as.string_val.str, "rb");
  if (!fp)
    return value_none();

  Value result = read_lines_stream(fp);
  fclose(fp);
  return result;
}

static Value builtin_stdout_write(Interpreter *interp, Value *args, int argc) {
  (void)interp;
  if (argc != 1)
    return value_int(0);

  Value rendered = builtin_str(interp, args, argc);
  if (rendered.type != VAL_STRING)
    return value_int(0);

  fwrite(rendered.as.string_val.str, 1, (size_t)rendered.as.string_val.length, stdout);
  fflush(stdout);
  Value result = value_int(rendered.as.string_val.length);
  value_free(&rendered);
  return result;
}

static Value builtin_stderr_write(Interpreter *interp, Value *args, int argc) {
  (void)interp;
  if (argc != 1)
    return value_int(0);

  Value rendered = builtin_str(interp, args, argc);
  if (rendered.type != VAL_STRING)
    return value_int(0);

  fwrite(rendered.as.string_val.str, 1, (size_t)rendered.as.string_val.length, stderr);
  fflush(stderr);
  Value result = value_int(rendered.as.string_val.length);
  value_free(&rendered);
  return result;
}

static Value builtin_take(Interpreter *interp, Value *args, int argc) {
  int limit;
  Value result = {.type = VAL_ARRAY};
  (void)interp;
  if (argc != 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_INT)
    return value_none();

  limit = (int)args[1].as.int_val;
  if (limit < 0)
    limit = 0;
  if (limit > args[0].as.array_val.count)
    limit = args[0].as.array_val.count;

  result.as.array_val.count = limit;
  result.as.array_val.capacity = limit;
  result.as.array_val.items = limit > 0 ? malloc(limit * sizeof(Value)) : NULL;
  for (int i = 0; i < limit; i++) {
    result.as.array_val.items[i] = args[0].as.array_val.items[i];
  }
  return result;
}

static Value builtin_skip(Interpreter *interp, Value *args, int argc) {
  int start;
  int count;
  Value result = {.type = VAL_ARRAY};
  (void)interp;
  if (argc != 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_INT)
    return value_none();

  start = (int)args[1].as.int_val;
  if (start < 0)
    start = 0;
  if (start > args[0].as.array_val.count)
    start = args[0].as.array_val.count;

  count = args[0].as.array_val.count - start;
  result.as.array_val.count = count;
  result.as.array_val.capacity = count;
  result.as.array_val.items = count > 0 ? malloc(count * sizeof(Value)) : NULL;
  for (int i = 0; i < count; i++) {
    result.as.array_val.items[i] = args[0].as.array_val.items[start + i];
  }
  return result;
}

static Value builtin_array_map(Interpreter *interp, Value *args, int argc) {
  Value result = {.type = VAL_ARRAY};
  if (argc != 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_PROC)
    return value_none();

  result.as.array_val.count = args[0].as.array_val.count;
  result.as.array_val.capacity = args[0].as.array_val.count;
  result.as.array_val.items =
      result.as.array_val.count > 0
          ? malloc(result.as.array_val.count * sizeof(Value))
          : NULL;

  for (int i = 0; i < args[0].as.array_val.count; i++) {
    Value elem = args[0].as.array_val.items[i];
    result.as.array_val.items[i] = call_value(interp, &args[1], &elem, 1);
    if (interp->had_error)
      return result;
  }

  return result;
}

static Value builtin_array_filter(Interpreter *interp, Value *args, int argc) {
  Value result = {.type = VAL_ARRAY};
  if (argc != 2 || args[0].type != VAL_ARRAY || args[1].type != VAL_PROC)
    return value_none();

  result.as.array_val.capacity = args[0].as.array_val.count;
  result.as.array_val.count = 0;
  result.as.array_val.items =
      result.as.array_val.capacity > 0
          ? malloc(result.as.array_val.capacity * sizeof(Value))
          : NULL;

  for (int i = 0; i < args[0].as.array_val.count; i++) {
    Value elem = args[0].as.array_val.items[i];
    Value test = call_value(interp, &args[1], &elem, 1);
    if (interp->had_error)
      return test;
    if (value_is_truthy(&test)) {
      result.as.array_val.items[result.as.array_val.count++] = elem;
    }
  }

  return result;
}

static Value builtin_array_reduce(Interpreter *interp, Value *args, int argc) {
  if (argc != 3 || args[0].type != VAL_ARRAY)
    return value_none();

  Value fn;
  Value acc;
  if (args[1].type == VAL_PROC) {
    fn = args[1];
    acc = args[2];
  } else if (args[2].type == VAL_PROC) {
    fn = args[2];
    acc = args[1];
  } else {
    return value_none();
  }

  for (int i = 0; i < args[0].as.array_val.count; i++) {
    Value call_args[2] = {acc, args[0].as.array_val.items[i]};
    acc = call_value(interp, &fn, call_args, 2);
    if (interp->had_error)
      return acc;
  }

  return acc;
}

static bool lazy_stream_next(Interpreter *interp, QiscLazyStream *stream,
                             Value *out) {
  if (!stream || !stream->handle || stream->consumed || !out)
    return false;

  while (stream->handle->state == STREAM_READY) {
    StreamElement elem = stream->handle->next(stream->handle);
    Value current;

    if (elem.data == NULL)
      continue;

    if (stream->source_kind == STREAM_KIND_STRING) {
      const char *text = (const char *)elem.data;
      current = value_string(text, (int)strlen(text));
    } else {
      current = value_int(*(int64_t *)elem.data);
    }

    bool discard = false;
    for (StreamOp *op = stream->ops_head; op; op = op->next) {
      if (op->kind == STREAM_OP_SKIP) {
        if (op->progress < op->amount) {
          op->progress++;
          discard = true;
          break;
        }
      } else if (op->kind == STREAM_OP_FILTER) {
        Value test = call_value(interp, &op->proc, &current, 1);
        if (interp->had_error) {
          value_free(&current);
          return false;
        }
        bool keep = value_is_truthy(&test);
        value_free(&test);
        if (!keep) {
          value_free(&current);
          discard = true;
          break;
        }
      } else if (op->kind == STREAM_OP_MAP) {
        Value mapped = call_value(interp, &op->proc, &current, 1);
        if (interp->had_error) {
          value_free(&current);
          return false;
        }
        if ((op->result_kind == STREAM_KIND_INT && mapped.type != VAL_INT) ||
            (op->result_kind == STREAM_KIND_STRING &&
             mapped.type != VAL_STRING)) {
          value_free(&current);
          value_free(&mapped);
          runtime_error(interp,
                        "stream_map must preserve element type for lazy streams");
          return false;
        }
        value_free(&current);
        current = mapped;
        if (interp->had_error)
          return false;
      } else if (op->kind == STREAM_OP_TAKE) {
        if (op->progress >= op->amount) {
          stream_free(stream->handle);
          stream->handle = NULL;
          stream->consumed = true;
          return false;
        }
        op->progress++;
      }
    }

    if (discard)
      continue;

    *out = current;
    return true;
  }

  if (stream->handle) {
    stream_free(stream->handle);
    stream->handle = NULL;
  }
  stream->consumed = true;
  return false;
}

static Value builtin_stream_range(Interpreter *interp, Value *args, int argc) {
  int64_t start, end, step;
  (void)interp;
  if ((argc != 2 && argc != 3) || args[0].type != VAL_INT ||
      args[1].type != VAL_INT)
    return value_none();

  start = args[0].as.int_val;
  end = args[1].as.int_val;
  step = (argc == 3 && args[2].type == VAL_INT) ? args[2].as.int_val : 1;
  return value_stream(stream_range(start, end, step), STREAM_KIND_INT);
}

static Value builtin_stream_file_lines(Interpreter *interp, Value *args,
                                       int argc) {
  (void)interp;
  if (argc != 1 || args[0].type != VAL_STRING)
    return value_none();
  return value_stream(stream_file_lines(args[0].as.string_val.str, 0),
                      STREAM_KIND_STRING);
}

static Value builtin_stream_take(Interpreter *interp, Value *args, int argc) {
  QiscLazyStream *stream;
  StreamOp *op;
  (void)interp;
  if (argc != 2 || args[0].type != VAL_STREAM || args[1].type != VAL_INT)
    return value_none();

  stream = value_stream_get(&args[0]);
  if (!stream)
    return value_none();

  op = stream_op_new(STREAM_OP_TAKE);
  if (!op)
    return value_none();
  op->amount = args[1].as.int_val < 0 ? 0 : args[1].as.int_val;
  return value_stream_with_op(stream, op, stream->kind);
}

static Value builtin_stream_skip(Interpreter *interp, Value *args, int argc) {
  QiscLazyStream *stream;
  StreamOp *op;
  (void)interp;
  if (argc != 2 || args[0].type != VAL_STREAM || args[1].type != VAL_INT)
    return value_none();

  stream = value_stream_get(&args[0]);
  if (!stream)
    return value_none();

  op = stream_op_new(STREAM_OP_SKIP);
  if (!op)
    return value_none();
  op->amount = args[1].as.int_val < 0 ? 0 : args[1].as.int_val;
  return value_stream_with_op(stream, op, stream->kind);
}

static Value builtin_stream_map(Interpreter *interp, Value *args, int argc) {
  QiscLazyStream *stream;
  StreamOp *op;
  StreamKind result_kind;
  (void)interp;
  if (argc != 2 || args[0].type != VAL_STREAM || args[1].type != VAL_PROC)
    return value_none();

  stream = value_stream_get(&args[0]);
  if (!stream)
    return value_none();

  result_kind = stream_proc_result_kind(&args[1], stream->kind);
  if (stream->kind != STREAM_KIND_INT && stream->kind != STREAM_KIND_STRING)
    return value_none();

  op = stream_op_new(STREAM_OP_MAP);
  if (!op)
    return value_none();
  op->result_kind = result_kind;
  op->proc = args[1];
  return value_stream_with_op(stream, op,
                              result_kind == STREAM_KIND_UNKNOWN ? stream->kind
                                                                 : result_kind);
}

static Value builtin_stream_filter(Interpreter *interp, Value *args, int argc) {
  QiscLazyStream *stream;
  StreamOp *op;
  (void)interp;
  if (argc != 2 || args[0].type != VAL_STREAM || args[1].type != VAL_PROC)
    return value_none();

  stream = value_stream_get(&args[0]);
  if (!stream ||
      (stream->kind != STREAM_KIND_INT && stream->kind != STREAM_KIND_STRING))
    return value_none();

  op = stream_op_new(STREAM_OP_FILTER);
  if (!op)
    return value_none();
  op->proc = args[1];
  return value_stream_with_op(stream, op, stream->kind);
}

static Value builtin_stream_count(Interpreter *interp, Value *args, int argc) {
  QiscLazyStream *stream;
  int64_t count = 0;
  (void)interp;
  if (argc != 1 || args[0].type != VAL_STREAM)
    return value_none();

  stream = value_stream_get(&args[0]);
  if (!stream)
    return value_int(0);

  Value item;
  while (lazy_stream_next(interp, stream, &item)) {
    count++;
  }
  return value_int(count);
}

static Value builtin_stream_first(Interpreter *interp, Value *args, int argc) {
  QiscLazyStream *stream;
  Value result = value_none();
  (void)interp;
  if (argc != 1 || args[0].type != VAL_STREAM)
    return value_none();

  stream = value_stream_get(&args[0]);
  if (!stream)
    return value_none();

  lazy_stream_next(interp, stream, &result);
  return result;
}

static Value builtin_stream_collect(Interpreter *interp, Value *args, int argc) {
  QiscLazyStream *stream;
  Value result = {.type = VAL_ARRAY};
  int capacity = 8;
  (void)interp;
  if (argc != 1 || args[0].type != VAL_STREAM)
    return value_none();

  stream = value_stream_get(&args[0]);
  if (!stream)
    return value_none();

  result.as.array_val.count = 0;
  result.as.array_val.capacity = capacity;
  result.as.array_val.items = malloc(capacity * sizeof(Value));
  if (!result.as.array_val.items)
    return value_none();

  while (1) {
    Value item;
    if (!lazy_stream_next(interp, stream, &item))
      break;

    if (result.as.array_val.count >= result.as.array_val.capacity) {
      int new_capacity = result.as.array_val.capacity * 2;
      Value *grown =
          realloc(result.as.array_val.items, new_capacity * sizeof(Value));
      if (!grown) {
        free(result.as.array_val.items);
        return value_none();
      }
      result.as.array_val.items = grown;
      result.as.array_val.capacity = new_capacity;
    }

    result.as.array_val.items[result.as.array_val.count++] = item;
  }
  return result;
}

static Value builtin_stream_reduce(Interpreter *interp, Value *args, int argc) {
  QiscLazyStream *stream;
  Value acc;

  if (argc != 3 || args[0].type != VAL_STREAM || args[1].type != VAL_PROC)
    return value_none();

  stream = value_stream_get(&args[0]);
  if (!stream)
    return args[2];

  acc = args[2];
  while (1) {
    Value item;
    Value call_args[2];
    if (!lazy_stream_next(interp, stream, &item))
      break;
    call_args[0] = acc;
    call_args[1] = item;
    acc = call_value(interp, &args[1], call_args, 2);
    value_free(&item);
    if (interp->had_error)
      return acc;
  }
  return acc;
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
  interp->method_count = 0;

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
static Value call_value(Interpreter *interp, Value *fn, Value *args, int argc);

/* ======== Expression Evaluation ======== */

static Value eval_binary(Interpreter *interp, AstNode *node) {
  BinaryOp op = node->as.binary.op;

  /* Pipeline: handle specially - inject left as first arg to right call */
  if (op == OP_PIPELINE) {
    Value left = evaluate(interp, node->as.binary.left);
    if (interp->had_error)
      return left;

    /* Check if right side is a function call (filter, map, etc.) */
    AstNode *rhs = node->as.binary.right;
    if (rhs->type == AST_CALL && rhs->as.call.callee->type == AST_IDENTIFIER) {
      char *fn_name = rhs->as.call.callee->as.identifier.name;

      if (strcmp(fn_name, "filter") == 0 && rhs->as.call.args.count >= 1) {
        /* filter(pred): keep elements where pred(elem) is truthy */
        Value pred = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return pred;
        if (left.type == VAL_ARRAY && pred.type == VAL_PROC) {
          Value result = {.type = VAL_ARRAY};
          result.as.array_val.capacity = left.as.array_val.count;
          result.as.array_val.items =
              malloc(result.as.array_val.capacity * sizeof(Value));
          result.as.array_val.count = 0;
          for (int i = 0; i < left.as.array_val.count; i++) {
            Value elem = left.as.array_val.items[i];
            Value test = call_value(interp, &pred, &elem, 1);
            if (interp->had_error)
              return test;
            if (value_is_truthy(&test)) {
              result.as.array_val.items[result.as.array_val.count++] = elem;
            }
          }
          return result;
        }
        return left;
      }

      if (strcmp(fn_name, "map") == 0 && rhs->as.call.args.count >= 1) {
        /* map(fn): transform each element */
        Value fn = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return fn;
        if (left.type == VAL_ARRAY && fn.type == VAL_PROC) {
          Value result = {.type = VAL_ARRAY};
          result.as.array_val.count = left.as.array_val.count;
          result.as.array_val.capacity = left.as.array_val.count;
          result.as.array_val.items =
              malloc(result.as.array_val.count * sizeof(Value));
          for (int i = 0; i < left.as.array_val.count; i++) {
            Value elem = left.as.array_val.items[i];
            result.as.array_val.items[i] = call_value(interp, &fn, &elem, 1);
            if (interp->had_error)
              return result;
          }
          return result;
        }
        return left;
      }

      if (strcmp(fn_name, "collect") == 0) {
        /* collect(): materialize pipeline - return as-is */
        return left;
      }

      if (strcmp(fn_name, "take") == 0 && rhs->as.call.args.count >= 1) {
        Value limit = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return limit;
        Value call_args[2] = {left, limit};
        return builtin_take(interp, call_args, 2);
      }

      if (strcmp(fn_name, "skip") == 0 && rhs->as.call.args.count >= 1) {
        Value offset = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return offset;
        Value call_args[2] = {left, offset};
        return builtin_skip(interp, call_args, 2);
      }

      if (strcmp(fn_name, "stream_take") == 0 && rhs->as.call.args.count >= 1) {
        Value limit = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return limit;
        Value call_args[2] = {left, limit};
        return builtin_stream_take(interp, call_args, 2);
      }

      if (strcmp(fn_name, "stream_skip") == 0 && rhs->as.call.args.count >= 1) {
        Value offset = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return offset;
        Value call_args[2] = {left, offset};
        return builtin_stream_skip(interp, call_args, 2);
      }

      if (strcmp(fn_name, "stream_map") == 0 && rhs->as.call.args.count >= 1) {
        Value fn = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return fn;
        Value call_args[2] = {left, fn};
        return builtin_stream_map(interp, call_args, 2);
      }

      if (strcmp(fn_name, "stream_filter") == 0 &&
          rhs->as.call.args.count >= 1) {
        Value fn = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return fn;
        Value call_args[2] = {left, fn};
        return builtin_stream_filter(interp, call_args, 2);
      }

      if (strcmp(fn_name, "stream_count") == 0) {
        Value call_args[1] = {left};
        return builtin_stream_count(interp, call_args, 1);
      }

      if (strcmp(fn_name, "stream_first") == 0) {
        Value call_args[1] = {left};
        return builtin_stream_first(interp, call_args, 1);
      }

      if (strcmp(fn_name, "stream_collect") == 0) {
        Value call_args[1] = {left};
        return builtin_stream_collect(interp, call_args, 1);
      }

      if (strcmp(fn_name, "stream_reduce") == 0 && rhs->as.call.args.count >= 2) {
        Value first = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return first;
        Value second = evaluate(interp, rhs->as.call.args.items[1]);
        if (interp->had_error)
          return second;
        Value fn;
        Value initial;
        if (first.type == VAL_PROC) {
          fn = first;
          initial = second;
        } else {
          fn = second;
          initial = first;
        }
        Value call_args[3] = {left, fn, initial};
        return builtin_stream_reduce(interp, call_args, 3);
      }

      if (strcmp(fn_name, "reduce") == 0 && rhs->as.call.args.count >= 2) {
        /* Support both reduce(fn, init) and the older reduce(init, fn) form. */
        Value first = evaluate(interp, rhs->as.call.args.items[0]);
        if (interp->had_error)
          return first;
        Value second = evaluate(interp, rhs->as.call.args.items[1]);
        if (interp->had_error)
          return second;

        Value acc;
        Value fn;
        if (first.type == VAL_PROC) {
          fn = first;
          acc = second;
        } else {
          acc = first;
          fn = second;
        }

        if (left.type == VAL_ARRAY && fn.type == VAL_PROC) {
          for (int i = 0; i < left.as.array_val.count; i++) {
            Value call_args[2] = {acc, left.as.array_val.items[i]};
            acc = call_value(interp, &fn, call_args, 2);
            if (interp->had_error)
              return acc;
          }
        }
        return acc;
      }
      /* General user-function pipeline: a >> fn(args) → fn(a, args) */
      Value *fn_ptr = env_get(interp->current, fn_name);
      if (fn_ptr && fn_ptr->type == VAL_PROC) {
        int orig_argc = rhs->as.call.args.count;
        int new_argc = 1 + orig_argc;
        Value *call_args = malloc(new_argc * sizeof(Value));
        call_args[0] = left;
        for (int i = 0; i < orig_argc; i++) {
          call_args[i + 1] = evaluate(interp, rhs->as.call.args.items[i]);
          if (interp->had_error) {
            free(call_args);
            return left;
          }
        }
        Value result = call_value(interp, fn_ptr, call_args, new_argc);
        free(call_args);
        return result;
      }
      interp->had_error = false; /* clear any lookup error */
    }

    /* Default pipeline: evaluate right and return it */
    Value right = evaluate(interp, rhs);
    if (right.type == VAL_PROC) {
      return call_value(interp, &right, &left, 1);
    }
    return right;
  }

  Value left = evaluate(interp, node->as.binary.left);
  if (interp->had_error)
    return left;

  /* Has check must short-circuit before right eval (right may be NULL) */
  if (op == OP_HAS) {
    return value_bool(left.type != VAL_NONE);
  }

  Value right = evaluate(interp, node->as.binary.right);
  if (interp->had_error)
    return right;

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
    /* Maybe-default: if left is none, return right; else return left */
    if (left.type == VAL_NONE)
      return right;
    return left;
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

/* Helper: call a VAL_PROC value (proc or lambda) with arguments */
static Value call_value(Interpreter *interp, Value *fn, Value *args, int argc) {
  if (!fn || fn->type != VAL_PROC) {
    return runtime_error(interp, "Value is not callable");
  }
  AstNode *node = fn->as.proc_val.proc;
  Environment *closure = fn->as.proc_val.closure;
  Environment *call_env = env_new(closure ? closure : interp->global);
  Environment *prev = interp->current;

  if (node->type == AST_LAMBDA) {
    /* Lambda: params are in lambda.params, body is lambda.body */
    for (int i = 0; i < node->as.lambda.params.count && i < argc; i++) {
      AstNode *p = node->as.lambda.params.items[i];
      if (p->type == AST_VAR_DECL)
        env_define(call_env, p->as.var_decl.name, args[i]);
      else if (p->type == AST_IDENTIFIER)
        env_define(call_env, p->as.identifier.name, args[i]);
    }
    interp->current = call_env;
    Value result;
    if (node->as.lambda.body->type == AST_BLOCK) {
      /* do block: execute statements, return via give */
      interp->returning = false;
      execute(interp, node->as.lambda.body);
      result = interp->returning ? interp->return_value : value_none();
      interp->returning = false;
    } else {
      /* expression lambda */
      result = evaluate(interp, node->as.lambda.body);
    }
    interp->current = prev;
    env_free(call_env);
    return result;
  } else if (node->type == AST_PROC) {
    /* Regular proc */
    for (int i = 0; i < node->as.proc.params.count && i < argc; i++) {
      AstNode *p = node->as.proc.params.items[i];
      env_define(call_env, p->as.var_decl.name, args[i]);
    }
    interp->current = call_env;
    interp->returning = false;
    execute(interp, node->as.proc.body);
    Value result = interp->return_value;
    interp->return_value = value_none();
    interp->returning = false;
    interp->current = prev;
    env_free(call_env);
    return result;
  }
  return value_none();
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
    } else if (strcmp(name, "stdin_text") == 0) {
      result = builtin_stdin_text(interp, args, argc);
    } else if (strcmp(name, "stdin_lines") == 0) {
      result = builtin_stdin_lines(interp, args, argc);
    } else if (strcmp(name, "stream_range") == 0) {
      result = builtin_stream_range(interp, args, argc);
    } else if (strcmp(name, "stream_file_lines") == 0) {
      result = builtin_stream_file_lines(interp, args, argc);
    } else if (strcmp(name, "stream_take") == 0) {
      result = builtin_stream_take(interp, args, argc);
    } else if (strcmp(name, "stream_skip") == 0) {
      result = builtin_stream_skip(interp, args, argc);
    } else if (strcmp(name, "stream_map") == 0) {
      result = builtin_stream_map(interp, args, argc);
    } else if (strcmp(name, "stream_filter") == 0) {
      result = builtin_stream_filter(interp, args, argc);
    } else if (strcmp(name, "stream_count") == 0) {
      result = builtin_stream_count(interp, args, argc);
    } else if (strcmp(name, "stream_first") == 0) {
      result = builtin_stream_first(interp, args, argc);
    } else if (strcmp(name, "stream_collect") == 0) {
      result = builtin_stream_collect(interp, args, argc);
    } else if (strcmp(name, "stream_reduce") == 0) {
      result = builtin_stream_reduce(interp, args, argc);
    } else if (strcmp(name, "read_file") == 0) {
      result = builtin_read_file(interp, args, argc);
    } else if (strcmp(name, "file_lines") == 0) {
      result = builtin_file_lines(interp, args, argc);
    } else if (strcmp(name, "stdout_write") == 0) {
      result = builtin_stdout_write(interp, args, argc);
    } else if (strcmp(name, "stderr_write") == 0) {
      result = builtin_stderr_write(interp, args, argc);
    } else if (strcmp(name, "take") == 0) {
      result = builtin_take(interp, args, argc);
    } else if (strcmp(name, "skip") == 0) {
      result = builtin_skip(interp, args, argc);
    } else if (strcmp(name, "filter") == 0) {
      if (argc == 2 && args[0].type == VAL_ARRAY && args[1].type == VAL_PROC) {
        result = builtin_array_filter(interp, args, argc);
      } else if (argc >= 1 && args[0].type == VAL_PROC) {
        result = args[0];
      } else {
        result = value_none();
      }
    } else if (strcmp(name, "map") == 0) {
      if (argc == 2 && args[0].type == VAL_ARRAY && args[1].type == VAL_PROC) {
        result = builtin_array_map(interp, args, argc);
      } else if (argc >= 1 && args[0].type == VAL_PROC) {
        result = args[0];
      } else {
        result = value_none();
      }
    } else if (strcmp(name, "collect") == 0) {
      /* Collect materializes a pipeline - return input as-is */
      result = argc > 0 ? args[0] : value_none();
    } else if (strcmp(name, "reduce") == 0) {
      if (argc == 3 && args[0].type == VAL_ARRAY &&
          (args[1].type == VAL_PROC || args[2].type == VAL_PROC)) {
        result = builtin_array_reduce(interp, args, argc);
      } else if (argc >= 2 &&
                 (args[0].type == VAL_PROC || args[1].type == VAL_PROC)) {
        result = (args[0].type == VAL_PROC) ? args[1] : args[0];
      } else {
        result = argc > 0 ? args[0] : value_none();
      }
    } else if (strcmp(name, "range") == 0) {
      if (argc >= 2 && args[0].type == VAL_INT && args[1].type == VAL_INT) {
        int64_t start = args[0].as.int_val;
        int64_t end = args[1].as.int_val;
        int64_t count = end - start;
        if (count < 0)
          count = 0;

        Value arr = {.type = VAL_ARRAY};
        arr.as.array_val.count = (int)count;
        arr.as.array_val.capacity = (int)count;
        arr.as.array_val.items =
            count > 0 ? malloc(count * sizeof(Value)) : NULL;
        for (int64_t i = 0; i < count; i++) {
          arr.as.array_val.items[i] = value_int(start + i);
        }
        result = arr;
      } else {
        result = value_none();
      }
    } else if (strcmp(name, "typeof") == 0) {
      if (argc >= 1) {
        const char *tname = "unknown";
        switch (args[0].type) {
        case VAL_INT:
          tname = "int";
          break;
        case VAL_FLOAT:
          tname = "float";
          break;
        case VAL_BOOL:
          tname = "bool";
          break;
        case VAL_STRING:
          tname = "string";
          break;
        case VAL_ARRAY:
          tname = "array";
          break;
        case VAL_STRUCT:
          tname = "struct";
          break;
        case VAL_PROC:
          tname = "proc";
          break;
        case VAL_NONE:
          tname = "none";
          break;
        default:
          break;
        }
        result = value_string(tname, (int)strlen(tname));
      } else {
        result = value_string("none", 4);
      }
    } else if (strcmp(name, "sizeof") == 0) {
      if (argc >= 1) {
        switch (args[0].type) {
        case VAL_INT:
          result = value_int(8);
          break;
        case VAL_FLOAT:
          result = value_int(8);
          break;
        case VAL_BOOL:
          result = value_int(1);
          break;
        case VAL_STRING:
          result = value_int(args[0].as.string_val.length);
          break;
        case VAL_ARRAY:
          result = value_int(args[0].as.array_val.count);
          break;
        default:
          result = value_int(0);
          break;
        }
      } else {
        result = value_int(0);
      }
    } else if (strcmp(name, "file_exists") == 0 ||
               strcmp(name, "parse_json") == 0 ||
               strcmp(name, "db_connect") == 0) {
      /* I/O stubs - return none */
      result = value_none();
    } else {
      /* Look up user-defined function */
      Value *proc_val = env_get(interp->current, name);
      if (!proc_val || proc_val->type != VAL_PROC) {
        free(args);
        return runtime_error(interp, "Undefined function: %s", name);
      }

      result = call_value(interp, proc_val, args, argc);
    }

    free(args);
    return result;
  }

  /* Handle method calls: obj.method() */
  if (node->as.call.callee->type == AST_MEMBER) {
    Value obj = evaluate(interp, node->as.call.callee->as.member.object);
    if (interp->had_error)
      return obj;
    char *method_name = node->as.call.callee->as.member.member;

    /* Determine type name */
    char *type_name = NULL;
    if (obj.type == VAL_STRUCT)
      type_name = obj.as.struct_val.type_name;
    else if (obj.type == VAL_ARRAY)
      type_name = "Array";
    else if (obj.type == VAL_STRING)
      type_name = "String";
    else if (obj.type == VAL_INT)
      type_name = "int";

    if (type_name) {
      /* Look up method in registry */
      for (int m = 0; m < interp->method_count; m++) {
        if (strcmp(interp->methods[m].type_name, type_name) == 0 &&
            strcmp(interp->methods[m].method_name, method_name) == 0) {
          AstNode *proc = interp->methods[m].proc;
          Environment *method_env = env_new(interp->global);

          /* Bind 'self' to the object */
          env_define(method_env, "self", obj);

          /* Evaluate and bind remaining args (skip 'self' param) */
          int argc = node->as.call.args.count;
          int param_start = 0;
          if (proc->as.proc.params.count > 0) {
            AstNode *first_param = proc->as.proc.params.items[0];
            if (strcmp(first_param->as.var_decl.name, "self") == 0)
              param_start = 1;
          }
          for (int i = 0;
               i < argc && (i + param_start) < proc->as.proc.params.count;
               i++) {
            Value arg_val = evaluate(interp, node->as.call.args.items[i]);
            if (interp->had_error) {
              env_free(method_env);
              return arg_val;
            }
            AstNode *param = proc->as.proc.params.items[i + param_start];
            env_define(method_env, param->as.var_decl.name, arg_val);
          }

          Environment *prev = interp->current;
          interp->current = method_env;
          interp->returning = false;
          execute(interp, proc->as.proc.body);
          Value result = interp->return_value;
          interp->return_value = value_none();
          interp->returning = false;
          interp->current = prev;
          env_free(method_env);
          return result;
        }
      }
    }
    return runtime_error(interp, "No method '%s' on type '%s'", method_name,
                         type_name ? type_name : "unknown");
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

  case AST_VAR_DECL: {
    /* Handle var decl appearing in expression context */
    Value init = value_none();
    if (node->as.var_decl.initializer) {
      init = evaluate(interp, node->as.var_decl.initializer);
      if (interp->had_error)
        return init;
    }
    env_define(interp->current, node->as.var_decl.name, init);
    return init;
  }

  case AST_MEMBER: {
    /* Member access: obj.field */
    Value obj = evaluate(interp, node->as.member.object);
    if (interp->had_error)
      return obj;
    if (obj.type == VAL_STRUCT) {
      char *field = node->as.member.member;
      for (int i = 0; i < obj.as.struct_val.field_count; i++) {
        if (strcmp(obj.as.struct_val.field_names[i], field) == 0) {
          return obj.as.struct_val.field_values[i];
        }
      }
      return runtime_error(interp, "Struct '%s' has no field '%s'",
                           obj.as.struct_val.type_name, field);
    }
    if (obj.type == VAL_ARRAY &&
        strcmp(node->as.member.member, "length") == 0) {
      return value_int(obj.as.array_val.count);
    }
    if (obj.type == VAL_STRING &&
        strcmp(node->as.member.member, "length") == 0) {
      return value_int(obj.as.string_val.length);
    }
    return runtime_error(interp,
                         "Cannot access member '%s' on value of type %d",
                         node->as.member.member, obj.type);
  }

  case AST_INDEX: {
    /* Array indexing: arr[i] */
    Value obj = evaluate(interp, node->as.index.object);
    if (interp->had_error)
      return obj;
    Value idx = evaluate(interp, node->as.index.index);
    if (interp->had_error)
      return idx;
    if (obj.type == VAL_ARRAY && idx.type == VAL_INT) {
      int i = (int)idx.as.int_val;
      if (i < 0 || i >= obj.as.array_val.count)
        return runtime_error(interp, "Array index %d out of bounds (size %d)",
                             i, obj.as.array_val.count);
      return obj.as.array_val.items[i];
    }
    if (obj.type == VAL_STRING && idx.type == VAL_INT) {
      int i = (int)idx.as.int_val;
      if (i < 0 || i >= obj.as.string_val.length)
        return runtime_error(interp, "String index %d out of bounds", i);
      return value_string(&obj.as.string_val.str[i], 1);
    }
    return runtime_error(interp, "Cannot index value of this type");
  }

  case AST_ARRAY_LITERAL: {
    /* Build runtime array from elements */
    int count = node->as.array_literal.elements.count;
    Value arr = {.type = VAL_ARRAY};
    arr.as.array_val.count = count;
    arr.as.array_val.capacity = count;
    arr.as.array_val.items = count > 0 ? malloc(count * sizeof(Value)) : NULL;
    for (int i = 0; i < count; i++) {
      arr.as.array_val.items[i] =
          evaluate(interp, node->as.array_literal.elements.items[i]);
      if (interp->had_error)
        return arr;
    }
    return arr;
  }

  case AST_LAMBDA: {
    /* Lambda expression: store as a proc-like value with closure */
    Value lambda = {.type = VAL_PROC};
    lambda.as.proc_val.proc = node;
    lambda.as.proc_val.closure = interp->current;
    return lambda;
  }

  case AST_ASSIGN:
    /* Assignment as expression: evaluate and return the value */
    {
      Value val = evaluate(interp, node->as.assign.value);
      if (interp->had_error)
        return val;
      if (node->as.assign.target->type == AST_IDENTIFIER) {
        char *name = node->as.assign.target->as.identifier.name;
        Value *existing = env_get(interp->current, name);
        if (existing) {
          *existing = val;
        } else {
          env_define(interp->current, name, val);
        }
      }
      return val;
    }

  case AST_STRUCT_LITERAL: {
    /* Build runtime struct from field assignments */
    int fc = node->as.struct_decl.fields.count;
    Value sv = {.type = VAL_STRUCT};
    sv.as.struct_val.field_count = fc;
    sv.as.struct_val.type_name = strdup(node->as.struct_decl.name);
    sv.as.struct_val.field_names = malloc(fc * sizeof(char *));
    sv.as.struct_val.field_values = malloc(fc * sizeof(Value));
    for (int i = 0; i < fc; i++) {
      AstNode *field = node->as.struct_decl.fields.items[i];
      /* Fields stored as AST_ASSIGN with target=identifier, value=expr */
      sv.as.struct_val.field_names[i] =
          strdup(field->as.assign.target->as.identifier.name);
      sv.as.struct_val.field_values[i] =
          evaluate(interp, field->as.assign.value);
      if (interp->had_error)
        return sv;
    }
    return sv;
  }

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
  AstNode *cond_node = node->as.if_stmt.condition;

  /* Check for 'if expr has name' pattern (named maybe unwrapping) */
  if (cond_node && cond_node->type == AST_BINARY_OP &&
      cond_node->as.binary.op == OP_HAS && cond_node->as.binary.right != NULL &&
      cond_node->as.binary.right->type == AST_IDENTIFIER) {

    /* Evaluate the left side (the maybe value) */
    Value maybe_val = evaluate(interp, cond_node->as.binary.left);
    if (interp->had_error)
      return;

    if (maybe_val.type != VAL_NONE) {
      /* Value present — create new scope and bind the unwrapped value */
      const char *bind_name = cond_node->as.binary.right->as.identifier.name;
      Environment *prev = interp->current;
      interp->current = env_new(prev);
      env_define(interp->current, bind_name, maybe_val);
      execute(interp, node->as.if_stmt.then_branch);
      Environment *scope = interp->current;
      interp->current = prev;
      env_free(scope);
    } else if (node->as.if_stmt.else_branch) {
      execute(interp, node->as.if_stmt.else_branch);
    }
    return;
  }

  /* Normal if-else */
  Value cond = evaluate(interp, cond_node);
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
  if (node->as.var_decl.is_const) {
    env_define_const(interp->current, node->as.var_decl.name, val);
  } else {
    env_define(interp->current, node->as.var_decl.name, val);
  }
}

static void exec_assign(Interpreter *interp, AstNode *node) {
  Value val = evaluate(interp, node->as.assign.value);
  if (interp->had_error)
    return;

  if (node->as.assign.target->type == AST_IDENTIFIER) {
    char *name = node->as.assign.target->as.identifier.name;
    if (!env_set(interp->current, name, val)) {
      /* env_set returns false for const vars or undefined vars */
      /* Check if it exists at all */
      Value *existing = env_get(interp->current, name);
      if (existing) {
        /* It exists but is const — error already printed by env_set */
        runtime_error(interp, "Cannot reassign const variable '%s'", name);
      } else {
        /* Not found — define it */
        env_define(interp->current, name, val);
      }
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

  case AST_WHEN: {
    /* Pattern matching: evaluate value, compare against each case */
    Value val = evaluate(interp, node->as.when_stmt.value);
    if (interp->had_error)
      return;
    for (int i = 0; i < node->as.when_stmt.cases.count; i++) {
      AstNode *c = node->as.when_stmt.cases.items[i];
      AstNode *pattern = c->as.if_stmt.condition;

      /* Wildcard _ matches everything */
      if (pattern->type == AST_IDENTIFIER &&
          strcmp(pattern->as.identifier.name, "_") == 0) {
        execute(interp, c->as.if_stmt.then_branch);
        break;
      }

      /* Range pattern: AST_BINARY_OP with NULL left → direct compare */
      if (pattern->type == AST_BINARY_OP && pattern->as.binary.left == NULL) {
        Value rhs = evaluate(interp, pattern->as.binary.right);
        if (interp->had_error)
          return;
        int matched = 0;
        double lv =
            (val.type == VAL_FLOAT) ? val.as.float_val : (double)val.as.int_val;
        double rv =
            (rhs.type == VAL_FLOAT) ? rhs.as.float_val : (double)rhs.as.int_val;
        switch (pattern->as.binary.op) {
        case OP_GT:
          matched = (lv > rv);
          break;
        case OP_GE:
          matched = (lv >= rv);
          break;
        case OP_LT:
          matched = (lv < rv);
          break;
        case OP_LE:
          matched = (lv <= rv);
          break;
        default:
          break;
        }
        if (matched) {
          execute(interp, c->as.if_stmt.then_branch);
          break;
        }
        continue;
      }

      /* Multi-pattern: AST_BLOCK — match if any sub-pattern matches */
      if (pattern->type == AST_BLOCK) {
        int any_matched = 0;
        for (int j = 0; j < pattern->as.block.statements.count; j++) {
          Value pv = evaluate(interp, pattern->as.block.statements.items[j]);
          if (interp->had_error)
            return;
          if (val.type == pv.type) {
            if ((val.type == VAL_INT && val.as.int_val == pv.as.int_val) ||
                (val.type == VAL_STRING &&
                 val.as.string_val.length == pv.as.string_val.length &&
                 memcmp(val.as.string_val.str, pv.as.string_val.str,
                        val.as.string_val.length) == 0) ||
                (val.type == VAL_BOOL && val.as.bool_val == pv.as.bool_val) ||
                (val.type == VAL_NONE)) {
              any_matched = 1;
              break;
            }
          }
        }
        if (any_matched) {
          execute(interp, c->as.if_stmt.then_branch);
          break;
        }
        continue;
      }

      /* Single pattern: evaluate and compare */
      Value pv = evaluate(interp, pattern);
      if (interp->had_error)
        return;
      int matched = 0;
      if (val.type == pv.type) {
        switch (val.type) {
        case VAL_INT:
          matched = (val.as.int_val == pv.as.int_val);
          break;
        case VAL_FLOAT:
          matched = (val.as.float_val == pv.as.float_val);
          break;
        case VAL_BOOL:
          matched = (val.as.bool_val == pv.as.bool_val);
          break;
        case VAL_STRING:
          matched = (val.as.string_val.length == pv.as.string_val.length &&
                     memcmp(val.as.string_val.str, pv.as.string_val.str,
                            val.as.string_val.length) == 0);
          break;
        case VAL_NONE:
          matched = 1;
          break;
        default:
          break;
        }
      }
      if (matched) {
        execute(interp, c->as.if_stmt.then_branch);
        break;
      }
    }
    break;
  }

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

  case AST_ENUM: {
    /* Register enum as a struct where fields are variants with int values */
    Value enum_val;
    enum_val.type = VAL_STRUCT;
    enum_val.as.struct_val.type_name = strdup(node->as.enum_decl.name);
    int num_variants = node->as.enum_decl.variants.count;
    enum_val.as.struct_val.field_count = num_variants;
    enum_val.as.struct_val.field_names = calloc(num_variants, sizeof(char *));
    enum_val.as.struct_val.field_values = calloc(num_variants, sizeof(Value));
    for (int i = 0; i < num_variants; i++) {
      AstNode *v = node->as.enum_decl.variants.items[i];
      enum_val.as.struct_val.field_names[i] = strdup(v->as.identifier.name);
      enum_val.as.struct_val.field_values[i] = value_int(i);
      if (!env_get(interp->global, v->as.identifier.name)) {
        env_define_const(interp->global, v->as.identifier.name, value_int(i));
      }
    }
    env_define(interp->global, node->as.enum_decl.name, enum_val);
    break;
  }

  case AST_STRUCT: {
    /* Register struct type definition:
     * Stores a descriptor with default field values (none) so that
     * constructors like `Person { name: "Alice", age: 30 }` can work
     * and the type name is accessible via typeof(). */
    int nfields = node->as.struct_decl.fields.count;
    Value stype;
    stype.type = VAL_STRUCT;
    stype.as.struct_val.type_name = strdup(node->as.struct_decl.name);
    stype.as.struct_val.field_count = nfields;
    stype.as.struct_val.field_names = calloc(nfields, sizeof(char *));
    stype.as.struct_val.field_values = calloc(nfields, sizeof(Value));
    for (int i = 0; i < nfields; i++) {
      AstNode *f = node->as.struct_decl.fields.items[i];
      stype.as.struct_val.field_names[i] = strdup(f->as.var_decl.name);
      /* Default value is none; struct literals override at construction */
      stype.as.struct_val.field_values[i] = value_none();
    }
    env_define(interp->global, node->as.struct_decl.name, stype);
    break;
  }

  case AST_MODULE:
  case AST_IMPORT:
  case AST_PRAGMA:
    /* Module/import = currently no-op (stubs for future linking) */
    /* Pragma = handled at compile-time, no runtime effect */
    break;

  case AST_EXTEND: {
    /* Register methods in the method table */
    char *type_name = node->as.extend_decl.type_name;
    for (int i = 0; i < node->as.extend_decl.methods.count; i++) {
      AstNode *method = node->as.extend_decl.methods.items[i];
      if (interp->method_count < 256) {
        int idx = interp->method_count++;
        interp->methods[idx].type_name = strdup(type_name);
        interp->methods[idx].method_name = strdup(method->as.proc.name);
        interp->methods[idx].proc = method;
      }
    }
    break;
  }

  case AST_TRY: {
    /* Execute try block */
    execute(interp, node->as.try_stmt.try_block);

    /* If error occurred, try to handle with catch */
    if (interp->had_error && node->as.try_stmt.catches.count > 0) {
      /* Save error message */
      char saved_error[512];
      strncpy(saved_error, interp->error_message, sizeof(saved_error) - 1);
      saved_error[sizeof(saved_error) - 1] = '\0';

      /* Clear error state */
      interp->had_error = false;
      interp->error_message[0] = '\0';

      /* Use the last catch block (catch-all: catch any e) */
      int ci = node->as.try_stmt.catches.count - 1;
      AstNode *catch_node = node->as.try_stmt.catches.items[ci];
      /* condition holds the error variable name */
      const char *err_var =
          catch_node->as.if_stmt.condition->as.identifier.name;

      /* Bind error variable in current scope */
      Value err_val = value_string(saved_error, (int)strlen(saved_error));
      env_define(interp->current, err_var, err_val);

      /* Execute catch body */
      execute(interp, catch_node->as.if_stmt.then_branch);
    }
    break;
  }

  case AST_FAIL: {
    /* Evaluate error expression and trigger error */
    Value err = evaluate(interp, node->as.fail_stmt.error);
    if (interp->had_error)
      return;
    if (err.type == VAL_STRING) {
      runtime_error(interp, "%.*s", err.as.string_val.length,
                    err.as.string_val.str);
    } else {
      runtime_error(interp, "fail: unhandled error");
    }
    return;
  }

  case AST_NONE_LITERAL:
    /* No-op: used as placeholder for struct definitions */
    break;

  case AST_FOR: {
    /* for-in loop: iterate over array */
    if (node->as.for_stmt.iterable && node->as.for_stmt.var_name) {
      Value iterable = evaluate(interp, node->as.for_stmt.iterable);
      if (interp->had_error)
        return;
      if (iterable.type == VAL_ARRAY) {
        Environment *loop_env = env_new(interp->current);
        Environment *prev = interp->current;
        interp->current = loop_env;
        for (int i = 0; i < iterable.as.array_val.count; i++) {
          env_define(loop_env, node->as.for_stmt.var_name,
                     iterable.as.array_val.items[i]);
          execute(interp, node->as.for_stmt.body);
          if (interp->had_error || interp->returning)
            break;
          if (interp->breaking) {
            interp->breaking = false;
            break;
          }
          if (interp->continuing) {
            interp->continuing = false;
          }
        }
        interp->current = prev;
        env_free(loop_env);
      }
    }
    break;
  }

  case AST_EXPR_STMT:
  case AST_INT_LITERAL:
  case AST_FLOAT_LITERAL:
  case AST_STRING_LITERAL:
  case AST_BOOL_LITERAL:
  case AST_IDENTIFIER:
  case AST_BINARY_OP:
  case AST_UNARY_OP:
  case AST_CALL:
  case AST_MEMBER:
  case AST_INDEX:
  case AST_ARRAY_LITERAL:
  case AST_STRUCT_LITERAL:
  case AST_LAMBDA:
  case AST_PIPELINE:
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

void interpreter_exec(Interpreter *interp, AstNode *node) {
  execute(interp, node);
}

Value interpreter_eval(Interpreter *interp, AstNode *expr) {
  return evaluate(interp, expr);
}
