/*
 * QISC IR Metadata System Implementation
 *
 * Implements "Living IR" self-awareness for optimization guidance.
 */

#include "ir_metadata.h"
#include <stdio.h>
#include <string.h>

/* Thresholds for hot/cold path detection */
#define HOT_PATH_THRESHOLD 10.0    /* > 10% of execution time */
#define COLD_PATH_THRESHOLD 0.1    /* < 0.1% of execution time */

/* Confidence thresholds */
#define MIN_CALLS_FOR_CONFIDENCE 100
#define HIGH_CONFIDENCE_CALLS 10000

/* Unroll factor heuristics */
#define DEFAULT_UNROLL_FACTOR 4
#define MAX_UNROLL_FACTOR 16

/* Helper function to convert pattern to string */
static const char *dataflow_to_string(DataFlowPattern pattern) {
    switch (pattern) {
        case DATAFLOW_UNKNOWN:    return "unknown";
        case DATAFLOW_LINEAR:     return "linear";
        case DATAFLOW_ACCUMULATE: return "accumulate";
        case DATAFLOW_BROADCAST:  return "broadcast";
        case DATAFLOW_SCATTER:    return "scatter";
        case DATAFLOW_GATHER:     return "gather";
        default:                  return "invalid";
    }
}

static const char *memory_to_string(MemoryBehavior behavior) {
    switch (behavior) {
        case MEMORY_UNKNOWN:    return "unknown";
        case MEMORY_SEQUENTIAL: return "sequential";
        case MEMORY_STRIDED:    return "strided";
        case MEMORY_RANDOM:     return "random";
        case MEMORY_LOCALIZED:  return "localized";
        default:                return "invalid";
    }
}

static const char *pattern_to_string(CodePattern pattern) {
    switch (pattern) {
        case PATTERN_NONE:         return "none";
        case PATTERN_MAP:          return "map";
        case PATTERN_FILTER:       return "filter";
        case PATTERN_REDUCE:       return "reduce";
        case PATTERN_SCAN:         return "scan";
        case PATTERN_STATEMACHINE: return "statemachine";
        case PATTERN_PIPELINE:     return "pipeline";
        default:                   return "invalid";
    }
}

void ir_metadata_init(IRMetadata *meta) {
    if (!meta) return;
    
    memset(meta, 0, sizeof(IRMetadata));
    
    meta->execution_count = 0;
    meta->execution_percentage = 0.0;
    meta->data_flow = DATAFLOW_UNKNOWN;
    meta->memory_behavior = MEMORY_UNKNOWN;
    meta->branch_predictability = 0.5;  /* Assume 50/50 by default */
    meta->pattern = PATTERN_NONE;
    
    meta->hints.should_inline = false;
    meta->hints.should_vectorize = false;
    meta->hints.should_parallelize = false;
    meta->hints.should_unroll = false;
    meta->hints.unroll_factor = 0;
    meta->hints.is_hot_path = false;
    meta->hints.is_cold_path = false;
    
    meta->confidence = 0.0;
}

void ir_metadata_from_profile(IRMetadata *meta, uint64_t call_count,
                              uint64_t total_calls, double time_percentage) {
    if (!meta) return;
    
    meta->execution_count = call_count;
    meta->execution_percentage = time_percentage;
    
    /* Determine hot/cold paths based on time percentage */
    meta->hints.is_hot_path = (time_percentage > HOT_PATH_THRESHOLD);
    meta->hints.is_cold_path = (time_percentage < COLD_PATH_THRESHOLD);
    
    /* Calculate confidence based on sample size */
    if (total_calls == 0) {
        meta->confidence = 0.0;
    } else if (call_count < MIN_CALLS_FOR_CONFIDENCE) {
        /* Low confidence for small sample sizes */
        meta->confidence = (double)call_count / MIN_CALLS_FOR_CONFIDENCE;
    } else if (call_count >= HIGH_CONFIDENCE_CALLS) {
        /* High confidence for large sample sizes */
        meta->confidence = 1.0;
    } else {
        /* Linear interpolation between thresholds */
        meta->confidence = 0.5 + 0.5 * 
            ((double)(call_count - MIN_CALLS_FOR_CONFIDENCE) /
             (HIGH_CONFIDENCE_CALLS - MIN_CALLS_FOR_CONFIDENCE));
    }
}

