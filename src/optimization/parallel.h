/*
 * QISC Auto-Parallelization Engine — Header
 *
 * Detects parallelizable patterns in QISC pipelines and generates
 * efficient parallel code using POSIX threads.
 *
 * Key patterns detected:
 *   - Independent pipeline stages (data parallelism)
 *   - Map operations over arrays (embarrassingly parallel)
 *   - Reduce with associative operators (parallel reduction)
 *   - Filter operations (parallel filter with local buffers)
 *
 * Features:
 *   - Profile-driven parallelization (only parallelize if data is large enough)
 *   - Work-stealing queue for dynamic load balancing
 *   - Thread pool management for efficient resource usage
 *   - Automatic chunk sizing based on cache line alignment
 *
 * Example transformation:
 *   let nums = range(0, 1000000)
 *   let sum = nums |> map(|x| x * x) |> reduce(|a, b| a + b, 0)
 *
 * Generates:
 *   - Split array into chunks (one per thread)
 *   - Map each chunk in parallel
 *   - Parallel tree reduction of partial results
 */

#ifndef QISC_OPTIMIZATION_PARALLEL_H
#define QISC_OPTIMIZATION_PARALLEL_H

#include "../parser/ast.h"
#include "../codegen/codegen.h"
#include "../profile/profile.h"
#include "fusion.h"
#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stdint.h>

/* ============================================================================
 * Configuration Constants
 * ============================================================================ */

/* Default thread pool configuration */
#define PARALLEL_DEFAULT_THREADS        0       /* 0 = auto-detect CPU count */
#define PARALLEL_MAX_THREADS            64      /* Maximum thread count */
#define PARALLEL_MIN_THREADS            2       /* Minimum for parallelization */

/* Work distribution */
#define PARALLEL_CHUNK_SIZE_MIN         1024    /* Minimum elements per chunk */
#define PARALLEL_CHUNK_SIZE_DEFAULT     4096    /* Default chunk size */
#define PARALLEL_CACHE_LINE_SIZE        64      /* Cache line alignment */

/* Profiling thresholds */
#define PARALLEL_MIN_ELEMENTS           10000   /* Min elements to parallelize */
#define PARALLEL_MIN_WORK_PER_ELEMENT   10      /* Min cycles per element */
#define PARALLEL_OVERHEAD_CYCLES        50000   /* Thread spawn overhead estimate */

/* Work-stealing queue */
#define PARALLEL_WORK_QUEUE_SIZE        4096    /* Max work items in queue */
#define PARALLEL_STEAL_BATCH_SIZE       8       /* Items to steal at once */

/* Maximum tracked items */
#define PARALLEL_MAX_OPPORTUNITIES      128
#define PARALLEL_MAX_REGIONS            64

/* ============================================================================
 * Parallel Pattern Types
 * ============================================================================ */

typedef enum {
    PARALLEL_NONE = 0,
    
    /* Single-stage parallelization */
    PARALLEL_MAP,               /* array |> map(f) → parallel map */
    PARALLEL_FILTER,            /* array |> filter(p) → parallel filter */
    PARALLEL_FOREACH,           /* array |> forEach(f) → parallel for-each */
    
    /* Two-stage parallelization */
    PARALLEL_MAP_REDUCE,        /* array |> map(f) |> reduce(g, init) */
    PARALLEL_FILTER_REDUCE,     /* array |> filter(p) |> reduce(g, init) */
    PARALLEL_MAP_FILTER,        /* array |> map(f) |> filter(p) */
    PARALLEL_FILTER_MAP,        /* array |> filter(p) |> map(f) */
    
    /* Multi-stage parallelization */
    PARALLEL_FILTER_MAP_REDUCE, /* array |> filter(p) |> map(f) |> reduce(g, init) */
    PARALLEL_MAP_MAP_REDUCE,    /* array |> map(f) |> map(g) |> reduce(h, init) */
    
    /* Loop parallelization */
    PARALLEL_FOR_LOOP,          /* Independent loop iterations */
    PARALLEL_NESTED_LOOP,       /* Parallelized outer loop */
    
    /* Special patterns */
    PARALLEL_SCAN,              /* Prefix sum / parallel scan */
    PARALLEL_PARTITION,         /* Split array by predicate */
    PARALLEL_SORT,              /* Parallel merge sort */
    
    /* Sentinel for iteration */
    PARALLEL_PATTERN_COUNT
} ParallelPattern;

