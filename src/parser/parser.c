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

static bool token_is_word(Token *token, const char *word) {
  size_t len = strlen(word);
  return token && token->length == (int)len &&
         strncmp(token->start, word, len) == 0;
}

static bool parser_python_style(Parser *parser) {
  return parser->current_style == PARSER_STYLE_PYTHON;
}

static bool parser_expression_style(Parser *parser) {
  return parser->current_style == PARSER_STYLE_EXPRESSION;
}

static bool expression_style_inline_boundary(Parser *parser) {
  return check(parser, TOK_ELSE) || check(parser, TOK_CATCH) ||
         check(parser, TOK_IS);
}

static void skip_newlines(Parser *parser) {
  while (match(parser, TOK_NEWLINE)) {
  }
}

static void consume_statement_terminator(Parser *parser,
                                         const char *message) {
  if (match(parser, TOK_SEMICOLON))
    return;
  if (parser_python_style(parser) || parser_expression_style(parser)) {
    if (match(parser, TOK_NEWLINE))
      return;
    if (check(parser, TOK_EOF))
      return;
    if (parser_python_style(parser) && check(parser, TOK_DEDENT))
      return;
    if (parser_expression_style(parser) &&
        expression_style_inline_boundary(parser))
      return;
  }
  parser_error_at_current(parser, message);
}

static ParserStyle parse_style_directive(const char *value) {
  if (!value)
    return PARSER_STYLE_DEFAULT;
  while (*value == ' ' || *value == '\t')
    value++;
  if (strcasecmp(value, "brace") == 0)
    return PARSER_STYLE_BRACE;
  if (strcasecmp(value, "pipeline") == 0)
    return PARSER_STYLE_PIPELINE;
  if (strcasecmp(value, "expression") == 0 ||
      strcasecmp(value, "functional") == 0)
    return PARSER_STYLE_EXPRESSION;
  if (strcasecmp(value, "python") == 0)
    return PARSER_STYLE_PYTHON;
  return PARSER_STYLE_DEFAULT;
}

static void append_text(char **buffer, size_t *length, size_t *capacity,
                        const char *text, size_t text_len) {
  if (!buffer || !length || !capacity || !text || text_len == 0)
    return;

  if (*capacity <= *length + text_len + 1) {
    size_t new_capacity = *capacity == 0 ? 64 : *capacity;
    while (new_capacity <= *length + text_len + 1) {
      new_capacity *= 2;
    }
    *buffer = realloc(*buffer, new_capacity);
    *capacity = new_capacity;
  }

  memcpy(*buffer + *length, text, text_len);
  *length += text_len;
  (*buffer)[*length] = '\0';
}

static bool is_syntax_profile_key_token(Token *token) {
  return token && token->type == TOK_IDENT &&
         (token_is_word(token, "pipelines") || token_is_word(token, "pipeline") ||
          token_is_word(token, "functional") ||
          token_is_word(token, "imperative") ||
          token_is_word(token, "declarative"));
}

static bool is_syntax_profile_value_token(TokenType type) {
  switch (type) {
  case TOK_IDENT:
  case TOK_INT_LIT:
  case TOK_FLOAT_LIT:
  case TOK_PERCENT:
  case TOK_TRUE:
  case TOK_FALSE:
  case TOK_AUTO:
  case TOK_PLUS:
  case TOK_MINUS:
  case TOK_DOT:
    return true;
  default:
    return false;
  }
}