DataFlowPattern ir_detect_dataflow(void *ir_node) {
    /* Placeholder: actual implementation would analyze IR structure */
    if (!ir_node) return DATAFLOW_UNKNOWN;
    
    /* 
     * In a full implementation, we would:
     * 1. Analyze def-use chains
     * 2. Look at operand relationships
     * 3. Check for reduction patterns (same target accumulation)
     * 4. Check for broadcast patterns (same source, multiple uses)
     * 5. Analyze phi nodes for gather patterns
     */
    
    return DATAFLOW_UNKNOWN;
}

MemoryBehavior ir_detect_memory(void *ir_node) {
    /* Placeholder: actual implementation would analyze memory accesses */
    if (!ir_node) return MEMORY_UNKNOWN;
    
    /*
     * In a full implementation, we would:
     * 1. Analyze address calculations in load/store
     * 2. Check for base + index patterns (sequential)
     * 3. Check for base + i*stride patterns (strided)
     * 4. Analyze loop induction variables
     * 5. Check for repeated addresses (localized)
     */
    
    return MEMORY_UNKNOWN;
}

CodePattern ir_detect_pattern(void *ir_node) {
    /* Placeholder: actual implementation would analyze IR structure */
    if (!ir_node) return PATTERN_NONE;
    
    /*
     * In a full implementation, we would:
     * 1. Recognize loop structures and their characteristics
     * 2. Check for element-wise operations (map)
     * 3. Check for conditional element selection (filter)
     * 4. Check for accumulator patterns (reduce)
     * 5. Check for prefix-sum style computations (scan)
     * 6. Analyze control flow for state machines
     * 7. Check for chained transformations (pipeline)
     */
    
    return PATTERN_NONE;
}

/* ========== Pattern Detection Heuristics ========== */

/* Map: loop that transforms each element without dependencies */
/* Look for: for item in array: result[i] = f(item) */
static bool is_map_pattern(void *node) {
    if (!node) return false;
    
    /*
     * Detection heuristics:
     * 1. Loop with index variable
     * 2. Output array indexed by loop variable
     * 3. No cross-iteration dependencies
     * 4. Each iteration reads one element, writes one element
     */
    return false;
}

/* Reduce: accumulator updated in loop */
/* Look for: acc = initial; for item in array: acc = f(acc, item) */
static bool is_reduce_pattern(void *node, bool *has_identity) {
    if (!node) return false;
    
    /*
     * Detection heuristics:
     * 1. Variable initialized before loop
     * 2. Same variable updated each iteration
     * 3. Update uses previous value and current element
     * 4. Common ops: +, *, min, max, and, or
     */
    if (has_identity) *has_identity = false;
    return false;
}

/* Filter: select elements by predicate */
/* Look for: for item in array: if predicate(item): output.push(item) */
static bool is_filter_pattern(void *node) {
    if (!node) return false;
    
    /*
     * Detection heuristics:
     * 1. Loop with conditional body
     * 2. Condition depends only on current element
     * 3. True branch writes element to output
     * 4. Output index may differ from input index
     */
    return false;
}

/* Pipeline: chain of |> operators or function composition */
static bool is_pipeline_pattern(void *node, int *stage_count) {
    if (!node) return false;
    
    /*
     * Detection heuristics:
     * 1. Chain of function calls where output feeds to next input
     * 2. Pipe operator (|>) in source
     * 3. Composed functions: f(g(h(x)))
     */
    if (stage_count) *stage_count = 0;
    return false;
}

