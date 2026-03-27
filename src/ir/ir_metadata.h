/*
 * QISC IR Metadata System
 *
 * Enables "Living IR" self-awareness: IR nodes carry metadata about
 * their execution characteristics to guide optimization.
 */

#ifndef QISC_IR_METADATA_H
#define QISC_IR_METADATA_H

#include <stdbool.h>
#include <stdint.h>

/* Data flow pattern types */
typedef enum {
    DATAFLOW_UNKNOWN,
    DATAFLOW_LINEAR,         /* Data flows through in one direction */
    DATAFLOW_ACCUMULATE,     /* Values accumulate (reduce pattern) */
    DATAFLOW_BROADCAST,      /* One value used many times */
    DATAFLOW_SCATTER,        /* One value split to many */
    DATAFLOW_GATHER,         /* Many values combined to one */
} DataFlowPattern;

/* Memory access patterns */
typedef enum {
    MEMORY_UNKNOWN,
    MEMORY_SEQUENTIAL,       /* Linear access pattern */
    MEMORY_STRIDED,          /* Regular stride access */
    MEMORY_RANDOM,           /* Unpredictable access */
    MEMORY_LOCALIZED,        /* Repeated access to same region */
} MemoryBehavior;

/* Pattern classification for optimization */
typedef enum {
    PATTERN_NONE,
    PATTERN_MAP,             /* Transform each element */
    PATTERN_FILTER,          /* Select elements by predicate */
    PATTERN_REDUCE,          /* Combine elements to one */
    PATTERN_SCAN,            /* Running accumulation */
    PATTERN_STATEMACHINE,    /* State transitions */
    PATTERN_PIPELINE,        /* Chain of transformations */
} CodePattern;

/* Pattern detection result */
typedef struct {
    CodePattern pattern;
    double confidence;      /* 0.0 to 1.0 */
    const char *description;
    
    /* Pattern-specific info */
    union {
        struct { int depth; } nested_loops;
        struct { int stage_count; } pipeline;
        struct { int state_count; } state_machine;
        struct { bool has_identity; } reduce;
    } details;
} PatternMatch;

/* IR node metadata */
typedef struct {
    /* Execution frequency (from profiling) */
    uint64_t execution_count;
    double execution_percentage;  /* % of total program time */
    
    /* Data flow classification */
    DataFlowPattern data_flow;
    
    /* Memory behavior */
    MemoryBehavior memory_behavior;
    
    /* Branch predictability (0.0 = random, 1.0 = always same) */
    double branch_predictability;
    
    /* Detected high-level pattern */
    CodePattern pattern;
    
    /* Optimization hints (computed from above) */
    struct {
        bool should_inline;
        bool should_vectorize;
        bool should_parallelize;
        bool should_unroll;
        int unroll_factor;
        bool is_hot_path;
        bool is_cold_path;
    } hints;
    
    /* Profile confidence (0.0 = no data, 1.0 = highly confident) */
    double confidence;
} IRMetadata;

/* Initialize metadata with defaults */
void ir_metadata_init(IRMetadata *meta);

/* Update metadata from profile data */
void ir_metadata_from_profile(IRMetadata *meta, uint64_t call_count, 
                              uint64_t total_calls, double time_percentage);

/* Detect data flow pattern from IR structure */
DataFlowPattern ir_detect_dataflow(void *ir_node);

/* Detect memory behavior from access patterns */
MemoryBehavior ir_detect_memory(void *ir_node);

/* Detect high-level code pattern */
CodePattern ir_detect_pattern(void *ir_node);

/* Detect patterns in AST node (returns best match) */
PatternMatch ir_detect_ast_pattern(void *ast_node);

/* Get optimization recommendation for pattern */
const char* ir_pattern_optimization(CodePattern pattern);

/* Compute optimization hints from all metadata */
void ir_metadata_compute_hints(IRMetadata *meta);

/* Merge metadata from multiple runs */
void ir_metadata_merge(IRMetadata *target, const IRMetadata *source);

/* Print metadata for debugging */
void ir_metadata_dump(const IRMetadata *meta);

#endif /* QISC_IR_METADATA_H */
