/*
 * QISC Living IR Mutation System
 *
 * The core innovation of QISC: IR that actually restructures itself
 * based on profile data. This transforms static compilation into an
 * evolutionary process where code adapts to observed execution patterns.
 *
 * Key transformations:
 * - Hot path inlining: Inline frequently-called functions at hot call sites
 * - Cold code outlining: Extract rarely-executed code to separate functions
 * - Loop restructuring: Unroll, tile, or add prefetch based on iteration counts
 * - Block reordering: Optimize basic block layout for cache efficiency
 *
 * Philosophy: The IR is alive - it learns, adapts, and evolves.
 */

#ifndef QISC_LIVING_IR_H
#define QISC_LIVING_IR_H

#include "ir_metadata.h"
#include "../profile/profile.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <stdbool.h>
#include <stdint.h>

/* Thresholds for mutation decisions */
#define LIVING_IR_HOT_CALL_THRESHOLD     1000     /* Min calls to consider hot */
#define LIVING_IR_COLD_FREQ_THRESHOLD    0.001    /* <0.1% execution = cold */
#define LIVING_IR_INLINE_SIZE_LIMIT      100      /* Max instructions to inline */
#define LIVING_IR_UNROLL_SMALL_LOOP      4        /* Full unroll if < 4 iterations */
#define LIVING_IR_UNROLL_MEDIUM_LOOP     16       /* 4x unroll if < 16 iterations */
#define LIVING_IR_PREFETCH_THRESHOLD     1000     /* Add prefetch if > 1000 iterations */

/* Inlining decision */
typedef struct {
    const char *caller;
    const char *callee;
    uint64_t call_count;
    double hotness_score;
    int callee_size;
    bool should_inline;
    const char *reason;
} InlineCandidate;

/* Cold block for outlining */
typedef struct {
    LLVMBasicBlockRef block;
    const char *function_name;
    double execution_freq;
    int instruction_count;
    bool can_outline;
    const char *reason;
} ColdBlock;

/* Loop for restructuring */
typedef struct {
    const char *location;
    LLVMBasicBlockRef header;
    double avg_iterations;
    int suggested_unroll;
    bool should_prefetch;
    bool should_tile;
    int tile_size;
} LoopMutation;

/* Block reorder entry */
typedef struct {
    LLVMBasicBlockRef block;
    double execution_freq;
    int original_order;
    int new_order;
    bool is_hot;
} BlockOrder;

/* Maximum tracked items */
#define LIVING_IR_MAX_INLINE_CANDIDATES 256
#define LIVING_IR_MAX_COLD_BLOCKS       512
#define LIVING_IR_MAX_LOOP_MUTATIONS    256
#define LIVING_IR_MAX_BLOCK_REORDERS    1024

/* Living IR metrics - track what we've done */
typedef struct {
    int functions_analyzed;
    int functions_inlined;
    int inline_candidates_found;
    int cold_blocks_found;
    int cold_blocks_outlined;
    int loops_analyzed;
    int loops_restructured;
    int loops_unrolled;
    int loops_prefetched;
    int blocks_reordered;
    double estimated_speedup;
    uint64_t code_size_delta;
} LivingIRMetrics;

/* Living IR context */
typedef struct {
    /* LLVM module to mutate */
    LLVMModuleRef module;
    LLVMContextRef context;
    
    /* Profile data driving mutations */
    QiscProfile *profile;
    
    /* IR metadata for additional hints */
    IRMetadata *metadata;
    
    /* Tracked candidates and mutations */
    InlineCandidate inline_candidates[LIVING_IR_MAX_INLINE_CANDIDATES];
    int inline_candidate_count;
    
    ColdBlock cold_blocks[LIVING_IR_MAX_COLD_BLOCKS];
    int cold_block_count;
    
    LoopMutation loop_mutations[LIVING_IR_MAX_LOOP_MUTATIONS];
    int loop_mutation_count;
    
    BlockOrder block_orders[LIVING_IR_MAX_BLOCK_REORDERS];
    int block_order_count;
    
    /* Mutation metrics */
    LivingIRMetrics metrics;
    
    /* Configuration */
    struct {
        bool enable_inlining;
        bool enable_outlining;
        bool enable_loop_restructure;
        bool enable_block_reorder;
        bool aggressive_mode;
        int max_inline_depth;
        double min_confidence;
    } config;
    
    /* Error handling */
    bool had_error;
    char error_msg[512];
} LivingIR;

