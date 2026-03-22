/*
 * QISC Parser - Recursive Descent Parser Implementation
 */

#define _POSIX_C_SOURCE 200809L
#include "parser.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======== Helper Functions ======== */

static void advance(Parser *parser) {
  parser->previous = parser->current;

  for (;;) {
    parser->current = lexer_scan_token(parser->lexer);
    if (parser->current.type != TOK_ERROR)
      break;

    parser_error_at_current(parser, parser->current.start);
  }
}

static bool check(Parser *parser, TokenType type) {
  return parser->current.type == type;
}

static bool match(Parser *parser, TokenType type) {
  if (!check(parser, type))
    return false;
  advance(parser);
  return true;
}

static void consume(Parser *parser, TokenType type, const char *message) {
  if (parser->current.type == type) {
    advance(parser);
    return;
  }
  parser_error_at_current(parser, message);
}

static char *token_string(Token *token) {
  char *str = malloc(token->length + 1);
  memcpy(str, token->start, token->length);
  str[token->length] = '\0';
  return str;
}

/* ======== Error Handling ======== */

void parser_error(Parser *parser, const char *message) {
  if (parser->panic_mode)
    return;
  parser->panic_mode = true;
  parser->had_error = true;

  fprintf(stderr, "[line %d] Error at '%.*s': %s\n", parser->previous.line,
          parser->previous.length, parser->previous.start, message);

  strncpy(parser->error_message, message, sizeof(parser->error_message) - 1);
}

void parser_error_at_current(Parser *parser, const char *message) {
  if (parser->panic_mode)
    return;
  parser->panic_mode = true;
  parser->had_error = true;

  fprintf(stderr, "[line %d] Error at '%.*s': %s\n", parser->current.line,
          parser->current.length, parser->current.start, message);

  strncpy(parser->error_message, message, sizeof(parser->error_message) - 1);
}

static void synchronize(Parser *parser) {
  parser->panic_mode = false;

  while (parser->current.type != TOK_EOF) {
    if (parser->previous.type == TOK_SEMICOLON)
      return;

    switch (parser->current.type) {
    case TOK_PROC:
    case TOK_STRUCT:
    case TOK_IF:
    case TOK_WHILE:
    case TOK_FOR:
    case TOK_GIVE:
      return;
    default:
      break;
    }
    advance(parser);
  }
}

/* ======== Forward Declarations ======== */

static AstNode *expression(Parser *parser);
static AstNode *assignment(Parser *parser);
static AstNode *statement(Parser *parser);
static AstNode *declaration(Parser *parser);
static AstNode *block(Parser *parser);
static AstNode *ast_alloc_member(AstNode *obj, char *member, int line, int col);
static AstNode *parse_interpolated_string(Parser *parser, const char *str,
                                          int len, int line, int col);
static TypeInfo *parse_type(Parser *parser);

/* ======== Expression Parsing ======== */

/* primary → INT | FLOAT | STRING | TRUE | FALSE | NONE | IDENT | '(' expr ')'
 */
