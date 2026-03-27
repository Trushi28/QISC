/*
 * QISC Stage Fusion Optimization System
 *
 * Optimizes pipeline operations by detecting and fusing adjacent stages
 * into single-pass operations for improved performance.
 *
 * Key optimizations:
 *   - filter |> map    → single pass (no intermediate allocation)
 *   - map |> map       → composed function
 *   - filter |> filter → combined predicate (short-circuit AND)
 *   - map |> reduce    → fused map-reduce
 *   - filter |> reduce → conditional accumulation
 *   - take |> filter   → early exit filter
 */

#ifndef QISC_OPTIMIZATION_FUSION_H
#define QISC_OPTIMIZATION_FUSION_H

#include "../parser/ast.h"
#include "../codegen/codegen.h"
#include <stdbool.h>
#include <stdint.h>

/* Maximum fusion opportunities to track per pipeline */
#define FUSION_MAX_OPPORTUNITIES 100

/* Maximum chain length for multi-stage fusion */
#define FUSION_MAX_CHAIN_LENGTH 16

/* ============================================================================
 * Fusion Patterns
 * ============================================================================ */

typedef enum {
    FUSION_NONE = 0,
    
    /* Two-stage fusions */
    FUSION_FILTER_MAP,      /* filter |> map → single pass with conditional yield */
    FUSION_MAP_MAP,         /* map |> map → composed function f(g(x)) */
    FUSION_FILTER_FILTER,   /* filter |> filter → combined predicate (p1 && p2) */
    FUSION_MAP_REDUCE,      /* map |> reduce → map-reduce (reduce with mapped values) */
    FUSION_FILTER_REDUCE,   /* filter |> reduce → conditional accumulation */
    FUSION_TAKE_FILTER,     /* take |> filter → early exit filter with counter */
    FUSION_FILTER_TAKE,     /* filter |> take → filtered take (early exit) */
    FUSION_MAP_FILTER,      /* map |> filter → transform then test */
    FUSION_TAKE_MAP,        /* take |> map → limited map */
    FUSION_SKIP_FILTER,     /* skip |> filter → skip then filter */
    FUSION_SKIP_MAP,        /* skip |> map → skip then map */
    
    /* Three-stage fusions (advanced patterns) */
    FUSION_FILTER_MAP_REDUCE,   /* filter |> map |> reduce */
    FUSION_MAP_FILTER_REDUCE,   /* map |> filter |> reduce */
    FUSION_FILTER_FILTER_MAP,   /* filter |> filter |> map */
    FUSION_MAP_MAP_REDUCE,      /* map |> map |> reduce */
    
    /* Sentinel for iteration */
    FUSION_PATTERN_COUNT
} FusionPattern;

/* Pipeline stage types for fusion analysis */
typedef enum {
    STAGE_UNKNOWN = 0,
    STAGE_FILTER,       /* filter(pred) - keep elements matching predicate */
    STAGE_MAP,          /* map(fn) - transform each element */
    STAGE_REDUCE,       /* reduce(init, fn) - fold into single value */
    STAGE_TAKE,         /* take(n) - limit to first n elements */
    STAGE_SKIP,         /* skip(n) - skip first n elements */
    STAGE_FLAT_MAP,     /* flatMap(fn) - map and flatten */
    STAGE_COLLECT,      /* collect() - materialize to array */
    STAGE_FOREACH,      /* forEach(fn) - execute for side effects */
    STAGE_ANY,          /* any(pred) - short-circuit true if any match */
    STAGE_ALL,          /* all(pred) - short-circuit false if any fails */
    STAGE_FIND,         /* find(pred) - first matching element */
    STAGE_COUNT,        /* count() - element count */
    STAGE_SUM,          /* sum() - numeric sum (specialized reduce) */
    STAGE_DISTINCT,     /* distinct() - unique elements only */
    STAGE_SORTED,       /* sorted() - sort elements */
    STAGE_GROUPED,      /* grouped(fn) - group by key function */
} PipelineStageType;

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Represents a single stage in a pipeline */
typedef struct PipelineStage {
    PipelineStageType type;
    AstNode *node;              /* Original AST node for this stage */
    AstNode *lambda;            /* Lambda/function argument */
    AstNode *initial_value;     /* For reduce: initial accumulator value */
    int64_t count_arg;          /* For take/skip: element count */
    struct PipelineStage *next; /* Next stage in pipeline */
    struct PipelineStage *prev; /* Previous stage (for backwards traversal) */
    
    /* Source location for diagnostics */
    int line;
    int column;
} PipelineStage;

