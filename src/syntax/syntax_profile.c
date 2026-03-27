/*
 * QISC Multi-Syntax Profiling System Implementation
 * 
 * Analyzes code syntax patterns to determine dominant coding style
 * and optimize accordingly (pipeline-heavy vs loop-heavy vs functional).
 */

#define _POSIX_C_SOURCE 200809L
#include "syntax_profile.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ======== Known Higher-Order Functions ======== */

static const char *FUNCTIONAL_FUNCTIONS[] = {
    "map", "filter", "reduce", "fold", "foldl", "foldr",
    "zip", "zipWith", "take", "drop", "skip", "head", "tail",
    "first", "last", "reverse", "sort", "sortBy",
    "flatMap", "flatten", "concat", "append",
    "all", "any", "none", "find", "findIndex",
    "partition", "groupBy", "unique", "distinct",
    "compose", "pipe", "curry", "partial",
    NULL
};

/* Check if a function name is a known higher-order function */
static bool is_functional_call(const char *name) {
    if (!name) return false;
    for (int i = 0; FUNCTIONAL_FUNCTIONS[i] != NULL; i++) {
        if (strcmp(name, FUNCTIONAL_FUNCTIONS[i]) == 0) {
            return true;
        }
    }
    return false;
}

/* Get specific functional function type */
static void categorize_functional_call(const char *name, SyntaxPatternCounts *counts) {
    if (!name || !counts) return;
    
    if (strcmp(name, "map") == 0) counts->map++;
    else if (strcmp(name, "filter") == 0) counts->filter++;
    else if (strcmp(name, "reduce") == 0) counts->reduce++;
    else if (strcmp(name, "fold") == 0 || strcmp(name, "foldl") == 0 || 
             strcmp(name, "foldr") == 0) counts->fold++;
    else if (strcmp(name, "zip") == 0 || strcmp(name, "zipWith") == 0) counts->zip++;
    else if (strcmp(name, "take") == 0) counts->take++;
    else if (strcmp(name, "drop") == 0 || strcmp(name, "skip") == 0) counts->skip++;
}

/* ======== Profile Creation & Destruction ======== */

SyntaxProfile *syntax_profile_new(const char *module_name) {
    SyntaxProfile *profile = calloc(1, sizeof(SyntaxProfile));
    if (!profile) return NULL;
    
    if (module_name) {
        profile->module_name = strdup(module_name);
    } else {
        profile->module_name = strdup("<anonymous>");
    }
    
    profile->dominant_style = SYNTAX_STYLE_DEFAULT;
    profile->preferred_style = SYNTAX_STYLE_DEFAULT;
    profile->has_pragma_override = false;
    
    return profile;
}

void syntax_profile_free(SyntaxProfile *profile) {
    if (!profile) return;
    free(profile->module_name);
    free(profile);
}

SyntaxProfile *syntax_profile_clone(const SyntaxProfile *profile) {
    if (!profile) return NULL;
    
    SyntaxProfile *clone = malloc(sizeof(SyntaxProfile));
    if (!clone) return NULL;
    
    memcpy(clone, profile, sizeof(SyntaxProfile));
    if (profile->module_name) {
        clone->module_name = strdup(profile->module_name);
    }
    
    return clone;
}

/* ======== Pragma Parsing ======== */

/* Helper: parse percentage value like "70%" */
static float parse_percentage(const char *s) {
    if (!s) return 0.0f;
    
    float val = 0.0f;
    while (*s && isspace((unsigned char)*s)) s++;
    
    char *end;
    val = strtof(s, &end);
    
    /* Check for % suffix */
    while (*end && isspace((unsigned char)*end)) end++;
    if (*end == '%') {
        val /= 100.0f;
    }
    
    /* Clamp to valid range */
    if (val < 0.0f) val = 0.0f;
    if (val > 1.0f) val = 1.0f;
    
    return val;
}

/* Parse style pragma: "pipeline", "functional", "imperative" */
static SyntaxStyle parse_style_value(const char *value) {
    if (!value) return SYNTAX_STYLE_DEFAULT;
    
    /* Skip leading whitespace */
    while (*value && isspace((unsigned char)*value)) value++;
    
    if (strcasecmp(value, "pipeline") == 0) {
        return SYNTAX_STYLE_PIPELINE;
    } else if (strcasecmp(value, "functional") == 0) {
        return SYNTAX_STYLE_FUNCTIONAL;
    } else if (strcasecmp(value, "imperative") == 0) {
        return SYNTAX_STYLE_IMPERATIVE;
    } else if (strcasecmp(value, "brace") == 0) {
        return SYNTAX_STYLE_BRACE;
    } else if (strcasecmp(value, "python") == 0) {
        return SYNTAX_STYLE_PYTHON;
    }
    
    return SYNTAX_STYLE_DEFAULT;
}

