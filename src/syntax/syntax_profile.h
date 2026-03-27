/*
 * QISC Multi-Syntax Profiling System
 * 
 * Analyzes code syntax patterns to determine dominant coding style
 * and optimize accordingly (pipeline-heavy vs loop-heavy vs functional).
 */

#ifndef QISC_SYNTAX_PROFILE_H
#define QISC_SYNTAX_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include "../parser/ast.h"

/* ======== Syntax Style Enumeration ======== */

typedef enum {
    SYNTAX_STYLE_DEFAULT,     /* No dominant style detected */
    SYNTAX_STYLE_PIPELINE,    /* data |> transform |> collect */
    SYNTAX_STYLE_FUNCTIONAL,  /* map(filter(data, pred), func) */
    SYNTAX_STYLE_IMPERATIVE,  /* for loops, assignments */
    SYNTAX_STYLE_BRACE,       /* C-like braces (neutral) */
    SYNTAX_STYLE_PYTHON,      /* Indentation-based (future) */
} SyntaxStyle;

/* ======== Syntax Profile Structure ======== */

typedef struct {
    /* Module identification */
    char *module_name;
    
    /* Density metrics (0.0 - 1.0) */
    float pipeline_density;    /* % of code using |> */
    float functional_density;  /* % using map/filter/reduce */
    float imperative_density;  /* % using loops/assignments */
    float declarative_density; /* % using DSL constructs */
    
    /* Style determination */
    SyntaxStyle dominant_style;
    SyntaxStyle preferred_style;  /* From pragma override */
    bool has_pragma_override;
    
    /* Raw counts (for detailed analysis) */
    int total_nodes;
    int pipeline_nodes;
    int functional_nodes;
    int imperative_nodes;
    int declarative_nodes;
    
    /* Higher-order function usage */
    int map_count;
    int filter_count;
    int reduce_count;
    int fold_count;
    
    /* Loop counts */
    int for_count;
    int while_count;
    int loop_count;  /* infinite loop constructs */
    
    /* Assignment patterns */
    int assignment_count;
    int mutation_count;  /* in-place modifications */
} SyntaxProfile;

/* ======== Optimization Strategy ======== */

typedef struct {
    /* Stream/pipeline optimizations */
    bool enable_stream_fusion;     /* Fuse pipeline stages */
    bool enable_lazy_evaluation;   /* Delay computation */
    
    /* Parallelization */
    bool enable_parallel;          /* Auto-parallelize */
    bool enable_simd;              /* SIMD vectorization */
    bool enable_vectorization;     /* General vectorization */
    
    /* Functional optimizations */
    bool enable_memoization;       /* Cache results */
    bool enable_tail_call;         /* Tail call optimization */
    bool enable_inlining;          /* Aggressive inlining */
    
    /* Loop optimizations */
    bool enable_loop_opts;         /* General loop optimizations */
    bool enable_loop_unroll;       /* Loop unrolling */
    bool enable_loop_fusion;       /* Fuse adjacent loops */
    bool enable_loop_interchange;  /* Interchange nested loops */
    int unroll_factor;             /* Unrolling factor (0 = auto) */
    
    /* Memory optimizations */
    bool enable_allocation_opts;   /* Stack vs heap */
    bool enable_copy_elision;      /* Avoid unnecessary copies */
} OptStrategy;

/* ======== Syntax Pattern Counts (Internal) ======== */

typedef struct {
    int total;
    int pipeline;
    int functional;
    int imperative;
    int declarative;
    
    /* Detailed functional */
    int map;
    int filter;
    int reduce;
    int fold;
    int zip;
    int take;
    int skip;
    
    /* Detailed imperative */
    int for_loops;
    int while_loops;
    int assignments;
    int mutations;
    
    /* DSL/declarative */
    int pattern_matches;
    int comprehensions;
} SyntaxPatternCounts;

/* ======== Profile Creation & Destruction ======== */

/* Create a new empty syntax profile */
SyntaxProfile *syntax_profile_new(const char *module_name);

/* Free a syntax profile */
void syntax_profile_free(SyntaxProfile *profile);

/* Clone a syntax profile */
SyntaxProfile *syntax_profile_clone(const SyntaxProfile *profile);

/* ======== Pragma Parsing ======== */

/*
 * Parse syntax-related pragmas:
 *   #pragma style:pipeline
 *   #pragma style:functional
 *   #pragma style:imperative
 *   #pragma syntax_profile: pipelines:70%, functional:20%, imperative:10%
 */
SyntaxProfile *parse_syntax_pragma(const char *pragma_name, const char *pragma_value);

/*
 * Apply pragma overrides to an existing profile
 */
void syntax_profile_apply_pragma(SyntaxProfile *profile, 
                                  const char *pragma_name,
                                  const char *pragma_value);

/* ======== AST Analysis ======== */

/*
 * Analyze an AST to detect syntax patterns and build a profile.
 * This is the main entry point for syntax detection.
 */
SyntaxProfile *analyze_syntax(AstNode *ast, const char *module_name);

/*
 * Count syntax patterns in an AST subtree (recursive).
 * Results are accumulated in the counts structure.
 */