static char *parse_syntax_profile_value(Parser *parser) {
  char *value = NULL;
  size_t length = 0;
  size_t capacity = 0;
  bool saw_entry = false;

  while (is_syntax_profile_key_token(&parser->current)) {
    int value_line;
    bool saw_value = false;

    if (saw_entry) {
      append_text(&value, &length, &capacity, ", ", 2);
    }
    saw_entry = true;

    append_text(&value, &length, &capacity, parser->current.start,
                (size_t)parser->current.length);
    advance(parser);

    if (!match(parser, TOK_COLON)) {
      parser_error_at_current(parser,
                              "Expected ':' after syntax_profile key");
      break;
    }
    append_text(&value, &length, &capacity, ":", 1);

    value_line = parser->current.line;

    while (!check(parser, TOK_EOF)) {
      Token next = lexer_peek(parser->lexer);
      bool next_entry =
          parser->current.line > value_line &&
          is_syntax_profile_key_token(&parser->current) &&
          next.type == TOK_COLON;

      if (next_entry || check(parser, TOK_COMMA) ||
          !is_syntax_profile_value_token(parser->current.type)) {
        break;
      }

      append_text(&value, &length, &capacity, parser->current.start,
                  (size_t)parser->current.length);
      saw_value = true;
      advance(parser);
    }

    if (!saw_value) {
      parser_error_at_current(parser, "Expected syntax_profile value");
      break;
    }

    match(parser, TOK_COMMA);
  }

  if (!saw_entry) {
    free(value);
    return NULL;
  }

  return value;
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
static AstNode *python_block(Parser *parser, int line, int col);
static AstNode *parse_statement_block(Parser *parser, const char *message);
static AstNode *try_statement(Parser *parser);
static AstNode *struct_declaration(Parser *parser);
static AstNode *enum_declaration(Parser *parser);
static AstNode *extend_declaration(Parser *parser);
static AstNode *expression_inline_block(Parser *parser, int line, int col);

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
    if (!parser->disallow_inline_lambda && check(parser, TOK_FATARROW)) {
      advance(parser); /* consume => */
      AstNode *body;
      if (match(parser, TOK_LBRACE))
        body = block(parser);
      else
        body = expression(parser);
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
      if (match(parser, TOK_LBRACE))
        lambda->as.lambda.body = block(parser);
      else
        lambda->as.lambda.body = expression(parser);
      return lambda;
    }

    consume(parser, TOK_RPAREN, "Expected ')' after expression");

    /* Check for single-param lambda: (x) => expr */
    if (!parser->disallow_inline_lambda && check(parser, TOK_FATARROW) &&
        first && first->type == AST_IDENTIFIER) {
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
      if (match(parser, TOK_LBRACE))
        lambda->as.lambda.body = block(parser);
      else
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

/* pipeline → logic_or (('>>' | '|>') logic_or)* */
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
  consume_statement_terminator(parser, "Expected statement terminator after expression");
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

static AstNode *python_block(Parser *parser, int line, int col) {
  AstNode *blk = ast_new_block(line, col);

  consume(parser, TOK_NEWLINE, "Expected newline after ':'");
  consume(parser, TOK_INDENT, "Expected indented block");

  skip_newlines(parser);
  while (!check(parser, TOK_DEDENT) && !check(parser, TOK_EOF)) {
    AstNode *decl = declaration(parser);
    if (decl) {
      ast_array_push(&blk->as.block.statements, decl);
    }
    skip_newlines(parser);
  }

  consume(parser, TOK_DEDENT, "Expected end of indented block");
  return blk;
}

static AstNode *expression_inline_block(Parser *parser, int line, int col) {
  AstNode *blk = ast_new_block(line, col);
  AstNode *stmt = declaration(parser);
  if (stmt) {
    ast_array_push(&blk->as.block.statements, stmt);
  }
  return blk;
}

static AstNode *parse_statement_block(Parser *parser, const char *message) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  if (parser_python_style(parser)) {
    consume(parser, TOK_COLON, message);
    return python_block(parser, line, col);
  }

  if (parser_expression_style(parser) &&
      (match(parser, TOK_FATARROW) || match(parser, TOK_ASSIGN))) {
    return expression_inline_block(parser, line, col);
  }

  consume(parser, TOK_LBRACE, message);
  return block(parser);
}

/* if_statement → 'if' expression block ('else' (if_statement | block))? */
static AstNode *if_statement(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  AstNode *condition = expression(parser);
  AstNode *then_branch = parse_statement_block(
      parser, parser_python_style(parser) ? "Expected ':' after if condition"
                                          : "Expected '{' after if condition");

  AstNode *else_branch = NULL;
  skip_newlines(parser);
  if (match(parser, TOK_ELSE)) {
    if (match(parser, TOK_IF)) {
      else_branch = if_statement(parser);
    } else {
      else_branch = parse_statement_block(
          parser, parser_python_style(parser) ? "Expected ':' after else"
                                              : "Expected '{' after else");
    }
  }

  return ast_new_if(condition, then_branch, else_branch, line, col);
}

/* while_statement → 'while' expression block */
static AstNode *while_statement(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  AstNode *condition = expression(parser);
  AstNode *body = parse_statement_block(
      parser, parser_python_style(parser)
                  ? "Expected ':' after while condition"
                  : "Expected '{' after while condition");

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
        AstNode *body = parse_statement_block(
            parser, parser_python_style(parser)
                        ? "Expected ':' after for...in"
                        : "Expected '{' after for...in");

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
  if (!(check(parser, TOK_LBRACE) ||
        (parser_python_style(parser) && check(parser, TOK_COLON)))) {
    update = expression(parser);
  }

  AstNode *body = parse_statement_block(
      parser, parser_python_style(parser)
                  ? "Expected ':' after for clauses"
                  : "Expected '{' after for clauses");

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
  if (!(check(parser, TOK_SEMICOLON) ||
        ((parser_python_style(parser) || parser_expression_style(parser)) &&
         (check(parser, TOK_NEWLINE) || check(parser, TOK_DEDENT) ||
          check(parser, TOK_EOF) ||
          (parser_expression_style(parser) &&
           expression_style_inline_boundary(parser)) ||
          check(parser, TOK_ELSE))))) {
    value = expression(parser);
  }
  consume_statement_terminator(parser, "Expected statement terminator after give value");

  return ast_new_give(value, line, col);
}

/* when/is pattern matching */
static AstNode *when_statement(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;

  /* Parse the value to match on */
  AstNode *value = expression(parser);

  /* Build when node */
  AstNode *node = calloc(1, sizeof(AstNode));
  node->type = AST_WHEN;
  node->line = line;
  node->column = col;
  node->as.when_stmt.value = value;
  node->as.when_stmt.cases.count = 0;
  node->as.when_stmt.cases.capacity = 8;
  node->as.when_stmt.cases.items = calloc(8, sizeof(AstNode *));

  if (parser_python_style(parser)) {
    consume(parser, TOK_COLON, "Expected ':' after when expression");
    consume(parser, TOK_NEWLINE, "Expected newline after ':'");
    consume(parser, TOK_INDENT, "Expected indented when block");
  } else if (!parser_expression_style(parser)) {
    consume(parser, TOK_LBRACE, "Expected '{' after when expression");
  } else {
    skip_newlines(parser);
  }

  /* Parse cases: is pattern { body } or else { body } */
  while (!check(parser, TOK_EOF)) {
    AstNode *pattern = NULL;
    int case_line;
    int case_col;
    AstNode *body;

    skip_newlines(parser);
    if ((parser_python_style(parser) && check(parser, TOK_DEDENT)) ||
        (!parser_expression_style(parser) && check(parser, TOK_RBRACE)) ||
        (parser_expression_style(parser) &&
         !(check(parser, TOK_IS) || check(parser, TOK_ELSE))) ||
        check(parser, TOK_EOF)) {
      break;
    }

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
        if (parser_expression_style(parser)) {
          bool saved_disallow_inline_lambda = parser->disallow_inline_lambda;
          parser->disallow_inline_lambda = true;
          pattern = expression(parser);
          parser->disallow_inline_lambda = saved_disallow_inline_lambda;
        } else {
          pattern = expression(parser);
        }
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

    case_line = pattern->line;
    case_col = pattern->column;
    body = parse_statement_block(
        parser, parser_python_style(parser)
                    ? "Expected ':' after when case"
                    : "Expected '{' after is pattern");

    /* Store case as AST_IF node: condition=pattern, then_branch=body */
    AstNode *case_node = calloc(1, sizeof(AstNode));
    case_node->type = AST_IF;
    case_node->line = case_line;
    case_node->column = case_col;
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

  if (parser_python_style(parser)) {
    consume(parser, TOK_DEDENT, "Expected end of when block");
  } else if (!parser_expression_style(parser)) {
    consume(parser, TOK_RBRACE, "Expected '}' to close when block");
  }
  return node;
}

static AstNode *try_statement(Parser *parser) {
  AstNode *node = calloc(1, sizeof(AstNode));
  node->type = AST_TRY;
  node->line = parser->previous.line;
  node->column = parser->previous.column;

  node->as.try_stmt.try_block = parse_statement_block(
      parser, parser_python_style(parser) ? "Expected ':' after try"
                                          : "Expected '{' after try");
  ast_array_init(&node->as.try_stmt.catches);

  skip_newlines(parser);
  while (match(parser, TOK_CATCH)) {
    AstNode *catch_node = calloc(1, sizeof(AstNode));
    char *err_name = NULL;

    catch_node->type = AST_IF;
    catch_node->line = parser->previous.line;
    catch_node->column = parser->previous.column;

    if (check(parser, TOK_IDENT)) {
      advance(parser);
      err_name = token_string(&parser->previous);

      if (check(parser, TOK_IDENT)) {
        char *type_name = err_name;
        advance(parser);
        err_name = token_string(&parser->previous);
        free(type_name);
      }
    } else {
      err_name = strdup("_error");
    }

    catch_node->as.if_stmt.condition = ast_new_identifier(
        err_name, parser->previous.line, parser->previous.column);
    free(err_name);

    catch_node->as.if_stmt.then_branch = parse_statement_block(
        parser, parser_python_style(parser)
                    ? "Expected ':' after catch"
                    : "Expected '{' after catch variable");
    catch_node->as.if_stmt.else_branch = NULL;

    ast_array_push(&node->as.try_stmt.catches, catch_node);
    skip_newlines(parser);
  }

  return node;
}

/* statement → if | when | while | for | give | try | block | expr_stmt */
static AstNode *statement(Parser *parser) {
  skip_newlines(parser);
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
    consume_statement_terminator(parser,
                                 "Expected statement terminator after break");
    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_BREAK;
    return node;
  }
  if (match(parser, TOK_CONTINUE)) {
    consume_statement_terminator(
        parser, "Expected statement terminator after continue");
    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_CONTINUE;
    return node;
  }
  /* try/catch/fail */
  if (match(parser, TOK_TRY))
    return try_statement(parser);
  if (match(parser, TOK_FAIL)) {
    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_FAIL;
    node->line = parser->previous.line;
    node->column = parser->previous.column;
    node->as.fail_stmt.error = expression(parser);
    consume_statement_terminator(
        parser, "Expected statement terminator after fail expression");
    return node;
  }

  return expression_statement(parser);
}

/* ======== Declaration Parsing ======== */

/* Parse type annotation */
static TypeInfo *parse_type(Parser *parser) {
  char *name = NULL;

  if (match(parser, TOK_PROC)) {
    char buffer[1024];
    int offset = 0;
    TypeInfo *param_types[32] = {0};
    int param_count = 0;
    TypeInfo *return_type = NULL;

    consume(parser, TOK_LPAREN, "Expected '(' after 'proc' in type");
    if (!check(parser, TOK_RPAREN)) {
      do {
        if (param_count >= 32) {
          parser_error(parser, "Too many callable type parameters");
          break;
        }
        param_types[param_count] = parse_type(parser);
        if (!param_types[param_count]) {
          parser_error_at_current(parser,
                                  "Expected parameter type in callable type");
          break;
        }
        param_count++;
      } while (match(parser, TOK_COMMA));
    }
    consume(parser, TOK_RPAREN, "Expected ')' after callable type parameters");
    consume(parser, TOK_ARROW, "Expected '->' after callable type parameters");
    return_type = parse_type(parser);
    if (!return_type) {
      parser_error_at_current(parser, "Expected return type after '->'");
    }

    offset += snprintf(buffer + offset, sizeof(buffer) - (size_t)offset,
                       "proc(");
    for (int i = 0; i < param_count; i++) {
      offset += snprintf(buffer + offset, sizeof(buffer) - (size_t)offset,
                         "%s%s", i == 0 ? "" : ", ",
                         param_types[i] ? param_types[i]->name : "any");
    }
    snprintf(buffer + offset, sizeof(buffer) - (size_t)offset, ")->%s",
             return_type ? return_type->name : "any");
    name = strdup(buffer);

    for (int i = 0; i < param_count; i++) {
      type_info_free(param_types[i]);
    }
    type_info_free(return_type);
  } else if (check(parser, TOK_IDENT) &&
             token_is_word(&parser->current, "stream")) {
    TypeInfo *element_type = NULL;
    char buffer[512];

    advance(parser);
    consume(parser, TOK_LPAREN, "Expected '(' after 'stream' in type");
    element_type = parse_type(parser);
    if (!element_type) {
      parser_error_at_current(parser, "Expected element type in stream type");
      return NULL;
    }
    consume(parser, TOK_RPAREN, "Expected ')' after stream element type");
    snprintf(buffer, sizeof(buffer), "stream(%s)", element_type->name);
    name = strdup(buffer);
    type_info_free(element_type);
  } else if (match(parser, TOK_INT))
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
    size_t len;
    info->is_array = true;
    if (!check(parser, TOK_RBRACKET)) {
      /* Array size - skip for now */
      expression(parser);
    }
    consume(parser, TOK_RBRACKET, "Expected ']' after array type");
    len = strlen(info->name);
    info->name = realloc(info->name, len + 3);
    memcpy(info->name + len, "[]", 3);
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

  consume_statement_terminator(
      parser, "Expected statement terminator after variable declaration");

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
  if (parser->current_style == PARSER_STYLE_EXPRESSION &&
      (match(parser, TOK_FATARROW) || match(parser, TOK_ASSIGN))) {
    AstNode *body_expr = expression(parser);
    AstNode *body_block = ast_new_block(line, col);
    consume_statement_terminator(
        parser, "Expected statement terminator after expression-style procedure body");
    ast_array_push(&body_block->as.block.statements,
                   ast_new_give(body_expr, line, col));
    proc->as.proc.body = body_block;
  } else if (parser_python_style(parser)) {
    consume(parser, TOK_COLON, "Expected ':' before procedure body");
    proc->as.proc.body = python_block(parser, line, col);
  } else {
    consume(parser, TOK_LBRACE, "Expected '{' before procedure body");
    proc->as.proc.body = block(parser);
  }

  return proc;
}

static AstNode *struct_declaration(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;
  AstNode *snode;

  consume(parser, TOK_IDENT, "Expected struct name");
  char *sname = token_string(&parser->previous);

  snode = calloc(1, sizeof(AstNode));
  snode->type = AST_STRUCT;
  snode->line = line;
  snode->column = col;
  snode->as.struct_decl.name = sname;
  snode->as.struct_decl.fields.count = 0;
  snode->as.struct_decl.fields.capacity = 8;
  snode->as.struct_decl.fields.items = calloc(8, sizeof(AstNode *));

  if (parser_expression_style(parser) &&
      (match(parser, TOK_ASSIGN) || match(parser, TOK_FATARROW))) {
    do {
      TypeInfo *ftype = parse_maybe_type(parser);
      AstNode *field;
      char *fname;

      if (!ftype) {
        parser_error(parser, "Expected field type in struct declaration");
        break;
      }

      consume(parser, TOK_IDENT, "Expected field name");
      fname = token_string(&parser->previous);
      field = ast_new_var_decl(fname, ftype, NULL, false,
                               parser->previous.line, parser->previous.column);
      free(fname);

      if (snode->as.struct_decl.fields.count >=
          snode->as.struct_decl.fields.capacity) {
        snode->as.struct_decl.fields.capacity *= 2;
        snode->as.struct_decl.fields.items =
            realloc(snode->as.struct_decl.fields.items,
                    snode->as.struct_decl.fields.capacity * sizeof(AstNode *));
      }
      snode->as.struct_decl.fields.items[snode->as.struct_decl.fields.count++] =
          field;
    } while (match(parser, TOK_COMMA));

    consume_statement_terminator(
        parser, "Expected statement terminator after expression-style struct declaration");
    return snode;
  }

  if (parser_python_style(parser)) {
    consume(parser, TOK_COLON, "Expected ':' after struct name");
    consume(parser, TOK_NEWLINE, "Expected newline after ':'");
    consume(parser, TOK_INDENT, "Expected indented struct body");

    skip_newlines(parser);
    while (!check(parser, TOK_DEDENT) && !check(parser, TOK_EOF)) {
      TypeInfo *ftype = parse_maybe_type(parser);
      AstNode *field;
      char *fname;

      if (!ftype) {
        parser_error(parser, "Expected field type in struct body");
        break;
      }

      consume(parser, TOK_IDENT, "Expected field name");
      fname = token_string(&parser->previous);
      field = ast_new_var_decl(fname, ftype, NULL, false,
                               parser->previous.line, parser->previous.column);
      free(fname);

      if (snode->as.struct_decl.fields.count >=
          snode->as.struct_decl.fields.capacity) {
        snode->as.struct_decl.fields.capacity *= 2;
        snode->as.struct_decl.fields.items =
            realloc(snode->as.struct_decl.fields.items,
                    snode->as.struct_decl.fields.capacity * sizeof(AstNode *));
      }
      snode->as.struct_decl.fields.items[snode->as.struct_decl.fields.count++] =
          field;

      consume_statement_terminator(
          parser, "Expected statement terminator after struct field");
      skip_newlines(parser);
    }

    consume(parser, TOK_DEDENT, "Expected end of struct body");
    return snode;
  }

  consume(parser, TOK_LBRACE, "Expected '{' after struct name");

  while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
    TypeInfo *ftype = parse_maybe_type(parser);
    AstNode *field;
    char *fname;

    if (!ftype) {
      parser_error(parser, "Expected field type in struct body");
      break;
    }

    consume(parser, TOK_IDENT, "Expected field name");
    fname = token_string(&parser->previous);
    field = ast_new_var_decl(fname, ftype, NULL, false,
                             parser->previous.line, parser->previous.column);
    free(fname);

    if (snode->as.struct_decl.fields.count >= snode->as.struct_decl.fields.capacity) {
      snode->as.struct_decl.fields.capacity *= 2;
      snode->as.struct_decl.fields.items =
          realloc(snode->as.struct_decl.fields.items,
                  snode->as.struct_decl.fields.capacity * sizeof(AstNode *));
    }
    snode->as.struct_decl.fields.items[snode->as.struct_decl.fields.count++] =
        field;

    match(parser, TOK_SEMICOLON);
  }

  consume(parser, TOK_RBRACE, "Expected '}' after struct body");
  return snode;
}

