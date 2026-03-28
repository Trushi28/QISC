/*
 * QISC Pragma System Implementation
 * Validation and processing of compiler directives
 */

#include "pragma.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Known pragma directives */
static const char *KNOWN_STYLE_PRAGMAS[] = {
    "pipeline", "functional", "imperative", "brace", "python", "expression", NULL
};

static const char *KNOWN_CONTEXT_PRAGMAS[] = {
    "quantum", "classical", "hybrid", "simulation",
    "cli", "server", "embedded", "web", "notebook", NULL
};

static const char *KNOWN_OPTIMIZE_PRAGMAS[] = {
    "speed", "size", "latency", "throughput", "power", "none", NULL
};

static const char *KNOWN_BEHAVIOR_PRAGMAS[] = {
    "inline", "noinline", "vectorize", "novectorize", "unroll", "nounroll", NULL
};

static const char *KNOWN_PROFILE_PRAGMAS[] = {
    "hot_path", "cold_path", "likely", "unlikely", NULL
};

/* Helper: check if string is in null-terminated array */
static bool is_known_directive(const char *directive, const char **known_list) {
    for (int i = 0; known_list[i] != NULL; i++) {
        if (strcmp(directive, known_list[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Helper: skip whitespace */
static const char *skip_whitespace(const char *s) {
    while (*s && isspace((unsigned char)*s)) s++;
    return s;
}

/* Helper: extract identifier (letters, digits, underscores) */
static int extract_identifier(const char *s, char *out, size_t max_len) {
    size_t i = 0;
    while (s[i] && (isalnum((unsigned char)s[i]) || s[i] == '_') && i < max_len - 1) {
        out[i] = s[i];
        i++;
    }
    out[i] = '\0';
    return (int)i;
}

/* Validate pragma syntax */
PragmaValidation pragma_validate_syntax(const char *pragma_text) {
    PragmaValidation result = {.valid = false, .error = NULL, .warning = NULL, .suggestion = NULL};
    
    if (!pragma_text || !*pragma_text) {
        result.error = "Empty pragma directive";
        return result;
    }
    
    const char *p = skip_whitespace(pragma_text);
    
    /* Check for #pragma prefix (optional - may be pre-stripped) */
    if (strncmp(p, "#pragma", 7) == 0) {
        p = skip_whitespace(p + 7);
    }
    
    if (!*p) {
        result.error = "Missing pragma directive after #pragma";
        result.suggestion = "Use format: #pragma <category>:<value> or #pragma <directive>";
        return result;
    }
    
    /* Extract the directive name */
    char directive[64];
    int len = extract_identifier(p, directive, sizeof(directive));
    
    if (len == 0) {
        result.error = "Invalid pragma directive name";
        result.suggestion = "Directive must start with a letter or underscore";
        return result;
    }
    
    p += len;
    p = skip_whitespace(p);
    
    /* Check for category:value format */
    if (*p == ':') {
        p = skip_whitespace(p + 1);
        
        char value[128];
        int value_len = extract_identifier(p, value, sizeof(value));
        
        if (value_len == 0) {
            result.error = "Missing value after colon in pragma";
            result.suggestion = "Use format: #pragma category:value";
            return result;
        }
        
        /* Validate known categories */
        if (strcmp(directive, "style") == 0) {
            if (!is_known_directive(value, KNOWN_STYLE_PRAGMAS)) {
                result.valid = true;
                result.warning = "Unknown style pragma value";
                result.suggestion = "Known values: pipeline, functional, imperative, brace, python, expression";
                return result;
            }
        } else if (strcmp(directive, "context") == 0) {
            if (!is_known_directive(value, KNOWN_CONTEXT_PRAGMAS)) {
                result.valid = true;
                result.warning = "Unknown context pragma value";
                result.suggestion = "Known values: quantum, classical, hybrid, simulation";
                return result;
            }
        } else if (strcmp(directive, "optimize") == 0) {
            if (!is_known_directive(value, KNOWN_OPTIMIZE_PRAGMAS)) {
                result.valid = true;
                result.warning = "Unknown optimization target";
                result.suggestion = "Known values: speed, size, latency, throughput, power, none";
                return result;
            }
        } else if (strcmp(directive, "profile") == 0) {
            /* profile:X format - accept any value */
        } else {
            result.valid = true;
            result.warning = "Unknown pragma category";
            result.suggestion = "Known categories: style, context, optimize, profile";
            return result;
        }
    } else {
        /* Simple directive format (no colon) */
        if (!is_known_directive(directive, KNOWN_BEHAVIOR_PRAGMAS) &&
            !is_known_directive(directive, KNOWN_PROFILE_PRAGMAS)) {
            result.valid = true;
            result.warning = "Unknown pragma directive";
            result.suggestion = "Known directives: inline, noinline, vectorize, hot_path, etc.";
            return result;
        }
    }
    
    /* Check for trailing garbage */
    p = skip_whitespace(p);
    while (*p && (isalnum((unsigned char)*p) || *p == '_')) p++;
    p = skip_whitespace(p);
    
    if (*p && *p != '\n' && *p != '\r') {
        result.valid = true;
        result.warning = "Unexpected content after pragma directive";
        return result;
    }
    
    result.valid = true;
    return result;
}

/* Validate pragma semantics (in context) */
PragmaValidation pragma_validate_semantic(ParsedPragma *pragma, void *context) {
    PragmaValidation result = {.valid = true, .error = NULL, .warning = NULL, .suggestion = NULL};
    
    if (!pragma) {
        result.valid = false;
        result.error = "NULL pragma";
        return result;
    }
    
    (void)context; /* Context interpretation is implementation-specific */
    
    /* Check category-specific semantic rules */
    switch (pragma->category) {
        case PRAGMA_CAT_BEHAVIOR:
            /* vectorize only makes sense on loops */
            if (strcmp(pragma->name, "vectorize") == 0 && pragma->scope != PRAGMA_SCOPE_BLOCK) {
                result.warning = "vectorize pragma outside of loop block may have no effect";
                result.suggestion = "Place #pragma vectorize immediately before a loop";
            }
            /* unroll only makes sense on loops */
            if (strcmp(pragma->name, "unroll") == 0 && pragma->scope != PRAGMA_SCOPE_BLOCK) {
                result.warning = "unroll pragma outside of loop block may have no effect";
                result.suggestion = "Place #pragma unroll immediately before a loop";
            }
            /* inline only makes sense at function scope */
            if (strcmp(pragma->name, "inline") == 0 && pragma->scope == PRAGMA_SCOPE_BLOCK) {
                result.warning = "inline pragma at block scope is unusual";
                result.suggestion = "Place #pragma inline before function definition";
            }
            break;
            
        case PRAGMA_CAT_PROFILE:
            /* hot_path/cold_path should be at function or block scope */
            if (strcmp(pragma->name, "hot_path") == 0 || strcmp(pragma->name, "cold_path") == 0) {
                if (pragma->scope == PRAGMA_SCOPE_FILE || pragma->scope == PRAGMA_SCOPE_MODULE) {
                    result.warning = "Path hint at file/module scope affects all code";
                    result.suggestion = "Consider applying hot_path/cold_path to specific functions";
                }
            }
            break;
            
        case PRAGMA_CAT_OPTIMIZE:
            /* Optimization at block scope is fine but unusual */
            if (pragma->scope == PRAGMA_SCOPE_BLOCK) {
                result.warning = "Block-level optimization override may have limited effect";
            }
            break;
            
        case PRAGMA_CAT_STYLE:
        case PRAGMA_CAT_CONTEXT:
            /* These are typically file or module scope */
            if (pragma->scope == PRAGMA_SCOPE_BLOCK) {
                result.warning = "Style/context pragma at block scope is unusual";
                result.suggestion = "Consider placing at file or module scope";
            }
            break;
    }
    
    return result;
}

/* Validate pragma against profile (post-compilation) */
PragmaValidation pragma_validate_profile(ParsedPragma *pragma, void *profile) {
    PragmaValidation result = {.valid = true, .error = NULL, .warning = NULL, .suggestion = NULL};
    
    if (!pragma) {
        result.valid = false;
        result.error = "NULL pragma";
        return result;
    }
    
    (void)profile; /* Profile data structure is implementation-specific */
    
    /* Example profile-based validations (when profile data is available):
     * - hot_path pragma on code that profiler shows is cold
     * - optimize:latency on code that isn't latency-critical
     * - vectorize on code that couldn't be vectorized
     */
    
    if (pragma->category == PRAGMA_CAT_PROFILE) {
        /* Would check against actual profile data here */
        if (strcmp(pragma->name, "hot_path") == 0) {
            /* Placeholder: if profile shows this isn't hot, warn */
            /* result.warning = "hot_path pragma on code with <1% execution time"; */
        }
    }
    
    return result;
}

/* Check for conflicting pragmas */
PragmaValidation pragma_check_conflicts(ParsedPragma *pragmas, int count) {
    PragmaValidation result = {.valid = true, .error = NULL, .warning = NULL, .suggestion = NULL};
    
    if (!pragmas || count <= 0) {
        return result;
    }
    
    /* Check each pair of pragmas for conflicts */
    for (int i = 0; i < count; i++) {
        for (int j = i + 1; j < count; j++) {
            ParsedPragma *a = &pragmas[i];
            ParsedPragma *b = &pragmas[j];
            
            /* Same category conflicts */
            if (a->category == b->category) {
                
                /* Conflicting optimize values */
                if (a->category == PRAGMA_CAT_OPTIMIZE) {
                    if ((strcmp(a->value, "speed") == 0 && strcmp(b->value, "size") == 0) ||
                        (strcmp(a->value, "size") == 0 && strcmp(b->value, "speed") == 0)) {
                        result.valid = false;
                        result.error = "Conflicting optimization pragmas: speed vs size";
                        result.suggestion = "Choose one optimization target or use optimize:none";
                        return result;
                    }
                    if ((strcmp(a->value, "latency") == 0 && strcmp(b->value, "throughput") == 0) ||
                        (strcmp(a->value, "throughput") == 0 && strcmp(b->value, "latency") == 0)) {
                        result.warning = "Potentially conflicting: latency vs throughput optimization";
                        result.suggestion = "These goals may conflict; consider which is more important";
                    }
                }
                
                /* Conflicting behavior pragmas */
                if (a->category == PRAGMA_CAT_BEHAVIOR) {
                    if ((strcmp(a->name, "inline") == 0 && strcmp(b->name, "noinline") == 0) ||
                        (strcmp(a->name, "noinline") == 0 && strcmp(b->name, "inline") == 0)) {
                        result.valid = false;
                        result.error = "Conflicting pragmas: inline and noinline";
                        result.suggestion = "Remove one of the conflicting directives";
                        return result;
                    }
                    if ((strcmp(a->name, "vectorize") == 0 && strcmp(b->name, "novectorize") == 0) ||
                        (strcmp(a->name, "novectorize") == 0 && strcmp(b->name, "vectorize") == 0)) {
                        result.valid = false;
                        result.error = "Conflicting pragmas: vectorize and novectorize";
                        result.suggestion = "Remove one of the conflicting directives";
                        return result;
                    }
                    if ((strcmp(a->name, "unroll") == 0 && strcmp(b->name, "nounroll") == 0) ||
                        (strcmp(a->name, "nounroll") == 0 && strcmp(b->name, "unroll") == 0)) {
                        result.valid = false;
                        result.error = "Conflicting pragmas: unroll and nounroll";
                        result.suggestion = "Remove one of the conflicting directives";
                        return result;
                    }
                }
                
                /* Conflicting profile hints */
                if (a->category == PRAGMA_CAT_PROFILE) {
                    if ((strcmp(a->name, "hot_path") == 0 && strcmp(b->name, "cold_path") == 0) ||
                        (strcmp(a->name, "cold_path") == 0 && strcmp(b->name, "hot_path") == 0)) {
                        result.valid = false;
                        result.error = "Conflicting pragmas: hot_path and cold_path";
                        result.suggestion = "Code cannot be both hot and cold";
                        return result;
                    }
                    if ((strcmp(a->name, "likely") == 0 && strcmp(b->name, "unlikely") == 0) ||
                        (strcmp(a->name, "unlikely") == 0 && strcmp(b->name, "likely") == 0)) {
                        result.valid = false;
                        result.error = "Conflicting pragmas: likely and unlikely";
                        result.suggestion = "Branch cannot be both likely and unlikely";
                        return result;
                    }
                }
                
                /* Conflicting context */
                if (a->category == PRAGMA_CAT_CONTEXT) {
                    if (strcmp(a->value, b->value) != 0 && 
                        strcmp(a->value, "hybrid") != 0 && strcmp(b->value, "hybrid") != 0) {
                        result.warning = "Multiple context pragmas with different values";
                        result.suggestion = "Use context:hybrid if mixing quantum and classical";
                    }
                }
            }
        }
    }
    
    return result;
}

/* Get pragma scope precedence (higher = overrides lower) */
int pragma_scope_precedence(PragmaScope scope) {
    switch (scope) {
        case PRAGMA_SCOPE_FILE:     return 1;
        case PRAGMA_SCOPE_MODULE:   return 2;
        case PRAGMA_SCOPE_FUNCTION: return 3;
        case PRAGMA_SCOPE_BLOCK:    return 4;
        default:                    return 0;
    }
}

/* Print validation result */
void pragma_print_validation(PragmaValidation *result, int line) {
    if (!result) return;
    
    if (!result->valid && result->error) {
        fprintf(stderr, "error: line %d: %s\n", line, result->error);
    }
    
    if (result->warning) {
        fprintf(stderr, "warning: line %d: %s\n", line, result->warning);
    }
    
    if (result->suggestion) {
        fprintf(stderr, "  suggestion: %s\n", result->suggestion);
    }
}

/* Parse context type from string */
bool pragma_parse_context(const char *value, PragmaContextType *out_type) {
    if (!value || !out_type) return false;
    
    if (strcmp(value, "cli") == 0) {
        *out_type = PRAGMA_CONTEXT_CLI;
        return true;
    } else if (strcmp(value, "server") == 0) {
        *out_type = PRAGMA_CONTEXT_SERVER;
        return true;
    } else if (strcmp(value, "embedded") == 0) {
        *out_type = PRAGMA_CONTEXT_EMBEDDED;
        return true;
    } else if (strcmp(value, "web") == 0) {
        *out_type = PRAGMA_CONTEXT_WEB;
        return true;
    } else if (strcmp(value, "notebook") == 0) {
        *out_type = PRAGMA_CONTEXT_NOTEBOOK;
        return true;
    }
    
    return false;
}

/* Get optimization flags for a context type */
ContextOptFlags pragma_get_context_opt_flags(PragmaContextType context) {
    ContextOptFlags flags = {
        .context_type = context,
        .opt_level = 2,
        .size_level = 0,
        .aggressive_inlining = false,
        .loop_unrolling = false,
        .vectorization = false,
        .fast_math = false,
        .minimal_runtime = false,
        .keep_debug_info = false,
        .incremental_friendly = false,
        .avoid_float = false,
        .stack_optimization = false,
        .cache_optimization = false,
        .fast_startup = false,
    };
    
    switch (context) {
        case PRAGMA_CONTEXT_CLI:
            /* Optimize startup, small binary */
            flags.opt_level = 2;
            flags.size_level = 1;
            flags.fast_startup = true;
            flags.aggressive_inlining = false;
            flags.loop_unrolling = false;
            break;
            
        case PRAGMA_CONTEXT_SERVER:
            /* Optimize throughput, allow larger binary */
            flags.opt_level = 3;
            flags.size_level = 0;
            flags.aggressive_inlining = true;
            flags.loop_unrolling = true;
            flags.vectorization = true;
            flags.cache_optimization = true;
            break;
            
        case PRAGMA_CONTEXT_EMBEDDED:
            /* Optimize size, energy */
            flags.opt_level = 1;
            flags.size_level = 2;
            flags.minimal_runtime = true;
            flags.avoid_float = true;
            flags.stack_optimization = true;
            flags.aggressive_inlining = false;
            flags.loop_unrolling = false;
            break;
            
        case PRAGMA_CONTEXT_WEB:
            /* Optimize for WASM/size */
            flags.opt_level = 2;
            flags.size_level = 2;
            flags.minimal_runtime = true;
            flags.fast_startup = true;
            flags.aggressive_inlining = false;
            break;
            
        case PRAGMA_CONTEXT_NOTEBOOK:
            /* Optimize for interactive use */
            flags.opt_level = 1;
            flags.size_level = 0;
            flags.keep_debug_info = true;
            flags.incremental_friendly = true;
            flags.fast_startup = true;
            break;
    }
    
    return flags;
}

/* Get context type name as string */
const char *pragma_context_type_name(PragmaContextType context) {
    switch (context) {
        case PRAGMA_CONTEXT_CLI:      return "cli";
        case PRAGMA_CONTEXT_SERVER:   return "server";
        case PRAGMA_CONTEXT_EMBEDDED: return "embedded";
        case PRAGMA_CONTEXT_WEB:      return "web";
        case PRAGMA_CONTEXT_NOTEBOOK: return "notebook";
        default:                      return "unknown";
    }
}
