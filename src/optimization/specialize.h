/*
 * QISC Function Specialization Engine — Header
 *
 * Profile-driven function specialization: generates optimized versions
 * of functions for common input patterns observed at runtime.
 *
 * Key insight: Most functions are called with a small set of common
 * argument patterns. By generating specialized versions for these
 * patterns, we can unlock aggressive optimizations:
 *   - Stack allocation instead of heap for known-size arrays
 *   - Complete loop unrolling for small iteration counts
 *   - Dead code elimination when arguments are constants
 *   - Bounds check elimination when sizes are known
 *
 * This is the core of QISC's "Living IR" intelligence.
 */

#ifndef QISC_SPECIALIZE_H
#define QISC_SPECIALIZE_H

#include "../profile/profile.h"
#include "../codegen/codegen.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declaration */
struct LivingIR;

/* Maximum tracked items */
#define SPEC_MAX_OPPORTUNITIES      256
#define SPEC_MAX_SPECIALIZED_FUNCS  128
#define SPEC_MAX_HISTOGRAM_BUCKETS  16
#define SPEC_MAX_PARAMS             8

/* Minimum thresholds for specialization */
#define SPEC_MIN_CALL_COUNT         100      /* Minimum calls to consider */
#define SPEC_MIN_PERCENTAGE         0.70     /* 70% of calls must match pattern */
#define SPEC_SMALL_ARRAY_THRESHOLD  64       /* Max size for stack allocation */
#define SPEC_UNROLL_RANGE_THRESHOLD 4        /* Max range for full unroll */

/* ======== Specialization Types ======== */

typedef enum {
    SPEC_ARRAY_SIZE,     /* Array size falls in specific range */
    SPEC_VALUE_RANGE,    /* Input value in specific numeric range */
    SPEC_CONSTANT_ARG,   /* Argument is always the same value */
    SPEC_NULL_CHECK,     /* Argument is never/always null */
    SPEC_TYPE_NARROW,    /* More specific type available (e.g., int32 vs int64) */
    SPEC_ENUM_VALUE,     /* Enum arg is always specific variant */
    SPEC_BOOL_CONST,     /* Boolean arg is always true/false */
    SPEC_STRING_LENGTH,  /* String length in specific range */
} SpecializationType;

/* ======== Histogram Bucket ======== */

typedef struct {
    int64_t min;          /* Bucket minimum value (inclusive) */
    int64_t max;          /* Bucket maximum value (inclusive) */
    uint64_t count;       /* Number of calls in this bucket */
    double percentage;    /* Percentage of total calls */
} HistogramBucket;

/* Histogram for value distribution */
typedef struct {
    HistogramBucket buckets[SPEC_MAX_HISTOGRAM_BUCKETS];
    int bucket_count;
    uint64_t total_samples;
    int64_t min_observed;
    int64_t max_observed;
    double mean;
    double stddev;
} ValueHistogram;

/* ======== Specialization Opportunity ======== */

typedef struct {
    const char *function_name;     /* Name of function to specialize */
    int param_index;               /* Which parameter exhibits the pattern */
    SpecializationType type;       /* Type of specialization */

    /* Type-specific data */
    union {
        struct {
            int min;
            int max;
        } array_size;

        struct {
            int64_t min;
            int64_t max;
        } value_range;

        struct {
            int64_t value;
        } constant;

        struct {
            bool is_never_null;     /* true = never null, false = always null */
        } null_check;

        struct {
            const char *narrow_type; /* More specific type name */
            int bit_width;           /* Narrower bit width */
        } type_narrow;

        struct {
            int enum_value;
            const char *variant_name;
        } enum_val;

        struct {
            bool value;
        } bool_const;

        struct {
            int min_len;
            int max_len;
        } string_length;
    } data;

    uint64_t call_count;           /* How many calls match this pattern */
    float percentage;              /* Percentage of calls matching */
    float confidence;              /* Confidence in this pattern (0.0 - 1.0) */
    
    /* Source tracking */
    const char *source_location;   /* Where function is defined */
    
    /* Estimated benefit */
    double estimated_speedup;      /* Expected speedup factor (e.g., 1.5 = 50% faster) */
    int code_size_increase;        /* Expected code size increase in bytes */
} SpecializationOpportunity;

/* ======== Specialized Function ======== */

typedef struct {
    const char *original_name;     /* Original function name */
    const char *specialized_name;  /* New specialized function name */
    LLVMValueRef original_func;    /* Original LLVM function */
    LLVMValueRef specialized_func; /* Specialized LLVM function */
    
    SpecializationType spec_type;  /* Type of specialization applied */
    int param_index;               /* Which parameter was specialized */
    
    /* Specialization parameters */
    union {
        struct {
            int min_size;
            int max_size;
        } array_range;
        
        struct {
            int64_t value;
        } constant_arg;
        
        struct {
            int64_t min;
            int64_t max;
        } value_range;
    } spec_params;
    
    /* Statistics */
    uint64_t dispatch_count;       /* Times dispatcher chose this version */
    double measured_speedup;       /* Actual measured speedup (if available) */
    int instruction_count;         /* Instructions in specialized version */
} SpecializedFunction;