/* ============================================================================
 * Associativity Detection for Reduce Operations
 * ============================================================================ */

typedef enum {
    ASSOC_UNKNOWN = 0,
    ASSOC_NONE,             /* Not associative, cannot parallelize */
    ASSOC_STRICT,           /* Strictly associative: (a ⊕ b) ⊕ c = a ⊕ (b ⊕ c) */
    ASSOC_COMMUTATIVE,      /* Associative and commutative */
    ASSOC_APPROXIMATE,      /* Approximately associative (floating point) */
} AssociativityType;

/* Known associative operators */
typedef struct {
    BinaryOp op;
    AssociativityType assoc;
    bool has_identity;
    int64_t identity_int;       /* Identity element for integers */
    double identity_float;      /* Identity element for floats */
} AssociativeOp;

/* ============================================================================
 * Work Item and Work Queue (Work-Stealing)
 * ============================================================================ */

typedef struct {
    void *data;                 /* Pointer to data segment */
    size_t start;               /* Start index in array */
    size_t end;                 /* End index (exclusive) */
    void *result;               /* Where to store result */
    uint32_t chunk_id;          /* Chunk identifier */
    bool completed;             /* Completion flag */
} WorkItem;

typedef struct {
    WorkItem items[PARALLEL_WORK_QUEUE_SIZE];
    volatile int head;          /* Dequeue from head (owner) */
    volatile int tail;          /* Enqueue at tail */
    volatile int steal_head;    /* Steal from steal_head (thieves) */
} WorkStealingQueue;

/* ============================================================================
 * Thread Pool
 * ============================================================================ */

typedef struct {
    int thread_count;           /* Number of worker threads */
    bool running;               /* Pool is active */
    bool shutdown;              /* Shutdown requested */
    
    /* Work distribution */
    WorkStealingQueue *queues;  /* Per-thread work queues */
    volatile int active_workers;/* Currently active workers */
    
    /* Synchronization (represented as opaque pointers for header) */
    void *mutex;                /* pthread_mutex_t* */
    void *cond;                 /* pthread_cond_t* */
    void *barrier;              /* pthread_barrier_t* */
    void *threads;              /* pthread_t* array */
    
    /* Statistics */
    uint64_t tasks_completed;
    uint64_t tasks_stolen;
    uint64_t total_wait_cycles;
} ThreadPool;

/* ============================================================================
 * Parallel Region (runtime representation)
 * ============================================================================ */

typedef struct {
    ParallelPattern pattern;
    
    /* Data source */
    void *data;                 /* Array pointer */
    size_t element_size;        /* Size of each element */
    size_t element_count;       /* Number of elements */
    
    /* Operations (function pointers at runtime) */
    void *map_fn;               /* Map function */
    void *filter_fn;            /* Filter predicate */
    void *reduce_fn;            /* Reduce function */
    void *identity;             /* Reduce identity element */
    
    /* Chunking */
    size_t chunk_size;          /* Elements per chunk */
    int num_chunks;             /* Total chunks */
    
    /* Result collection */
    void *results;              /* Array of partial results */
    size_t result_size;         /* Size of each result */
    
    /* Profiling */
    uint64_t start_cycles;
    uint64_t end_cycles;
} ParallelRegion;

/* ============================================================================
 * Parallelization Opportunity (compile-time analysis)
 * ============================================================================ */

typedef struct {
    ParallelPattern pattern;
    
    /* Pipeline stages involved */
    PipelineStage *first_stage;
    PipelineStage *second_stage;
    PipelineStage *third_stage;     /* NULL for single/two-stage */
    
    /* Original AST nodes */
    AstNode *source_node;           /* Data source (array, range) */
    AstNode *map_node;              /* Map operation node */
    AstNode *filter_node;           /* Filter operation node */
    AstNode *reduce_node;           /* Reduce operation node */
    
    /* Lambda/function arguments */
    AstNode *map_lambda;
    AstNode *filter_lambda;
    AstNode *reduce_lambda;
    AstNode *reduce_init;           /* Initial value for reduce */
    
    /* Associativity analysis */
    AssociativityType reduce_assoc;
    
    /* Cost/benefit analysis */
    size_t estimated_elements;      /* Estimated element count */
    int estimated_work_per_elem;    /* Work per element (cycles) */
    double estimated_speedup;       /* Expected speedup factor */
    bool requires_gather;           /* Needs result gathering */
    bool requires_sync;             /* Needs synchronization */
    
    /* Profile data hints */
    bool has_profile_data;
    uint64_t observed_element_count;/* From profile */
    double observed_time_ms;        /* From profile */
    
    /* Decision */
    bool should_parallelize;        /* Final decision */
    const char *reason;             /* Why/why not */
    
    /* Source location */
    int line;
    int column;
} ParallelOpportunity;