SyntaxProfile *parse_syntax_pragma(const char *pragma_name, const char *pragma_value) {
    if (!pragma_name) return NULL;
    
    SyntaxProfile *profile = syntax_profile_new(NULL);
    if (!profile) return NULL;
    
    /* Handle #pragma style:<style> */
    if (strcmp(pragma_name, "style") == 0) {
        profile->preferred_style = parse_style_value(pragma_value);
        profile->dominant_style = profile->preferred_style;
        profile->has_pragma_override = true;
        
        /* Set density based on preferred style */
        switch (profile->preferred_style) {
            case SYNTAX_STYLE_PIPELINE:
                profile->pipeline_density = 1.0f;
                break;
            case SYNTAX_STYLE_FUNCTIONAL:
                profile->functional_density = 1.0f;
                break;
            case SYNTAX_STYLE_IMPERATIVE:
                profile->imperative_density = 1.0f;
                break;
            default:
                break;
        }
        return profile;
    }
    
    /* Handle #pragma syntax_profile: pipelines:70%, functional:20%, imperative:10% */
    if (strcmp(pragma_name, "syntax_profile") == 0 && pragma_value) {
        profile->has_pragma_override = true;
        
        /* Parse comma-separated key:value pairs */
        char *value_copy = strdup(pragma_value);
        char *saveptr = NULL;
        char *token = strtok_r(value_copy, ",", &saveptr);
        
        float max_density = 0.0f;
        SyntaxStyle max_style = SYNTAX_STYLE_DEFAULT;
        
        while (token) {
            /* Skip whitespace */
            while (*token && isspace((unsigned char)*token)) token++;
            
            /* Find colon */
            char *colon = strchr(token, ':');
            if (colon) {
                *colon = '\0';
                char *key = token;
                char *val = colon + 1;
                
                /* Trim key */
                char *end = key + strlen(key) - 1;
                while (end > key && isspace((unsigned char)*end)) *end-- = '\0';
                
                float pct = parse_percentage(val);
                
                if (strcasecmp(key, "pipelines") == 0 || strcasecmp(key, "pipeline") == 0) {
                    profile->pipeline_density = pct;
                    if (pct > max_density) {
                        max_density = pct;
                        max_style = SYNTAX_STYLE_PIPELINE;
                    }
                } else if (strcasecmp(key, "functional") == 0) {
                    profile->functional_density = pct;
                    if (pct > max_density) {
                        max_density = pct;
                        max_style = SYNTAX_STYLE_FUNCTIONAL;
                    }
                } else if (strcasecmp(key, "imperative") == 0) {
                    profile->imperative_density = pct;
                    if (pct > max_density) {
                        max_density = pct;
                        max_style = SYNTAX_STYLE_IMPERATIVE;
                    }
                } else if (strcasecmp(key, "declarative") == 0) {
                    profile->declarative_density = pct;
                }
            }
            
            token = strtok_r(NULL, ",", &saveptr);
        }
        
        profile->preferred_style = max_style;
        profile->dominant_style = max_style;
        
        free(value_copy);
        return profile;
    }
    
    syntax_profile_free(profile);
    return NULL;
}

void syntax_profile_apply_pragma(SyntaxProfile *profile,
                                  const char *pragma_name,
                                  const char *pragma_value) {
    if (!profile || !pragma_name) return;
    
    SyntaxProfile *pragma_profile = parse_syntax_pragma(pragma_name, pragma_value);
    if (!pragma_profile) return;
    
    /* Apply pragma overrides */
    if (pragma_profile->has_pragma_override) {
        profile->preferred_style = pragma_profile->preferred_style;
        profile->has_pragma_override = true;
        
        /* Pragma-specified densities override detected ones */
        if (pragma_profile->pipeline_density > 0) {
            profile->pipeline_density = pragma_profile->pipeline_density;
        }
        if (pragma_profile->functional_density > 0) {
            profile->functional_density = pragma_profile->functional_density;
        }
        if (pragma_profile->imperative_density > 0) {
            profile->imperative_density = pragma_profile->imperative_density;
        }
        if (pragma_profile->declarative_density > 0) {
            profile->declarative_density = pragma_profile->declarative_density;
        }
    }
    
    syntax_profile_free(pragma_profile);
}

/* ======== AST Analysis ======== */

/* Get identifier name from a call's callee */
static const char *get_callee_name(AstNode *callee) {
    if (!callee) return NULL;
    
    if (callee->type == AST_IDENTIFIER) {
        return callee->as.identifier.name;
    }
    /* Could handle member access for method calls */
    if (callee->type == AST_MEMBER) {
        return callee->as.member.member;
    }
    
    return NULL;
}