/* ======== Dispatcher ======== */

typedef struct {
    const char *function_name;     /* Original function name */
    LLVMValueRef dispatcher_func;  /* Dispatcher function */
    
    /* Specialized versions dispatched to */
    SpecializedFunction *versions[SPEC_MAX_SPECIALIZED_FUNCS];
    int version_count;
    
    /* Default fallback */
    LLVMValueRef fallback_func;    /* Generic version for unmatched patterns */
} FunctionDispatcher;

/* ======== Specialization Metrics ======== */

typedef struct {
    int functions_analyzed;        /* Total functions analyzed */
    int opportunities_found;       /* Specialization opportunities found */
    int specializations_generated; /* Specialized versions generated */
    int dispatchers_created;       /* Dispatcher functions created */
    
    /* Performance estimates */
    double estimated_speedup;      /* Overall estimated speedup */
    int64_t code_size_delta;       /* Change in code size (bytes) */
    
    /* Breakdown by type */
    int array_size_specs;          /* Array size specializations */
    int constant_arg_specs;        /* Constant argument specializations */
    int value_range_specs;         /* Value range specializations */
    int null_check_specs;          /* Null check specializations */
    int type_narrow_specs;         /* Type narrowing specializations */
    
    /* Timing */
    double analysis_time_ms;       /* Time spent analyzing */
    double generation_time_ms;     /* Time spent generating specializations */
} SpecializationMetrics;

/* ======== Specialization Context ======== */

typedef struct {
    /* LLVM context */
    LLVMModuleRef module;
    LLVMContextRef llvm_ctx;
    LLVMBuilderRef builder;
    
    /* Profile data */
    QiscProfile *profile;
    
    /* Discovered opportunities */
    SpecializationOpportunity opportunities[SPEC_MAX_OPPORTUNITIES];
    int opportunity_count;
    
    /* Generated specializations */
    SpecializedFunction specialized[SPEC_MAX_SPECIALIZED_FUNCS];
    int specialized_count;
    
    /* Dispatchers */
    FunctionDispatcher dispatchers[SPEC_MAX_SPECIALIZED_FUNCS];
    int dispatcher_count;
    
    /* Metrics */
    SpecializationMetrics metrics;
    
    /* Configuration */
    struct {
        float min_percentage;      /* Min % of calls for specialization */
        int min_call_count;        /* Min call count to consider */
        int max_specializations;   /* Max specialized versions per function */
        bool enable_array_size;    /* Enable array size specialization */
        bool enable_constant_arg;  /* Enable constant argument specialization */
        bool enable_value_range;   /* Enable value range specialization */
        bool enable_null_check;    /* Enable null check specialization */
        bool enable_type_narrow;   /* Enable type narrowing */
        bool aggressive_mode;      /* Generate more specializations */
        bool emit_guards;          /* Emit runtime guards for safety */
    } config;
    
    /* Error handling */
    bool had_error;
    char error_msg[512];
} SpecializationContext;

/* ======== Context Creation ======== */

/* Create specialization context */
SpecializationContext *spec_create(LLVMModuleRef module, QiscProfile *profile);

/* Create with custom configuration */
SpecializationContext *spec_create_with_config(
    LLVMModuleRef module,
    QiscProfile *profile,
    float min_percentage,
    int min_call_count
);

/* Destroy context and free resources */
void spec_destroy(SpecializationContext *ctx);

/* ======== Configuration ======== */

/* Enable/disable specific specialization types */
void spec_set_array_size_enabled(SpecializationContext *ctx, bool enable);
void spec_set_constant_arg_enabled(SpecializationContext *ctx, bool enable);
void spec_set_value_range_enabled(SpecializationContext *ctx, bool enable);
void spec_set_null_check_enabled(SpecializationContext *ctx, bool enable);
void spec_set_type_narrow_enabled(SpecializationContext *ctx, bool enable);

/* Set aggressive mode */
void spec_set_aggressive(SpecializationContext *ctx, bool aggressive);

/* Set minimum thresholds */
void spec_set_min_percentage(SpecializationContext *ctx, float percentage);
void spec_set_min_call_count(SpecializationContext *ctx, int count);

/* ======== Analysis Phase ======== */

/* Analyze profile to find specialization opportunities */
int spec_find_opportunities(SpecializationContext *ctx);