/* ============================================================================
 * Parallel Optimizer State
 * ============================================================================ */

typedef struct {
    /* Detection results */
    ParallelOpportunity opportunities[PARALLEL_MAX_OPPORTUNITIES];
    int opportunity_count;
    
    /* Configuration */
    int target_thread_count;        /* Threads to use (0 = auto) */
    size_t min_elements;            /* Minimum elements to parallelize */
    size_t chunk_size;              /* Preferred chunk size */
    bool profile_guided;            /* Use profile data for decisions */
    bool aggressive;                /* Parallelize more speculatively */
    bool emit_timing;               /* Emit timing instrumentation */
    
    /* Profile integration */
    QiscProfile *profile;           /* Runtime profile data */
    
    /* LLVM code generation */
    Codegen *codegen;               /* Code generator reference */
    LLVMValueRef thread_pool;       /* Global thread pool variable */
    LLVMValueRef parallel_map_fn;   /* Runtime parallel_map function */
    LLVMValueRef parallel_reduce_fn;/* Runtime parallel_reduce function */
    LLVMValueRef parallel_filter_fn;/* Runtime parallel_filter function */
    
    /* Statistics */
    int pipelines_analyzed;
    int opportunities_found;
    int regions_generated;
    int estimated_threads_used;
    double estimated_total_speedup;
    
    /* Error state */
    bool had_error;
    char error_msg[256];
} ParallelOptimizer;

/* ============================================================================
 * Parallelization Metrics
 * ============================================================================ */

typedef struct {
    /* Counts by pattern */
    int map_parallelizations;
    int filter_parallelizations;
    int reduce_parallelizations;
    int map_reduce_parallelizations;
    int loop_parallelizations;
    
    /* Aggregate metrics */
    int total_opportunities;
    int total_parallelized;
    int rejected_too_small;         /* Not enough elements */
    int rejected_no_assoc;          /* Reduce not associative */
    int rejected_has_deps;          /* Has dependencies */
    
    /* Performance estimates */
    double estimated_speedup;       /* Overall speedup factor */
    int estimated_thread_usage;     /* Average thread utilization */
    
    /* Code generation */
    int runtime_calls_generated;    /* Calls to parallel runtime */
    int work_queues_created;        /* Work queues instantiated */
    
    /* Timing */
    double analysis_time_ms;
    double codegen_time_ms;
} ParallelMetrics;

/* ============================================================================
 * Initialization and Configuration
 * ============================================================================ */

/* Initialize parallel optimizer with default settings */
void parallel_init(ParallelOptimizer *opt);

/* Initialize with code generator reference */
void parallel_init_with_codegen(ParallelOptimizer *opt, Codegen *cg);

/* Initialize with profile data for guided parallelization */
void parallel_init_with_profile(ParallelOptimizer *opt, Codegen *cg, 
                                 QiscProfile *profile);

/* Free optimizer resources */
void parallel_free(ParallelOptimizer *opt);

/* Reset optimizer state for new analysis */
void parallel_reset(ParallelOptimizer *opt);

/* Configuration setters */
void parallel_set_thread_count(ParallelOptimizer *opt, int count);
void parallel_set_min_elements(ParallelOptimizer *opt, size_t min);
void parallel_set_chunk_size(ParallelOptimizer *opt, size_t size);
void parallel_set_aggressive(ParallelOptimizer *opt, bool aggressive);
void parallel_set_profile_guided(ParallelOptimizer *opt, bool guided);

/* ============================================================================
 * Pattern Detection
 * ============================================================================ */

/* Analyze pipeline for parallelization opportunities */
int parallel_analyze_pipeline(ParallelOptimizer *opt, Pipeline *pipeline);

/* Analyze entire AST for opportunities */
int parallel_analyze_ast(ParallelOptimizer *opt, AstNode *ast);

/* Detect parallel pattern in consecutive stages */
ParallelPattern parallel_detect_pattern(PipelineStage *first,
                                         PipelineStage *second,
                                         PipelineStage *third);