void count_syntax_patterns(AstNode *node, SyntaxPatternCounts *counts) {
    if (!node || !counts) return;
    
    counts->total++;
    
    switch (node->type) {
        case AST_PIPELINE:
            counts->pipeline++;
            /* Pipeline left and right operands */
            count_syntax_patterns(node->as.binary.left, counts);
            count_syntax_patterns(node->as.binary.right, counts);
            break;
            
        case AST_BINARY_OP:
            /* Check for pipeline operator */
            if (node->as.binary.op == OP_PIPELINE) {
                counts->pipeline++;
            }
            count_syntax_patterns(node->as.binary.left, counts);
            count_syntax_patterns(node->as.binary.right, counts);
            break;
            
        case AST_CALL: {
            const char *name = get_callee_name(node->as.call.callee);
            if (is_functional_call(name)) {
                counts->functional++;
                categorize_functional_call(name, counts);
            }
            /* Recurse into callee and arguments */
            count_syntax_patterns(node->as.call.callee, counts);
            for (int i = 0; i < node->as.call.args.count; i++) {
                count_syntax_patterns(node->as.call.args.items[i], counts);
            }
            break;
        }
            
        case AST_FOR:
            counts->imperative++;
            counts->for_loops++;
            count_syntax_patterns(node->as.for_stmt.init, counts);
            count_syntax_patterns(node->as.for_stmt.condition, counts);
            count_syntax_patterns(node->as.for_stmt.update, counts);
            count_syntax_patterns(node->as.for_stmt.body, counts);
            count_syntax_patterns(node->as.for_stmt.iterable, counts);
            break;
            
        case AST_WHILE:
            counts->imperative++;
            counts->while_loops++;
            count_syntax_patterns(node->as.while_stmt.condition, counts);
            count_syntax_patterns(node->as.while_stmt.body, counts);
            break;
            
        case AST_ASSIGN:
            counts->imperative++;
            counts->assignments++;
            count_syntax_patterns(node->as.assign.target, counts);
            count_syntax_patterns(node->as.assign.value, counts);
            break;
            
        case AST_VAR_DECL:
            /* Variable declarations with mutations count as imperative */
            if (!node->as.var_decl.is_const) {
                counts->mutations++;
            }
            count_syntax_patterns(node->as.var_decl.initializer, counts);
            break;
            
        case AST_WHEN:
            /* Pattern matching is declarative/functional */
            counts->declarative++;
            counts->pattern_matches++;
            count_syntax_patterns(node->as.when_stmt.value, counts);
            for (int i = 0; i < node->as.when_stmt.cases.count; i++) {
                count_syntax_patterns(node->as.when_stmt.cases.items[i], counts);
            }
            break;
            
        case AST_LAMBDA:
            /* Lambdas are functional */
            counts->functional++;
            for (int i = 0; i < node->as.lambda.params.count; i++) {
                count_syntax_patterns(node->as.lambda.params.items[i], counts);
            }
            count_syntax_patterns(node->as.lambda.body, counts);
            break;
            
        case AST_BLOCK:
            for (int i = 0; i < node->as.block.statements.count; i++) {
                count_syntax_patterns(node->as.block.statements.items[i], counts);
            }
            break;
            
        case AST_IF:
            count_syntax_patterns(node->as.if_stmt.condition, counts);
            count_syntax_patterns(node->as.if_stmt.then_branch, counts);
            count_syntax_patterns(node->as.if_stmt.else_branch, counts);
            break;
            
        case AST_PROC:
            for (int i = 0; i < node->as.proc.params.count; i++) {
                count_syntax_patterns(node->as.proc.params.items[i], counts);
            }
            count_syntax_patterns(node->as.proc.body, counts);
            break;
            
        case AST_PROGRAM:
            /* Process all pragmas first */
            for (int i = 0; i < node->as.program.pragmas.count; i++) {
                count_syntax_patterns(node->as.program.pragmas.items[i], counts);
            }
            /* Process declarations */
            for (int i = 0; i < node->as.program.declarations.count; i++) {
                count_syntax_patterns(node->as.program.declarations.items[i], counts);
            }
            break;
            
        case AST_UNARY_OP:
            count_syntax_patterns(node->as.unary.operand, counts);
            break;
            
        case AST_INDEX:
            count_syntax_patterns(node->as.index.object, counts);
            count_syntax_patterns(node->as.index.index, counts);
            break;
            
        case AST_MEMBER:
            count_syntax_patterns(node->as.member.object, counts);
            break;
            
        case AST_GIVE:
            count_syntax_patterns(node->as.give_stmt.value, counts);
            break;
            
        case AST_TRY:
            count_syntax_patterns(node->as.try_stmt.try_block, counts);
            for (int i = 0; i < node->as.try_stmt.catches.count; i++) {
                count_syntax_patterns(node->as.try_stmt.catches.items[i], counts);
            }
            break;
            
        case AST_FAIL:
            count_syntax_patterns(node->as.fail_stmt.error, counts);
            break;
            
        case AST_STRUCT:
            for (int i = 0; i < node->as.struct_decl.fields.count; i++) {
                count_syntax_patterns(node->as.struct_decl.fields.items[i], counts);
            }
            break;
            
        case AST_ENUM:
            for (int i = 0; i < node->as.enum_decl.variants.count; i++) {
                count_syntax_patterns(node->as.enum_decl.variants.items[i], counts);
            }
            break;
            
        case AST_EXTEND:
            for (int i = 0; i < node->as.extend_decl.methods.count; i++) {
                count_syntax_patterns(node->as.extend_decl.methods.items[i], counts);
            }
            break;
            
        case AST_ARRAY_LITERAL:
            for (int i = 0; i < node->as.array_literal.elements.count; i++) {
                count_syntax_patterns(node->as.array_literal.elements.items[i], counts);
            }
            break;
            
        case AST_EXPR_STMT:
            /* Expression statement - handled via the expression types above */
            break;
            
        /* Leaf nodes - no recursion needed */
        case AST_INT_LITERAL:
        case AST_FLOAT_LITERAL:
        case AST_STRING_LITERAL:
        case AST_BOOL_LITERAL:
        case AST_NONE_LITERAL:
        case AST_IDENTIFIER:
        case AST_BREAK:
        case AST_CONTINUE:
        case AST_PRAGMA:
        case AST_MODULE:
        case AST_IMPORT:
        case AST_STRUCT_LITERAL:
            break;
    }
}

