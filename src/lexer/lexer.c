/*
 * QISC Lexer Implementation
 */

#include "lexer.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Initialize lexer */
void lexer_init(Lexer *lexer, const char *source) {
  lexer->source = source;
  lexer->start = source;
  lexer->current = source;
  lexer->line = 1;
  lexer->column = 1;
  lexer->start_column = 1;
  lexer->had_error = false;
  lexer->error_message[0] = '\0';
  lexer->emit_layout_tokens = false;
  lexer->emit_newline_tokens = false;
  lexer->line_start = true;
  lexer->indent_stack[0] = 0;
  lexer->indent_depth = 0;
  lexer->pending_dedents = 0;
  lexer->layout_indent = 0;
  lexer->layout_indent_ready = false;
}

Token lexer_peek(Lexer *lexer) {
  Lexer copy = *lexer;
  return lexer_scan_token(&copy);
}

/* Helper: check if at end */
bool lexer_is_at_end(Lexer *lexer) { return *lexer->current == '\0'; }

/* Helper: advance and return current char */
static char advance(Lexer *lexer) {
  char c = *lexer->current++;
  if (c == '\n') {
    lexer->line++;
    lexer->column = 1;
    lexer->line_start = true;
  } else {
    lexer->column++;
    lexer->line_start = false;
  }
  return c;
}

/* Helper: peek current char without advancing */
static char peek(Lexer *lexer) { return *lexer->current; }

/* Helper: peek next char */
static char peek_next(Lexer *lexer) {
  if (lexer_is_at_end(lexer))
    return '\0';
  return lexer->current[1];
}

/* Helper: match and consume if matches */
static bool match(Lexer *lexer, char expected) {
  if (lexer_is_at_end(lexer))
    return false;
  if (*lexer->current != expected)
    return false;
  advance(lexer);
  return true;
}

/* Helper: create token */
static Token make_token(Lexer *lexer, TokenType type) {
  Token token;
  token.type = type;
  token.start = lexer->start;
  token.length = (int)(lexer->current - lexer->start);
  token.line = lexer->line;
  token.column = lexer->start_column;
  token.literal.int_value = 0;
  return token;
}

/* Helper: create error token */
static Token error_token(Lexer *lexer, const char *message) {
  Token token;
  token.type = TOK_ERROR;
  token.start = message;
  token.length = (int)strlen(message);
  token.line = lexer->line;
  token.column = lexer->start_column;

  lexer->had_error = true;
  strncpy(lexer->error_message, message, sizeof(lexer->error_message) - 1);

  return token;
}

static Token make_layout_token(Lexer *lexer, TokenType type) {
  lexer->start = lexer->current;
  lexer->start_column = lexer->column;
  return make_token(lexer, type);
}

static void skip_comment(Lexer *lexer) {
  if (peek_next(lexer) == '/') {
    while (peek(lexer) != '\n' && !lexer_is_at_end(lexer)) {
      advance(lexer);
    }
    return;
  }

  if (peek_next(lexer) == '*') {
    advance(lexer); /* / */
    advance(lexer); /* * */
    while (!lexer_is_at_end(lexer)) {
      if (peek(lexer) == '*' && peek_next(lexer) == '/') {
        advance(lexer);
        advance(lexer);
        break;
      }
      advance(lexer);
    }
  }
}

/* Skip whitespace and comments */
static void skip_whitespace(Lexer *lexer) {
  for (;;) {
    char c = peek(lexer);
    switch (c) {
    case ' ':
    case '\t':
    case '\r':
      if (lexer->emit_layout_tokens && lexer->line_start) {
        return;
      }
      advance(lexer);
      break;
    case '\n':
      if (lexer->emit_newline_tokens) {
        return;
      }
      advance(lexer);
      break;
    case '/':
      if (peek_next(lexer) == '/') {
        if (lexer->emit_layout_tokens && lexer->line_start) {
          return;
        }
        skip_comment(lexer);
      } else if (peek_next(lexer) == '*') {
        skip_comment(lexer);
      } else {
        return;
      }
      break;
    default:
      return;
    }
  }
}

