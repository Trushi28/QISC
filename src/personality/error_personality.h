/*
 * QISC Personality-Aware Error Messages
 *
 * Makes compiler errors feel less frustrating and more like helpful guidance.
 * Supports multiple personality modes with context-aware suggestions.
 */

#ifndef QISC_ERROR_PERSONALITY_H
#define QISC_ERROR_PERSONALITY_H

#include "qisc.h"
#include <stdarg.h>
#include <stdbool.h>
#include <time.h>

/* Error type categories */
typedef enum {
    ERR_TYPE_SYNTAX,       /* Syntax errors (missing braces, semicolons, etc.) */
    ERR_TYPE_TYPE_MISMATCH,/* Type incompatibility errors */
    ERR_TYPE_UNDEFINED,    /* Undefined variable/function references */
    ERR_TYPE_UNUSED,       /* Unused variable/import warnings */
    ERR_TYPE_UNREACHABLE,  /* Unreachable code detected */
    ERR_TYPE_OVERFLOW,     /* Integer/buffer overflow potential */
    ERR_TYPE_NULL,         /* Null pointer dereference risk */
    ERR_TYPE_BOUNDS,       /* Array bounds violation */
    ERR_TYPE_PERFORMANCE,  /* Performance warnings */
    ERR_TYPE_COUNT         /* Number of error types */
} ErrorType;

/* Syntax error subtypes for more specific messages */
typedef enum {
    SYNTAX_MISSING_SEMICOLON,
    SYNTAX_MISSING_BRACE_OPEN,
    SYNTAX_MISSING_BRACE_CLOSE,
    SYNTAX_MISSING_PAREN_OPEN,
    SYNTAX_MISSING_PAREN_CLOSE,
    SYNTAX_MISSING_BRACKET,
    SYNTAX_UNEXPECTED_TOKEN,
    SYNTAX_INVALID_EXPRESSION,
    SYNTAX_GENERIC
} SyntaxErrorSubtype;

/* Error context structure */
typedef struct {
    ErrorType type;
    int line;
    int column;
    const char *message;
    const char *code_context;
    const char *suggestion;
    const char *filename;
    SyntaxErrorSubtype syntax_subtype;
    /* For type mismatch errors */
    const char *expected_type;
    const char *actual_type;
    /* For undefined errors */
    const char *undefined_name;
    const char *similar_name; /* Did-you-mean suggestion */
} ErrorContext;

/* Error history tracking for learning messages */
#define QISC_ERROR_HISTORY_SIZE 32

typedef struct {
    ErrorType type;
    int count;
    time_t last_occurrence;
} ErrorHistoryEntry;

typedef struct {
    ErrorHistoryEntry entries[QISC_ERROR_HISTORY_SIZE];
    int total_errors;
    int session_errors;
} ErrorHistory;

/*
 * Core error display functions
 */

/* Display error with personality-appropriate formatting */
void qisc_error_with_personality(QiscPersonality p, ErrorContext *ctx);

/* Convenience function with printf-style formatting */
void qisc_error_personality(QiscPersonality p, ErrorType type,
                            int line, int col, const char *fmt, ...);

/* Display error with full context */
void qisc_error_full(QiscPersonality p, ErrorType type, int line, int col,
                     const char *filename, const char *code_context,
                     const char *fmt, ...);

/*
 * Context-aware suggestion functions
 */

/* Get suggestion for type mismatch errors */
const char *suggest_for_type_error(const char *expected, const char *got);

/* Get suggestion for undefined variable with similar name hint */
const char *suggest_for_undefined(const char *name, const char *similar);

/* Get suggestion for syntax errors */
const char *suggest_for_syntax(SyntaxErrorSubtype subtype);

/* Get suggestion for performance issues */
const char *suggest_for_performance(const char *pattern);

/*
 * Error message retrieval
 */

/* Get personality-appropriate message for error type */
const char *get_error_message(QiscPersonality p, ErrorType type);

/* Get a random variant of the error message */
const char *get_error_message_variant(QiscPersonality p, ErrorType type);

/* Get the learning tip for repeated errors */
const char *get_learning_message(ErrorType type);

/*
 * Error history tracking
 */

/* Record an error occurrence */
void error_record(ErrorType type);

/* Get count of specific error type */
int error_get_count(ErrorType type);

/* Get total error count for session */
int error_get_session_total(void);

/* Reset session error count */
void error_reset_session(void);

/* Check if user is struggling with a specific error type */
bool error_is_struggling(ErrorType type);

/*
 * Visual formatting helpers
 */

/* Print code context with line highlighting */
void error_print_context(int line, int col, const char *code,
                         const char *highlight_color);

/* Print error box with personality styling */
void error_print_box(QiscPersonality p, const char *title, const char *message);

/* Print suggestion in a friendly format */
void error_print_suggestion(QiscPersonality p, const char *suggestion);

/* Print "did you mean" style hints */
void error_print_did_you_mean(QiscPersonality p, const char *original,
                              const char *suggestion);

/*
 * Error message customization
 */

/* Set custom message for an error type (per personality) */
void error_set_custom_message(QiscPersonality p, ErrorType type,
                              const char *message);

/* Reset all custom messages to defaults */
void error_reset_messages(void);

/*
 * Initialization and cleanup
 */

/* Initialize error personality system */
void error_personality_init(void);

/* Cleanup and save error history */
void error_personality_cleanup(void);

#endif /* QISC_ERROR_PERSONALITY_H */