/* Determine dominant style from counts */
static SyntaxStyle determine_dominant_style(const SyntaxPatternCounts *counts, 
                                             float pipeline_density,
                                             float functional_density,
                                             float imperative_density) {
    (void)counts;  /* For potential future use */
    
    /* Check thresholds in order of specificity */
    if (pipeline_density >= SYNTAX_PIPELINE_THRESHOLD) {
        return SYNTAX_STYLE_PIPELINE;
    }
    
    if (functional_density >= SYNTAX_FUNCTIONAL_THRESHOLD) {
        return SYNTAX_STYLE_FUNCTIONAL;
    }
    
    if (imperative_density >= SYNTAX_IMPERATIVE_THRESHOLD) {
        return SYNTAX_STYLE_IMPERATIVE;
    }
    
    /* Find maximum density */
    if (pipeline_density >= functional_density && pipeline_density >= imperative_density) {
        return SYNTAX_STYLE_PIPELINE;
    }
    
    if (functional_density >= pipeline_density && functional_density >= imperative_density) {
        return SYNTAX_STYLE_FUNCTIONAL;
    }
    
    return SYNTAX_STYLE_IMPERATIVE;
}

SyntaxProfile *analyze_syntax(AstNode *ast, const char *module_name) {
    SyntaxProfile *profile = syntax_profile_new(module_name);
    if (!profile) return NULL;
    
    if (!ast) {
        return profile;
    }
    
    /* Count patterns */
    SyntaxPatternCounts counts = {0};
    count_syntax_patterns(ast, &counts);
    
    /* Store raw counts */
    profile->total_nodes = counts.total;
    profile->pipeline_nodes = counts.pipeline;
    profile->functional_nodes = counts.functional;
    profile->imperative_nodes = counts.imperative;
    profile->declarative_nodes = counts.declarative;
    
    profile->map_count = counts.map;
    profile->filter_count = counts.filter;
    profile->reduce_count = counts.reduce;
    profile->fold_count = counts.fold;
    
    profile->for_count = counts.for_loops;
    profile->while_count = counts.while_loops;
    
    profile->assignment_count = counts.assignments;
    profile->mutation_count = counts.mutations;
    
    /* Calculate densities */
    if (counts.total > 0) {
        profile->pipeline_density = (float)counts.pipeline / (float)counts.total;
        profile->functional_density = (float)counts.functional / (float)counts.total;
        profile->imperative_density = (float)counts.imperative / (float)counts.total;
        profile->declarative_density = (float)counts.declarative / (float)counts.total;
    }
    
    /* Determine dominant style */
    profile->dominant_style = determine_dominant_style(&counts,
                                                        profile->pipeline_density,
                                                        profile->functional_density,
                                                        profile->imperative_density);
    
    /* Check for pragma overrides in the AST */
    if (ast->type == AST_PROGRAM) {
        for (int i = 0; i < ast->as.program.pragmas.count; i++) {
            AstNode *pragma = ast->as.program.pragmas.items[i];
            if (pragma && pragma->type == AST_PRAGMA) {
                syntax_profile_apply_pragma(profile,
                                            pragma->as.pragma.name,
                                            pragma->as.pragma.value);
            }
        }
    }
    
    return profile;
}

SyntaxProfile *syntax_profile_merge(const SyntaxProfile *a, const SyntaxProfile *b) {
    if (!a && !b) return NULL;
    if (!a) return syntax_profile_clone(b);
    if (!b) return syntax_profile_clone(a);
    
    /* Create merged profile with combined module name */
    size_t name_len = strlen(a->module_name) + strlen(b->module_name) + 4;
    char *merged_name = malloc(name_len);
    snprintf(merged_name, name_len, "%s + %s", a->module_name, b->module_name);
    
    SyntaxProfile *merged = syntax_profile_new(merged_name);
    free(merged_name);
    
    if (!merged) return NULL;
    
    /* Sum counts */
    merged->total_nodes = a->total_nodes + b->total_nodes;
    merged->pipeline_nodes = a->pipeline_nodes + b->pipeline_nodes;
    merged->functional_nodes = a->functional_nodes + b->functional_nodes;
    merged->imperative_nodes = a->imperative_nodes + b->imperative_nodes;
    merged->declarative_nodes = a->declarative_nodes + b->declarative_nodes;
    
    merged->map_count = a->map_count + b->map_count;
    merged->filter_count = a->filter_count + b->filter_count;
    merged->reduce_count = a->reduce_count + b->reduce_count;
    merged->fold_count = a->fold_count + b->fold_count;
    
    merged->for_count = a->for_count + b->for_count;
    merged->while_count = a->while_count + b->while_count;
    
    merged->assignment_count = a->assignment_count + b->assignment_count;
    merged->mutation_count = a->mutation_count + b->mutation_count;
    
    /* Recalculate densities */
    if (merged->total_nodes > 0) {
        merged->pipeline_density = (float)merged->pipeline_nodes / (float)merged->total_nodes;
        merged->functional_density = (float)merged->functional_nodes / (float)merged->total_nodes;
        merged->imperative_density = (float)merged->imperative_nodes / (float)merged->total_nodes;
        merged->declarative_density = (float)merged->declarative_nodes / (float)merged->total_nodes;
    }
    
    /* Determine style */
    SyntaxPatternCounts counts = {
        .total = merged->total_nodes,
        .pipeline = merged->pipeline_nodes,
        .functional = merged->functional_nodes,
        .imperative = merged->imperative_nodes
    };
    merged->dominant_style = determine_dominant_style(&counts,
                                                       merged->pipeline_density,
                                                       merged->functional_density,
                                                       merged->imperative_density);
    
    /* Pragma override takes precedence (prefer a's if both have one) */
    if (a->has_pragma_override) {
        merged->preferred_style = a->preferred_style;
        merged->has_pragma_override = true;
    } else if (b->has_pragma_override) {
        merged->preferred_style = b->preferred_style;
        merged->has_pragma_override = true;
    }
    
    return merged;
}