/* Represents a detected fusion opportunity */
typedef struct {
    FusionPattern pattern;
    
    /* Stages being fused (2 or 3 depending on pattern) */
    PipelineStage *first_stage;
    PipelineStage *second_stage;
    PipelineStage *third_stage;     /* NULL for two-stage fusions */
    
    /* Original AST nodes */
    AstNode *first_node;
    AstNode *second_node;
    AstNode *third_node;
    
    /* Result of fusion transformation */
    AstNode *fused_result;
    PipelineStage *fused_stage;
    
    /* Cost/benefit analysis */
    int estimated_passes_saved;     /* Number of data passes eliminated */
    int estimated_allocs_saved;     /* Intermediate allocations avoided */
    double estimated_speedup;       /* Multiplicative speedup factor */
    bool requires_reanalysis;       /* True if fusion may enable further fusions */
} FusionOpportunity;

/* Pipeline representation for optimization */
typedef struct {
    PipelineStage *head;            /* First stage */
    PipelineStage *tail;            /* Last stage */
    int stage_count;
    AstNode *source;                /* Data source (array, range, etc.) */
    
    /* Optimization state */
    bool analyzed;
    bool optimized;
    int fusion_count;               /* Number of fusions applied */
} Pipeline;

/* Fusion optimizer state */
typedef struct {
    /* Detection results */
    FusionOpportunity opportunities[FUSION_MAX_OPPORTUNITIES];
    int opportunity_count;
    
    /* Optimization settings */
    bool aggressive_fusion;         /* Enable speculative fusions */
    bool preserve_debug_info;       /* Keep source mappings for debugging */
    int max_fusion_depth;           /* Max chained stages to consider */
    
    /* Statistics */
    int pipelines_analyzed;
    int fusions_applied;
    int passes_eliminated;
    int allocs_avoided;
    
    /* Error state */
    bool had_error;
    char error_msg[256];
} FusionOptimizer;

/* Aggregated metrics across all fusion operations */
typedef struct {
    /* Per-pattern counts */
    int filter_map_fusions;
    int map_map_fusions;
    int filter_filter_fusions;
    int map_reduce_fusions;
    int filter_reduce_fusions;
    int take_filter_fusions;
    int other_fusions;
    
    /* Aggregate metrics */
    int total_fusions;
    int total_passes_eliminated;
    int total_allocs_avoided;
    double estimated_speedup;       /* Overall estimated improvement */
    
    /* Pipeline statistics */
    int pipelines_analyzed;
    int pipelines_optimized;
    int max_chain_fused;            /* Longest fusion chain */
    
    /* Timing */
    double analysis_time_ms;
    double transform_time_ms;
} FusionMetrics;

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

/* Initialize fusion optimizer with default settings */
void fusion_optimizer_init(FusionOptimizer *opt);

/* Initialize with custom settings */
void fusion_optimizer_init_ex(FusionOptimizer *opt, bool aggressive, 
                               bool preserve_debug, int max_depth);

/* Free optimizer resources */
void fusion_optimizer_free(FusionOptimizer *opt);

/* Reset optimizer state for new analysis pass */
void fusion_optimizer_reset(FusionOptimizer *opt);

/* ============================================================================
 * Pipeline Analysis
 * ============================================================================ */

/* Extract pipeline structure from AST node */
Pipeline *extract_pipeline(AstNode *node);

/* Free pipeline structure */
void pipeline_free(Pipeline *pipeline);

/* Identify the type of a pipeline stage from its AST */
PipelineStageType identify_stage_type(AstNode *stage);

/* Get human-readable name for stage type */
const char *stage_type_name(PipelineStageType type);

/* Check if a stage is fusible (has no side effects, etc.) */
bool is_stage_fusible(PipelineStage *stage);

/* Check if two stages can be fused together */
bool can_fuse_stages(PipelineStage *first, PipelineStage *second);

/* ============================================================================
 * Fusion Detection
 * ============================================================================ */

/* Check if two adjacent stages match a fusion pattern */
FusionPattern check_fusible(AstNode *first, AstNode *second);

/* Extended check including three-stage patterns */
FusionPattern check_fusible_extended(PipelineStage *first, 
                                      PipelineStage *second,
                                      PipelineStage *third);

/* Detect all fusion opportunities in a pipeline */
int detect_fusion_opportunities(AstNode *pipeline, 
                                 FusionOpportunity *out, 
                                 int max_out);

/* Detect opportunities in already-extracted pipeline structure */
int detect_pipeline_opportunities(Pipeline *pipeline,
                                   FusionOpportunity *out,
                                   int max_out);

/* Analyze entire AST for pipeline fusion opportunities */
int analyze_ast_for_fusion(FusionOptimizer *opt, AstNode *ast);

/* Get human-readable name for fusion pattern */
const char *fusion_pattern_name(FusionPattern pattern);

