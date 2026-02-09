/*
 * QISC AST - Abstract Syntax Tree Node Definitions
 */

#ifndef QISC_AST_H
#define QISC_AST_H

#include "qisc.h"
#include <stdint.h>

/* AST Node Types */
typedef enum {
  /* Literals */
  AST_INT_LITERAL,
  AST_FLOAT_LITERAL,
  AST_STRING_LITERAL,
  AST_BOOL_LITERAL,
  AST_NONE_LITERAL,

  /* Expressions */
  AST_IDENTIFIER,
  AST_BINARY_OP,
  AST_UNARY_OP,
  AST_CALL,
  AST_INDEX,
  AST_MEMBER,
  AST_PIPELINE,
  AST_LAMBDA,
  AST_ARRAY_LITERAL,
  AST_STRUCT_LITERAL,

  /* Statements */
  AST_VAR_DECL,
  AST_ASSIGN,
  AST_EXPR_STMT,
  AST_BLOCK,
  AST_IF,
  AST_WHEN,
  AST_FOR,
  AST_WHILE,
  AST_GIVE,
  AST_BREAK,
  AST_CONTINUE,
  AST_TRY,
  AST_FAIL,

  /* Declarations */
  AST_PROC,
  AST_STRUCT,
  AST_ENUM,
  AST_EXTEND,
  AST_MODULE,
  AST_IMPORT,

  /* Top level */
  AST_PROGRAM,
  AST_PRAGMA,
} AstNodeType;

/* Binary operators */
typedef enum {
  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_MOD,
  OP_EQ,
  OP_NE,
  OP_LT,
  OP_GT,
  OP_LE,
  OP_GE,
  OP_AND,
  OP_OR,
  OP_BIT_AND,
  OP_BIT_OR,
  OP_BIT_XOR,
  OP_LSHIFT,
  OP_RSHIFT,
  OP_PIPELINE,
} BinaryOp;

/* Unary operators */
typedef enum {
  OP_NEG,     /* - */
  OP_NOT,     /* not / ! */
  OP_BIT_NOT, /* ~ */
  OP_DEREF,   /* * */
  OP_ADDR,    /* & */
  OP_INC,     /* ++ (postfix) */
  OP_DEC,     /* -- (postfix) */
} UnaryOp;

/* Forward declaration */
typedef struct AstNode AstNode;

/* Array of AST nodes */
typedef struct {
  AstNode **items;
  int count;
  int capacity;
} AstNodeArray;

/* Type info (simplified for now) */
typedef struct {
  char *name; /* Type name */
  bool is_pointer;
  bool is_array;
  bool is_maybe;
  bool is_canfail;
} TypeInfo;

/* AST Node - union of all node types */
struct AstNode {
  AstNodeType type;
  int line;
  int column;

  union {
    /* Literals */
    struct {
      int64_t value;
    } int_literal;
    struct {
      double value;
    } float_literal;
    struct {
      char *value;
      int length;
    } string_literal;
    struct {
      bool value;
    } bool_literal;

    /* Identifier */
    struct {
      char *name;
    } identifier;

    /* Binary operation */
    struct {
      BinaryOp op;
      AstNode *left;
      AstNode *right;
    } binary;

    /* Unary operation */
    struct {
      UnaryOp op;
      AstNode *operand;
    } unary;

    /* Function call */
    struct {
      AstNode *callee;
      AstNodeArray args;
    } call;

    /* Index access: arr[index] */
    struct {
      AstNode *object;
      AstNode *index;
    } index;

    /* Member access: obj.member */
    struct {
      AstNode *object;
      char *member;
    } member;

    /* Lambda: x => x * 2 */
    struct {
      AstNodeArray params;
      AstNode *body;
    } lambda;

    /* Variable declaration */
    struct {
      char *name;
      TypeInfo *type_info;
      AstNode *initializer;
      bool is_auto;
    } var_decl;

    /* Assignment */
    struct {
      AstNode *target;
      AstNode *value;
    } assign;

    /* Block */
    struct {
      AstNodeArray statements;
    } block;

    /* If statement */
    struct {
      AstNode *condition;
      AstNode *then_branch;
      AstNode *else_branch;
    } if_stmt;

    /* When statement (pattern match) */
    struct {
      AstNode *value;
      AstNodeArray cases;
    } when_stmt;

    /* For loop */
    struct {
      AstNode *init;
      AstNode *condition;
      AstNode *update;
      AstNode *body;
      /* Or for-in style */
      char *var_name;
      TypeInfo *var_type;
      AstNode *iterable;
    } for_stmt;

    /* While loop */
    struct {
      AstNode *condition;
      AstNode *body;
    } while_stmt;

    /* Give (return) */
    struct {
      AstNode *value;
    } give_stmt;

    /* Try/catch */
    struct {
      AstNode *try_block;
      AstNodeArray catches;
    } try_stmt;

    /* Fail */
    struct {
      AstNode *error;
    } fail_stmt;

    /* Procedure declaration */
    struct {
      char *name;
      AstNodeArray params;
      TypeInfo *return_type;
      AstNode *body;
      bool is_canfail;
    } proc;

    /* Parameter */
    struct {
      char *name;
      TypeInfo *type_info;
    } param;

    /* Struct declaration */
    struct {
      char *name;
      AstNodeArray fields;
    } struct_decl;

    /* Pragma */
    struct {
      char *name;
      char *value;
    } pragma;

    /* Program (root) */
    struct {
      AstNodeArray declarations;
      AstNodeArray pragmas;
    } program;

    /* Array literal */
    struct {
      AstNodeArray elements;
    } array_literal;

  } as;
};

/* Node creation functions */
AstNode *ast_new_int(int64_t value, int line, int col);
AstNode *ast_new_float(double value, int line, int col);
AstNode *ast_new_string(const char *value, int length, int line, int col);
AstNode *ast_new_bool(bool value, int line, int col);
AstNode *ast_new_none(int line, int col);
AstNode *ast_new_identifier(const char *name, int line, int col);
AstNode *ast_new_binary(BinaryOp op, AstNode *left, AstNode *right, int line,
                        int col);
AstNode *ast_new_unary(UnaryOp op, AstNode *operand, int line, int col);
AstNode *ast_new_call(AstNode *callee, int line, int col);
AstNode *ast_new_block(int line, int col);
AstNode *ast_new_if(AstNode *cond, AstNode *then_b, AstNode *else_b, int line,
                    int col);
AstNode *ast_new_while(AstNode *cond, AstNode *body, int line, int col);
AstNode *ast_new_give(AstNode *value, int line, int col);
AstNode *ast_new_var_decl(const char *name, TypeInfo *type, AstNode *init,
                          bool is_auto, int line, int col);
AstNode *ast_new_assign(AstNode *target, AstNode *value, int line, int col);
AstNode *ast_new_proc(const char *name, int line, int col);
AstNode *ast_new_program(void);

/* Node array operations */
void ast_array_init(AstNodeArray *arr);
void ast_array_push(AstNodeArray *arr, AstNode *node);
void ast_array_free(AstNodeArray *arr);

/* Type info */
TypeInfo *type_info_new(const char *name);
void type_info_free(TypeInfo *info);

/* Free AST */
void ast_free(AstNode *node);

/* Debug print */
void ast_print(AstNode *node, int indent);

#endif /* QISC_AST_H */
