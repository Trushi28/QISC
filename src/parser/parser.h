/*
 * QISC Parser Header
 */

#ifndef QISC_PARSER_H
#define QISC_PARSER_H

#include "../lexer/lexer.h"
#include "ast.h"

typedef enum {
  PARSER_STYLE_DEFAULT = 0,
  PARSER_STYLE_BRACE,
  PARSER_STYLE_PIPELINE,
  PARSER_STYLE_EXPRESSION,
  PARSER_STYLE_PYTHON,
} ParserStyle;

/* Parser state */
typedef struct {
  Lexer *lexer;
  Token current;
  Token previous;
  ParserStyle current_style;
  bool disallow_inline_lambda;
  bool had_error;
  bool panic_mode;
  char error_message[256];
} Parser;

/* Initialize parser */
void parser_init(Parser *parser, Lexer *lexer);

/* Parse entire program */
AstNode *parser_parse(Parser *parser);

/* Parse specific constructs (for testing) */
AstNode *parser_parse_expression(Parser *parser);
AstNode *parser_parse_statement(Parser *parser);
AstNode *parser_parse_declaration(Parser *parser);

/* Error reporting */
void parser_error(Parser *parser, const char *message);
void parser_error_at_current(Parser *parser, const char *message);

#endif /* QISC_PARSER_H */
