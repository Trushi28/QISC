/*
 * QISC Pragma System
 * Validation and processing of compiler directives
 */

#ifndef QISC_PRAGMA_H
#define QISC_PRAGMA_H

#include <stdbool.h>

/* Pragma categories */
typedef enum {
    PRAGMA_CAT_STYLE,       /* #pragma style:... */
    PRAGMA_CAT_CONTEXT,     /* #pragma context:... */
    PRAGMA_CAT_OPTIMIZE,    /* #pragma optimize:... */
    PRAGMA_CAT_BEHAVIOR,    /* #pragma inline, vectorize, etc */
    PRAGMA_CAT_PROFILE,     /* #pragma hot_path, profile:... */
} PragmaCategory;

/* Context types for context-specific compilation */
typedef enum {
    PRAGMA_CONTEXT_CLI,       /* Optimize startup, small binary */
    PRAGMA_CONTEXT_SERVER,    /* Optimize throughput, allow larger binary */
    PRAGMA_CONTEXT_EMBEDDED,  /* Optimize size, energy */
    PRAGMA_CONTEXT_WEB,       /* Optimize for WASM/size */
    PRAGMA_CONTEXT_NOTEBOOK,  /* Optimize for interactive use */
} PragmaContextType;

/* Context-specific optimization flags */
typedef struct {
    PragmaContextType context_type;
    
    /* Optimization level (LLVM: 0-3) */
    int opt_level;
    
    /* Size optimization level (LLVM: 0-2) */
    int size_level;
    
    /* Feature flags */
    bool aggressive_inlining;
    bool loop_unrolling;
    bool vectorization;
    bool fast_math;
    bool minimal_runtime;
    bool keep_debug_info;
    bool incremental_friendly;
    bool avoid_float;
    bool stack_optimization;
    bool cache_optimization;
    bool fast_startup;
} ContextOptFlags;

/* Pragma scope */
typedef enum {
    PRAGMA_SCOPE_FILE,      /* Affects entire file */
    PRAGMA_SCOPE_MODULE,    /* Affects module */
    PRAGMA_SCOPE_FUNCTION,  /* Affects function */
    PRAGMA_SCOPE_BLOCK,     /* Affects block only */
} PragmaScope;

/* Validation result */
typedef struct {
    bool valid;
    const char *error;      /* Error message if invalid */
    const char *warning;    /* Warning message (may be valid but suspicious) */
    const char *suggestion; /* Suggested fix or alternative */
} PragmaValidation;

/* Parsed pragma */
typedef struct {
    PragmaCategory category;
    PragmaScope scope;
    char name[64];          /* e.g., "hot_path", "optimize" */
    char value[128];        /* e.g., "latency", "auto" */
    int line;
    int column;
} ParsedPragma;

/* Validate pragma syntax */
PragmaValidation pragma_validate_syntax(const char *pragma_text);

/* Validate pragma semantics (in context) */
PragmaValidation pragma_validate_semantic(ParsedPragma *pragma, void *context);

/* Validate pragma against profile (post-compilation) */
PragmaValidation pragma_validate_profile(ParsedPragma *pragma, void *profile);

/* Check for conflicting pragmas */
PragmaValidation pragma_check_conflicts(ParsedPragma *pragmas, int count);

/* Get pragma scope precedence (higher = overrides lower) */
int pragma_scope_precedence(PragmaScope scope);

/* Print validation result */
void pragma_print_validation(PragmaValidation *result, int line);

/* Parse context type from string */
bool pragma_parse_context(const char *value, PragmaContextType *out_type);

/* Get optimization flags for a context type */
ContextOptFlags pragma_get_context_opt_flags(PragmaContextType context);

/* Get context type name as string */
const char *pragma_context_type_name(PragmaContextType context);

#endif /* QISC_PRAGMA_H */
