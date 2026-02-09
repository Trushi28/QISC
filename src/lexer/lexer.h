/*
 * QISC Lexer Header
 */

#ifndef QISC_LEXER_H
#define QISC_LEXER_H

#include "qisc.h"
#include "tokens.h"

/* Lexer state */
typedef struct {
  const char *source;  /* Source code */
  const char *start;   /* Start of current token */
  const char *current; /* Current position */
  int line;            /* Current line (1-indexed) */
  int column;          /* Current column (1-indexed) */
  int start_column;    /* Column at token start */

  /* Error handling */
  bool had_error;
  char error_message[256];
} Lexer;

/* Initialize lexer with source code */
void lexer_init(Lexer *lexer, const char *source);

/* Scan next token */
Token lexer_scan_token(Lexer *lexer);

/* Peek at next token without consuming */
Token lexer_peek(Lexer *lexer);

/* Check if at end of file */
bool lexer_is_at_end(Lexer *lexer);

/* Get current line content (for error messages) */
const char *lexer_get_line(Lexer *lexer, int line_number);

#endif /* QISC_LEXER_H */
