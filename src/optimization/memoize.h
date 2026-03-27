/*
 * QISC Automatic Memoization System
 *
 * Profile-driven automatic memoization for pure functions.
 * Detects functions with no side effects and generates cache
 * lookup/store code to avoid redundant computation.
 *
 * Key features:
 *   - Pure function detection via static analysis
 *   - Profile-driven cache sizing (more calls = bigger cache)
 *   - LRU eviction for bounded memory
 *   - Argument hashing for cache keys
 *   - Specialized cache lookups for common types (int, float, array)
 *
 * Integration with Living IR: memoization candidates are identified
 * during analysis and cache code is generated at codegen time.
 */

#ifndef QISC_OPTIMIZATION_MEMOIZE_H
#define QISC_OPTIMIZATION_MEMOIZE_H

#include "../parser/ast.h"
#include "../codegen/codegen.h"
#include "../profile/profile.h"
#include "../ir/living_ir.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <stdbool.h>
#include <stdint.h>

/* Maximum tracked items */
#define MEMO_MAX_FUNCTIONS       256
#define MEMO_MAX_PARAMS          8
#define MEMO_MAX_CALL_SITES      1024

/* Cache sizing thresholds */
#define MEMO_MIN_CALLS_FOR_CACHE     100    /* Min calls to enable memoization */
#define MEMO_DEFAULT_CACHE_SIZE      64     /* Default LRU cache entries */
#define MEMO_SMALL_CACHE_SIZE        16     /* For rarely-called functions */
#define MEMO_MEDIUM_CACHE_SIZE       128    /* For moderately-called functions */
#define MEMO_LARGE_CACHE_SIZE        512    /* For hot functions */
#define MEMO_HUGE_CACHE_SIZE         4096   /* For very hot recursive functions */

/* Call count thresholds for cache sizing */
#define MEMO_THRESHOLD_SMALL         100
#define MEMO_THRESHOLD_MEDIUM        1000
#define MEMO_THRESHOLD_LARGE         10000
#define MEMO_THRESHOLD_HUGE          100000

/* ============================================================================
 * Purity Analysis Types
 * ============================================================================ */

/* Side effect types that prevent memoization */
typedef enum {
    EFFECT_NONE            = 0,
    EFFECT_WRITES_GLOBAL   = 1 << 0,   /* Writes to global variables */
    EFFECT_READS_MUTABLE   = 1 << 1,   /* Reads mutable global state */
    EFFECT_IO              = 1 << 2,   /* Performs I/O operations */
    EFFECT_ALLOC           = 1 << 3,   /* Allocates memory (may be OK) */
    EFFECT_THROWS          = 1 << 4,   /* May throw/fail */
    EFFECT_CALLS_IMPURE    = 1 << 5,   /* Calls impure functions */
    EFFECT_RANDOM          = 1 << 6,   /* Uses random numbers */
    EFFECT_TIME            = 1 << 7,   /* Uses current time */
    EFFECT_MODIFIES_PARAM  = 1 << 8,   /* Modifies parameters in-place */
} SideEffect;

/* Result of purity analysis */
typedef struct {
    const char *function_name;
    bool is_pure;                      /* True if function has no side effects */
    uint32_t effects;                  /* Bitmask of detected side effects */
    bool is_recursive;                 /* Function calls itself */
    bool is_mutually_recursive;        /* Part of mutual recursion cycle */
    int call_depth;                    /* Estimated max call depth */
    double purity_confidence;          /* 0.0-1.0 confidence in analysis */
    
    /* Reason for impurity (if !is_pure) */
    const char *impurity_reason;
    int impurity_line;                 /* Line number of impure operation */
} PurityAnalysis;

/* ============================================================================
 * Parameter Type Classification
 * ============================================================================ */

typedef enum {
    PARAM_TYPE_UNKNOWN,
    PARAM_TYPE_INT,                    /* Integer types (i8, i16, i32, i64) */
    PARAM_TYPE_FLOAT,                  /* Floating point (f32, f64) */
    PARAM_TYPE_BOOL,                   /* Boolean */
    PARAM_TYPE_STRING,                 /* String pointer */
    PARAM_TYPE_ARRAY,                  /* Array/slice */
    PARAM_TYPE_STRUCT,                 /* Struct by value */
    PARAM_TYPE_POINTER,                /* Generic pointer */
} MemoParamType;

/* Parameter info for cache key generation */
typedef struct {
    int index;                         /* Parameter index */
    const char *name;                  /* Parameter name */
    MemoParamType type;                /* Classified type */
    int bit_width;                     /* Bit width for numeric types */
    bool is_nullable;                  /* Can be null/none */
    const char *type_name;             /* Original type name */
} MemoParam;

/* ============================================================================
 * Memoization Candidate
 * ============================================================================ */