/* State machine: switch on state variable, each case sets new state */
static bool is_state_machine_pattern(void *node, int *state_count) {
    if (!node) return false;
    
    /*
     * Detection heuristics:
     * 1. Switch/match on a "state" variable
     * 2. Each case modifies the state variable
     * 3. Loop containing the switch
     * 4. State variable is enum or integer
     */
    if (state_count) *state_count = 0;
    return false;
}

/* Scan: running accumulation (prefix sum, etc.) */
static bool is_scan_pattern(void *node) {
    if (!node) return false;
    
    /*
     * Detection heuristics:
     * 1. Accumulator like reduce
     * 2. But output[i] = accumulator at step i
     * 3. Each element depends on all previous elements
     */
    return false;
}

/* Detect nested loop depth */
static int detect_loop_depth(void *node) {
    if (!node) return 0;
    
    /*
     * Count nested loop levels for loop tiling decisions
     */
    return 0;
}

PatternMatch ir_detect_ast_pattern(void *ast_node) {
    PatternMatch match = {
        .pattern = PATTERN_NONE,
        .confidence = 0.0,
        .description = "No pattern detected"
    };
    
    if (!ast_node) return match;
    
    /* Try each pattern detector in order of specificity */
    
    /* Check for pipeline first (most specific) */
    int stage_count = 0;
    if (is_pipeline_pattern(ast_node, &stage_count)) {
        match.pattern = PATTERN_PIPELINE;
        match.confidence = 0.85;
        match.description = "Pipeline pattern: chain of transformations";
        match.details.pipeline.stage_count = stage_count;
        return match;
    }
    
    /* Check for state machine */
    int state_count = 0;
    if (is_state_machine_pattern(ast_node, &state_count)) {
        match.pattern = PATTERN_STATEMACHINE;
        match.confidence = 0.9;
        match.description = "State machine pattern: switch-based state transitions";
        match.details.state_machine.state_count = state_count;
        return match;
    }
    
    /* Check for scan (prefix sum) */
    if (is_scan_pattern(ast_node)) {
        match.pattern = PATTERN_SCAN;
        match.confidence = 0.8;
        match.description = "Scan pattern: running accumulation";
        return match;
    }
    
    /* Check for reduce */
    bool has_identity = false;
    if (is_reduce_pattern(ast_node, &has_identity)) {
        match.pattern = PATTERN_REDUCE;
        match.confidence = 0.8;
        match.description = "Reduce pattern: fold elements to single value";
        match.details.reduce.has_identity = has_identity;
        return match;
    }
    
    /* Check for filter */
    if (is_filter_pattern(ast_node)) {
        match.pattern = PATTERN_FILTER;
        match.confidence = 0.75;
        match.description = "Filter pattern: select elements by predicate";
        return match;
    }
    
    /* Check for map (most general loop pattern) */
    if (is_map_pattern(ast_node)) {
        match.pattern = PATTERN_MAP;
        match.confidence = 0.8;
        match.description = "Map pattern: transform each element";
        
        /* Check for nested loops */
        int depth = detect_loop_depth(ast_node);
        if (depth > 1) {
            match.details.nested_loops.depth = depth;
        }
        return match;
    }
    
    return match;
}

const char* ir_pattern_optimization(CodePattern pattern) {
    switch (pattern) {
        case PATTERN_MAP:
            return "vectorize, parallelize, loop tiling";
        case PATTERN_FILTER:
            return "branch-free select, SIMD masking, predicated stores";
        case PATTERN_REDUCE:
            return "tree reduction, SIMD horizontal ops, parallel reduce";
        case PATTERN_SCAN:
            return "work-efficient parallel scan, SIMD prefix sum";
        case PATTERN_PIPELINE:
            return "stage fusion, streaming, software pipelining";
        case PATTERN_STATEMACHINE:
            return "jump table, computed goto, branch prediction hints";
        case PATTERN_NONE:
        default:
            return "none";
    }
}