/* Find opportunities for a specific function */
int spec_find_function_opportunities(
    SpecializationContext *ctx,
    const char *func_name,
    SpecializationOpportunity *out,
    int max_out
);

/* Analyze histogram to find dominant value range */
int spec_find_dominant_range(ValueHistogram *histogram, int64_t *min, int64_t *max);

/* Check if argument is effectively constant */
bool spec_is_constant_arg(
    SpecializationContext *ctx,
    const char *func_name,
    int param_index,
    int64_t *value,
    float *percentage
);

/* ======== Specialization Generation ======== */

/* Generate all identified specializations */
int spec_generate_all(SpecializationContext *ctx);

/* Generate specialized version for array size range */
LLVMValueRef spec_generate_array_size(
    SpecializationContext *ctx,
    LLVMValueRef original_func,
    int min_size,
    int max_size
);

/* Generate specialized version for constant argument */
LLVMValueRef spec_generate_constant_arg(
    SpecializationContext *ctx,
    LLVMValueRef original_func,
    int param_index,
    int64_t value
);

/* Generate specialized version for value range */
LLVMValueRef spec_generate_value_range(
    SpecializationContext *ctx,
    LLVMValueRef original_func,
    int param_index,
    int64_t min_val,
    int64_t max_val
);

/* Generate specialized version assuming non-null parameter */
LLVMValueRef spec_generate_nonnull(
    SpecializationContext *ctx,
    LLVMValueRef original_func,
    int param_index
);

/* ======== Dispatcher Generation ======== */

/* Generate dispatcher that routes to specialized versions */
LLVMValueRef spec_generate_dispatcher(
    SpecializationContext *ctx,
    const char *func_name,
    SpecializedFunction *versions,
    int version_count,
    LLVMValueRef fallback
);

/* Replace original function with dispatcher */
void spec_replace_with_dispatcher(
    SpecializationContext *ctx,
    const char *func_name
);

/* ======== Optimization Helpers ======== */

/* Clone function for specialization */
LLVMValueRef spec_clone_function(
    SpecializationContext *ctx,
    LLVMValueRef original,
    const char *new_name
);

/* Replace heap allocations with stack for small arrays */
void spec_replace_heap_with_stack(
    SpecializationContext *ctx,
    LLVMValueRef func,
    int max_size
);

/* Unroll loops completely when iteration count is small */
void spec_unroll_loops_completely(
    SpecializationContext *ctx,
    LLVMValueRef func
);

/* Remove bounds checks when size is known */
void spec_remove_bounds_checks(
    SpecializationContext *ctx,
    LLVMValueRef func,
    int known_size
);

/* Propagate constant argument through function */
void spec_propagate_constant(
    SpecializationContext *ctx,
    LLVMValueRef func,
    int param_index,
    int64_t value
);

/* Remove null checks when parameter is known non-null */
void spec_remove_null_checks(
    SpecializationContext *ctx,
    LLVMValueRef func,
    int param_index
);

/* ======== Living IR Integration ======== */

/* Apply specializations to Living IR context */
void spec_apply_to_living_ir(
    SpecializationContext *ctx,
    struct LivingIR *ir
);

/* Check if function should be specialized based on profile */
bool spec_should_specialize(
    SpecializationContext *ctx,
    const char *func_name
);

/* Get best opportunity for a function */
SpecializationOpportunity *spec_get_best_opportunity(
    SpecializationContext *ctx,
    const char *func_name
);

/* ======== Metrics and Reporting ======== */

/* Get specialization metrics */
SpecializationMetrics spec_get_metrics(SpecializationContext *ctx);

/* Print specialization report */
void spec_print_report(SpecializationContext *ctx);

/* Print detailed opportunity list */
void spec_print_opportunities(SpecializationContext *ctx);

/* Print generated specializations */
void spec_print_specializations(SpecializationContext *ctx);

/* ======== Verification ======== */

/* Verify module is still valid after specialization */
bool spec_verify(SpecializationContext *ctx);

/* Get error message */
const char *spec_get_error(SpecializationContext *ctx);

/* ======== Utility Functions ======== */

/* Generate specialized function name */
void spec_make_name(
    char *out,
    size_t out_size,
    const char *base_name,
    SpecializationType type,
    int64_t param1,
    int64_t param2
);

/* Calculate histogram bucket for a value */
int spec_histogram_bucket(ValueHistogram *hist, int64_t value);

/* Add sample to histogram */
void spec_histogram_add(ValueHistogram *hist, int64_t value);

/* Get the specialization type name */
const char *spec_type_name(SpecializationType type);

/* Estimate speedup for a specialization */
double spec_estimate_speedup(SpecializationOpportunity *opp);

/* Estimate code size increase */
int spec_estimate_code_size(SpecializationOpportunity *opp, LLVMValueRef func);

#endif /* QISC_SPECIALIZE_H */