typedef struct {
    /* Function identification */
    const char *function_name;
    AstNode *ast_node;                 /* Original AST node */
    LLVMValueRef llvm_func;            /* LLVM function value */
    
    /* Purity analysis result */
    PurityAnalysis purity;
    
    /* Parameter information */
    MemoParam params[MEMO_MAX_PARAMS];
    int param_count;
    
    /* Return type info */
    MemoParamType return_type;
    int return_bit_width;
    
    /* Profile data */
    uint64_t call_count;               /* Total calls observed */
    uint64_t unique_inputs;            /* Estimated unique input combinations */
    double avg_execution_time_us;      /* Average execution time */
    double total_time_percentage;      /* % of total program time */
    
    /* Cache configuration */
    int recommended_cache_size;        /* Profile-driven cache size */
    double estimated_hit_rate;         /* Expected cache hit rate */
    double estimated_speedup;          /* Expected speedup from memoization */
    
    /* Memoization decision */
    bool should_memoize;               /* Final decision */
    const char *decision_reason;       /* Why yes/no */
    
    /* Source location */
    int line;
    int column;
} MemoCandidate;

/* ============================================================================
 * Call Site Information
 * ============================================================================ */

typedef struct {
    const char *caller;                /* Calling function */
    const char *callee;                /* Called (memoized) function */
    int line;
    int column;
    uint64_t call_count;               /* Times this site was executed */
    bool is_in_loop;                   /* Call is inside a loop */
    int loop_depth;                    /* Nesting depth of loops */
} MemoCallSite;

/* ============================================================================
 * Generated Cache Structures
 * ============================================================================ */

/* Cache entry for a memoized function */
typedef struct {
    LLVMTypeRef key_type;              /* Type for cache keys */
    LLVMTypeRef value_type;            /* Type for cached values */
    LLVMTypeRef entry_type;            /* Full cache entry struct type */
    LLVMValueRef cache_global;         /* Global cache array */
    LLVMValueRef size_global;          /* Cache size constant */
    LLVMValueRef valid_bitmap;         /* Bitmap for valid entries */
    LLVMValueRef lru_counter;          /* Global LRU timestamp counter */
} MemoCache;

/* Generated memoized function */
typedef struct {
    const char *original_name;         /* Original function name */
    const char *memoized_name;         /* Generated memoized name */
    LLVMValueRef original_func;        /* Original function */
    LLVMValueRef memoized_func;        /* Generated wrapper */
    LLVMValueRef hash_func;            /* Generated hash function */
    LLVMValueRef lookup_func;          /* Cache lookup function */
    LLVMValueRef store_func;           /* Cache store function */
    MemoCache cache;                   /* Cache structures */
    int cache_size;                    /* Configured cache size */
    
    /* Statistics */
    uint64_t hits;                     /* Cache hits (runtime) */
    uint64_t misses;                   /* Cache misses (runtime) */
} MemoizedFunction;

/* ============================================================================
 * Memoization Metrics
 * ============================================================================ */

typedef struct {
    int functions_analyzed;            /* Total functions analyzed */
    int pure_functions_found;          /* Functions deemed pure */
    int candidates_found;              /* Memoization candidates identified */
    int functions_memoized;            /* Actually memoized */
    
    /* By decision reason */
    int rejected_impure;               /* Rejected: not pure */
    int rejected_low_calls;            /* Rejected: not called enough */
    int rejected_low_benefit;          /* Rejected: insufficient speedup */
    int rejected_complex_args;         /* Rejected: can't hash arguments */
    
    /* Cache statistics */
    int total_cache_entries;           /* Sum of all cache sizes */
    size_t estimated_memory_bytes;     /* Memory for all caches */
    
    /* Performance estimates */
    double estimated_overall_speedup;  /* Overall program speedup */
    double estimated_memory_overhead;  /* Memory overhead ratio */
    
    /* Timing */
    double analysis_time_ms;
    double codegen_time_ms;
} MemoMetrics;

/* ============================================================================
 * Memoization Context
 * ============================================================================ */