/* ======== Creation and Destruction ======== */

/* Create Living IR context with module and profile data */
LivingIR *living_ir_create(LLVMModuleRef module, QiscProfile *profile);

/* Create with additional IR metadata */
LivingIR *living_ir_create_with_metadata(LLVMModuleRef module, 
                                          QiscProfile *profile,
                                          IRMetadata *metadata);

/* Destroy Living IR context and free resources */
void living_ir_destroy(LivingIR *ir);

/* ======== Configuration ======== */

/* Enable/disable specific mutation types */
void living_ir_set_inlining(LivingIR *ir, bool enable);
void living_ir_set_outlining(LivingIR *ir, bool enable);
void living_ir_set_loop_restructure(LivingIR *ir, bool enable);
void living_ir_set_block_reorder(LivingIR *ir, bool enable);

/* Set aggressive mode (more mutations, potentially larger code) */
void living_ir_set_aggressive(LivingIR *ir, bool aggressive);

/* Set minimum profile confidence to trigger mutations */
void living_ir_set_min_confidence(LivingIR *ir, double confidence);

/* ======== Analysis Phase ======== */

/* Analyze module to identify mutation candidates (doesn't modify IR) */
void living_ir_analyze(LivingIR *ir);

/* Get analysis results */
int living_ir_get_inline_candidates(LivingIR *ir, InlineCandidate **candidates);
int living_ir_get_cold_blocks(LivingIR *ir, ColdBlock **blocks);
int living_ir_get_loop_mutations(LivingIR *ir, LoopMutation **mutations);

/* ======== Mutation Phase ======== */

/* Apply all mutations based on analysis (modifies IR) */
void living_ir_evolve(LivingIR *ir);

/* Apply specific mutation types */
void living_ir_inline_hot_paths(LivingIR *ir);
void living_ir_outline_cold_paths(LivingIR *ir);
void living_ir_restructure_loops(LivingIR *ir);
void living_ir_reorder_blocks(LivingIR *ir);

/* ======== Advanced Mutations ======== */

/* Clone hot functions for specialization */
void living_ir_clone_hot_functions(LivingIR *ir);

/* Devirtualize calls based on profile (single target = direct call) */
void living_ir_devirtualize_calls(LivingIR *ir);

/* Merge rarely-called small functions */
void living_ir_merge_cold_functions(LivingIR *ir);

/* Insert prefetch instructions for predictable memory access */
void living_ir_insert_prefetch(LivingIR *ir);

/* ======== Metrics and Reporting ======== */

/* Get mutation metrics */
LivingIRMetrics living_ir_get_metrics(LivingIR *ir);

/* Print summary of mutations performed */
void living_ir_print_summary(LivingIR *ir);

/* Dump detailed mutation log */
void living_ir_dump(LivingIR *ir);

/* ======== Verification ======== */

/* Verify IR is still valid after mutations */
bool living_ir_verify(LivingIR *ir);

/* Get error message if verification failed */
const char *living_ir_get_error(LivingIR *ir);

/* ======== Utilities ======== */

/* Calculate hotness score for a function */
double living_ir_calc_hotness(LivingIR *ir, const char *func_name);

/* Check if a function should be inlined at a call site */
bool living_ir_should_inline(LivingIR *ir, const char *caller, 
                              const char *callee, uint64_t call_count);

/* Check if a block is cold enough to outline */
bool living_ir_is_cold_block(LivingIR *ir, LLVMBasicBlockRef block,
                              double freq);

/* Get suggested unroll factor for a loop */
int living_ir_suggest_unroll(LivingIR *ir, const char *loop_location);

#endif /* QISC_LIVING_IR_H */