void ir_metadata_compute_hints(IRMetadata *meta) {
    if (!meta) return;
    
    /* Reset hints before computing */
    meta->hints.should_inline = false;
    meta->hints.should_vectorize = false;
    meta->hints.should_parallelize = false;
    meta->hints.should_unroll = false;
    meta->hints.unroll_factor = 0;
    
    /* Hot path optimizations */
    if (meta->hints.is_hot_path) {
        meta->hints.should_inline = true;
        meta->hints.should_unroll = true;
        meta->hints.unroll_factor = DEFAULT_UNROLL_FACTOR;
    }
    
    /* Pattern-based optimization hints */
    switch (meta->pattern) {
        case PATTERN_MAP:
            /* Map patterns are highly vectorizable and parallelizable */
            meta->hints.should_vectorize = true;
            meta->hints.should_parallelize = true;
            break;
            
        case PATTERN_FILTER:
            /* Filters benefit from vectorization with masking */
            meta->hints.should_vectorize = true;
            break;
            
        case PATTERN_REDUCE:
            /* Reductions can be parallelized with careful handling */
            meta->hints.should_vectorize = true;
            /* Only parallelize if we have enough work */
            if (meta->execution_count > 1000) {
                meta->hints.should_parallelize = true;
            }
            break;
            
        case PATTERN_SCAN:
            /* Scans have limited parallelism but can be vectorized */
            meta->hints.should_vectorize = true;
            meta->hints.should_unroll = true;
            meta->hints.unroll_factor = DEFAULT_UNROLL_FACTOR;
            break;
            
        case PATTERN_STATEMACHINE:
            /* State machines are hard to vectorize, focus on inlining */
            meta->hints.should_inline = true;
            break;
            
        case PATTERN_PIPELINE:
            /* Pipelines can be parallelized at stage boundaries */
            meta->hints.should_parallelize = true;
            break;
            
        default:
            break;
    }
    
    /* Memory behavior adjustments */
    switch (meta->memory_behavior) {
        case MEMORY_SEQUENTIAL:
            /* Sequential access is ideal for vectorization */
            if (meta->pattern == PATTERN_MAP || meta->pattern == PATTERN_REDUCE) {
                meta->hints.should_vectorize = true;
            }
            break;
            
        case MEMORY_STRIDED:
            /* Strided access can still vectorize with gather/scatter */
            meta->hints.should_vectorize = true;
            break;
            
        case MEMORY_RANDOM:
            /* Random access defeats vectorization */
            meta->hints.should_vectorize = false;
            break;
            
        case MEMORY_LOCALIZED:
            /* Localized access benefits from unrolling for cache reuse */
            meta->hints.should_unroll = true;
            if (meta->hints.unroll_factor < DEFAULT_UNROLL_FACTOR) {
                meta->hints.unroll_factor = DEFAULT_UNROLL_FACTOR;
            }
            break;
            
        default:
            break;
    }
    
    /* Data flow adjustments */
    switch (meta->data_flow) {
        case DATAFLOW_LINEAR:
            /* Linear flow is easy to optimize */
            meta->hints.should_inline = true;
            break;
            
        case DATAFLOW_BROADCAST:
            /* Broadcast patterns vectorize well */
            meta->hints.should_vectorize = true;
            break;
            
        case DATAFLOW_ACCUMULATE:
            /* Accumulation needs careful vectorization */
            meta->hints.should_unroll = true;
            break;
            
        case DATAFLOW_GATHER:
        case DATAFLOW_SCATTER:
            /* These may limit vectorization effectiveness */
            break;
            
        default:
            break;
    }
    
    /* Branch predictability affects unrolling */
    if (meta->branch_predictability < 0.6) {
        /* Unpredictable branches - reduce unroll factor */
        meta->hints.unroll_factor = meta->hints.unroll_factor / 2;
        if (meta->hints.unroll_factor < 2) {
            meta->hints.should_unroll = false;
            meta->hints.unroll_factor = 0;
        }
    } else if (meta->branch_predictability > 0.9) {
        /* Highly predictable - can unroll more aggressively */
        if (meta->hints.should_unroll && meta->hints.unroll_factor < MAX_UNROLL_FACTOR) {
            meta->hints.unroll_factor = meta->hints.unroll_factor * 2;
            if (meta->hints.unroll_factor > MAX_UNROLL_FACTOR) {
                meta->hints.unroll_factor = MAX_UNROLL_FACTOR;
            }
        }
    }
    
    /* Cold path: disable most optimizations to save compile time/code size */
    if (meta->hints.is_cold_path) {
        meta->hints.should_inline = false;
        meta->hints.should_vectorize = false;
        meta->hints.should_parallelize = false;
        meta->hints.should_unroll = false;
        meta->hints.unroll_factor = 0;
    }
}