typedef struct {
    /* LLVM context */
    LLVMModuleRef module;
    LLVMContextRef llvm_ctx;
    LLVMBuilderRef builder;
    
    /* Profile data */
    QiscProfile *profile;
    
    /* Living IR for integration */
    LivingIR *living_ir;
    
    /* Analysis results */
    MemoCandidate candidates[MEMO_MAX_FUNCTIONS];
    int candidate_count;
    
    /* Call site tracking */
    MemoCallSite call_sites[MEMO_MAX_CALL_SITES];
    int call_site_count;
    
    /* Generated memoized functions */
    MemoizedFunction memoized[MEMO_MAX_FUNCTIONS];
    int memoized_count;
    
    /* Purity cache (avoid re-analyzing) */
    PurityAnalysis purity_cache[MEMO_MAX_FUNCTIONS];
    int purity_cache_count;
    
    /* Metrics */
    MemoMetrics metrics;
    
    /* Configuration */
    struct {
        int min_call_count;            /* Min calls to consider memoization */
        double min_speedup;            /* Min speedup to justify memoization */
        int max_cache_size;            /* Maximum cache entries per function */
        int max_param_count;           /* Max params for memoization */
        bool enable_recursive;         /* Memoize recursive functions */
        bool aggressive_mode;          /* Memoize even uncertain cases */
        bool emit_statistics;          /* Emit cache hit/miss stats */
        double memory_budget_mb;       /* Max memory for all caches */
    } config;
    
    /* Cached LLVM types and functions */
    struct {
        LLVMTypeRef cache_entry_type;
        LLVMValueRef fn_hash_combine;
        LLVMValueRef fn_hash_int;
        LLVMValueRef fn_hash_float;
        LLVMValueRef fn_hash_string;
        LLVMValueRef fn_hash_array;
        LLVMValueRef fn_memcmp;
        LLVMValueRef fn_time_counter;
    } builtins;
    
    /* Error handling */
    bool had_error;
    char error_msg[512];
} MemoContext;

/* ============================================================================
 * Context Creation and Configuration
 * ============================================================================ */

/* Create memoization context */
MemoContext *memo_create(LLVMModuleRef module, QiscProfile *profile);

/* Create with Living IR integration */
MemoContext *memo_create_with_ir(LLVMModuleRef module, QiscProfile *profile,
                                  LivingIR *living_ir);

/* Destroy context and free resources */
void memo_destroy(MemoContext *ctx);

/* Configure memoization behavior */
void memo_set_min_calls(MemoContext *ctx, int min_calls);
void memo_set_min_speedup(MemoContext *ctx, double min_speedup);
void memo_set_max_cache_size(MemoContext *ctx, int max_size);
void memo_set_memory_budget(MemoContext *ctx, double mb);
void memo_set_aggressive(MemoContext *ctx, bool aggressive);
void memo_enable_recursive(MemoContext *ctx, bool enable);
void memo_enable_statistics(MemoContext *ctx, bool enable);

/* ============================================================================
 * Purity Analysis
 * ============================================================================ */

/* Analyze if a function is pure (no side effects) */
PurityAnalysis memo_analyze_purity(MemoContext *ctx, AstNode *func_node);

/* Analyze purity of LLVM function */
PurityAnalysis memo_analyze_purity_llvm(MemoContext *ctx, LLVMValueRef func);

/* Check if a specific expression has side effects */
bool memo_expr_has_effects(MemoContext *ctx, AstNode *expr);

/* Check if function calls are pure */
bool memo_call_is_pure(MemoContext *ctx, const char *callee_name);

/* Get cached purity analysis result */
PurityAnalysis *memo_get_purity(MemoContext *ctx, const char *func_name);

/* ============================================================================
 * Candidate Detection
 * ============================================================================ */

/* Analyze AST to find memoization candidates */
int memo_find_candidates(MemoContext *ctx, AstNode *program);

/* Analyze a single function as potential candidate */
MemoCandidate *memo_analyze_function(MemoContext *ctx, AstNode *func_node);

/* Check if function should be memoized based on profile */
bool memo_should_memoize(MemoContext *ctx, const char *func_name);

/* Get candidate by function name */
MemoCandidate *memo_get_candidate(MemoContext *ctx, const char *func_name);

/* ============================================================================
 * Cache Sizing (Profile-Driven)
 * ============================================================================ */

/* Calculate recommended cache size from profile data */
int memo_recommend_cache_size(MemoContext *ctx, const char *func_name);

/* Estimate hit rate for given cache size */
double memo_estimate_hit_rate(MemoContext *ctx, MemoCandidate *candidate,
                               int cache_size);

/* Estimate speedup from memoization */
double memo_estimate_speedup(MemoContext *ctx, MemoCandidate *candidate);

/* Estimate memory usage for cache */
size_t memo_estimate_memory(MemoContext *ctx, MemoCandidate *candidate,
                             int cache_size);

/* ============================================================================
 * Hash Function Generation
 * ============================================================================ */

/* Generate hash function for arguments */
LLVMValueRef memo_gen_hash_function(MemoContext *ctx, MemoCandidate *candidate);

/* Generate hash for specific parameter type */
LLVMValueRef memo_gen_param_hash(MemoContext *ctx, LLVMBuilderRef builder,
                                  MemoParam *param, LLVMValueRef value);

/* Generate combined hash from multiple values */
LLVMValueRef memo_gen_hash_combine(MemoContext *ctx, LLVMBuilderRef builder,
                                    LLVMValueRef *hashes, int count);