static AstNode *enum_declaration(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;
  AstNode *node;

  consume(parser, TOK_IDENT, "Expected enum name");
  char *name = token_string(&parser->previous);

  node = calloc(1, sizeof(AstNode));
  node->type = AST_ENUM;
  node->line = line;
  node->column = col;
  node->as.enum_decl.name = name;
  node->as.enum_decl.variants.count = 0;
  node->as.enum_decl.variants.capacity = 8;
  node->as.enum_decl.variants.items = calloc(8, sizeof(AstNode *));

  if (parser_expression_style(parser) &&
      (match(parser, TOK_ASSIGN) || match(parser, TOK_FATARROW))) {
    do {
      AstNode *variant;
      consume(parser, TOK_IDENT, "Expected variant name");
      variant = calloc(1, sizeof(AstNode));
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
    } while (match(parser, TOK_COMMA));

    consume_statement_terminator(
        parser, "Expected statement terminator after expression-style enum declaration");
    return node;
  }

  if (parser_python_style(parser)) {
    consume(parser, TOK_COLON, "Expected ':' after enum name");
    consume(parser, TOK_NEWLINE, "Expected newline after ':'");
    consume(parser, TOK_INDENT, "Expected indented enum body");

    skip_newlines(parser);
    while (!check(parser, TOK_DEDENT) && !check(parser, TOK_EOF)) {
      AstNode *variant;
      consume(parser, TOK_IDENT, "Expected variant name");
      variant = calloc(1, sizeof(AstNode));
      variant->type = AST_IDENTIFIER;
      variant->line = parser->previous.line;
      variant->column = parser->previous.column;
      variant->as.identifier.name = token_string(&parser->previous);

      if (node->as.enum_decl.variants.count >= node->as.enum_decl.variants.capacity) {
        node->as.enum_decl.variants.capacity *= 2;
        node->as.enum_decl.variants.items =
            realloc(node->as.enum_decl.variants.items,
                    node->as.enum_decl.variants.capacity * sizeof(AstNode *));
      }
      node->as.enum_decl.variants.items[node->as.enum_decl.variants.count++] =
          variant;

      consume_statement_terminator(
          parser, "Expected statement terminator after enum variant");
      skip_newlines(parser);
    }

    consume(parser, TOK_DEDENT, "Expected end of enum body");
    return node;
  }

  consume(parser, TOK_LBRACE, "Expected '{' after enum name");
  while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
    AstNode *variant;
    consume(parser, TOK_IDENT, "Expected variant name");
    variant = calloc(1, sizeof(AstNode));
    variant->type = AST_IDENTIFIER;
    variant->line = parser->previous.line;
    variant->column = parser->previous.column;
    variant->as.identifier.name = token_string(&parser->previous);

    if (node->as.enum_decl.variants.count >= node->as.enum_decl.variants.capacity) {
      node->as.enum_decl.variants.capacity *= 2;
      node->as.enum_decl.variants.items =
          realloc(node->as.enum_decl.variants.items,
                  node->as.enum_decl.variants.capacity * sizeof(AstNode *));
    }
    node->as.enum_decl.variants.items[node->as.enum_decl.variants.count++] =
        variant;

    if (!match(parser, TOK_COMMA))
      break;
  }

  consume(parser, TOK_RBRACE, "Expected '}' after enum variants");
  return node;
}