/* ======== Optimization Strategy ======== */

OptStrategy *opt_strategy_new(void) {
    return calloc(1, sizeof(OptStrategy));
}

void opt_strategy_free(OptStrategy *strategy) {
    free(strategy);
}

OptStrategy *strategy_for_syntax(const SyntaxProfile *profile) {
    OptStrategy *s = opt_strategy_new();
    if (!s) return NULL;
    
    /* Default conservative settings */
    s->unroll_factor = 0;  /* Auto-detect */
    
    if (!profile) {
        return s;
    }
    
    /* Use preferred style if pragma override, otherwise dominant */
    SyntaxStyle style = profile->has_pragma_override ? 
                        profile->preferred_style : 
                        profile->dominant_style;
    
    switch (style) {
        case SYNTAX_STYLE_PIPELINE:
            /* Pipeline-heavy code benefits from stream fusion */
            s->enable_stream_fusion = true;
            s->enable_lazy_evaluation = true;
            s->enable_parallel = true;
            s->enable_vectorization = true;
            s->enable_simd = true;
            s->enable_copy_elision = true;
            break;
            
        case SYNTAX_STYLE_FUNCTIONAL:
            /* Functional code benefits from memoization and tail calls */
            s->enable_memoization = true;
            s->enable_tail_call = true;
            s->enable_inlining = true;
            s->enable_copy_elision = true;
            s->enable_allocation_opts = true;
            /* Also enable stream fusion for chained HOFs */
            if (profile->pipeline_density > 0.1f) {
                s->enable_stream_fusion = true;
            }
            break;
            
        case SYNTAX_STYLE_IMPERATIVE:
            /* Imperative code benefits from loop optimizations */
            s->enable_loop_opts = true;
            s->enable_loop_unroll = true;
            s->enable_loop_fusion = true;
            s->enable_loop_interchange = true;
            s->enable_vectorization = true;
            s->enable_simd = true;
            /* Determine unroll factor based on loop count */
            if (profile->for_count + profile->while_count > 5) {
                s->unroll_factor = 4;
            } else {
                s->unroll_factor = 2;
            }
            break;
            
        case SYNTAX_STYLE_BRACE:
        case SYNTAX_STYLE_PYTHON:
        case SYNTAX_STYLE_DEFAULT:
            /* Mixed or default style - enable general optimizations */
            s->enable_inlining = true;
            s->enable_copy_elision = true;
            
            /* Enable based on what we actually see */
            if (profile->pipeline_density > 0.15f) {
                s->enable_stream_fusion = true;
            }
            if (profile->functional_density > 0.15f) {
                s->enable_memoization = true;
                s->enable_tail_call = true;
            }
            if (profile->imperative_density > 0.25f) {
                s->enable_loop_opts = true;
                s->unroll_factor = 2;
            }
            break;
    }
    
    return s;
}

/* ======== Style Names ======== */

const char *syntax_style_name(SyntaxStyle style) {
    switch (style) {
        case SYNTAX_STYLE_DEFAULT:    return "default";
        case SYNTAX_STYLE_PIPELINE:   return "pipeline";
        case SYNTAX_STYLE_FUNCTIONAL: return "functional";
        case SYNTAX_STYLE_IMPERATIVE: return "imperative";
        case SYNTAX_STYLE_BRACE:      return "brace";
        case SYNTAX_STYLE_PYTHON:     return "python";
        default:                       return "unknown";
    }
}

const char *strategy_name_for_style(SyntaxStyle style) {
    switch (style) {
        case SYNTAX_STYLE_PIPELINE:
            return "Stream Fusion + Vectorization";
        case SYNTAX_STYLE_FUNCTIONAL:
            return "Memoization + Tail Call Optimization";
        case SYNTAX_STYLE_IMPERATIVE:
            return "Loop Optimization + Unrolling";
        case SYNTAX_STYLE_BRACE:
        case SYNTAX_STYLE_PYTHON:
        case SYNTAX_STYLE_DEFAULT:
            return "Balanced Optimization";
        default:
            return "Unknown Strategy";
    }
}

/* ======== Reporting ======== */

const char *progress_bar(float density) {
    static char bar[32];
    const int width = 20;
    
    int filled = (int)(density * width + 0.5f);
    if (filled > width) filled = width;
    if (filled < 0) filled = 0;
    
    bar[0] = '[';
    for (int i = 0; i < width; i++) {
        bar[i + 1] = (i < filled) ? '#' : '-';
    }
    bar[width + 1] = ']';
    bar[width + 2] = '\0';
    
    return bar;
}