static AstNode *primary(Parser *parser) {
  int line = parser->current.line;
  int col = parser->current.column;

  if (match(parser, TOK_INT_LIT)) {
    return ast_new_int(parser->previous.literal.int_value, line, col);
  }

  if (match(parser, TOK_FLOAT_LIT)) {
    return ast_new_float(parser->previous.literal.float_value, line, col);
  }

  if (match(parser, TOK_STRING_LIT)) {
    /* Remove quotes and handle string interpolation */
    const char *str = parser->previous.start + 1;
    int len = parser->previous.length - 2;
    return parse_interpolated_string(parser, str, len, line, col);
  }

  if (match(parser, TOK_TRUE)) {
    return ast_new_bool(true, line, col);
  }

  if (match(parser, TOK_FALSE)) {
    return ast_new_bool(false, line, col);
  }

  if (match(parser, TOK_NONE)) {
    return ast_new_none(line, col);
  }

  /* do block: multi-line lambda */
  if (match(parser, TOK_DO)) {
    AstNode *lambda = calloc(1, sizeof(AstNode));
    lambda->type = AST_LAMBDA;
    lambda->line = line;
    lambda->column = col;
    ast_array_init(&lambda->as.lambda.params);

    /* Optional params: do |x, y| { body } */
    if (match(parser, TOK_PIPE)) {
      if (!check(parser, TOK_PIPE)) {
        do {
          consume(parser, TOK_IDENT, "Expected parameter name");
          char *pname = token_string(&parser->previous);
          AstNode *param = calloc(1, sizeof(AstNode));
          param->type = AST_VAR_DECL;
          param->line = parser->previous.line;
          param->column = parser->previous.column;
          param->as.var_decl.name = pname;
          param->as.var_decl.type_info = NULL;
          param->as.var_decl.initializer = NULL;
          param->as.var_decl.is_auto = false;
          ast_array_push(&lambda->as.lambda.params, param);
        } while (match(parser, TOK_COMMA));
      }
      consume(parser, TOK_PIPE, "Expected '|' after do parameters");
    }

    consume(parser, TOK_LBRACE, "Expected '{' for do block body");
    lambda->as.lambda.body = block(parser);
    return lambda;
  }

  /* sizeof/typeof keywords → treat as identifiers for builtin call dispatch */
  if (match(parser, TOK_SIZEOF)) {
    return ast_new_identifier("sizeof", line, col);
  }
  if (match(parser, TOK_TYPEOF)) {
    return ast_new_identifier("typeof", line, col);
  }

  if (match(parser, TOK_SELF)) {
    char *name = strdup("self");
    AstNode *node = ast_new_identifier(name, line, col);
    free(name);
    return node;
  }

  if (match(parser, TOK_IDENT)) {
    char *name = token_string(&parser->previous);

    /* Check for lambda: ident => expr */
    if (check(parser, TOK_FATARROW)) {
      advance(parser); /* consume => */
      AstNode *body = expression(parser);
      AstNode *lambda = calloc(1, sizeof(AstNode));
      lambda->type = AST_LAMBDA;
      lambda->line = line;
      lambda->column = col;
      ast_array_init(&lambda->as.lambda.params);
      /* Create param node */
      AstNode *param = calloc(1, sizeof(AstNode));
      param->type = AST_VAR_DECL;
      param->as.var_decl.name = name;
      param->as.var_decl.type_info = NULL;
      ast_array_push(&lambda->as.lambda.params, param);
      lambda->as.lambda.body = body;
      return lambda;
    }

    /* Check for struct literal: Name { field: value, ... }
     * Only enter struct literal mode if the identifier starts with uppercase
     * (convention for type/struct names). This prevents ambiguity with
     * patterns like 'names { ... }' where { starts a block. */
    if (check(parser, TOK_LBRACE) && name[0] >= 'A' && name[0] <= 'Z') {
      advance(parser); /* consume { */
      AstNode *lit = calloc(1, sizeof(AstNode));
      lit->type = AST_STRUCT_LITERAL;
      lit->line = line;
      lit->column = col;
      /* Store struct name - we'll need a field for this */
      /* For now, store the name and parse fields */
      lit->as.struct_decl.name = name; /* Reuse struct_decl temporarily */
      ast_array_init(&lit->as.struct_decl.fields);

      if (!check(parser, TOK_RBRACE)) {
        do {
          /* Parse field: name ':' value */
          consume(parser, TOK_IDENT, "Expected field name");
          char *field_name = token_string(&parser->previous);
          consume(parser, TOK_COLON, "Expected ':' after field name");
          AstNode *field_val = expression(parser);

          /* Store as assign node temporarily */
          AstNode *field = ast_new_assign(
              ast_new_identifier(field_name, line, col), field_val, line, col);
          ast_array_push(&lit->as.struct_decl.fields, field);
        } while (match(parser, TOK_COMMA) && !check(parser, TOK_RBRACE));
      }
      consume(parser, TOK_RBRACE, "Expected '}' after struct literal");
      return lit;
    }

    return ast_new_identifier(name, line, col);
  }

  /* Lambda with parens: (x, y) => expr */
  if (match(parser, TOK_LPAREN)) {
    /* Could be grouped expr or lambda params */
    AstNode *first = expression(parser);

    /* Check if this is a lambda with multiple params */
    if (match(parser, TOK_COMMA)) {
      /* This is a lambda parameter list */
      AstNode *lambda = calloc(1, sizeof(AstNode));
      lambda->type = AST_LAMBDA;
      lambda->line = line;
      lambda->column = col;
      ast_array_init(&lambda->as.lambda.params);

      /* First param */
      if (first && first->type == AST_IDENTIFIER) {
        AstNode *param = calloc(1, sizeof(AstNode));
        param->type = AST_VAR_DECL;
        param->as.var_decl.name = first->as.identifier.name;
        ast_array_push(&lambda->as.lambda.params, param);
      }

      /* More params */
      do {
        consume(parser, TOK_IDENT, "Expected parameter name");
        AstNode *param = calloc(1, sizeof(AstNode));
        param->type = AST_VAR_DECL;
        param->as.var_decl.name = token_string(&parser->previous);
        ast_array_push(&lambda->as.lambda.params, param);
      } while (match(parser, TOK_COMMA));

      consume(parser, TOK_RPAREN, "Expected ')' after parameters");
      consume(parser, TOK_FATARROW, "Expected '=>' after parameters");
      lambda->as.lambda.body = expression(parser);
      return lambda;
    }

    consume(parser, TOK_RPAREN, "Expected ')' after expression");

    /* Check for single-param lambda: (x) => expr */
    if (check(parser, TOK_FATARROW) && first && first->type == AST_IDENTIFIER) {
      advance(parser); /* consume => */
      AstNode *lambda = calloc(1, sizeof(AstNode));
      lambda->type = AST_LAMBDA;
      lambda->line = line;
      lambda->column = col;
      ast_array_init(&lambda->as.lambda.params);
      AstNode *param = calloc(1, sizeof(AstNode));
      param->type = AST_VAR_DECL;
      param->as.var_decl.name = first->as.identifier.name;
      ast_array_push(&lambda->as.lambda.params, param);
      lambda->as.lambda.body = expression(parser);
      return lambda;
    }

    return first;
  }

  /* Array literal: [expr, expr, ...] with optional trailing comma */
  if (match(parser, TOK_LBRACKET)) {
    AstNode *arr = calloc(1, sizeof(AstNode));
    arr->type = AST_ARRAY_LITERAL;
    arr->line = line;
    arr->column = col;
    ast_array_init(&arr->as.array_literal.elements);

    if (!check(parser, TOK_RBRACKET)) {
      do {
        /* Allow trailing comma */
        if (check(parser, TOK_RBRACKET))
          break;
        AstNode *elem = expression(parser);
        if (elem)
          ast_array_push(&arr->as.array_literal.elements, elem);
      } while (match(parser, TOK_COMMA));
    }
    consume(parser, TOK_RBRACKET, "Expected ']' after array elements");
    return arr;
  }

  parser_error_at_current(parser, "Expected expression");
  return NULL;
}

/* Helper: parse string interpolation - converts "hello {name}" into concat */
static AstNode *parse_interpolated_string(Parser *parser, const char *str,
                                          int len, int line, int col) {
  /* Simple string without interpolation */
  bool has_interpolation = false;
  for (int i = 0; i < len; i++) {
    if (str[i] == '{') {
      has_interpolation = true;
      break;
    }
  }

  if (!has_interpolation) {
    return ast_new_string(str, len, line, col);
  }

  /* Build concatenation expression */
  AstNode *result = NULL;
  int start = 0;

  for (int i = 0; i < len; i++) {
    if (str[i] == '{') {
      /* Add literal part before { */
      if (i > start) {
        AstNode *lit = ast_new_string(str + start, i - start, line, col);
        if (!result)
          result = lit;
        else
          result = ast_new_binary(OP_ADD, result, lit, line, col);
      }

      /* Find closing } */
      int end = i + 1;
      int depth = 1;
      while (end < len && depth > 0) {
        if (str[end] == '{') depth++;
        else if (str[end] == '}') depth--;
        if (depth > 0) end++;
      }

      /* Extract expression text between { and } */
      int expr_len = end - i - 1;
      char *expr_text = malloc(expr_len + 1);
      memcpy(expr_text, str + i + 1, expr_len);
      expr_text[expr_len] = '\0';

      /* Parse the expression text as a dotted identifier chain.
       * For "p.x" → AST_MEMBER(AST_IDENTIFIER("p"), "x")
       * For "a.b.c" → AST_MEMBER(AST_MEMBER(AST_IDENTIFIER("a"), "b"), "c")
       * For "name" → AST_IDENTIFIER("name")
       */
      AstNode *expr_node = NULL;
      char *expr_copy = strdup(expr_text);
      char *saveptr = NULL;
      char *part = strtok_r(expr_copy, ".", &saveptr);
      while (part != NULL) {
        /* Trim any whitespace */
        while (*part == ' ') part++;
        char *pend = part + strlen(part) - 1;
        while (pend > part && *pend == ' ') *pend-- = '\0';

        if (expr_node == NULL) {
          expr_node = ast_new_identifier(strdup(part), line, col);
        } else {
          expr_node = ast_alloc_member(expr_node, strdup(part), line, col);
        }
        part = strtok_r(NULL, ".", &saveptr);
      }
      free(expr_copy);
      free(expr_text);

      if (!expr_node)
        expr_node = ast_new_identifier(strdup(""), line, col);

      /* Create call to str(expr) for conversion */
      AstNode *str_call =
          ast_new_call(ast_new_identifier("str", line, col), line, col);
      ast_array_push(&str_call->as.call.args, expr_node);

      if (!result)
        result = str_call;
      else
        result = ast_new_binary(OP_ADD, result, str_call, line, col);

      start = end + 1;
      i = end;
    }
  }

  /* Add remaining literal part */
  if (start < len) {
    AstNode *lit = ast_new_string(str + start, len - start, line, col);
    if (!result)
      result = lit;
    else
      result = ast_new_binary(OP_ADD, result, lit, line, col);
  }

  return result ? result : ast_new_string("", 0, line, col);
}