/* ============================================================================
 * Fusion Transformations
 * ============================================================================ */

/* Apply a single fusion transformation */
AstNode *apply_fusion(FusionOpportunity *opportunity);

/* Specific fusion implementations */
AstNode *fuse_filter_map(AstNode *filter, AstNode *map);
AstNode *fuse_map_map(AstNode *map1, AstNode *map2);
AstNode *fuse_filter_filter(AstNode *filter1, AstNode *filter2);
AstNode *fuse_map_reduce(AstNode *map, AstNode *reduce);
AstNode *fuse_filter_reduce(AstNode *filter, AstNode *reduce);
AstNode *fuse_take_filter(AstNode *take, AstNode *filter);
AstNode *fuse_filter_take(AstNode *filter, AstNode *take);
AstNode *fuse_map_filter(AstNode *map, AstNode *filter);

/* Three-stage fusions */
AstNode *fuse_filter_map_reduce(AstNode *filter, AstNode *map, AstNode *reduce);
AstNode *fuse_map_filter_reduce(AstNode *map, AstNode *filter, AstNode *reduce);

/* Replace stages in AST with fused result */
void replace_stages(AstNode *ast, FusionOpportunity *opp, AstNode *fused);

/* Replace stages in pipeline structure */
void replace_pipeline_stages(Pipeline *pipeline, FusionOpportunity *opp,
                              PipelineStage *fused_stage);

/* ============================================================================
 * AST Construction Helpers
 * ============================================================================ */

/* Create a lambda node for fused operations */
AstNode *ast_create_lambda(void);

/* Create an identifier node */
AstNode *ast_create_ident(const char *name);

/* Create a function call node */
AstNode *ast_create_call(AstNode *callee, AstNode *arg);

/* Create a call with multiple arguments */
AstNode *ast_create_call_n(AstNode *callee, AstNode **args, int count);

/* Create an if statement node */
AstNode *ast_create_if(void);

/* Create a binary operation node */
AstNode *ast_create_binary_op(BinaryOp op, AstNode *left, AstNode *right);

/* Create a yield/give expression (for generators) */
AstNode *ast_create_yield(AstNode *value);

/* Clone an AST node (deep copy) */
AstNode *ast_clone(AstNode *node);

/* ============================================================================
 * Optimization Pipeline
 * ============================================================================ */

/* Apply all detected fusion optimizations to an AST */
void apply_fusion_optimizations(AstNode *ast);

/* Apply with custom optimizer settings */
void apply_fusion_optimizations_ex(AstNode *ast, FusionOptimizer *opt);

/* Iterative optimization until no more opportunities found */
int apply_fusion_until_fixed_point(AstNode *ast, int max_iterations);

/* ============================================================================
 * Code Generation Support
 * ============================================================================ */

/* Generate LLVM IR for a fused filter-map operation */
LLVMValueRef emit_fused_filter_map(Codegen *cg, FusionOpportunity *fused);

/* Generate LLVM IR for a fused map-map operation */
LLVMValueRef emit_fused_map_map(Codegen *cg, FusionOpportunity *fused);

/* Generate LLVM IR for a fused filter-filter operation */
LLVMValueRef emit_fused_filter_filter(Codegen *cg, FusionOpportunity *fused);

/* Generate LLVM IR for a fused map-reduce operation */
LLVMValueRef emit_fused_map_reduce(Codegen *cg, FusionOpportunity *fused);

/* Generate LLVM IR for a fused filter-reduce operation */
LLVMValueRef emit_fused_filter_reduce(Codegen *cg, FusionOpportunity *fused);

/* Generate LLVM IR for any fused operation (dispatcher) */
LLVMValueRef emit_fused_operation(Codegen *cg, FusionOpportunity *fused);

/* ============================================================================
 * Metrics and Reporting
 * ============================================================================ */

/* Get current fusion metrics */
FusionMetrics get_fusion_metrics(void);

/* Reset global metrics */
void reset_fusion_metrics(void);

/* Print detailed fusion optimization report */
void print_fusion_report(void);

/* Print compact summary (single line) */
void print_fusion_summary(void);

/* Dump detected opportunities (for debugging) */
void dump_fusion_opportunities(FusionOpportunity *opportunities, int count);

/* ============================================================================
 * Cost Model
 * ============================================================================ */

/* Estimate cost/benefit of a fusion opportunity */
double estimate_fusion_benefit(FusionOpportunity *opp);

/* Check if fusion is profitable (benefit > overhead) */
bool is_fusion_profitable(FusionOpportunity *opp);

/* Estimate memory savings from avoiding intermediate allocation */
size_t estimate_memory_saved(FusionOpportunity *opp, size_t input_size);

#endif /* QISC_OPTIMIZATION_FUSION_H */