static bool lex_layout_prefix(Lexer *lexer, Token *out) {
  for (;;) {
    int indent = 0;

    if (lexer->pending_dedents > 0) {
      lexer->pending_dedents--;
      lexer->line_start = true;
      *out = make_layout_token(lexer, TOK_DEDENT);
      return true;
    }

    if (lexer_is_at_end(lexer)) {
      if (lexer->indent_depth > 0) {
        lexer->indent_depth--;
        lexer->line_start = true;
        *out = make_layout_token(lexer, TOK_DEDENT);
        return true;
      }
      *out = make_layout_token(lexer, TOK_EOF);
      return true;
    }

    if (lexer->layout_indent_ready) {
      indent = lexer->layout_indent;
    } else {
      while (peek(lexer) == ' ' || peek(lexer) == '\t') {
        indent += (peek(lexer) == '\t') ? 4 : 1;
        lexer->current++;
        lexer->column++;
      }
      lexer->layout_indent = indent;
      lexer->layout_indent_ready = true;
    }

    if (peek(lexer) == '/' &&
        (peek_next(lexer) == '/' || peek_next(lexer) == '*')) {
      skip_comment(lexer);
      if (peek(lexer) == '\n') {
        lexer->layout_indent_ready = false;
        lexer->start = lexer->current;
        lexer->start_column = lexer->column;
        advance(lexer);
        *out = make_token(lexer, TOK_NEWLINE);
        return true;
      }
      continue;
    }

    if (peek(lexer) == '\n') {
      lexer->layout_indent_ready = false;
      lexer->start = lexer->current;
      lexer->start_column = lexer->column;
      advance(lexer);
      *out = make_token(lexer, TOK_NEWLINE);
      return true;
    }

    if (peek(lexer) == '\0') {
      if (lexer->indent_depth > 0) {
        lexer->indent_depth--;
        lexer->line_start = true;
        *out = make_layout_token(lexer, TOK_DEDENT);
        return true;
      }
      lexer->layout_indent_ready = false;
      *out = make_layout_token(lexer, TOK_EOF);
      return true;
    }

    if (indent > lexer->indent_stack[lexer->indent_depth]) {
      if (lexer->indent_depth >= 63) {
        *out = error_token(lexer, "Indentation nesting too deep");
        return true;
      }
      lexer->indent_depth++;
      lexer->indent_stack[lexer->indent_depth] = indent;
      lexer->layout_indent_ready = false;
      lexer->line_start = false;
      *out = make_layout_token(lexer, TOK_INDENT);
      return true;
    }

    if (indent < lexer->indent_stack[lexer->indent_depth]) {
      int dedents = 0;
      while (lexer->indent_depth > 0 &&
             indent < lexer->indent_stack[lexer->indent_depth]) {
        lexer->indent_depth--;
        dedents++;
      }
      if (indent != lexer->indent_stack[lexer->indent_depth]) {
        *out = error_token(lexer, "Inconsistent indentation");
        return true;
      }
      lexer->pending_dedents = dedents - 1;
      lexer->line_start = true;
      *out = make_layout_token(lexer, TOK_DEDENT);
      return true;
    }

    lexer->layout_indent_ready = false;
    lexer->line_start = false;
    return false;
  }
}

/* Check keyword */
static TokenType check_keyword(Lexer *lexer, int start, int length,
                               const char *rest, TokenType type) {
  if (lexer->current - lexer->start == start + length &&
      memcmp(lexer->start + start, rest, length) == 0) {
    return type;
  }
  return TOK_IDENT;
}