/* call → primary ( '(' arguments? ')' | '[' expr ']' | '.' ident | '++' | '--'
 * )* */
static AstNode *call(Parser *parser) {
  AstNode *expr = primary(parser);
  if (!expr)
    return NULL; /* Prevent segfault on parse error */

  while (true) {
    if (match(parser, TOK_LPAREN)) {
      AstNode *call_node = ast_new_call(expr, expr->line, expr->column);

      if (!check(parser, TOK_RPAREN)) {
        do {
          AstNode *arg = expression(parser);
          if (arg)
            ast_array_push(&call_node->as.call.args, arg);
        } while (match(parser, TOK_COMMA));
      }

      consume(parser, TOK_RPAREN, "Expected ')' after arguments");
      expr = call_node;
    } else if (match(parser, TOK_LBRACKET)) {
      /* Array indexing: expr[index] */
      int line = parser->previous.line;
      int col = parser->previous.column;
      AstNode *index_expr = expression(parser);
      consume(parser, TOK_RBRACKET, "Expected ']' after index");
      AstNode *index_node = calloc(1, sizeof(AstNode));
      index_node->type = AST_INDEX;
      index_node->line = line;
      index_node->column = col;
      index_node->as.index.object = expr;
      index_node->as.index.index = index_expr;
      expr = index_node;
    } else if (match(parser, TOK_DOT)) {
      consume(parser, TOK_IDENT, "Expected property name after '.'");
      /* Create member access node */
      AstNode *member = ast_alloc_member(expr, token_string(&parser->previous),
                                         expr->line, expr->column);
      if (!member)
        return expr; /* Prevent segfault */
      expr = member;
    } else if (match(parser, TOK_PLUSPLUS)) {
      /* Postfix increment: expr++ */
      int line = parser->previous.line;
      int col = parser->previous.column;
      expr = ast_new_unary(OP_INC, expr, line, col);
    } else if (match(parser, TOK_MINUSMINUS)) {
      /* Postfix decrement: expr-- */
      int line = parser->previous.line;
      int col = parser->previous.column;
      expr = ast_new_unary(OP_DEC, expr, line, col);
    } else if (match(parser, TOK_BANG)) {
      /* Error propagation: expr! - pass through for now */
      /* In full impl, would unwrap or propagate error */
      (void)parser; /* no-op, just consume the ! */
    } else {
      break;
    }
  }

  return expr;
}

/* Helper for member node - we need to add this */
static AstNode *ast_alloc_member(AstNode *obj, char *member, int line,
                                 int col) {
  AstNode *node = calloc(1, sizeof(AstNode));
  node->type = AST_MEMBER;
  node->line = line;
  node->column = col;
  node->as.member.object = obj;
  node->as.member.member = member;
  return node;
}

/* unary → ('!' | '-' | 'not') unary | call */
static AstNode *unary(Parser *parser) {
  int line = parser->current.line;
  int col = parser->current.column;

  if (match(parser, TOK_MINUS)) {
    return ast_new_unary(OP_NEG, unary(parser), line, col);
  }
  if (match(parser, TOK_BANG) || match(parser, TOK_NOT)) {
    return ast_new_unary(OP_NOT, unary(parser), line, col);
  }
  if (match(parser, TOK_TILDE)) {
    return ast_new_unary(OP_BIT_NOT, unary(parser), line, col);
  }

  return call(parser);
}

/* factor → unary (('*' | '/' | '%') unary)* */
static AstNode *factor(Parser *parser) {
  AstNode *expr = unary(parser);

  while (match(parser, TOK_STAR) || match(parser, TOK_SLASH) ||
         match(parser, TOK_PERCENT)) {
    BinaryOp op;
    switch (parser->previous.type) {
    case TOK_STAR:
      op = OP_MUL;
      break;
    case TOK_SLASH:
      op = OP_DIV;
      break;
    case TOK_PERCENT:
      op = OP_MOD;
      break;
    default:
      op = OP_MUL;
      break;
    }
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = unary(parser);
    expr = ast_new_binary(op, expr, right, line, col);
  }

  return expr;
}

/* term → factor (('+' | '-') factor)* */
static AstNode *term(Parser *parser) {
  AstNode *expr = factor(parser);

  while (match(parser, TOK_PLUS) || match(parser, TOK_MINUS)) {
    BinaryOp op = parser->previous.type == TOK_PLUS ? OP_ADD : OP_SUB;
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = factor(parser);
    expr = ast_new_binary(op, expr, right, line, col);
  }

  return expr;
}

/* comparison → term (('<' | '>' | '<=' | '>=' | 'has') term)* */
static AstNode *comparison(Parser *parser) {
  AstNode *expr = term(parser);

  while (match(parser, TOK_LT) || match(parser, TOK_GT) ||
         match(parser, TOK_LE) || match(parser, TOK_GE) ||
         match(parser, TOK_HAS)) {
    BinaryOp op;
    int line = parser->previous.line;
    int col = parser->previous.column;

    if (parser->previous.type == TOK_HAS) {
      /* has _ = boolean truthiness check (is value not none?)
       * has name = named unwrap (bind value to 'name' in if-block scope) */
      AstNode *right = NULL;
      if (check(parser, TOK_IDENT)) {
        if (parser->current.length == 1 && parser->current.start[0] == '_') {
          advance(parser); /* consume _ */
          /* right stays NULL = anonymous has check */
        } else {
          /* Named binding: has varname */
          advance(parser);
          right = ast_new_identifier(token_string(&parser->previous),
                                     parser->previous.line,
                                     parser->previous.column);
        }
      }
      expr = ast_new_binary(OP_HAS, expr, right, line, col);
      continue;
    }

    switch (parser->previous.type) {
    case TOK_LT:
      op = OP_LT;
      break;
    case TOK_GT:
      op = OP_GT;
      break;
    case TOK_LE:
      op = OP_LE;
      break;
    case TOK_GE:
      op = OP_GE;
      break;
    default:
      op = OP_LT;
      break;
    }
    AstNode *right = term(parser);
    expr = ast_new_binary(op, expr, right, line, col);
  }

  return expr;
}