void ir_metadata_merge(IRMetadata *target, const IRMetadata *source) {
    if (!target || !source) return;
    
    /* Calculate weights for weighted average based on confidence */
    double total_confidence = target->confidence + source->confidence;
    double target_weight, source_weight;
    
    if (total_confidence < 0.001) {
        /* Both have no confidence, equal weight */
        target_weight = 0.5;
        source_weight = 0.5;
    } else {
        target_weight = target->confidence / total_confidence;
        source_weight = source->confidence / total_confidence;
    }
    
    /* Merge execution counts (additive) */
    target->execution_count += source->execution_count;
    
    /* Merge execution percentage (weighted average) */
    target->execution_percentage = 
        target_weight * target->execution_percentage +
        source_weight * source->execution_percentage;
    
    /* Merge branch predictability (weighted average) */
    target->branch_predictability = 
        target_weight * target->branch_predictability +
        source_weight * source->branch_predictability;
    
    /* For enum fields, prefer the one with higher confidence */
    if (source->confidence > target->confidence) {
        if (source->data_flow != DATAFLOW_UNKNOWN) {
            target->data_flow = source->data_flow;
        }
        if (source->memory_behavior != MEMORY_UNKNOWN) {
            target->memory_behavior = source->memory_behavior;
        }
        if (source->pattern != PATTERN_NONE) {
            target->pattern = source->pattern;
        }
    }
    
    /* Update confidence to combined confidence (capped at 1.0) */
    target->confidence = target_weight * target->confidence +
                        source_weight * source->confidence;
    if (target->confidence > 1.0) {
        target->confidence = 1.0;
    }
    
    /* Recompute hints based on merged data */
    ir_metadata_compute_hints(target);
}

void ir_metadata_dump(const IRMetadata *meta) {
    if (!meta) {
        printf("IRMetadata: (null)\n");
        return;
    }
    
    printf("=== IR Metadata ===\n");
    printf("Execution:\n");
    printf("  count: %lu\n", (unsigned long)meta->execution_count);
    printf("  percentage: %.4f%%\n", meta->execution_percentage);
    printf("  confidence: %.4f\n", meta->confidence);
    
    printf("Patterns:\n");
    printf("  data_flow: %s\n", dataflow_to_string(meta->data_flow));
    printf("  memory: %s\n", memory_to_string(meta->memory_behavior));
    printf("  pattern: %s\n", pattern_to_string(meta->pattern));
    printf("  branch_predictability: %.4f\n", meta->branch_predictability);
    
    printf("Optimization Hints:\n");
    printf("  should_inline: %s\n", meta->hints.should_inline ? "yes" : "no");
    printf("  should_vectorize: %s\n", meta->hints.should_vectorize ? "yes" : "no");
    printf("  should_parallelize: %s\n", meta->hints.should_parallelize ? "yes" : "no");
    printf("  should_unroll: %s", meta->hints.should_unroll ? "yes" : "no");
    if (meta->hints.should_unroll) {
        printf(" (factor=%d)", meta->hints.unroll_factor);
    }
    printf("\n");
    printf("  is_hot_path: %s\n", meta->hints.is_hot_path ? "yes" : "no");
    printf("  is_cold_path: %s\n", meta->hints.is_cold_path ? "yes" : "no");
    printf("==================\n");
}
