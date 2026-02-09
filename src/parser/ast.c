/*
 * QISC AST Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Helper to allocate node */
static AstNode *ast_alloc(AstNodeType type, int line, int col) {
  AstNode *node = calloc(1, sizeof(AstNode));
  node->type = type;
  node->line = line;
  node->column = col;
  return node;
}

/* Node array operations */
void ast_array_init(AstNodeArray *arr) {
  arr->items = NULL;
  arr->count = 0;
  arr->capacity = 0;
}

void ast_array_push(AstNodeArray *arr, AstNode *node) {
  if (arr->count >= arr->capacity) {
    arr->capacity = arr->capacity == 0 ? 8 : arr->capacity * 2;
    arr->items = realloc(arr->items, arr->capacity * sizeof(AstNode *));
  }
  arr->items[arr->count++] = node;
}

void ast_array_free(AstNodeArray *arr) {
  for (int i = 0; i < arr->count; i++) {
    ast_free(arr->items[i]);
  }
  free(arr->items);
  arr->items = NULL;
  arr->count = 0;
  arr->capacity = 0;
}

/* Type info */
TypeInfo *type_info_new(const char *name) {
  TypeInfo *info = calloc(1, sizeof(TypeInfo));
  info->name = strdup(name);
  return info;
}

void type_info_free(TypeInfo *info) {
  if (info) {
    free(info->name);
    free(info);
  }
}

/* Literal constructors */
AstNode *ast_new_int(int64_t value, int line, int col) {
  AstNode *node = ast_alloc(AST_INT_LITERAL, line, col);
  node->as.int_literal.value = value;
  return node;
}

AstNode *ast_new_float(double value, int line, int col) {
  AstNode *node = ast_alloc(AST_FLOAT_LITERAL, line, col);
  node->as.float_literal.value = value;
  return node;
}

AstNode *ast_new_string(const char *value, int length, int line, int col) {
  AstNode *node = ast_alloc(AST_STRING_LITERAL, line, col);
  node->as.string_literal.value = malloc(length + 1);
  memcpy(node->as.string_literal.value, value, length);
  node->as.string_literal.value[length] = '\0';
  node->as.string_literal.length = length;
  return node;
}

AstNode *ast_new_bool(bool value, int line, int col) {
  AstNode *node = ast_alloc(AST_BOOL_LITERAL, line, col);
  node->as.bool_literal.value = value;
  return node;
}

AstNode *ast_new_none(int line, int col) {
  return ast_alloc(AST_NONE_LITERAL, line, col);
}

AstNode *ast_new_identifier(const char *name, int line, int col) {
  AstNode *node = ast_alloc(AST_IDENTIFIER, line, col);
  node->as.identifier.name = strdup(name);
  return node;
}

/* Expression constructors */
AstNode *ast_new_binary(BinaryOp op, AstNode *left, AstNode *right, int line,
                        int col) {
  AstNode *node = ast_alloc(AST_BINARY_OP, line, col);
  node->as.binary.op = op;
  node->as.binary.left = left;
  node->as.binary.right = right;
  return node;
}

AstNode *ast_new_unary(UnaryOp op, AstNode *operand, int line, int col) {
  AstNode *node = ast_alloc(AST_UNARY_OP, line, col);
  node->as.unary.op = op;
  node->as.unary.operand = operand;
  return node;
}

AstNode *ast_new_call(AstNode *callee, int line, int col) {
  AstNode *node = ast_alloc(AST_CALL, line, col);
  node->as.call.callee = callee;
  ast_array_init(&node->as.call.args);
  return node;
}

/* Statement constructors */
AstNode *ast_new_block(int line, int col) {
  AstNode *node = ast_alloc(AST_BLOCK, line, col);
  ast_array_init(&node->as.block.statements);
  return node;
}

AstNode *ast_new_if(AstNode *cond, AstNode *then_b, AstNode *else_b, int line,
                    int col) {
  AstNode *node = ast_alloc(AST_IF, line, col);
  node->as.if_stmt.condition = cond;
  node->as.if_stmt.then_branch = then_b;
  node->as.if_stmt.else_branch = else_b;
  return node;
}

AstNode *ast_new_while(AstNode *cond, AstNode *body, int line, int col) {
  AstNode *node = ast_alloc(AST_WHILE, line, col);
  node->as.while_stmt.condition = cond;
  node->as.while_stmt.body = body;
  return node;
}

AstNode *ast_new_give(AstNode *value, int line, int col) {
  AstNode *node = ast_alloc(AST_GIVE, line, col);
  node->as.give_stmt.value = value;
  return node;
}

AstNode *ast_new_var_decl(const char *name, TypeInfo *type, AstNode *init,
                          bool is_auto, int line, int col) {
  AstNode *node = ast_alloc(AST_VAR_DECL, line, col);
  node->as.var_decl.name = strdup(name);
  node->as.var_decl.type_info = type;
  node->as.var_decl.initializer = init;
  node->as.var_decl.is_auto = is_auto;
  return node;
}

AstNode *ast_new_assign(AstNode *target, AstNode *value, int line, int col) {
  AstNode *node = ast_alloc(AST_ASSIGN, line, col);
  node->as.assign.target = target;
  node->as.assign.value = value;
  return node;
}