static AstNode *extend_declaration(Parser *parser) {
  int line = parser->previous.line;
  int col = parser->previous.column;
  AstNode *node;

  consume(parser, TOK_IDENT, "Expected type name after extend");
  char *type_name = token_string(&parser->previous);

  node = calloc(1, sizeof(AstNode));
  node->type = AST_EXTEND;
  node->line = line;
  node->column = col;
  node->as.extend_decl.type_name = type_name;
  node->as.extend_decl.methods.count = 0;
  node->as.extend_decl.methods.capacity = 8;
  node->as.extend_decl.methods.items = calloc(8, sizeof(AstNode *));

  if (parser_expression_style(parser) &&
      (match(parser, TOK_ASSIGN) || match(parser, TOK_FATARROW))) {
    if (match(parser, TOK_NEWLINE)) {
      while (!check(parser, TOK_EOF)) {
        AstNode *method;
        consume(parser, TOK_PROC,
                "Expected 'proc' in expression-style extend body");
        method = proc_declaration(parser);
        node->as.extend_decl.methods.items[node->as.extend_decl.methods.count++] =
            method;

        if (check(parser, TOK_PROC)) {
          continue;
        }
        break;
      }
    } else {
      AstNode *method;
      consume(parser, TOK_PROC,
              "Expected 'proc' after expression-style extend declaration");
      method = proc_declaration(parser);
      node->as.extend_decl.methods.items[node->as.extend_decl.methods.count++] =
          method;
    }
    return node;
  }

  if (parser_python_style(parser)) {
    consume(parser, TOK_COLON, "Expected ':' after extend type name");
    consume(parser, TOK_NEWLINE, "Expected newline after ':'");
    consume(parser, TOK_INDENT, "Expected indented extend body");

    skip_newlines(parser);
    while (!check(parser, TOK_DEDENT) && !check(parser, TOK_EOF)) {
      AstNode *method;
      consume(parser, TOK_PROC, "Expected 'proc' in extend block");
      method = proc_declaration(parser);
      if (node->as.extend_decl.methods.count >=
          node->as.extend_decl.methods.capacity) {
        node->as.extend_decl.methods.capacity *= 2;
        node->as.extend_decl.methods.items =
            realloc(node->as.extend_decl.methods.items,
                    node->as.extend_decl.methods.capacity * sizeof(AstNode *));
      }
      node->as.extend_decl.methods.items[node->as.extend_decl.methods.count++] =
          method;
      skip_newlines(parser);
    }

    consume(parser, TOK_DEDENT, "Expected end of extend body");
    return node;
  }

  consume(parser, TOK_LBRACE, "Expected '{' after extend type name");
  skip_newlines(parser);
  while (!check(parser, TOK_RBRACE) && !check(parser, TOK_EOF)) {
    AstNode *method;
    consume(parser, TOK_PROC, "Expected 'proc' in extend block");
    method = proc_declaration(parser);
    if (node->as.extend_decl.methods.count >= node->as.extend_decl.methods.capacity) {
      node->as.extend_decl.methods.capacity *= 2;
      node->as.extend_decl.methods.items =
          realloc(node->as.extend_decl.methods.items,
                  node->as.extend_decl.methods.capacity * sizeof(AstNode *));
    }
    node->as.extend_decl.methods.items[node->as.extend_decl.methods.count++] =
        method;
    skip_newlines(parser);
  }

  consume(parser, TOK_RBRACE, "Expected '}' after extend body");
  return node;
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
  case TOK_PROC:
  case TOK_AUTO:
  case TOK_CONST:
  case TOK_MAYBE:
    return true;
  default:
    return check(parser, TOK_IDENT) && token_is_word(&parser->current, "stream");
  }
}