/* shift → comparison (('<<' | '>>') comparison)* */
static AstNode *shift_expr(Parser *parser) {
  AstNode *expr = comparison(parser);

  while (match(parser, TOK_LSHIFT) || match(parser, TOK_RSHIFT)) {
    BinaryOp op = parser->previous.type == TOK_LSHIFT ? OP_LSHIFT : OP_RSHIFT;
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = comparison(parser);
    expr = ast_new_binary(op, expr, right, line, col);
  }

  return expr;
}

/* equality → shift (('==' | '!=') shift)* */
static AstNode *equality(Parser *parser) {
  AstNode *expr = shift_expr(parser);

  while (match(parser, TOK_EQ) || match(parser, TOK_NE)) {
    BinaryOp op = parser->previous.type == TOK_EQ ? OP_EQ : OP_NE;
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = shift_expr(parser);
    expr = ast_new_binary(op, expr, right, line, col);
  }

  return expr;
}

/* bit_and → equality ('&' equality)* */
static AstNode *bit_and(Parser *parser) {
  AstNode *expr = equality(parser);

  while (match(parser, TOK_AMP)) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = equality(parser);
    expr = ast_new_binary(OP_BIT_AND, expr, right, line, col);
  }

  return expr;
}

/* bit_xor → bit_and ('^' bit_and)* */
static AstNode *bit_xor(Parser *parser) {
  AstNode *expr = bit_and(parser);

  while (match(parser, TOK_CARET)) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = bit_and(parser);
    expr = ast_new_binary(OP_BIT_XOR, expr, right, line, col);
  }

  return expr;
}

/* bit_or → bit_xor ('|' bit_xor)* */
static AstNode *bit_or(Parser *parser) {
  AstNode *expr = bit_xor(parser);

  while (match(parser, TOK_PIPE)) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = bit_xor(parser);
    expr = ast_new_binary(OP_BIT_OR, expr, right, line, col);
  }

  return expr;
}

/* logic_and → bit_or (('&&' | 'and') bit_or)* */
static AstNode *logic_and(Parser *parser) {
  AstNode *expr = bit_or(parser);

  while (match(parser, TOK_AMPAMP) || match(parser, TOK_AND)) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = bit_or(parser);
    expr = ast_new_binary(OP_AND, expr, right, line, col);
  }

  return expr;
}

/* logic_or → logic_and (('||' | 'or') logic_and)* */
static AstNode *logic_or(Parser *parser) {
  AstNode *expr = logic_and(parser);

  while (match(parser, TOK_PIPEPIPE) || match(parser, TOK_OR)) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = logic_and(parser);
    expr = ast_new_binary(OP_OR, expr, right, line, col);
  }

  return expr;
}

/* pipeline → logic_or ('>>' logic_or)* */
static AstNode *pipeline(Parser *parser) {
  AstNode *expr = logic_or(parser);

  while (match(parser, TOK_PIPELINE)) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *right = logic_or(parser);
    expr = ast_new_binary(OP_PIPELINE, expr, right, line, col);
  }

  return expr;
}

/* expression → assignment */
static AstNode *expression(Parser *parser) { return assignment(parser); }

/* assignment → or_expr ('=' | '+=' | '-=' | '*=' | '/=' | '%=' ) assignment? */
static AstNode *assignment(Parser *parser) {
  AstNode *expr = pipeline(parser);
  if (!expr)
    return NULL;

  if (match(parser, TOK_ASSIGN)) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *value = assignment(parser); /* Right-associative */
    return ast_new_assign(expr, value, line, col);
  }

  /* Compound assignment: x += y --> x = x + y */
  BinaryOp compound_op = -1;
  if (match(parser, TOK_PLUS_ASSIGN))
    compound_op = OP_ADD;
  else if (match(parser, TOK_MINUS_ASSIGN))
    compound_op = OP_SUB;
  else if (match(parser, TOK_STAR_ASSIGN))
    compound_op = OP_MUL;
  else if (match(parser, TOK_SLASH_ASSIGN))
    compound_op = OP_DIV;
  else if (match(parser, TOK_PERCENT_ASSIGN))
    compound_op = OP_MOD;

  if (compound_op != (BinaryOp)-1) {
    int line = parser->previous.line;
    int col = parser->previous.column;
    AstNode *rhs = assignment(parser);
    AstNode *bin = ast_new_binary(compound_op, expr, rhs, line, col);
    /* Re-create target node (expr is consumed by binary, need a copy) */
    AstNode *target = calloc(1, sizeof(AstNode));
    *target = *expr; /* Shallow copy of the target identifier */
    if (expr->type == AST_IDENTIFIER)
      target->as.identifier.name = strdup(expr->as.identifier.name);
    return ast_new_assign(target, bin, line, col);
  }

  return expr;
}

/* ======== Statement Parsing ======== */

/* expression_statement → expression ';' */
static AstNode *expression_statement(Parser *parser) {
  AstNode *expr = expression(parser);
  if (!expr)
    return NULL;
  consume(parser, TOK_SEMICOLON, "Expected ';' after expression");
  return expr; /* Return the expression (assignment or otherwise) */
}

/* block → '{' declaration* '}' */
static AstNode *block(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  AstNode *blk = ast_new_block(line, col);

  while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
    AstNode *decl = declaration(parser);
    if (decl) {
      ast_array_push(&blk->as.block.statements, decl);
    }
  }

  consume(parser, TOK_RBRACE, "Expected '}' after block");
  return blk;
}