/* Identify keyword or identifier */
static TokenType identifier_type(Lexer *lexer) {
  switch (lexer->start[0]) {
  case 'a':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'n':
        return check_keyword(lexer, 2, 1, "d", TOK_AND);
      case 'u':
        return check_keyword(lexer, 2, 2, "to", TOK_AUTO);
      case 'y':
        return check_keyword(lexer, 2, 0, "", TOK_ANY);
      }
    }
    break;
  case 'b':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'o':
        return check_keyword(lexer, 2, 2, "ol", TOK_BOOL);
      case 'r':
        return check_keyword(lexer, 2, 3, "eak", TOK_BREAK);
      }
    }
    break;
  case 'c':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'a':
        if (lexer->current - lexer->start > 2 && lexer->start[2] == 'n') {
          return check_keyword(lexer, 3, 4, "fail", TOK_CANFAIL);
        }
        return check_keyword(lexer, 2, 3, "tch", TOK_CATCH);
      case 'h':
        return check_keyword(lexer, 2, 2, "ar", TOK_CHAR);
      case 'o':
        if (lexer->current - lexer->start > 3 && lexer->start[2] == 'n') {
          if (lexer->start[3] == 's')
            return check_keyword(lexer, 4, 1, "t", TOK_CONST);
          if (lexer->start[3] == 't')
            return check_keyword(lexer, 4, 4, "inue", TOK_CONTINUE);
        }
        break;
      }
    }
    break;
  case 'd':
    if (lexer->current - lexer->start > 1) {
      if (lexer->start[1] == 'o') {
        if (lexer->current - lexer->start == 2)
          return TOK_DO;
        return check_keyword(lexer, 2, 4, "uble", TOK_DOUBLE);
      }
    }
    break;
  case 'e':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'l':
        return check_keyword(lexer, 2, 2, "se", TOK_ELSE);
      case 'n':
        return check_keyword(lexer, 2, 2, "um", TOK_ENUM);
      case 'x':
        if (lexer->current - lexer->start > 2) {
          if (lexer->start[2] == 'p')
            return check_keyword(lexer, 3, 3, "ort", TOK_EXPORT);
          if (lexer->start[2] == 't')
            return check_keyword(lexer, 3, 3, "end", TOK_EXTEND);
        }
        break;
      }
    }
    break;
  case 'f':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case '3':
        return check_keyword(lexer, 2, 1, "2", TOK_F32);
      case '6':
        return check_keyword(lexer, 2, 1, "4", TOK_F64);
      case 'a':
        if (lexer->current - lexer->start > 2) {
          if (lexer->start[2] == 'i')
            return check_keyword(lexer, 3, 1, "l", TOK_FAIL);
          if (lexer->start[2] == 'l')
            return check_keyword(lexer, 3, 2, "se", TOK_FALSE);
        }
        break;
      case 'l':
        return check_keyword(lexer, 2, 3, "oat", TOK_FLOAT);
      case 'o':
        return check_keyword(lexer, 2, 1, "r", TOK_FOR);
      }
    }
    break;
  case 'g':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'i':
        if (lexer->current - lexer->start > 2) {
          if (lexer->start[2] == 'v') {
            if (lexer->current - lexer->start == 4)
              return check_keyword(lexer, 3, 1, "e", TOK_GIVE);
            return check_keyword(lexer, 3, 2, "es", TOK_GIVES);
          }
        }
        break;
      }
    }
    break;
  case 'h':
    return check_keyword(lexer, 1, 2, "as", TOK_HAS);
  case 'i':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case '8':
        return (lexer->current - lexer->start == 2) ? TOK_I8 : TOK_IDENT;
      case '1':
        return check_keyword(lexer, 2, 1, "6", TOK_I16);
      case '3':
        return check_keyword(lexer, 2, 1, "2", TOK_I32);
      case '6':
        return check_keyword(lexer, 2, 1, "4", TOK_I64);
      case 'f':
        return (lexer->current - lexer->start == 2) ? TOK_IF : TOK_IDENT;
      case 'm':
        return check_keyword(lexer, 2, 4, "port", TOK_IMPORT);
      case 'n':
        if (lexer->current - lexer->start == 2)
          return TOK_IN;
        return check_keyword(lexer, 2, 1, "t", TOK_INT);
      case 's':
        return (lexer->current - lexer->start == 2) ? TOK_IS : TOK_IDENT;
      }
    }
    break;
  case 'm':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'a':
        return check_keyword(lexer, 2, 3, "ybe", TOK_MAYBE);
      case 'o':
        return check_keyword(lexer, 2, 4, "dule", TOK_MODULE);
      }
    }
    break;
  case 'n':
    if (lexer->current - lexer->start > 1) {
      if (lexer->start[1] == 'o') {
        if (lexer->current - lexer->start == 3)
          return check_keyword(lexer, 2, 1, "t", TOK_NOT);
        return check_keyword(lexer, 2, 2, "ne", TOK_NONE);
      }
    }
    break;
  case 'o':
    return check_keyword(lexer, 1, 1, "r", TOK_OR);
  case 'p':
    return check_keyword(lexer, 1, 3, "roc", TOK_PROC);
  case 's':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'e':
        return check_keyword(lexer, 2, 2, "lf", TOK_SELF);
      case 'i':
        return check_keyword(lexer, 2, 4, "zeof", TOK_SIZEOF);
      case 't':
        if (lexer->current - lexer->start > 2) {
          if (lexer->start[2] == 'a')
            return check_keyword(lexer, 3, 3, "tic", TOK_STATIC);
          if (lexer->start[2] == 'r') {
            if (lexer->current - lexer->start > 4 && lexer->start[3] == 'u') {
              return check_keyword(lexer, 4, 2, "ct", TOK_STRUCT);
            } else {
              return check_keyword(lexer, 3, 3, "ing", TOK_STRING);
            }
          }
        }
        break;
      }
    }
    break;
  case 't':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case 'r':
        if (lexer->current - lexer->start == 3)
          return check_keyword(lexer, 2, 1, "y", TOK_TRY);
        return check_keyword(lexer, 2, 2, "ue", TOK_TRUE);
      case 'y':
        return check_keyword(lexer, 2, 4, "peof", TOK_TYPEOF);
      }
    }
    break;
  case 'u':
    if (lexer->current - lexer->start > 1) {
      switch (lexer->start[1]) {
      case '8':
        return (lexer->current - lexer->start == 2) ? TOK_U8 : TOK_IDENT;
      case '1':
        return check_keyword(lexer, 2, 1, "6", TOK_U16);
      case '3':
        return check_keyword(lexer, 2, 1, "2", TOK_U32);
      case '6':
        return check_keyword(lexer, 2, 1, "4", TOK_U64);
      case 'i':
        return check_keyword(lexer, 2, 2, "nt", TOK_UINT);
      }
    }
    break;
  case 'v':
    return check_keyword(lexer, 1, 3, "oid", TOK_VOID);
  case 'w':
    if (lexer->current - lexer->start > 1) {
      if (lexer->start[1] == 'h') {
        if (lexer->current - lexer->start > 2 && lexer->start[2] == 'e') {
          return check_keyword(lexer, 3, 1, "n", TOK_WHEN);
        }
        return check_keyword(lexer, 2, 3, "ile", TOK_WHILE);
      }
    }
    break;
  }
  return TOK_IDENT;
}

