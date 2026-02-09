/*
 * QISC Lexer - Token Definitions
 *
 * Defines all tokens for the QISC language.
 */

#ifndef QISC_TOKENS_H
#define QISC_TOKENS_H

/* Token types */
typedef enum {
  /* Special */
  TOK_EOF,
  TOK_ERROR,
  TOK_NEWLINE,

  /* Literals */
  TOK_IDENT,      /* identifier */
  TOK_INT_LIT,    /* 42 */
  TOK_FLOAT_LIT,  /* 3.14 */
  TOK_STRING_LIT, /* "hello" */
  TOK_CHAR_LIT,   /* 'a' */
  TOK_TRUE,       /* true */
  TOK_FALSE,      /* false */
  TOK_NONE,       /* none */

  /* Keywords - Types */
  TOK_INT,    /* int */
  TOK_UINT,   /* uint */
  TOK_I8,     /* i8 */
  TOK_I16,    /* i16 */
  TOK_I32,    /* i32 */
  TOK_I64,    /* i64 */
  TOK_U8,     /* u8 */
  TOK_U16,    /* u16 */
  TOK_U32,    /* u32 */
  TOK_U64,    /* u64 */
  TOK_F32,    /* f32 */
  TOK_F64,    /* f64 */
  TOK_FLOAT,  /* float */
  TOK_DOUBLE, /* double */
  TOK_BOOL,   /* bool */
  TOK_CHAR,   /* char */
  TOK_STRING, /* string */
  TOK_VOID,   /* void */
  TOK_AUTO,   /* auto */
  TOK_MAYBE,  /* maybe */

  /* Keywords - Declarations */
  TOK_PROC,   /* proc */
  TOK_STRUCT, /* struct */
  TOK_ENUM,   /* enum */
  TOK_EXTEND, /* extend */
  TOK_MODULE, /* module */
  TOK_IMPORT, /* import */
  TOK_EXPORT, /* export */
  TOK_CONST,  /* const */
  TOK_STATIC, /* static */

  /* Keywords - Control Flow */
  TOK_IF,       /* if */
  TOK_ELSE,     /* else */
  TOK_WHEN,     /* when */
  TOK_IS,       /* is */
  TOK_FOR,      /* for */
  TOK_WHILE,    /* while */
  TOK_DO,       /* do */
  TOK_BREAK,    /* break */
  TOK_CONTINUE, /* continue */
  TOK_GIVE,     /* give (return value) */
  TOK_GIVES,    /* gives (return type annotation) */

  /* Keywords - Error Handling */
  TOK_TRY,     /* try */
  TOK_CATCH,   /* catch */
  TOK_FAIL,    /* fail */
  TOK_CANFAIL, /* canfail */
  TOK_ANY,     /* any */

  /* Keywords - Other */
  TOK_SELF,   /* self */
  TOK_IN,     /* in */
  TOK_OR,     /* or */
  TOK_AND,    /* and */
  TOK_NOT,    /* not */
  TOK_HAS,    /* has */
  TOK_SIZEOF, /* sizeof */
  TOK_TYPEOF, /* typeof */

  /* Operators - Arithmetic */
  TOK_PLUS,       /* + */
  TOK_MINUS,      /* - */
  TOK_STAR,       /* * */
  TOK_SLASH,      /* / */
  TOK_PERCENT,    /* % */
  TOK_PLUSPLUS,   /* ++ */
  TOK_MINUSMINUS, /* -- */

  /* Operators - Assignment */
  TOK_ASSIGN,         /* = */
  TOK_PLUS_ASSIGN,    /* += */
  TOK_MINUS_ASSIGN,   /* -= */
  TOK_STAR_ASSIGN,    /* *= */
  TOK_SLASH_ASSIGN,   /* /= */
  TOK_PERCENT_ASSIGN, /* %= */

  /* Operators - Comparison */
  TOK_EQ, /* == */
  TOK_NE, /* != */
  TOK_LT, /* < */
  TOK_GT, /* > */
  TOK_LE, /* <= */
  TOK_GE, /* >= */

  /* Operators - Logical */
  TOK_AMPAMP,   /* && */
  TOK_PIPEPIPE, /* || */
  TOK_BANG,     /* ! */

  /* Operators - Bitwise */
  TOK_AMP,    /* & */
  TOK_PIPE,   /* | */
  TOK_CARET,  /* ^ */
  TOK_TILDE,  /* ~ */
  TOK_LSHIFT, /* << */
  TOK_RSHIFT, /* >> */

  /* Operators - Special */
  TOK_ARROW,      /* -> */
  TOK_FATARROW,   /* => */
  TOK_PIPELINE,   /* >> (pipeline) */
  TOK_PROPAGATE,  /* ! (error propagation) */
  TOK_QUESTION,   /* ? */
  TOK_DOT,        /* . */
  TOK_DOTDOT,     /* .. */
  TOK_DOTDOTDOT,  /* ... */
  TOK_COLON,      /* : */
  TOK_COLONCOLON, /* :: */

  /* Delimiters */
  TOK_LPAREN,    /* ( */
  TOK_RPAREN,    /* ) */
  TOK_LBRACE,    /* { */
  TOK_RBRACE,    /* } */
  TOK_LBRACKET,  /* [ */
  TOK_RBRACKET,  /* ] */
  TOK_SEMICOLON, /* ; */
  TOK_COMMA,     /* , */

  /* Preprocessor */
  TOK_PRAGMA, /* #pragma */
  TOK_HASH,   /* # */

} TokenType;

/* Token structure */
typedef struct {
  TokenType type;
  const char *start; /* Pointer to start in source */
  int length;        /* Length of token text */
  int line;          /* Line number (1-indexed) */
  int column;        /* Column number (1-indexed) */

  /* Literal values (for convenience) */
  union {
    long long int_value;
    double float_value;
  } literal;
} Token;

/* Token type to string (for debugging) */
const char *token_type_name(TokenType type);

/* Token to string (for debugging) */
void token_print(Token *token);

#endif /* QISC_TOKENS_H */