/* if_statement → 'if' expression block ('else' (if_statement | block))? */
static AstNode *if_statement(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  AstNode *condition = expression(parser);
  consume(parser, TOK_LBRACE, "Expected '{' after if condition");
  AstNode *then_branch = block(parser);

  AstNode *else_branch = NULL;
  if (match(parser, TOK_ELSE)) {
    if (match(parser, TOK_IF)) {
      else_branch = if_statement(parser);
    } else {
      consume(parser, TOK_LBRACE, "Expected '{' after else");
      else_branch = block(parser);
    }
  }

  return ast_new_if(condition, then_branch, else_branch, line, col);
}

/* while_statement → 'while' expression block */
static AstNode *while_statement(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  AstNode *condition = expression(parser);
  consume(parser, TOK_LBRACE, "Expected '{' after while condition");
  AstNode *body = block(parser);

  return ast_new_while(condition, body, line, col);
}

/* for_statement → 'for' (type? ident 'in' expr | init? ';' cond? ';' update?)
 * block */
static AstNode *for_statement(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  /* Check for for...in syntax: for TYPE IDENT in EXPR { } */
  /* Lookahead to detect 'in' keyword */
  TypeInfo *var_type = NULL;
  char *var_name = NULL;
  bool is_for_in = false;

  /* Try to detect for...in pattern */
  if (check(parser, TOK_INT) || check(parser, TOK_AUTO) ||
      check(parser, TOK_STRING) || check(parser, TOK_FLOAT) ||
      check(parser, TOK_BOOL) || check(parser, TOK_IDENT)) {
    /* Save FULL state for backtracking (lexer + parser tokens) */
    Lexer saved_lexer = *parser->lexer;
    Token saved_current = parser->current;
    Token saved_previous = parser->previous;

    /* Parse optional type */
    if (check(parser, TOK_INT) || check(parser, TOK_AUTO) ||
        check(parser, TOK_STRING) || check(parser, TOK_FLOAT) ||
        check(parser, TOK_BOOL)) {
      var_type = parse_type(parser);
    }

    /* Parse identifier */
    if (check(parser, TOK_IDENT)) {
      advance(parser);
      var_name = token_string(&parser->previous);

      /* Check for 'in' keyword */
      if (check(parser, TOK_IN)) {
        advance(parser); /* consume 'in' */
        is_for_in = true;

        /* Parse iterable expression */
        AstNode *iterable = expression(parser);

        consume(parser, TOK_LBRACE, "Expected '{' after for...in");
        AstNode *body = block(parser);

        /* Create for-in node */
        AstNode *for_node = calloc(1, sizeof(AstNode));
        for_node->type = AST_FOR;
        for_node->line = line;
        for_node->column = col;
        for_node->as.for_stmt.var_name = var_name;
        for_node->as.for_stmt.var_type = var_type;
        for_node->as.for_stmt.iterable = iterable;
        for_node->as.for_stmt.body = body;
        return for_node;
      }
    }

    /* Not for...in, restore FULL state for C-style parsing */
    if (!is_for_in) {
      *parser->lexer = saved_lexer;
      parser->current = saved_current;
      parser->previous = saved_previous;
      if (var_type)
        type_info_free(var_type);
      var_type = NULL;
      var_name = NULL;
    }
  }

  /* C-style for loop: for int i = 0; i < 10; i++ { } */
  AstNode *init = NULL;
  AstNode *condition = NULL;
  AstNode *update = NULL;

  /* Check for optional initializer (variable declaration or expression) */
  if (!check(parser, TOK_SEMICOLON)) {
    /* Could be a type name or expression */
    if (check(parser, TOK_INT) || check(parser, TOK_AUTO) ||
        check(parser, TOK_STRING) || check(parser, TOK_FLOAT) ||
        check(parser, TOK_BOOL)) {
      /* Variable declaration */
      init = declaration(parser);
    } else {
      init = expression(parser);
      consume(parser, TOK_SEMICOLON, "Expected ';' after for initializer");
    }
  } else {
    consume(parser, TOK_SEMICOLON, "Expected ';'");
  }

  /* Condition */
  if (!check(parser, TOK_SEMICOLON)) {
    condition = expression(parser);
  }
  consume(parser, TOK_SEMICOLON, "Expected ';' after for condition");

  /* Update */
  if (!check(parser, TOK_LBRACE)) {
    update = expression(parser);
  }

  consume(parser, TOK_LBRACE, "Expected '{' after for clauses");
  AstNode *body = block(parser);

  /* Desugar into while loop */
  /* { init; while (cond) { body; update; } } */
  AstNode *outer = ast_new_block(line, col);
  if (init) {
    ast_array_push(&outer->as.block.statements, init);
  }

  /* Add update to end of body */
  if (update) {
    ast_array_push(&body->as.block.statements, update);
  }

  /* Create while */
  if (!condition) {
    condition = ast_new_bool(true, line, col);
  }
  AstNode *while_node = ast_new_while(condition, body, line, col);
  ast_array_push(&outer->as.block.statements, while_node);

  return outer;
}

/* give_statement → 'give' expression? ';' */
static AstNode *give_statement(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  AstNode *value = NULL;
  if (!check(parser, TOK_SEMICOLON)) {
    value = expression(parser);
  }
  consume(parser, TOK_SEMICOLON, "Expected ';' after give value");

  return ast_new_give(value, line, col);
}