/* declaration → proc_declaration | struct_declaration | var_declaration |
 * statement */
static AstNode *declaration(Parser *parser) {
  AstNode *result = NULL;

  skip_newlines(parser);
  if (check(parser, TOK_DEDENT)) {
    return NULL;
  }

  if (check(parser, TOK_PROC)) {
    Token next = lexer_peek(parser->lexer);
    if (next.type == TOK_IDENT) {
      advance(parser);
      result = proc_declaration(parser);
    } else {
      result = var_declaration(parser);
    }
  } else if (match(parser, TOK_STRUCT)) {
    result = struct_declaration(parser);
  } else if (match(parser, TOK_ENUM)) {
    result = enum_declaration(parser);
  } else if (match(parser, TOK_EXTEND)) {
    result = extend_declaration(parser);
  } else if (match(parser, TOK_MODULE)) {
    /* module Name; */
    consume(parser, TOK_IDENT, "Expected module name");
    char *module_name = token_string(&parser->previous);
    while (match(parser, TOK_DOT)) {
      consume(parser, TOK_IDENT, "Expected identifier after '.'");
      char *part = token_string(&parser->previous);
      size_t old_len = strlen(module_name);
      size_t part_len = strlen(part);
      module_name = realloc(module_name, old_len + part_len + 2);
      module_name[old_len] = '.';
      memcpy(module_name + old_len + 1, part, part_len + 1);
      free(part);
    }
    consume_statement_terminator(
        parser, "Expected statement terminator after module declaration");
    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_MODULE;
    node->line = parser->previous.line;
    node->column = parser->previous.column;
    node->as.module_decl.name = module_name;
    result = node;
  } else if (match(parser, TOK_IMPORT)) {
    /* import Name; */
    consume(parser, TOK_IDENT, "Expected import name");
    char *import_path = token_string(&parser->previous);
    /* Handle dotted imports like import std.io */
    while (match(parser, TOK_DOT)) {
      consume(parser, TOK_IDENT, "Expected identifier after '.'");
      char *part = token_string(&parser->previous);
      size_t old_len = strlen(import_path);
      size_t part_len = strlen(part);
      import_path = realloc(import_path, old_len + part_len + 2);
      import_path[old_len] = '.';
      memcpy(import_path + old_len + 1, part, part_len + 1);
      free(part);
    }
    consume_statement_terminator(
        parser, "Expected statement terminator after import declaration");
    AstNode *node = calloc(1, sizeof(AstNode));
    node->type = AST_IMPORT;
    node->line = parser->previous.line;
    node->column = parser->previous.column;
    node->as.import_decl.path = import_path;
    result = node;
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
    if (strcmp(name, "syntax_profile") == 0) {
      value = parse_syntax_profile_value(parser);
      if (!value) {
        parser_error_at_current(parser, "Expected syntax_profile entries");
      }
    } else
    /*
     * Pragmas like syntax_profile can carry richer values than a single token.
     * Capture the remainder of the current source line so commas/percentages
     * survive parsing.
     */
    if (check(parser, TOK_EOF) || parser->current.line != line) {
      parser_error_at_current(parser, "Expected pragma value");
    } else {
      const char *start = parser->current.start;
      const char *end = NULL;

      while (!check(parser, TOK_EOF) && parser->current.line == line) {
        end = parser->current.start + parser->current.length;
        advance(parser);
      }

      if (!end || end <= start) {
        parser_error_at_current(parser, "Expected pragma value");
      } else {
        while (start < end && (*start == ' ' || *start == '\t')) {
          start++;
        }
        while (end > start && (end[-1] == ' ' || end[-1] == '\t')) {
          end--;
        }

        if (end <= start) {
          parser_error_at_current(parser, "Expected pragma value");
        } else {
          size_t len = (size_t)(end - start);
          value = malloc(len + 1);
          memcpy(value, start, len);
          value[len] = '\0';
        }
      }
    }
  }

  AstNode *pragma = calloc(1, sizeof(AstNode));
  pragma->type = AST_PRAGMA;
  pragma->line = line;
  pragma->column = col;
  pragma->as.pragma.name = name;
  pragma->as.pragma.value = value;

  if (strcmp(name, "style") == 0 && value) {
    parser->current_style = parse_style_directive(value);
    parser->lexer->emit_layout_tokens =
        parser->current_style == PARSER_STYLE_PYTHON;
    parser->lexer->emit_newline_tokens =
        parser->current_style == PARSER_STYLE_PYTHON ||
        parser->current_style == PARSER_STYLE_EXPRESSION;
    if (parser->lexer->emit_layout_tokens && parser->current.type != TOK_NEWLINE &&
        parser->current.type != TOK_EOF) {
      parser->lexer->line_start = false;
    }
  }

  return pragma;
}

/* ======== Main Parse Function ======== */

void parser_init(Parser *parser, Lexer *lexer) {
  parser->lexer = lexer;
  parser->current_style = PARSER_STYLE_DEFAULT;
  parser->disallow_inline_lambda = false;
  parser->had_error = false;
  parser->panic_mode = false;
  parser->error_message[0] = '\0';

  /* Prime the parser with first token */
  advance(parser);
}

AstNode *parser_parse(Parser *parser) {
  AstNode *program = ast_new_program();

  while (!check(parser, TOK_EOF)) {
    skip_newlines(parser);
    if (check(parser, TOK_DEDENT)) {
      advance(parser);
      continue;
    }
    if (check(parser, TOK_EOF))
      break;
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