/* Check if a map operation is parallelizable */
bool parallel_is_map_parallelizable(AstNode *map_lambda);

/* Check if a filter operation is parallelizable */
bool parallel_is_filter_parallelizable(AstNode *filter_lambda);

/* Check if a reduce operation is parallelizable */
bool parallel_is_reduce_parallelizable(AstNode *reduce_lambda,
                                        AssociativityType *out_assoc);

/* Detect associativity of a binary operation */
AssociativityType parallel_detect_associativity(AstNode *lambda);

/* Check if lambda has side effects (prevents parallelization) */
bool parallel_lambda_has_side_effects(AstNode *lambda);

/* Check if loop iterations are independent */
bool parallel_loop_is_independent(AstNode *for_loop);

/* Get human-readable pattern name */
const char *parallel_pattern_name(ParallelPattern pattern);

/* ============================================================================
 * Cost Model and Decision Making
 * ============================================================================ */

/* Estimate element count from source node */
size_t parallel_estimate_element_count(AstNode *source);

/* Estimate work per element (cycles) */
int parallel_estimate_work(AstNode *lambda);

/* Calculate if parallelization is profitable */
bool parallel_is_profitable(ParallelOpportunity *opp, int thread_count);

/* Calculate optimal chunk size */
size_t parallel_calc_chunk_size(size_t element_count, int thread_count,
                                 size_t element_size);

/* Estimate speedup from parallelization */
double parallel_estimate_speedup(ParallelOpportunity *opp, int thread_count);

/* Should parallelize based on profile data? */
bool parallel_should_parallelize_from_profile(ParallelOptimizer *opt,
                                               const char *location);

/* ============================================================================
 * Code Generation
 * ============================================================================ */

/* Generate runtime initialization code */
void parallel_emit_runtime_init(ParallelOptimizer *opt);

/* Generate runtime cleanup code */
void parallel_emit_runtime_cleanup(ParallelOptimizer *opt);

/* Emit thread pool creation */
LLVMValueRef parallel_emit_thread_pool(ParallelOptimizer *opt, int thread_count);

/* Generate parallel map operation */
LLVMValueRef parallel_emit_map(ParallelOptimizer *opt, 
                                ParallelOpportunity *opp);

/* Generate parallel filter operation */
LLVMValueRef parallel_emit_filter(ParallelOptimizer *opt,
                                   ParallelOpportunity *opp);

/* Generate parallel reduce operation */
LLVMValueRef parallel_emit_reduce(ParallelOptimizer *opt,
                                   ParallelOpportunity *opp);

/* Generate parallel map-reduce operation */
LLVMValueRef parallel_emit_map_reduce(ParallelOptimizer *opt,
                                       ParallelOpportunity *opp);

/* Generate parallel filter-map-reduce operation */
LLVMValueRef parallel_emit_filter_map_reduce(ParallelOptimizer *opt,
                                              ParallelOpportunity *opp);

/* Generate parallel for loop */
LLVMValueRef parallel_emit_for_loop(ParallelOptimizer *opt,
                                     AstNode *for_loop,
                                     ParallelOpportunity *opp);

/* Generate work distribution code */
LLVMValueRef parallel_emit_work_distribution(ParallelOptimizer *opt,
                                              LLVMValueRef array,
                                              LLVMValueRef count,
                                              int num_chunks);

/* Generate result collection code */
LLVMValueRef parallel_emit_result_collection(ParallelOptimizer *opt,
                                              ParallelOpportunity *opp,
                                              LLVMValueRef partial_results);

/* Emit profiling wrapper for parallel region */
LLVMValueRef parallel_emit_profiling_wrapper(ParallelOptimizer *opt,
                                              LLVMValueRef parallel_call,
                                              const char *region_name);

/* ============================================================================
 * Runtime Support Code Generation
 * ============================================================================ */

/* Emit work-stealing queue type and functions */
void parallel_emit_work_queue_types(ParallelOptimizer *opt);

/* Emit thread pool management functions */
void parallel_emit_thread_pool_funcs(ParallelOptimizer *opt);

/* Emit parallel map runtime function */
void parallel_emit_map_runtime(ParallelOptimizer *opt);

/* Emit parallel reduce runtime function */
void parallel_emit_reduce_runtime(ParallelOptimizer *opt);

/* Emit parallel filter runtime function */
void parallel_emit_filter_runtime(ParallelOptimizer *opt);