/* when/is pattern matching */
static AstNode *when_statement(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  /* Parse the value to match on */
  AstNode *value = expression(parser);
  consume(parser, TOK_LBRACE, "Expected '{' after when expression");

  /* Build when node */
  AstNode *node = calloc(1, sizeof(AstNode));
  node->type = AST_WHEN;
  node->line = line;
  node->column = col;
  node->as.when_stmt.value = value;
  node->as.when_stmt.cases.count = 0;
  node->as.when_stmt.cases.capacity = 8;
  node->as.when_stmt.cases.items = calloc(8, sizeof(AstNode *));

  /* Parse cases: is pattern { body } or else { body } */
  while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
    AstNode *pattern = NULL;

    if (match(parser, TOK_ELSE)) {
      /* 'else' is a catch-all (alias for is _) */
      pattern = ast_new_identifier("_", parser->previous.line,
                                   parser->previous.column);
    } else if (match(parser, TOK_IS)) {
      /* Check for range pattern: is > 100, is >= 50, is < 10 */
      if (check(parser, TOK_GT) || check(parser, TOK_GE) ||
          check(parser, TOK_LT) || check(parser, TOK_LE)) {
        Token op_tok = parser->current;
        advance(parser);
        AstNode *rhs = primary(parser);
        /* Create a binary node: NULL op rhs — interpreter will substitute val
         */
        AstNode *range = calloc(1, sizeof(AstNode));
        range->type = AST_BINARY_OP;
        range->line = op_tok.line;
        range->column = op_tok.column;
        range->as.binary.left = NULL; /* placeholder for when value */
        range->as.binary.right = rhs;
        if (op_tok.type == TOK_GT)
          range->as.binary.op = OP_GT;
        else if (op_tok.type == TOK_GE)
          range->as.binary.op = OP_GE;
        else if (op_tok.type == TOK_LT)
          range->as.binary.op = OP_LT;
        else
          range->as.binary.op = OP_LE;
        pattern = range;
      } else {
        /* Parse first pattern */
        pattern = expression(parser);
        /* Multi-pattern: is 1, 2, 3 — glue into AST_BLOCK */
        if (check(parser, TOK_COMMA)) {
          AstNode *multi = calloc(1, sizeof(AstNode));
          multi->type = AST_BLOCK;
          multi->line = pattern->line;
          multi->column = pattern->column;
          ast_array_init(&multi->as.block.statements);
          ast_array_push(&multi->as.block.statements, pattern);
          while (match(parser, TOK_COMMA)) {
            AstNode *p = expression(parser);
            ast_array_push(&multi->as.block.statements, p);
          }
          pattern = multi;
        }
      }
    } else {
      parser_error(parser, "Expected 'is' or 'else' in when block");
      break;
    }

    /* Parse the body block */
    consume(parser, TOK_LBRACE, "Expected '{' after is pattern");
    AstNode *body = block(parser);

    /* Store case as AST_IF node: condition=pattern, then_branch=body */
    AstNode *case_node = calloc(1, sizeof(AstNode));
    case_node->type = AST_IF;
    case_node->line = pattern->line;
    case_node->column = pattern->column;
    case_node->as.if_stmt.condition = pattern;
    case_node->as.if_stmt.then_branch = body;
    case_node->as.if_stmt.else_branch = NULL;

    /* Add to cases array */
    if (node->as.when_stmt.cases.count >= node->as.when_stmt.cases.capacity) {
      node->as.when_stmt.cases.capacity *= 2;
      node->as.when_stmt.cases.items =
          realloc(node->as.when_stmt.cases.items,
                  node->as.when_stmt.cases.capacity * sizeof(AstNode *));
    }
    node->as.when_stmt.cases.items[node->as.when_stmt.cases.count++] =
        case_node;
  }

  consume(parser, TOK_RBRACE, "Expected '}' to close when block");
  return node;
}

/* statement → if | when | while | for | give | try | block | expr_stmt */
static AstNode *statement(Parser *parser) {
  if (match(parser, TOK_IF))
    return if_statement(parser);
  if (match(parser, TOK_WHEN))
    return when_statement(parser);
  if (match(parser, TOK_WHILE))
    return while_statement(parser);
  if (match(parser, TOK_FOR))
    return for_statement(parser);
  if (match(parser, TOK_GIVE))
    return give_statement(parser);
  if (match(parser, TOK_LBRACE))
    return block(parser);
  if (match(parser, TOK_BREAK)) {
    consume(parser, TOK_SEMICOLON, "Expected ';' after break");
    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_BREAK;
    return node;
  }
  if (match(parser, TOK_CONTINUE)) {
    consume(parser, TOK_SEMICOLON, "Expected ';' after continue");
    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_CONTINUE;
    return node;
  }
  /* try/catch/fail */
  if (match(parser, TOK_TRY)) {
    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_TRY;
    node->line = parser->previous.line;
    node->column = parser->previous.column;

    /* Parse try block */
    consume(parser, TOK_LBRACE, "Expected '{' after try");
    node->as.try_stmt.try_block = block(parser);
    ast_array_init(&node->as.try_stmt.catches);

    /* Parse catch blocks: catch [TypeName] varname { ... }
     * If two identifiers before '{', first is type (ignored), second is var.
     * If one identifier before '{', it is the var name. */
    while (match(parser, TOK_CATCH)) {
      AstNode *catch_node = calloc(1, sizeof(AstNode));
      catch_node->type = AST_IF;
      catch_node->line = parser->previous.line;
      catch_node->column = parser->previous.column;

      /* First identifier */
      consume(parser, TOK_IDENT, "Expected identifier after catch");
      char *first = token_string(&parser->previous);

      char *err_name;
      if (check(parser, TOK_IDENT)) {
        /* Two identifiers: TypeName varname */
        advance(parser);
        err_name = token_string(&parser->previous);
        free(first); /* type name discarded for now */
      } else {
        /* One identifier: varname */
        err_name = first;
      }

      catch_node->as.if_stmt.condition = ast_new_identifier(
          err_name, parser->previous.line, parser->previous.column);
      free(err_name);

      /* Catch body */
      consume(parser, TOK_LBRACE, "Expected '{' after catch variable");
      catch_node->as.if_stmt.then_branch = block(parser);
      catch_node->as.if_stmt.else_branch = NULL;

      ast_array_push(&node->as.try_stmt.catches, catch_node);
    }
    return node;
  }
  if (match(parser, TOK_FAIL)) {
    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_FAIL;
    node->line = parser->previous.line;
    node->column = parser->previous.column;
    node->as.fail_stmt.error = expression(parser);
    consume(parser, TOK_SEMICOLON, "Expected ';' after fail expression");
    return node;
  }

  return expression_statement(parser);
}

/* ======== Declaration Parsing ======== */

/* Parse type annotation */
static TypeInfo *parse_type(Parser *parser) {
  char *name = NULL;

  if (match(parser, TOK_INT))
    name = strdup("int");
  else if (match(parser, TOK_UINT))
    name = strdup("uint");
  else if (match(parser, TOK_I8))
    name = strdup("i8");
  else if (match(parser, TOK_I16))
    name = strdup("i16");
  else if (match(parser, TOK_I32))
    name = strdup("i32");
  else if (match(parser, TOK_I64))
    name = strdup("i64");
  else if (match(parser, TOK_U8))
    name = strdup("u8");
  else if (match(parser, TOK_U16))
    name = strdup("u16");
  else if (match(parser, TOK_U32))
    name = strdup("u32");
  else if (match(parser, TOK_U64))
    name = strdup("u64");
  else if (match(parser, TOK_F32))
    name = strdup("f32");
  else if (match(parser, TOK_F64))
    name = strdup("f64");
  else if (match(parser, TOK_FLOAT))
    name = strdup("float");
  else if (match(parser, TOK_DOUBLE))
    name = strdup("double");
  else if (match(parser, TOK_BOOL))
    name = strdup("bool");
  else if (match(parser, TOK_CHAR))
    name = strdup("char");
  else if (match(parser, TOK_STRING))
    name = strdup("string");
  else if (match(parser, TOK_VOID))
    name = strdup("void");
  else if (match(parser, TOK_IDENT))
    name = token_string(&parser->previous);
  else
    return NULL;

  TypeInfo *info = type_info_new(name);
  free(name);

  /* Check for modifiers */
  if (match(parser, TOK_STAR)) {
    info->is_pointer = true;
  }
  if (match(parser, TOK_LBRACKET)) {
    info->is_array = true;
    if (!check(parser, TOK_RBRACKET)) {
      /* Array size - skip for now */
      expression(parser);
    }
    consume(parser, TOK_RBRACKET, "Expected ']' after array type");
  }

  return info;
}