const char *progress_bar_colored(float density) {
    static char bar[64];
    const int width = 20;
    
    int filled = (int)(density * width + 0.5f);
    if (filled > width) filled = width;
    if (filled < 0) filled = 0;
    
    /* Choose color based on density */
    const char *color;
    if (density >= 0.5f) {
        color = "\033[32m";  /* Green */
    } else if (density >= 0.25f) {
        color = "\033[33m";  /* Yellow */
    } else {
        color = "\033[90m";  /* Gray */
    }
    
    int pos = 0;
    pos += sprintf(bar + pos, "[%s", color);
    for (int i = 0; i < filled; i++) {
        bar[pos++] = '#';
    }
    pos += sprintf(bar + pos, "\033[0m");
    for (int i = filled; i < width; i++) {
        bar[pos++] = '-';
    }
    bar[pos++] = ']';
    bar[pos] = '\0';
    
    return bar;
}

void syntax_profile_print(const SyntaxProfile *profile) {
    if (!profile) {
        printf("Syntax Profile: (null)\n");
        return;
    }
    
    printf("╔══════════════════════════════════════════════════════════════╗\n");
    printf("║                    SYNTAX PROFILE REPORT                     ║\n");
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Module: %-54s ║\n", profile->module_name);
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Pipeline syntax:   %5.1f%%  %s    ║\n",
           profile->pipeline_density * 100,
           progress_bar(profile->pipeline_density));
    printf("║ Functional:        %5.1f%%  %s    ║\n",
           profile->functional_density * 100,
           progress_bar(profile->functional_density));
    printf("║ Imperative:        %5.1f%%  %s    ║\n",
           profile->imperative_density * 100,
           progress_bar(profile->imperative_density));
    printf("║ Declarative:       %5.1f%%  %s    ║\n",
           profile->declarative_density * 100,
           progress_bar(profile->declarative_density));
    printf("╠══════════════════════════════════════════════════════════════╣\n");
    printf("║ Dominant style: %-46s ║\n", syntax_style_name(profile->dominant_style));
    if (profile->has_pragma_override) {
        printf("║ Preferred style (pragma): %-36s ║\n", 
               syntax_style_name(profile->preferred_style));
    }
    printf("║ Optimization strategy: %-39s ║\n",
           strategy_name_for_style(profile->has_pragma_override ? 
                                   profile->preferred_style : 
                                   profile->dominant_style));
    printf("╚══════════════════════════════════════════════════════════════╝\n");
}

void syntax_profile_print_detailed(const SyntaxProfile *profile) {
    if (!profile) {
        printf("Syntax Profile: (null)\n");
        return;
    }
    
    /* Print standard report first */
    syntax_profile_print(profile);
    
    /* Print detailed counts */
    printf("\n┌──────────────────────────────────────────────────────────────┐\n");
    printf("│                      DETAILED COUNTS                         │\n");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Total AST nodes: %-44d │\n", profile->total_nodes);
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Pipeline patterns:                                           │\n");
    printf("│   |> operators: %-45d │\n", profile->pipeline_nodes);
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Functional patterns:                                         │\n");
    printf("│   map():    %-49d │\n", profile->map_count);
    printf("│   filter(): %-49d │\n", profile->filter_count);
    printf("│   reduce(): %-49d │\n", profile->reduce_count);
    printf("│   fold():   %-49d │\n", profile->fold_count);
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Imperative patterns:                                         │\n");
    printf("│   for loops:   %-46d │\n", profile->for_count);
    printf("│   while loops: %-46d │\n", profile->while_count);
    printf("│   assignments: %-46d │\n", profile->assignment_count);
    printf("│   mutations:   %-46d │\n", profile->mutation_count);
    printf("└──────────────────────────────────────────────────────────────┘\n");
}

void opt_strategy_print(const OptStrategy *strategy) {
    if (!strategy) {
        printf("Optimization Strategy: (null)\n");
        return;
    }
    
    printf("┌──────────────────────────────────────────────────────────────┐\n");
    printf("│                   OPTIMIZATION STRATEGY                      │\n");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Stream/Pipeline:                                             │\n");
    printf("│   Stream fusion:     %-40s │\n", strategy->enable_stream_fusion ? "ON" : "off");
    printf("│   Lazy evaluation:   %-40s │\n", strategy->enable_lazy_evaluation ? "ON" : "off");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Parallelization:                                             │\n");
    printf("│   Auto-parallel:     %-40s │\n", strategy->enable_parallel ? "ON" : "off");
    printf("│   SIMD:              %-40s │\n", strategy->enable_simd ? "ON" : "off");
    printf("│   Vectorization:     %-40s │\n", strategy->enable_vectorization ? "ON" : "off");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Functional:                                                  │\n");
    printf("│   Memoization:       %-40s │\n", strategy->enable_memoization ? "ON" : "off");
    printf("│   Tail call opt:     %-40s │\n", strategy->enable_tail_call ? "ON" : "off");
    printf("│   Inlining:          %-40s │\n", strategy->enable_inlining ? "ON" : "off");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Loop:                                                        │\n");
    printf("│   Loop opts:         %-40s │\n", strategy->enable_loop_opts ? "ON" : "off");
    printf("│   Unroll:            %-40s │\n", strategy->enable_loop_unroll ? "ON" : "off");
    printf("│   Unroll factor:     %-40d │\n", strategy->unroll_factor);
    printf("│   Loop fusion:       %-40s │\n", strategy->enable_loop_fusion ? "ON" : "off");
    printf("│   Loop interchange:  %-40s │\n", strategy->enable_loop_interchange ? "ON" : "off");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Memory:                                                      │\n");
    printf("│   Allocation opts:   %-40s │\n", strategy->enable_allocation_opts ? "ON" : "off");
    printf("│   Copy elision:      %-40s │\n", strategy->enable_copy_elision ? "ON" : "off");
    printf("└──────────────────────────────────────────────────────────────┘\n");
}