void count_syntax_patterns(AstNode *node, SyntaxPatternCounts *counts);

/*
 * Merge two syntax profiles (for multi-module analysis)
 */
SyntaxProfile *syntax_profile_merge(const SyntaxProfile *a, const SyntaxProfile *b);

/* ======== Optimization Strategy ======== */

/*
 * Generate an optimization strategy based on the syntax profile.
 * This determines which optimizations are most effective.
 */
OptStrategy *strategy_for_syntax(const SyntaxProfile *profile);

/* Create a default optimization strategy */
OptStrategy *opt_strategy_new(void);

/* Free an optimization strategy */
void opt_strategy_free(OptStrategy *strategy);

/*
 * Get a human-readable name for a syntax style
 */
const char *syntax_style_name(SyntaxStyle style);

/*
 * Get a human-readable strategy description
 */
const char *strategy_name_for_style(SyntaxStyle style);

/* ======== Reporting ======== */

/*
 * Generate a progress bar string for a density value.
 * Returns a pointer to a static buffer (not thread-safe).
 */
const char *progress_bar(float density);

/*
 * Generate a colored progress bar (with ANSI codes).
 * Returns a pointer to a static buffer (not thread-safe).
 */
const char *progress_bar_colored(float density);

/*
 * Print a syntax profile report to stdout.
 */
void syntax_profile_print(const SyntaxProfile *profile);

/*
 * Print a detailed syntax profile report with counts.
 */
void syntax_profile_print_detailed(const SyntaxProfile *profile);

/*
 * Print optimization strategy.
 */
void opt_strategy_print(const OptStrategy *strategy);

/*
 * Generate a JSON representation of the profile.
 * Caller must free the returned string.
 */
char *syntax_profile_to_json(const SyntaxProfile *profile);

/* ======== Comparison & Thresholds ======== */

/* Thresholds for style classification */
#define SYNTAX_PIPELINE_THRESHOLD  0.30f  /* 30% pipeline nodes -> pipeline style */
#define SYNTAX_FUNCTIONAL_THRESHOLD 0.25f /* 25% functional -> functional style */
#define SYNTAX_IMPERATIVE_THRESHOLD 0.40f /* 40% imperative -> imperative style */

/*
 * Check if a profile indicates a specific dominant style
 */
bool syntax_profile_is_style(const SyntaxProfile *profile, SyntaxStyle style);

/*
 * Get confidence level for the dominant style determination (0.0 - 1.0)
 */
float syntax_profile_confidence(const SyntaxProfile *profile);

/* ======== IR Generation Mode ======== */

/*
 * IR generation modes based on syntax analysis.
 * Different syntax styles benefit from different IR representations.
 */
typedef enum {
    IR_MODE_DEFAULT,      /* Standard balanced IR */
    IR_MODE_STREAM,       /* Stream-oriented IR for pipelines */
    IR_MODE_DATAFLOW,     /* Data-flow IR for functional code */
    IR_MODE_CONTROLFLOW,  /* Control-flow IR for imperative code */
} IRGenerationMode;

/*
 * IR generation hints based on syntax profile.
 * These guide the code generator to produce optimal IR for the style.
 */
typedef struct {
    IRGenerationMode mode;
    
    /* Stream-oriented hints (pipeline) */
    bool enable_lazy_streams;     /* Generate lazy iterators */
    bool enable_fusion;           /* Apply stream fusion */
    bool enable_demand_driven;    /* Pull-based evaluation */
    
    /* Data-flow hints (functional) */
    bool enable_ssa_promotion;    /* Aggressive SSA promotion */
    bool enable_aggressive_inline;/* Inline pure functions */
    bool enable_tail_call_opt;    /* Transform tail recursion */
    bool enable_immutable_default;/* Default to immutable values */
    
    /* Control-flow hints (imperative) */
    bool enable_mutation;         /* Allow in-place mutation */
    bool enable_traditional_loops;/* Standard loop constructs */
    bool enable_phi_minimization; /* Minimize PHI nodes */
    
    /* Memory management hints */
    bool enable_refcount;         /* Reference counting focus */
    bool enable_arena_alloc;      /* Arena allocation for pipelines */
    bool enable_stack_promotion;  /* Promote heap to stack */
    
    /* Optimization priorities */
    int fusion_priority;          /* 0-10: priority for fusion opts */
    int inline_priority;          /* 0-10: priority for inlining */
    int loop_opt_priority;        /* 0-10: priority for loop opts */
} IRGenerationHints;

/*
 * Determine IR generation mode from syntax profile.
 */
IRGenerationMode ir_mode_from_profile(const SyntaxProfile *profile);

/*
 * Get detailed IR generation hints from syntax profile.
 * These hints guide the code generator to produce optimal IR.
 */
IRGenerationHints *ir_hints_from_profile(const SyntaxProfile *profile);

/*
 * Free IR generation hints.
 */
void ir_hints_free(IRGenerationHints *hints);

/*
 * Get human-readable name for IR mode.
 */
const char *ir_mode_name(IRGenerationMode mode);

/*
 * Print IR generation hints.
 */
void ir_hints_print(const IRGenerationHints *hints);

#endif /* QISC_SYNTAX_PROFILE_H */