/* Parse type annotation with optional 'maybe' prefix */
static TypeInfo *parse_maybe_type(Parser *parser) {
  bool is_maybe = match(parser, TOK_MAYBE);
  TypeInfo *info = parse_type(parser);
  if (info && is_maybe) {
    info->is_maybe = true;
  }
  return info;
}

/* var_declaration → type ident ('=' expression)? ';' */
/*                 | 'auto' ident '=' expression ';' */
static AstNode *var_declaration(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;
  bool is_auto = false;
  bool is_const = false;
  TypeInfo *type = NULL;

  if (match(parser, TOK_CONST)) {
    is_const = true;
    is_auto = true; /* const infers type like auto */
  } else if (match(parser, TOK_AUTO)) {
    is_auto = true;
  } else {
    type = parse_type(parser);
    if (!type) {
      parser_error_at_current(parser, "Expected type");
      return NULL;
    }
  }

  consume(parser, TOK_IDENT, "Expected variable name");
  char *name = token_string(&parser->previous);

  AstNode *initializer = NULL;
  if (match(parser, TOK_ASSIGN)) {
    initializer = expression(parser);
  } else if (is_auto) {
    parser_error(parser, "auto/const variables must have an initializer");
  }

  consume(parser, TOK_SEMICOLON, "Expected ';' after variable declaration");

  AstNode *node = ast_new_var_decl(name, type, initializer, is_auto, line, col);
  node->as.var_decl.is_const = is_const;
  return node;
}

/* proc_declaration → 'proc' ident '(' params? ')' ('gives' type)? ('canfail')?
 * block */
static AstNode *proc_declaration(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  consume(parser, TOK_IDENT, "Expected procedure name");
  char *name = token_string(&parser->previous);

  AstNode *proc = ast_new_proc(name, line, col);
  free(name);

  consume(parser, TOK_LPAREN, "Expected '(' after procedure name");

  /* Parse parameters */
  if (!check(parser, TOK_RPAREN)) {
    do {
      /* Special case: bare 'self' parameter (no type) */
      if (check(parser, TOK_SELF)) {
        advance(parser);
        AstNode *param =
            ast_new_var_decl("self", NULL, NULL, false, parser->previous.line,
                             parser->previous.column);
        ast_array_push(&proc->as.proc.params, param);
      } else {
        TypeInfo *param_type = parse_type(parser);
        if (!param_type) {
          parser_error_at_current(parser, "Expected parameter type");
          break;
        }

        consume(parser, TOK_IDENT, "Expected parameter name");
        char *param_name = token_string(&parser->previous);

        AstNode *param =
            ast_new_var_decl(param_name, param_type, NULL, false,
                             parser->previous.line, parser->previous.column);
        ast_array_push(&proc->as.proc.params, param);
        free(param_name);
      }

    } while (match(parser, TOK_COMMA));
  }

  consume(parser, TOK_RPAREN, "Expected ')' after parameters");

  /* Return type */
  if (match(parser, TOK_GIVES)) {
    proc->as.proc.return_type = parse_type(parser);
  }

  /* canfail modifier */
  if (match(parser, TOK_CANFAIL)) {
    proc->as.proc.is_canfail = true;
  }

  /* Body */
  consume(parser, TOK_LBRACE, "Expected '{' before procedure body");
  proc->as.proc.body = block(parser);

  return proc;
}

/* Check if current token starts a type */
static bool is_type_start(Parser *parser) {
  switch (parser->current.type) {
  case TOK_INT:
  case TOK_UINT:
  case TOK_I8:
  case TOK_I16:
  case TOK_I32:
  case TOK_I64:
  case TOK_U8:
  case TOK_U16:
  case TOK_U32:
  case TOK_U64:
  case TOK_F32:
  case TOK_F64:
  case TOK_FLOAT:
  case TOK_DOUBLE:
  case TOK_BOOL:
  case TOK_CHAR:
  case TOK_STRING:
  case TOK_VOID:
  case TOK_AUTO:
  case TOK_CONST:
  case TOK_MAYBE:
    return true;
  default:
    return false;
  }
}

/* declaration → proc_declaration | struct_declaration | var_declaration |
 * statement */