char *syntax_profile_to_json(const SyntaxProfile *profile) {
    if (!profile) {
        return strdup("null");
    }
    
    /* Estimate size needed */
    size_t size = 1024;
    char *json = malloc(size);
    if (!json) return NULL;
    
    int written = snprintf(json, size,
        "{\n"
        "  \"module\": \"%s\",\n"
        "  \"density\": {\n"
        "    \"pipeline\": %.4f,\n"
        "    \"functional\": %.4f,\n"
        "    \"imperative\": %.4f,\n"
        "    \"declarative\": %.4f\n"
        "  },\n"
        "  \"counts\": {\n"
        "    \"total\": %d,\n"
        "    \"pipeline\": %d,\n"
        "    \"functional\": %d,\n"
        "    \"imperative\": %d,\n"
        "    \"map\": %d,\n"
        "    \"filter\": %d,\n"
        "    \"reduce\": %d,\n"
        "    \"for_loops\": %d,\n"
        "    \"while_loops\": %d,\n"
        "    \"assignments\": %d\n"
        "  },\n"
        "  \"dominant_style\": \"%s\",\n"
        "  \"preferred_style\": \"%s\",\n"
        "  \"has_pragma_override\": %s\n"
        "}",
        profile->module_name,
        profile->pipeline_density,
        profile->functional_density,
        profile->imperative_density,
        profile->declarative_density,
        profile->total_nodes,
        profile->pipeline_nodes,
        profile->functional_nodes,
        profile->imperative_nodes,
        profile->map_count,
        profile->filter_count,
        profile->reduce_count,
        profile->for_count,
        profile->while_count,
        profile->assignment_count,
        syntax_style_name(profile->dominant_style),
        syntax_style_name(profile->preferred_style),
        profile->has_pragma_override ? "true" : "false"
    );
    
    if (written >= (int)size) {
        /* Need more space - reallocate */
        free(json);
        size = (size_t)written + 1;
        json = malloc(size);
        if (!json) return NULL;
        snprintf(json, size, "{ \"error\": \"buffer overflow\" }");
    }
    
    return json;
}

/* ======== Comparison & Confidence ======== */

bool syntax_profile_is_style(const SyntaxProfile *profile, SyntaxStyle style) {
    if (!profile) return false;
    
    /* Check pragma override first */
    if (profile->has_pragma_override) {
        return profile->preferred_style == style;
    }
    
    return profile->dominant_style == style;
}

float syntax_profile_confidence(const SyntaxProfile *profile) {
    if (!profile || profile->total_nodes == 0) return 0.0f;
    
    /* Confidence is based on how much the dominant style stands out */
    float max_density = profile->pipeline_density;
    float second_density = 0.0f;
    
    if (profile->functional_density > max_density) {
        second_density = max_density;
        max_density = profile->functional_density;
    } else if (profile->functional_density > second_density) {
        second_density = profile->functional_density;
    }
    
    if (profile->imperative_density > max_density) {
        second_density = max_density;
        max_density = profile->imperative_density;
    } else if (profile->imperative_density > second_density) {
        second_density = profile->imperative_density;
    }
    
    /* Confidence is the gap between dominant and second style */
    /* Plus a base confidence from having significant density */
    float gap = max_density - second_density;
    float base = max_density > 0.3f ? 0.3f : max_density;
    
    float confidence = base + gap * 0.7f;
    if (confidence > 1.0f) confidence = 1.0f;
    
    return confidence;
}

/* ======== IR Generation Mode ======== */

IRGenerationMode ir_mode_from_profile(const SyntaxProfile *profile) {
    if (!profile) {
        return IR_MODE_DEFAULT;
    }
    
    /* Use preferred style if pragma override, otherwise dominant */
    SyntaxStyle style = profile->has_pragma_override ? 
                        profile->preferred_style : 
                        profile->dominant_style;
    
    switch (style) {
        case SYNTAX_STYLE_PIPELINE:
            return IR_MODE_STREAM;
        case SYNTAX_STYLE_FUNCTIONAL:
            return IR_MODE_DATAFLOW;
        case SYNTAX_STYLE_IMPERATIVE:
        case SYNTAX_STYLE_BRACE:
            return IR_MODE_CONTROLFLOW;
        case SYNTAX_STYLE_PYTHON:
            /* Python style uses control-flow with refcount focus */
            return IR_MODE_CONTROLFLOW;
        case SYNTAX_STYLE_DEFAULT:
        default:
            return IR_MODE_DEFAULT;
    }
}