/* Scan identifier */
static Token identifier(Lexer *lexer) {
  while (isalnum(peek(lexer)) || peek(lexer) == '_') {
    advance(lexer);
  }
  return make_token(lexer, identifier_type(lexer));
}

/* Scan number */
static Token number(Lexer *lexer) {
  bool is_float = false;

  /* Integer part */
  while (isdigit(peek(lexer))) {
    advance(lexer);
  }

  /* Decimal part */
  if (peek(lexer) == '.' && isdigit(peek_next(lexer))) {
    is_float = true;
    advance(lexer); /* consume '.' */
    while (isdigit(peek(lexer))) {
      advance(lexer);
    }
  }

  /* Exponent */
  if (peek(lexer) == 'e' || peek(lexer) == 'E') {
    is_float = true;
    advance(lexer);
    if (peek(lexer) == '+' || peek(lexer) == '-') {
      advance(lexer);
    }
    while (isdigit(peek(lexer))) {
      advance(lexer);
    }
  }

  Token token = make_token(lexer, is_float ? TOK_FLOAT_LIT : TOK_INT_LIT);

  /* Parse literal value */
  if (is_float) {
    token.literal.float_value = strtod(lexer->start, NULL);
  } else {
    token.literal.int_value = strtoll(lexer->start, NULL, 10);
  }

  return token;
}