/* Built-in hash functions */
LLVMValueRef memo_gen_hash_int(MemoContext *ctx, LLVMBuilderRef builder,
                                LLVMValueRef value);
LLVMValueRef memo_gen_hash_float(MemoContext *ctx, LLVMBuilderRef builder,
                                  LLVMValueRef value);
LLVMValueRef memo_gen_hash_string(MemoContext *ctx, LLVMBuilderRef builder,
                                   LLVMValueRef value);
LLVMValueRef memo_gen_hash_array(MemoContext *ctx, LLVMBuilderRef builder,
                                  LLVMValueRef array, LLVMValueRef length);

/* ============================================================================
 * Cache Structure Generation
 * ============================================================================ */

/* Generate cache data structures for a function */
MemoCache memo_gen_cache_structures(MemoContext *ctx, MemoCandidate *candidate,
                                     int cache_size);

/* Generate cache lookup code */
LLVMValueRef memo_gen_cache_lookup(MemoContext *ctx, MemoCandidate *candidate,
                                    MemoCache *cache, LLVMValueRef *args);

/* Generate cache store code */
void memo_gen_cache_store(MemoContext *ctx, MemoCandidate *candidate,
                          MemoCache *cache, LLVMValueRef *args,
                          LLVMValueRef result);

/* Generate LRU eviction logic */
LLVMValueRef memo_gen_lru_evict(MemoContext *ctx, MemoCache *cache);

/* ============================================================================
 * Memoized Function Generation
 * ============================================================================ */

/* Generate memoized wrapper for a function */
MemoizedFunction *memo_generate_wrapper(MemoContext *ctx, 
                                         MemoCandidate *candidate);

/* Generate all identified memoizations */
int memo_generate_all(MemoContext *ctx);

/* Replace original function calls with memoized versions */
void memo_replace_calls(MemoContext *ctx, const char *original_name);

/* ============================================================================
 * Code Generation Helpers
 * ============================================================================ */

/* Generate cache hit check block */
LLVMBasicBlockRef memo_gen_hit_check(MemoContext *ctx, LLVMValueRef func,
                                      MemoCache *cache, LLVMValueRef hash,
                                      LLVMValueRef *args, int arg_count);

/* Generate cache miss handling block */  
LLVMBasicBlockRef memo_gen_miss_handler(MemoContext *ctx, LLVMValueRef func,
                                         MemoCandidate *candidate,
                                         MemoCache *cache, LLVMValueRef hash);

/* Generate argument comparison for cache hit validation */
LLVMValueRef memo_gen_args_equal(MemoContext *ctx, LLVMBuilderRef builder,
                                  MemoCandidate *candidate,
                                  LLVMValueRef *args1, LLVMValueRef *args2);

/* Generate statistics collection code */
void memo_gen_stats_increment(MemoContext *ctx, LLVMBuilderRef builder,
                               bool is_hit);

/* ============================================================================
 * Living IR Integration
 * ============================================================================ */

/* Apply memoization to Living IR context */
void memo_apply_to_living_ir(MemoContext *ctx, LivingIR *ir);

/* Check if Living IR suggests memoization for function */
bool memo_living_ir_suggests(MemoContext *ctx, const char *func_name);

/* Update Living IR with memoization results */
void memo_update_living_ir(MemoContext *ctx, LivingIR *ir);

/* ============================================================================
 * Metrics and Reporting
 * ============================================================================ */

/* Get memoization metrics */
MemoMetrics memo_get_metrics(MemoContext *ctx);

/* Print detailed memoization report */
void memo_print_report(MemoContext *ctx);

/* Print candidate list */
void memo_print_candidates(MemoContext *ctx);

/* Print generated memoizations */
void memo_print_memoized(MemoContext *ctx);

/* Dump purity analysis results */
void memo_dump_purity(MemoContext *ctx);

/* ============================================================================
 * Verification
 * ============================================================================ */

/* Verify module is still valid after memoization */
bool memo_verify(MemoContext *ctx);

/* Get error message */
const char *memo_get_error(MemoContext *ctx);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/* Generate memoized function name */
void memo_make_name(char *out, size_t out_size, const char *base_name);

/* Get human-readable name for parameter type */
const char *memo_param_type_name(MemoParamType type);

/* Get human-readable name for side effect */
const char *memo_effect_name(SideEffect effect);

/* Classify parameter type from AST type info */
MemoParamType memo_classify_param_type(TypeInfo *type_info);

/* Classify LLVM type */
MemoParamType memo_classify_llvm_type(LLVMTypeRef type);

/* Check if type is hashable for memoization */
bool memo_type_is_hashable(MemoParamType type);

/* Calculate hash for cache size power-of-two */
int memo_next_power_of_two(int n);

#endif /* QISC_OPTIMIZATION_MEMOIZE_H */
