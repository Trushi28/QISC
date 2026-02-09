/*
 * QISC Interpreter Header
 */

#ifndef QISC_INTERPRETER_H
#define QISC_INTERPRETER_H

#include "../parser/ast.h"

/* Value types */
typedef enum {
  VAL_NONE,
  VAL_INT,
  VAL_FLOAT,
  VAL_BOOL,
  VAL_STRING,
  VAL_PROC,
  VAL_ARRAY,
  VAL_ERROR,
} ValueType;

/* Runtime value */
typedef struct Value {
  ValueType type;
  union {
    int64_t int_val;
    double float_val;
    bool bool_val;
    struct {
      char *str;
      int length;
    } string_val;
    struct {
      AstNode *proc;
      struct Environment *closure;
    } proc_val;
    struct {
      struct Value *items;
      int count;
      int capacity;
    } array_val;
    struct {
      char *message;
    } error_val;
  } as;
} Value;

/* Environment for variable storage */
typedef struct Environment {
  struct Environment *parent;
  char **names;
  Value *values;
  int count;
  int capacity;
} Environment;

/* Interpreter state */
typedef struct {
  Environment *global;
  Environment *current;
  bool had_error;
  char error_message[512];
  bool returning;
  Value return_value;
  bool breaking;
  bool continuing;
} Interpreter;

/* Initialize interpreter */
void interpreter_init(Interpreter *interp);

/* Free interpreter */
void interpreter_free(Interpreter *interp);

/* Interpret a program */
Value interpreter_run(Interpreter *interp, AstNode *program);

/* Interpret a single expression */
Value interpreter_eval(Interpreter *interp, AstNode *expr);

/* Environment operations */
Environment *env_new(Environment *parent);
void env_free(Environment *env);
void env_define(Environment *env, const char *name, Value value);
Value *env_get(Environment *env, const char *name);
bool env_set(Environment *env, const char *name, Value value);

/* Value operations */
Value value_none(void);
Value value_int(int64_t val);
Value value_float(double val);
Value value_bool(bool val);
Value value_string(const char *str, int length);
Value value_error(const char *message);
void value_free(Value *val);
void value_print(Value *val);
bool value_is_truthy(Value *val);

#endif /* QISC_INTERPRETER_H */