/* ============================================================================
 * AST Transformation
 * ============================================================================ */

/* Apply parallelization to detected opportunity */
AstNode *parallel_apply(ParallelOptimizer *opt, ParallelOpportunity *opp);

/* Replace pipeline stages with parallel version */
void parallel_replace_stages(AstNode *ast, ParallelOpportunity *opp,
                              AstNode *parallel_node);

/* Apply all parallelizations to AST */
int parallel_apply_all(ParallelOptimizer *opt, AstNode *ast);

/* ============================================================================
 * Thread Pool API (Runtime)
 * ============================================================================ */

/* Create thread pool with specified thread count */
ThreadPool *thread_pool_create(int thread_count);

/* Destroy thread pool */
void thread_pool_destroy(ThreadPool *pool);

/* Submit work to thread pool */
void thread_pool_submit(ThreadPool *pool, WorkItem *item);

/* Submit batch of work items */
void thread_pool_submit_batch(ThreadPool *pool, WorkItem *items, int count);

/* Wait for all work to complete */
void thread_pool_wait(ThreadPool *pool);

/* Get number of threads in pool */
int thread_pool_get_thread_count(ThreadPool *pool);

/* ============================================================================
 * Work-Stealing Queue API (Runtime)
 * ============================================================================ */

/* Initialize work-stealing queue */
void work_queue_init(WorkStealingQueue *queue);

/* Push work item (owner thread) */
bool work_queue_push(WorkStealingQueue *queue, WorkItem *item);

/* Pop work item (owner thread) */
bool work_queue_pop(WorkStealingQueue *queue, WorkItem *item);

/* Steal work item (thief thread) */
bool work_queue_steal(WorkStealingQueue *queue, WorkItem *item);

/* Check if queue is empty */
bool work_queue_is_empty(WorkStealingQueue *queue);

/* Get approximate size */
int work_queue_size(WorkStealingQueue *queue);

/* ============================================================================
 * Parallel Primitives (Runtime)
 * ============================================================================ */

/* Parallel map over array */
void parallel_map_array(ThreadPool *pool,
                         void *input, size_t elem_size, size_t count,
                         void *output,
                         void (*map_fn)(void *in, void *out));

/* Parallel filter over array */
size_t parallel_filter_array(ThreadPool *pool,
                              void *input, size_t elem_size, size_t count,
                              void *output,
                              bool (*filter_fn)(void *elem));

/* Parallel reduce over array */
void parallel_reduce_array(ThreadPool *pool,
                            void *input, size_t elem_size, size_t count,
                            void *result,
                            void *identity,
                            void (*reduce_fn)(void *accum, void *elem));

/* Parallel map-reduce (fused) */
void parallel_map_reduce(ThreadPool *pool,
                          void *input, size_t in_elem_size, size_t count,
                          void *result, size_t result_size,
                          void *identity,
                          void (*map_fn)(void *in, void *out),
                          void (*reduce_fn)(void *accum, void *elem));

/* Parallel for loop with index */
void parallel_for(ThreadPool *pool,
                   size_t start, size_t end,
                   void (*body)(size_t index, void *context),
                   void *context);

/* ============================================================================
 * Metrics and Reporting
 * ============================================================================ */

/* Get parallelization metrics */
ParallelMetrics parallel_get_metrics(ParallelOptimizer *opt);

/* Reset metrics */
void parallel_reset_metrics(ParallelOptimizer *opt);

/* Print parallelization report */
void parallel_print_report(ParallelOptimizer *opt);

/* Print detected opportunities */
void parallel_print_opportunities(ParallelOptimizer *opt);

/* Print compact summary */
void parallel_print_summary(ParallelOptimizer *opt);

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

/* Get CPU count for auto thread configuration */
int parallel_get_cpu_count(void);

/* Get cache line size */
size_t parallel_get_cache_line_size(void);

/* Align size to cache line */
size_t parallel_align_to_cache_line(size_t size);

/* Check if PARALLEL pragma is set */
bool parallel_pragma_enabled(AstNode *node);

/* Get associativity type name */
const char *parallel_assoc_name(AssociativityType assoc);

/* Generate parallel region name for profiling */
void parallel_make_region_name(char *out, size_t out_size,
                                ParallelPattern pattern,
                                int line, int column);

#endif /* QISC_OPTIMIZATION_PARALLEL_H */