static AstNode *declaration(Parser *parser) {
  AstNode *result = NULL;

  if (match(parser, TOK_PROC)) {
    result = proc_declaration(parser);
  } else if (match(parser, TOK_STRUCT)) {
    /* struct Name { type field; type field; ... } */
    int line = parser->previous.line;
    int col = parser->previous.column;

    consume(parser, TOK_IDENT, "Expected struct name");
    char *sname = token_string(&parser->previous);

    consume(parser, TOK_LBRACE, "Expected '{' after struct name");

    /* Create AST_STRUCT node */
    AstNode *snode = calloc(1, sizeof(AstNode));
    snode->type = AST_STRUCT;
    snode->line = line;
    snode->column = col;
    snode->as.struct_decl.name = sname;
    snode->as.struct_decl.fields.count = 0;
    snode->as.struct_decl.fields.capacity = 8;
    snode->as.struct_decl.fields.items = calloc(8, sizeof(AstNode *));

    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
      /* Parse: [maybe] type name ; */
      TypeInfo *ftype = parse_maybe_type(parser);
      if (!ftype) {
        parser_error(parser, "Expected field type in struct body");
        break;
      }

      consume(parser, TOK_IDENT, "Expected field name");
      char *fname = token_string(&parser->previous);

      /* Create field as AST_VAR_DECL (no initializer) */
      AstNode *field =
          ast_new_var_decl(fname, ftype, NULL, false, parser->previous.line,
                           parser->previous.column);
      free(fname);

      /* Add to fields */
      if (snode->as.struct_decl.fields.count >=
          snode->as.struct_decl.fields.capacity) {
        snode->as.struct_decl.fields.capacity *= 2;
        snode->as.struct_decl.fields.items =
            realloc(snode->as.struct_decl.fields.items,
                    snode->as.struct_decl.fields.capacity * sizeof(AstNode *));
      }
      snode->as.struct_decl.fields.items[snode->as.struct_decl.fields.count++] =
          field;

      match(parser, TOK_SEMICOLON); /* optional semicolon */
    }
    consume(parser, TOK_RBRACE, "Expected '}' after struct body");
    result = snode;
  } else if (match(parser, TOK_ENUM)) {
    /* enum Name { Variant1, Variant2, ... } */
    int line = parser->previous.line;
    int col = parser->previous.column;
    consume(parser, TOK_IDENT, "Expected enum name");
    char *name = token_string(&parser->previous);
    consume(parser, TOK_LBRACE, "Expected '{' after enum name");

    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_ENUM;
    node->line = line;
    node->column = col;
    node->as.enum_decl.name = name;
    node->as.enum_decl.variants.count = 0;
    node->as.enum_decl.variants.capacity = 8;
    node->as.enum_decl.variants.items = calloc(8, sizeof(AstNode *));

    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
      consume(parser, TOK_IDENT, "Expected variant name");
      AstNode *variant = calloc(1, sizeof(AstNode));
      variant->type = AST_IDENTIFIER;
      variant->line = parser->previous.line;
      variant->column = parser->previous.column;
      variant->as.identifier.name = token_string(&parser->previous);

      if (node->as.enum_decl.variants.count >=
          node->as.enum_decl.variants.capacity) {
        node->as.enum_decl.variants.capacity *= 2;
        node->as.enum_decl.variants.items =
            realloc(node->as.enum_decl.variants.items,
                    node->as.enum_decl.variants.capacity * sizeof(AstNode *));
      }
      node->as.enum_decl.variants.items[node->as.enum_decl.variants.count++] =
          variant;

      if (!match(parser, TOK_COMMA))
        break; /* Allow trailing comma */
    }
    consume(parser, TOK_RBRACE, "Expected '}' after enum variants");
    result = node;
  } else if (match(parser, TOK_EXTEND)) {
    /* extend TypeName { proc method(...) { } ... } */
    int line = parser->previous.line;
    int col = parser->previous.column;
    consume(parser, TOK_IDENT, "Expected type name after extend");
    char *type_name = token_string(&parser->previous);
    consume(parser, TOK_LBRACE, "Expected '{' after extend type name");

    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_EXTEND;
    node->line = line;
    node->column = col;
    node->as.extend_decl.type_name = type_name;
    node->as.extend_decl.methods.count = 0;
    node->as.extend_decl.methods.capacity = 8;
    node->as.extend_decl.methods.items = calloc(8, sizeof(AstNode *));

    while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
      consume(parser, TOK_PROC, "Expected 'proc' in extend block");
      AstNode *method = proc_declaration(parser);
      if (node->as.extend_decl.methods.count >=
          node->as.extend_decl.methods.capacity) {
        node->as.extend_decl.methods.capacity *= 2;
        node->as.extend_decl.methods.items =
            realloc(node->as.extend_decl.methods.items,
                    node->as.extend_decl.methods.capacity * sizeof(AstNode *));
      }
      node->as.extend_decl.methods.items[node->as.extend_decl.methods.count++] =
          method;
    }
    consume(parser, TOK_RBRACE, "Expected '}' after extend body");
    result = node;
  } else if (match(parser, TOK_MODULE)) {
    /* module Name; — stub: skip module name and semicolon */
    consume(parser, TOK_IDENT, "Expected module name");
    match(parser, TOK_SEMICOLON);
    result = ast_new_none(parser->previous.line, parser->previous.column);
  } else if (match(parser, TOK_IMPORT)) {
    /* import Name; — stub: skip import path and semicolon */
    consume(parser, TOK_IDENT, "Expected import name");
    /* Handle dotted imports like import std.io */
    while (match(parser, TOK_DOT)) {
      consume(parser, TOK_IDENT, "Expected identifier after '.'");
    }
    match(parser, TOK_SEMICOLON);
    result = ast_new_none(parser->previous.line, parser->previous.column);
  } else if (match(parser, TOK_EXPORT)) {
    /* export — prefix modifier, parse the next declaration */
    result = declaration(parser);
  } else if (match(parser, TOK_STATIC)) {
    /* static — prefix modifier, parse the next declaration
     * Marks variable/function as module-scoped (not exported) */
    result = declaration(parser);
  } else if (is_type_start(parser)) {
    result = var_declaration(parser);
  } else {
    result = statement(parser);
  }

  if (parser->panic_mode)
    synchronize(parser);
  return result;
}

/* ======== Pragma Parsing ======== */

static AstNode *parse_pragma(Parser *parser) {
  /* #pragma consumed, now expect name:value */
  int line = parser->previous.line;
  int col = parser->previous.column;

  consume(parser, TOK_IDENT, "Expected pragma name");
  char *name = token_string(&parser->previous);

  char *value = NULL;
  if (match(parser, TOK_COLON)) {
    consume(parser, TOK_IDENT, "Expected pragma value");
    value = token_string(&parser->previous);
  }

  AstNode *pragma = calloc(1, sizeof(AstNode));
  pragma->type = AST_PRAGMA;
  pragma->line = line;
  pragma->column = col;
  pragma->as.pragma.name = name;
  pragma->as.pragma.value = value;

  return pragma;
}

/* ======== Main Parse Function ======== */

void parser_init(Parser *parser, Lexer *lexer) {
  parser->lexer = lexer;
  parser->had_error = false;
  parser->panic_mode = false;
  parser->error_message[0] = '\0';

  /* Prime the parser with first token */
  advance(parser);
}

AstNode *parser_parse(Parser *parser) {
  AstNode *program = ast_new_program();

  while (!check(parser, TOK_EOF)) {
    if (match(parser, TOK_PRAGMA)) {
      AstNode *pragma = parse_pragma(parser);
      if (pragma) {
        ast_array_push(&program->as.program.pragmas, pragma);
      }
    } else {
      AstNode *decl = declaration(parser);
      if (decl) {
        ast_array_push(&program->as.program.declarations, decl);
      }
    }
  }

  return program;
}

AstNode *parser_parse_expression(Parser *parser) { return expression(parser); }

AstNode *parser_parse_statement(Parser *parser) { return statement(parser); }

AstNode *parser_parse_declaration(Parser *parser) {
  return declaration(parser);
}