/* Scan string */
static Token string(Lexer *lexer) {
  while (peek(lexer) != '"' && !lexer_is_at_end(lexer)) {
    if (peek(lexer) == '\\' && peek_next(lexer) != '\0') {
      advance(lexer); /* skip escape char */
    }
    advance(lexer);
  }

  if (lexer_is_at_end(lexer)) {
    return error_token(lexer, "Unterminated string");
  }

  advance(lexer); /* closing quote */
  return make_token(lexer, TOK_STRING_LIT);
}

/* Scan char literal */
static Token char_literal(Lexer *lexer) {
  if (peek(lexer) == '\\') {
    advance(lexer); /* escape */
  }
  if (!lexer_is_at_end(lexer)) {
    advance(lexer); /* char */
  }
  if (peek(lexer) != '\'') {
    return error_token(lexer, "Unterminated character literal");
  }
  advance(lexer); /* closing quote */
  return make_token(lexer, TOK_CHAR_LIT);
}

/* Main scan function */
Token lexer_scan_token(Lexer *lexer) {
  if (lexer->emit_layout_tokens && lexer->line_start) {
    Token layout;
    if (lex_layout_prefix(lexer, &layout)) {
      return layout;
    }
  }

  skip_whitespace(lexer);

  lexer->start = lexer->current;
  lexer->start_column = lexer->column;

  if (lexer_is_at_end(lexer)) {
    if (lexer->emit_layout_tokens && lexer->indent_depth > 0) {
      lexer->indent_depth--;
      lexer->line_start = false;
      return make_layout_token(lexer, TOK_DEDENT);
    }
    return make_token(lexer, TOK_EOF);
  }

  char c = advance(lexer);

  if (c == '\n' && lexer->emit_newline_tokens) {
    return make_token(lexer, TOK_NEWLINE);
  }

  /* Identifiers and keywords */
  if (isalpha(c) || c == '_') {
    return identifier(lexer);
  }

  /* Numbers */
  if (isdigit(c)) {
    return number(lexer);
  }

  /* Operators and delimiters */
  switch (c) {
  /* Single character */
  case '(':
    return make_token(lexer, TOK_LPAREN);
  case ')':
    return make_token(lexer, TOK_RPAREN);
  case '{':
    return make_token(lexer, TOK_LBRACE);
  case '}':
    return make_token(lexer, TOK_RBRACE);
  case '[':
    return make_token(lexer, TOK_LBRACKET);
  case ']':
    return make_token(lexer, TOK_RBRACKET);
  case ';':
    return make_token(lexer, TOK_SEMICOLON);
  case ',':
    return make_token(lexer, TOK_COMMA);
  case '~':
    return make_token(lexer, TOK_TILDE);
  case '?':
    return make_token(lexer, TOK_QUESTION);

  /* Possibly multi-character */
  case '+':
    if (match(lexer, '+'))
      return make_token(lexer, TOK_PLUSPLUS);
    if (match(lexer, '='))
      return make_token(lexer, TOK_PLUS_ASSIGN);
    return make_token(lexer, TOK_PLUS);
  case '-':
    if (match(lexer, '-'))
      return make_token(lexer, TOK_MINUSMINUS);
    if (match(lexer, '='))
      return make_token(lexer, TOK_MINUS_ASSIGN);
    if (match(lexer, '>'))
      return make_token(lexer, TOK_ARROW);
    return make_token(lexer, TOK_MINUS);
  case '*':
    if (match(lexer, '='))
      return make_token(lexer, TOK_STAR_ASSIGN);
    return make_token(lexer, TOK_STAR);
  case '/':
    if (match(lexer, '='))
      return make_token(lexer, TOK_SLASH_ASSIGN);
    return make_token(lexer, TOK_SLASH);
  case '%':
    if (match(lexer, '='))
      return make_token(lexer, TOK_PERCENT_ASSIGN);
    return make_token(lexer, TOK_PERCENT);
  case '!':
    if (match(lexer, '='))
      return make_token(lexer, TOK_NE);
    return make_token(lexer, TOK_BANG);
  case '=':
    if (match(lexer, '='))
      return make_token(lexer, TOK_EQ);
    if (match(lexer, '>'))
      return make_token(lexer, TOK_FATARROW);
    return make_token(lexer, TOK_ASSIGN);
  case '<':
    if (match(lexer, '='))
      return make_token(lexer, TOK_LE);
    if (match(lexer, '<'))
      return make_token(lexer, TOK_LSHIFT);
    return make_token(lexer, TOK_LT);
  case '>':
    if (match(lexer, '='))
      return make_token(lexer, TOK_GE);
    if (match(lexer, '>'))
      return make_token(lexer, TOK_PIPELINE); /* >> for pipeline */
    return make_token(lexer, TOK_GT);
  case '&':
    if (match(lexer, '&'))
      return make_token(lexer, TOK_AMPAMP);
    return make_token(lexer, TOK_AMP);
  case '|':
    if (match(lexer, '>'))
      return make_token(lexer, TOK_PIPELINE); /* |> for pipeline */
    if (match(lexer, '|'))
      return make_token(lexer, TOK_PIPEPIPE);
    return make_token(lexer, TOK_PIPE);
  case '^':
    return make_token(lexer, TOK_CARET);
  case '.':
    if (match(lexer, '.')) {
      if (match(lexer, '.'))
        return make_token(lexer, TOK_DOTDOTDOT);
      return make_token(lexer, TOK_DOTDOT);
    }
    return make_token(lexer, TOK_DOT);
  case ':':
    if (match(lexer, ':'))
      return make_token(lexer, TOK_COLONCOLON);
    return make_token(lexer, TOK_COLON);
  case '#':
    /* Check for #pragma */
    if (peek(lexer) == 'p') {
      const char *pragma = "pragma";
      bool is_pragma = true;
      for (int i = 0; pragma[i] != '\0'; i++) {
        if (lexer->current[i] != pragma[i]) {
          is_pragma = false;
          break;
        }
      }
      if (is_pragma) {
        for (int i = 0; i < 6; i++)
          advance(lexer);
        return make_token(lexer, TOK_PRAGMA);
      }
    }
    return make_token(lexer, TOK_HASH);

  /* String and char literals */
  case '"':
    return string(lexer);
  case '\'':
    return char_literal(lexer);
  }

  return error_token(lexer, "Unexpected character");
}