IRGenerationHints *ir_hints_from_profile(const SyntaxProfile *profile) {
    IRGenerationHints *hints = calloc(1, sizeof(IRGenerationHints));
    if (!hints) return NULL;
    
    /* Default balanced priorities */
    hints->fusion_priority = 5;
    hints->inline_priority = 5;
    hints->loop_opt_priority = 5;
    
    if (!profile) {
        hints->mode = IR_MODE_DEFAULT;
        return hints;
    }
    
    /* Determine mode from profile */
    hints->mode = ir_mode_from_profile(profile);
    
    /* Use preferred style if pragma override, otherwise dominant */
    SyntaxStyle style = profile->has_pragma_override ? 
                        profile->preferred_style : 
                        profile->dominant_style;
    
    switch (style) {
        case SYNTAX_STYLE_PIPELINE:
            /* Pipeline/stream-oriented IR hints */
            hints->enable_lazy_streams = true;
            hints->enable_fusion = true;
            hints->enable_demand_driven = true;
            hints->enable_arena_alloc = true;
            hints->enable_stack_promotion = true;
            hints->fusion_priority = 10;
            hints->inline_priority = 7;
            hints->loop_opt_priority = 3;
            break;
            
        case SYNTAX_STYLE_FUNCTIONAL:
            /* Data-flow/SSA-heavy IR hints */
            hints->enable_ssa_promotion = true;
            hints->enable_aggressive_inline = true;
            hints->enable_tail_call_opt = true;
            hints->enable_immutable_default = true;
            hints->enable_stack_promotion = true;
            hints->fusion_priority = 6;
            hints->inline_priority = 10;
            hints->loop_opt_priority = 4;
            break;
            
        case SYNTAX_STYLE_IMPERATIVE:
        case SYNTAX_STYLE_BRACE:
            /* Control-flow/mutable-friendly IR hints */
            hints->enable_mutation = true;
            hints->enable_traditional_loops = true;
            hints->enable_phi_minimization = true;
            hints->fusion_priority = 3;
            hints->inline_priority = 5;
            hints->loop_opt_priority = 10;
            break;
            
        case SYNTAX_STYLE_PYTHON:
            /* Python-style with reference counting focus */
            hints->enable_mutation = true;
            hints->enable_traditional_loops = true;
            hints->enable_refcount = true;
            hints->fusion_priority = 4;
            hints->inline_priority = 6;
            hints->loop_opt_priority = 7;
            break;
            
        case SYNTAX_STYLE_DEFAULT:
        default:
            /* Balanced hints based on what we detect */
            if (profile->pipeline_density > 0.15f) {
                hints->enable_lazy_streams = true;
                hints->enable_fusion = true;
                hints->fusion_priority = 7;
            }
            if (profile->functional_density > 0.15f) {
                hints->enable_ssa_promotion = true;
                hints->enable_tail_call_opt = true;
                hints->inline_priority = 7;
            }
            if (profile->imperative_density > 0.25f) {
                hints->enable_mutation = true;
                hints->enable_traditional_loops = true;
                hints->loop_opt_priority = 7;
            }
            break;
    }
    
    return hints;
}

void ir_hints_free(IRGenerationHints *hints) {
    free(hints);
}

const char *ir_mode_name(IRGenerationMode mode) {
    switch (mode) {
        case IR_MODE_DEFAULT:     return "default";
        case IR_MODE_STREAM:      return "stream-oriented";
        case IR_MODE_DATAFLOW:    return "data-flow";
        case IR_MODE_CONTROLFLOW: return "control-flow";
        default:                  return "unknown";
    }
}

void ir_hints_print(const IRGenerationHints *hints) {
    if (!hints) {
        printf("IR Generation Hints: (null)\n");
        return;
    }
    
    printf("┌──────────────────────────────────────────────────────────────┐\n");
    printf("│                   IR GENERATION HINTS                        │\n");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Mode: %-56s │\n", ir_mode_name(hints->mode));
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Stream-oriented:                                             │\n");
    printf("│   Lazy streams:      %-40s │\n", hints->enable_lazy_streams ? "ON" : "off");
    printf("│   Stream fusion:     %-40s │\n", hints->enable_fusion ? "ON" : "off");
    printf("│   Demand-driven:     %-40s │\n", hints->enable_demand_driven ? "ON" : "off");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Data-flow:                                                   │\n");
    printf("│   SSA promotion:     %-40s │\n", hints->enable_ssa_promotion ? "ON" : "off");
    printf("│   Aggressive inline: %-40s │\n", hints->enable_aggressive_inline ? "ON" : "off");
    printf("│   Tail call opt:     %-40s │\n", hints->enable_tail_call_opt ? "ON" : "off");
    printf("│   Immutable default: %-40s │\n", hints->enable_immutable_default ? "ON" : "off");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Control-flow:                                                │\n");
    printf("│   Mutation enabled:  %-40s │\n", hints->enable_mutation ? "ON" : "off");
    printf("│   Traditional loops: %-40s │\n", hints->enable_traditional_loops ? "ON" : "off");
    printf("│   PHI minimization:  %-40s │\n", hints->enable_phi_minimization ? "ON" : "off");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Memory management:                                           │\n");
    printf("│   Reference count:   %-40s │\n", hints->enable_refcount ? "ON" : "off");
    printf("│   Arena allocation:  %-40s │\n", hints->enable_arena_alloc ? "ON" : "off");
    printf("│   Stack promotion:   %-40s │\n", hints->enable_stack_promotion ? "ON" : "off");
    printf("├──────────────────────────────────────────────────────────────┤\n");
    printf("│ Priorities:                                                  │\n");
    printf("│   Fusion:            %-40d │\n", hints->fusion_priority);
    printf("│   Inlining:          %-40d │\n", hints->inline_priority);
    printf("│   Loop optimization: %-40d │\n", hints->loop_opt_priority);
    printf("└──────────────────────────────────────────────────────────────┘\n");
}