AstNode *ast_new_proc(const char *name, int line, int col) {
  AstNode *node = ast_alloc(AST_PROC, line, col);
  node->as.proc.name = strdup(name);
  ast_array_init(&node->as.proc.params);
  node->as.proc.return_type = NULL;
  node->as.proc.body = NULL;
  node->as.proc.is_canfail = false;
  return node;
}

AstNode *ast_new_program(void) {
  AstNode *node = ast_alloc(AST_PROGRAM, 1, 1);
  ast_array_init(&node->as.program.declarations);
  ast_array_init(&node->as.program.pragmas);
  return node;
}

/* Free AST node recursively */
void ast_free(AstNode *node) {
  if (!node)
    return;

  switch (node->type) {
  case AST_STRING_LITERAL:
    free(node->as.string_literal.value);
    break;
  case AST_IDENTIFIER:
    free(node->as.identifier.name);
    break;
  case AST_BINARY_OP:
    ast_free(node->as.binary.left);
    ast_free(node->as.binary.right);
    break;
  case AST_UNARY_OP:
    ast_free(node->as.unary.operand);
    break;
  case AST_CALL:
    ast_free(node->as.call.callee);
    ast_array_free(&node->as.call.args);
    break;
  case AST_BLOCK:
    ast_array_free(&node->as.block.statements);
    break;
  case AST_IF:
    ast_free(node->as.if_stmt.condition);
    ast_free(node->as.if_stmt.then_branch);
    ast_free(node->as.if_stmt.else_branch);
    break;
  case AST_WHILE:
    ast_free(node->as.while_stmt.condition);
    ast_free(node->as.while_stmt.body);
    break;
  case AST_GIVE:
    ast_free(node->as.give_stmt.value);
    break;
  case AST_VAR_DECL:
    free(node->as.var_decl.name);
    type_info_free(node->as.var_decl.type_info);
    ast_free(node->as.var_decl.initializer);
    break;
  case AST_ASSIGN:
    ast_free(node->as.assign.target);
    ast_free(node->as.assign.value);
    break;
  case AST_PROC:
    free(node->as.proc.name);
    ast_array_free(&node->as.proc.params);
    type_info_free(node->as.proc.return_type);
    ast_free(node->as.proc.body);
    break;
  case AST_PROGRAM:
    ast_array_free(&node->as.program.declarations);
    ast_array_free(&node->as.program.pragmas);
    break;
  default:
    break;
  }

  free(node);
}

/* Debug print helper */
static void print_indent(int indent) {
  for (int i = 0; i < indent; i++)
    printf("  ");
}

void ast_print(AstNode *node, int indent) {
  if (!node) {
    print_indent(indent);
    printf("(null)\n");
    return;
  }

  print_indent(indent);
  switch (node->type) {
  case AST_INT_LITERAL:
    printf("Int(%lld)\n", (long long)node->as.int_literal.value);
    break;
  case AST_FLOAT_LITERAL:
    printf("Float(%f)\n", node->as.float_literal.value);
    break;
  case AST_STRING_LITERAL:
    printf("String(\"%s\")\n", node->as.string_literal.value);
    break;
  case AST_BOOL_LITERAL:
    printf("Bool(%s)\n", node->as.bool_literal.value ? "true" : "false");
    break;
  case AST_NONE_LITERAL:
    printf("None\n");
    break;
  case AST_IDENTIFIER:
    printf("Ident(%s)\n", node->as.identifier.name);
    break;
  case AST_BINARY_OP:
    printf("BinaryOp(%d)\n", node->as.binary.op);
    ast_print(node->as.binary.left, indent + 1);
    ast_print(node->as.binary.right, indent + 1);
    break;
  case AST_CALL:
    printf("Call\n");
    ast_print(node->as.call.callee, indent + 1);
    for (int i = 0; i < node->as.call.args.count; i++) {
      ast_print(node->as.call.args.items[i], indent + 1);
    }
    break;
  case AST_BLOCK:
    printf("Block\n");
    for (int i = 0; i < node->as.block.statements.count; i++) {
      ast_print(node->as.block.statements.items[i], indent + 1);
    }
    break;
  case AST_IF:
    printf("If\n");
    ast_print(node->as.if_stmt.condition, indent + 1);
    ast_print(node->as.if_stmt.then_branch, indent + 1);
    if (node->as.if_stmt.else_branch) {
      ast_print(node->as.if_stmt.else_branch, indent + 1);
    }
    break;
  case AST_GIVE:
    printf("Give\n");
    ast_print(node->as.give_stmt.value, indent + 1);
    break;
  case AST_VAR_DECL:
    printf("VarDecl(%s, auto=%d)\n", node->as.var_decl.name,
           node->as.var_decl.is_auto);
    if (node->as.var_decl.initializer) {
      ast_print(node->as.var_decl.initializer, indent + 1);
    }
    break;
  case AST_PROC:
    printf("Proc(%s)\n", node->as.proc.name);
    if (node->as.proc.body) {
      ast_print(node->as.proc.body, indent + 1);
    }
    break;
  case AST_PROGRAM:
    printf("Program\n");
    for (int i = 0; i < node->as.program.declarations.count; i++) {
      ast_print(node->as.program.declarations.items[i], indent + 1);
    }
    break;
  default:
    printf("Node(%d)\n", node->type);
    break;
  }
}