/* Token type names for debugging */
const char *token_type_name(TokenType type) {
  switch (type) {
  case TOK_EOF:
    return "EOF";
  case TOK_ERROR:
    return "ERROR";
  case TOK_NEWLINE:
    return "NEWLINE";
  case TOK_INDENT:
    return "INDENT";
  case TOK_DEDENT:
    return "DEDENT";
  case TOK_IDENT:
    return "IDENT";
  case TOK_INT_LIT:
    return "INT";
  case TOK_FLOAT_LIT:
    return "FLOAT";
  case TOK_STRING_LIT:
    return "STRING";
  case TOK_PROC:
    return "PROC";
  case TOK_GIVES:
    return "GIVES";
  case TOK_GIVE:
    return "GIVE";
  case TOK_WHEN:
    return "WHEN";
  case TOK_IS:
    return "IS";
  case TOK_MAYBE:
    return "MAYBE";
  case TOK_CANFAIL:
    return "CANFAIL";
  case TOK_TRY:
    return "TRY";
  case TOK_CATCH:
    return "CATCH";
  case TOK_FAIL:
    return "FAIL";
  case TOK_EXTEND:
    return "EXTEND";
  case TOK_PIPELINE:
    return "PIPELINE";
  /* Add more as needed */
  default:
    return "TOKEN";
  }
}

/* Print token for debugging */
void token_print(Token *token) {
  printf("Token(%s, '%.*s', line %d, col %d)\n", token_type_name(token->type),
         token->length, token->start, token->line, token->column);
}
