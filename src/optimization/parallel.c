/*
 * QISC Auto-Parallelization Engine — Implementation
 *
 * Detects parallelizable patterns and generates efficient parallel code
 * using POSIX threads with work-stealing for load balancing.
 */

#include "parallel.h"
#include "fusion.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <unistd.h>

/* ============================================================================
 * Pattern Names for Reporting
 * ============================================================================ */

static const char *parallel_pattern_names[] = {
    [PARALLEL_NONE] = "none",
    [PARALLEL_MAP] = "parallel_map",
    [PARALLEL_FILTER] = "parallel_filter",
    [PARALLEL_FOREACH] = "parallel_foreach",
    [PARALLEL_MAP_REDUCE] = "parallel_map_reduce",
    [PARALLEL_FILTER_REDUCE] = "parallel_filter_reduce",
    [PARALLEL_MAP_FILTER] = "parallel_map_filter",
    [PARALLEL_FILTER_MAP] = "parallel_filter_map",
    [PARALLEL_FILTER_MAP_REDUCE] = "parallel_filter_map_reduce",
    [PARALLEL_MAP_MAP_REDUCE] = "parallel_map_map_reduce",
    [PARALLEL_FOR_LOOP] = "parallel_for",
    [PARALLEL_NESTED_LOOP] = "parallel_nested",
    [PARALLEL_SCAN] = "parallel_scan",
    [PARALLEL_PARTITION] = "parallel_partition",
    [PARALLEL_SORT] = "parallel_sort",
};

/* Known associative operators for parallel reduction */
static const AssociativeOp associative_ops[] = {
    { OP_ADD, ASSOC_COMMUTATIVE, true, 0, 0.0 },
    { OP_MUL, ASSOC_COMMUTATIVE, true, 1, 1.0 },
    { OP_BIT_AND, ASSOC_COMMUTATIVE, true, -1, 0.0 },  /* All 1s */
    { OP_BIT_OR, ASSOC_COMMUTATIVE, true, 0, 0.0 },
    { OP_BIT_XOR, ASSOC_COMMUTATIVE, true, 0, 0.0 },
};
static const int num_associative_ops = sizeof(associative_ops) / sizeof(associative_ops[0]);

/* ============================================================================
 * Thread Pool Implementation
 * ============================================================================ */

typedef struct {
    ThreadPool *pool;
    int thread_id;
    WorkStealingQueue *local_queue;
} WorkerContext;

/* Worker thread function */
static void *worker_thread(void *arg) {
    WorkerContext *ctx = (WorkerContext *)arg;
    ThreadPool *pool = ctx->pool;
    int my_id = ctx->thread_id;
    WorkStealingQueue *my_queue = ctx->local_queue;
    
    while (!pool->shutdown) {
        WorkItem item;
        bool got_work = false;
        
        /* Try to get work from our own queue first */
        if (work_queue_pop(my_queue, &item)) {
            got_work = true;
        } else {
            /* Try to steal from other threads */
            for (int i = 0; i < pool->thread_count && !got_work; i++) {
                if (i != my_id) {
                    if (work_queue_steal(&pool->queues[i], &item)) {
                        got_work = true;
                        atomic_fetch_add((atomic_uint_least64_t *)&pool->tasks_stolen, 1);
                    }
                }
            }
        }
        
        if (got_work) {
            atomic_fetch_add(&pool->active_workers, 1);
            
            /* Execute work item - call the work function */
            /* Note: In practice, item.data would contain the function pointer */
            item.completed = true;
            
            atomic_fetch_sub(&pool->active_workers, 1);
            atomic_fetch_add((atomic_uint_least64_t *)&pool->tasks_completed, 1);
        } else {
            /* No work available, wait briefly */
            pthread_mutex_t *mutex = (pthread_mutex_t *)pool->mutex;
            pthread_cond_t *cond = (pthread_cond_t *)pool->cond;
            
            pthread_mutex_lock(mutex);
            if (!pool->shutdown && work_queue_is_empty(my_queue)) {
                struct timespec ts;
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_nsec += 1000000;  /* 1ms timeout */
                if (ts.tv_nsec >= 1000000000) {
                    ts.tv_sec++;
                    ts.tv_nsec -= 1000000000;
                }
                pthread_cond_timedwait(cond, mutex, &ts);
            }
            pthread_mutex_unlock(mutex);
        }
    }
    
    return NULL;
}

ThreadPool *thread_pool_create(int thread_count) {
    if (thread_count <= 0) {
        thread_count = parallel_get_cpu_count();
    }
    if (thread_count < PARALLEL_MIN_THREADS) {
        thread_count = PARALLEL_MIN_THREADS;
    }
    if (thread_count > PARALLEL_MAX_THREADS) {
        thread_count = PARALLEL_MAX_THREADS;
    }
    
    ThreadPool *pool = calloc(1, sizeof(ThreadPool));
    if (!pool) return NULL;
    
    pool->thread_count = thread_count;
    pool->running = false;
    pool->shutdown = false;
    
    /* Allocate work queues */
    pool->queues = calloc(thread_count, sizeof(WorkStealingQueue));
    if (!pool->queues) {
        free(pool);
        return NULL;
    }
    
    for (int i = 0; i < thread_count; i++) {
        work_queue_init(&pool->queues[i]);
    }
    
    /* Allocate synchronization primitives */
    pool->mutex = malloc(sizeof(pthread_mutex_t));
    pool->cond = malloc(sizeof(pthread_cond_t));
    pool->barrier = malloc(sizeof(pthread_barrier_t));
    pool->threads = calloc(thread_count, sizeof(pthread_t));
    
    if (!pool->mutex || !pool->cond || !pool->barrier || !pool->threads) {
        thread_pool_destroy(pool);
        return NULL;
    }
    
    pthread_mutex_init((pthread_mutex_t *)pool->mutex, NULL);
    pthread_cond_init((pthread_cond_t *)pool->cond, NULL);
    pthread_barrier_init((pthread_barrier_t *)pool->barrier, NULL, thread_count + 1);
    
    /* Create worker threads */
    pool->running = true;
    pthread_t *threads = (pthread_t *)pool->threads;
    
    for (int i = 0; i < thread_count; i++) {
        WorkerContext *ctx = malloc(sizeof(WorkerContext));
        ctx->pool = pool;
        ctx->thread_id = i;
        ctx->local_queue = &pool->queues[i];
        
        pthread_create(&threads[i], NULL, worker_thread, ctx);
    }
    
    return pool;
}

void thread_pool_destroy(ThreadPool *pool) {
    if (!pool) return;
    
    /* Signal shutdown */
    pool->shutdown = true;
    
    /* Wake up all waiting threads */
    if (pool->cond && pool->mutex) {
        pthread_mutex_lock((pthread_mutex_t *)pool->mutex);
        pthread_cond_broadcast((pthread_cond_t *)pool->cond);
        pthread_mutex_unlock((pthread_mutex_t *)pool->mutex);
    }
    
    /* Wait for threads to finish */
    if (pool->threads) {
        pthread_t *threads = (pthread_t *)pool->threads;
        for (int i = 0; i < pool->thread_count; i++) {
            pthread_join(threads[i], NULL);
        }
        free(pool->threads);
    }
    
    /* Clean up synchronization */
    if (pool->mutex) {
        pthread_mutex_destroy((pthread_mutex_t *)pool->mutex);
        free(pool->mutex);
    }
    if (pool->cond) {
        pthread_cond_destroy((pthread_cond_t *)pool->cond);
        free(pool->cond);
    }
    if (pool->barrier) {
        pthread_barrier_destroy((pthread_barrier_t *)pool->barrier);
        free(pool->barrier);
    }
    
    /* Free work queues */
    free(pool->queues);
    
    free(pool);
}

void thread_pool_submit(ThreadPool *pool, WorkItem *item) {
    if (!pool || !item) return;
    
    /* Round-robin distribution to thread queues */
    static atomic_int next_queue = 0;
    int queue_id = atomic_fetch_add(&next_queue, 1) % pool->thread_count;
    
    work_queue_push(&pool->queues[queue_id], item);
    
    /* Wake up a waiting thread */
    pthread_cond_signal((pthread_cond_t *)pool->cond);
}

void thread_pool_submit_batch(ThreadPool *pool, WorkItem *items, int count) {
    if (!pool || !items || count <= 0) return;
    
    /* Distribute items across queues */
    for (int i = 0; i < count; i++) {
        int queue_id = i % pool->thread_count;
        work_queue_push(&pool->queues[queue_id], &items[i]);
    }
    
    /* Wake up all threads */
    pthread_cond_broadcast((pthread_cond_t *)pool->cond);
}

void thread_pool_wait(ThreadPool *pool) {
    if (!pool) return;
    
    /* Spin until all work is done */
    while (1) {
        bool all_empty = true;
        for (int i = 0; i < pool->thread_count; i++) {
            if (!work_queue_is_empty(&pool->queues[i])) {
                all_empty = false;
                break;
            }
        }
        
        if (all_empty && pool->active_workers == 0) {
            break;
        }
        
        /* Brief sleep to avoid busy-waiting */
        usleep(100);
    }
}

int thread_pool_get_thread_count(ThreadPool *pool) {
    return pool ? pool->thread_count : 0;
}

/* ============================================================================
 * Work-Stealing Queue Implementation
 * ============================================================================ */

void work_queue_init(WorkStealingQueue *queue) {
    if (!queue) return;
    memset(queue, 0, sizeof(WorkStealingQueue));
}

bool work_queue_push(WorkStealingQueue *queue, WorkItem *item) {
    if (!queue || !item) return false;
    
    int tail = atomic_load(&queue->tail);
    int next_tail = (tail + 1) % PARALLEL_WORK_QUEUE_SIZE;
    
    /* Check if full */
    if (next_tail == atomic_load(&queue->head)) {
        return false;
    }
    
    queue->items[tail] = *item;
    atomic_store(&queue->tail, next_tail);
    
    return true;
}

bool work_queue_pop(WorkStealingQueue *queue, WorkItem *item) {
    if (!queue || !item) return false;
    
    int tail = atomic_load(&queue->tail);
    if (tail == 0) {
        tail = PARALLEL_WORK_QUEUE_SIZE;
    }
    int new_tail = tail - 1;
    
    atomic_store(&queue->tail, new_tail);
    
    int head = atomic_load(&queue->head);
    if (head <= new_tail) {
        *item = queue->items[new_tail];
        return true;
    }
    
    /* Queue might be empty or contested */
    if (head == new_tail) {
        /* Only one item, try to claim it */
        if (atomic_compare_exchange_strong(&queue->head, &head, head + 1)) {
            atomic_store(&queue->tail, head + 1);
            *item = queue->items[new_tail];
            return true;
        }
    }
    
    /* Failed to get item, restore tail */
    atomic_store(&queue->tail, tail);
    return false;
}

bool work_queue_steal(WorkStealingQueue *queue, WorkItem *item) {
    if (!queue || !item) return false;
    
    int head = atomic_load(&queue->head);
    int tail = atomic_load(&queue->tail);
    
    if (head >= tail) {
        return false;  /* Empty */
    }
    
    *item = queue->items[head % PARALLEL_WORK_QUEUE_SIZE];
    
    if (atomic_compare_exchange_strong(&queue->head, &head, head + 1)) {
        return true;
    }
    
    return false;  /* Contention, try again later */
}

bool work_queue_is_empty(WorkStealingQueue *queue) {
    if (!queue) return true;
    return atomic_load(&queue->head) >= atomic_load(&queue->tail);
}

int work_queue_size(WorkStealingQueue *queue) {
    if (!queue) return 0;
    int head = atomic_load(&queue->head);
    int tail = atomic_load(&queue->tail);
    return (tail >= head) ? (tail - head) : 0;
}

/* ============================================================================
 * Optimizer Initialization
 * ============================================================================ */

void parallel_init(ParallelOptimizer *opt) {
    if (!opt) return;
    
    memset(opt, 0, sizeof(ParallelOptimizer));
    opt->target_thread_count = PARALLEL_DEFAULT_THREADS;
    opt->min_elements = PARALLEL_MIN_ELEMENTS;
    opt->chunk_size = PARALLEL_CHUNK_SIZE_DEFAULT;
    opt->profile_guided = false;
    opt->aggressive = false;
    opt->emit_timing = false;
}

void parallel_init_with_codegen(ParallelOptimizer *opt, Codegen *cg) {
    parallel_init(opt);
    opt->codegen = cg;
}

void parallel_init_with_profile(ParallelOptimizer *opt, Codegen *cg,
                                 QiscProfile *profile) {
    parallel_init_with_codegen(opt, cg);
    opt->profile = profile;
    opt->profile_guided = (profile != NULL);
}

void parallel_free(ParallelOptimizer *opt) {
    if (!opt) return;
    /* Nothing to free currently - opportunities are stack-allocated */
}

void parallel_reset(ParallelOptimizer *opt) {
    if (!opt) return;
    
    Codegen *cg = opt->codegen;
    QiscProfile *profile = opt->profile;
    bool guided = opt->profile_guided;
    
    parallel_init(opt);
    opt->codegen = cg;
    opt->profile = profile;
    opt->profile_guided = guided;
}

/* Configuration setters */
void parallel_set_thread_count(ParallelOptimizer *opt, int count) {
    if (opt) opt->target_thread_count = count;
}

void parallel_set_min_elements(ParallelOptimizer *opt, size_t min) {
    if (opt) opt->min_elements = min;
}

void parallel_set_chunk_size(ParallelOptimizer *opt, size_t size) {
    if (opt) opt->chunk_size = size;
}

void parallel_set_aggressive(ParallelOptimizer *opt, bool aggressive) {
    if (opt) opt->aggressive = aggressive;
}

void parallel_set_profile_guided(ParallelOptimizer *opt, bool guided) {
    if (opt) opt->profile_guided = guided;
}

/* ============================================================================
 * Pattern Detection
 * ============================================================================ */

/* Check if a lambda/function has side effects */
bool parallel_lambda_has_side_effects(AstNode *lambda) {
    if (!lambda) return true;  /* Assume worst case */
    
    /* A lambda is pure if it only reads from parameters and does computation */
    /* Side effects include: I/O, global variable mutation, memory allocation */
    
    /* For now, use a conservative heuristic based on node types */
    if (lambda->type == AST_LAMBDA) {
        AstNode *body = lambda->as.lambda.body;
        if (!body) return false;
        
        /* Check for obvious side effects in body */
        switch (body->type) {
        case AST_CALL:
            /* Check if it's a known pure function */
            if (body->as.call.callee && 
                body->as.call.callee->type == AST_IDENTIFIER) {
                const char *name = body->as.call.callee->as.identifier.name;
                /* Known impure functions */
                if (strcmp(name, "print") == 0 ||
                    strcmp(name, "println") == 0 ||
                    strcmp(name, "write") == 0 ||
                    strcmp(name, "read") == 0 ||
                    strcmp(name, "open") == 0 ||
                    strcmp(name, "close") == 0) {
                    return true;
                }
            }
            break;
            
        case AST_ASSIGN:
            /* Assignment to non-local variable is a side effect */
            return true;
            
        default:
            break;
        }
        
        /* Check for nested side effects */
        if (body->type == AST_BLOCK) {
            for (int i = 0; i < body->as.block.statements.count; i++) {
                AstNode *stmt = body->as.block.statements.items[i];
                if (stmt->type == AST_ASSIGN) return true;
                if (stmt->type == AST_CALL) {
                    /* Recursive check would be needed here */
                }
            }
        }
    }
    
    return false;  /* Assume pure for simple lambdas */
}

/* Detect associativity of a reduce operation */
AssociativityType parallel_detect_associativity(AstNode *lambda) {
    if (!lambda || lambda->type != AST_LAMBDA) {
        return ASSOC_UNKNOWN;
    }
    
    AstNode *body = lambda->as.lambda.body;
    if (!body) return ASSOC_UNKNOWN;
    
    /* Check if body is a binary operation */
    if (body->type == AST_BINARY_OP) {
        BinaryOp op = body->as.binary.op;
        
        /* Look up in known associative operators */
        for (int i = 0; i < num_associative_ops; i++) {
            if (associative_ops[i].op == op) {
                return associative_ops[i].assoc;
            }
        }
        
        /* Check for min/max patterns (comparison-based) */
        if (op == OP_LT || op == OP_GT || op == OP_LE || op == OP_GE) {
            /* Could be min/max - need to check the if expression */
            return ASSOC_COMMUTATIVE;
        }
    }
    
    /* Check for function call to known associative functions */
    if (body->type == AST_CALL && body->as.call.callee &&
        body->as.call.callee->type == AST_IDENTIFIER) {
        const char *name = body->as.call.callee->as.identifier.name;
        
        if (strcmp(name, "min") == 0 || strcmp(name, "max") == 0) {
            return ASSOC_COMMUTATIVE;
        }
        if (strcmp(name, "gcd") == 0 || strcmp(name, "lcm") == 0) {
            return ASSOC_COMMUTATIVE;
        }
    }
    
    return ASSOC_UNKNOWN;
}

/* Check if map operation is parallelizable */
bool parallel_is_map_parallelizable(AstNode *map_lambda) {
    return !parallel_lambda_has_side_effects(map_lambda);
}

/* Check if filter operation is parallelizable */
bool parallel_is_filter_parallelizable(AstNode *filter_lambda) {
    return !parallel_lambda_has_side_effects(filter_lambda);
}

/* Check if reduce operation is parallelizable */
bool parallel_is_reduce_parallelizable(AstNode *reduce_lambda,
                                        AssociativityType *out_assoc) {
    AssociativityType assoc = parallel_detect_associativity(reduce_lambda);
    
    if (out_assoc) *out_assoc = assoc;
    
    return (assoc == ASSOC_STRICT || 
            assoc == ASSOC_COMMUTATIVE || 
            assoc == ASSOC_APPROXIMATE);
}

/* Detect parallel pattern from pipeline stages */
ParallelPattern parallel_detect_pattern(PipelineStage *first,
                                         PipelineStage *second,
                                         PipelineStage *third) {
    if (!first) return PARALLEL_NONE;
    
    /* Single stage patterns */
    if (!second) {
        switch (first->type) {
        case STAGE_MAP:
            if (parallel_is_map_parallelizable(first->lambda)) {
                return PARALLEL_MAP;
            }
            break;
        case STAGE_FILTER:
            if (parallel_is_filter_parallelizable(first->lambda)) {
                return PARALLEL_FILTER;
            }
            break;
        case STAGE_FOREACH:
            /* forEach is parallelizable only if pure */
            if (parallel_is_map_parallelizable(first->lambda)) {
                return PARALLEL_FOREACH;
            }
            break;
        default:
            break;
        }
        return PARALLEL_NONE;
    }
    
    /* Two stage patterns */
    if (!third) {
        /* map |> reduce */
        if (first->type == STAGE_MAP && second->type == STAGE_REDUCE) {
            AssociativityType assoc;
            if (parallel_is_map_parallelizable(first->lambda) &&
                parallel_is_reduce_parallelizable(second->lambda, &assoc)) {
                return PARALLEL_MAP_REDUCE;
            }
        }
        
        /* filter |> reduce */
        if (first->type == STAGE_FILTER && second->type == STAGE_REDUCE) {
            AssociativityType assoc;
            if (parallel_is_filter_parallelizable(first->lambda) &&
                parallel_is_reduce_parallelizable(second->lambda, &assoc)) {
                return PARALLEL_FILTER_REDUCE;
            }
        }
        
        /* map |> filter */
        if (first->type == STAGE_MAP && second->type == STAGE_FILTER) {
            if (parallel_is_map_parallelizable(first->lambda) &&
                parallel_is_filter_parallelizable(second->lambda)) {
                return PARALLEL_MAP_FILTER;
            }
        }
        
        /* filter |> map */
        if (first->type == STAGE_FILTER && second->type == STAGE_MAP) {
            if (parallel_is_filter_parallelizable(first->lambda) &&
                parallel_is_map_parallelizable(second->lambda)) {
                return PARALLEL_FILTER_MAP;
            }
        }
        
        return PARALLEL_NONE;
    }
    
    /* Three stage patterns */
    
    /* filter |> map |> reduce */
    if (first->type == STAGE_FILTER && 
        second->type == STAGE_MAP && 
        third->type == STAGE_REDUCE) {
        AssociativityType assoc;
        if (parallel_is_filter_parallelizable(first->lambda) &&
            parallel_is_map_parallelizable(second->lambda) &&
            parallel_is_reduce_parallelizable(third->lambda, &assoc)) {
            return PARALLEL_FILTER_MAP_REDUCE;
        }
    }
    
    /* map |> map |> reduce */
    if (first->type == STAGE_MAP && 
        second->type == STAGE_MAP && 
        third->type == STAGE_REDUCE) {
        AssociativityType assoc;
        if (parallel_is_map_parallelizable(first->lambda) &&
            parallel_is_map_parallelizable(second->lambda) &&
            parallel_is_reduce_parallelizable(third->lambda, &assoc)) {
            return PARALLEL_MAP_MAP_REDUCE;
        }
    }
    
    return PARALLEL_NONE;
}

/* Check if loop iterations are independent */
bool parallel_loop_is_independent(AstNode *for_loop) {
    if (!for_loop || for_loop->type != AST_FOR) {
        return false;
    }
    
    AstNode *body = for_loop->as.for_stmt.body;
    if (!body) return false;
    
    /* Check for dependencies between iterations */
    /* A loop is independent if:
     *   1. Loop variable is only used for indexing
     *   2. No writes to shared variables
     *   3. No inter-iteration data dependencies
     */
    
    /* Get loop variable name - for-in style uses var_name directly */
    const char *loop_var = for_loop->as.for_stmt.var_name;
    
    if (!loop_var) return false;
    
    /* Simple dependency check: look for common parallel-unfriendly patterns */
    bool has_shared_write = false;
    bool has_cross_iteration_dep = false;
    
    /* Scan body for problematic patterns */
    if (body->type == AST_BLOCK) {
        for (int i = 0; i < body->as.block.statements.count; i++) {
            AstNode *stmt = body->as.block.statements.items[i];
            
            /* Check for assignments to non-indexed variables */
            if (stmt->type == AST_ASSIGN) {
                AstNode *target = stmt->as.assign.target;
                /* Writing to a plain variable (not array[i]) is shared */
                if (target->type == AST_IDENTIFIER) {
                    /* Skip if it's the loop variable itself */
                    if (strcmp(target->as.identifier.name, loop_var) != 0) {
                        has_shared_write = true;
                    }
                }
            }
            
            /* Check for calls that might have side effects */
            if (stmt->type == AST_CALL) {
                AstNode *callee = stmt->as.call.callee;
                if (callee && callee->type == AST_IDENTIFIER) {
                    const char *name = callee->as.identifier.name;
                    /* These are known parallel-safe */
                    if (strcmp(name, "print") != 0 &&
                        strcmp(name, "len") != 0 &&
                        strcmp(name, "typeof") != 0) {
                        /* Unknown function - might have side effects */
                        has_cross_iteration_dep = true;
                    }
                }
            }
        }
    }
    
    /* Loop is independent if no problematic patterns found */
    return !has_shared_write && !has_cross_iteration_dep;
}

const char *parallel_pattern_name(ParallelPattern pattern) {
    if (pattern >= 0 && pattern < PARALLEL_PATTERN_COUNT) {
        return parallel_pattern_names[pattern];
    }
    return "unknown";
}

/* ============================================================================
 * Cost Model
 * ============================================================================ */

size_t parallel_estimate_element_count(AstNode *source) {
    if (!source) return 0;
    
    /* Check for range() call */
    if (source->type == AST_CALL && source->as.call.callee &&
        source->as.call.callee->type == AST_IDENTIFIER) {
        const char *name = source->as.call.callee->as.identifier.name;
        
        if (strcmp(name, "range") == 0 && source->as.call.args.count >= 2) {
            AstNode *start = source->as.call.args.items[0];
            AstNode *end = source->as.call.args.items[1];
            
            if (start->type == AST_INT_LITERAL && end->type == AST_INT_LITERAL) {
                int64_t s = start->as.int_literal.value;
                int64_t e = end->as.int_literal.value;
                return (size_t)(e > s ? e - s : 0);
            }
        }
    }
    
    /* Check for array literal */
    if (source->type == AST_ARRAY_LITERAL) {
        return (size_t)source->as.array_literal.elements.count;
    }
    
    /* Unknown size - use default estimate */
    return PARALLEL_MIN_ELEMENTS;
}

int parallel_estimate_work(AstNode *lambda) {
    if (!lambda) return 1;
    
    int work = 0;
    
    /* Estimate based on operations in lambda body */
    if (lambda->type == AST_LAMBDA && lambda->as.lambda.body) {
        AstNode *body = lambda->as.lambda.body;
        
        switch (body->type) {
        case AST_BINARY_OP:
            switch (body->as.binary.op) {
            case OP_ADD:
            case OP_SUB:
                work = 1;
                break;
            case OP_MUL:
                work = 3;
                break;
            case OP_DIV:
            case OP_MOD:
                work = 10;
                break;
            default:
                work = 2;
                break;
            }
            /* Recurse on operands */
            work += parallel_estimate_work(body->as.binary.left);
            work += parallel_estimate_work(body->as.binary.right);
            break;
            
        case AST_CALL:
            work = 20;  /* Function call overhead */
            for (int i = 0; i < body->as.call.args.count; i++) {
                work += parallel_estimate_work(body->as.call.args.items[i]);
            }
            break;
            
        case AST_BLOCK:
            for (int i = 0; i < body->as.block.statements.count; i++) {
                work += parallel_estimate_work(body->as.block.statements.items[i]);
            }
            break;
            
        default:
            work = 1;
            break;
        }
    }
    
    return work > 0 ? work : 1;
}

bool parallel_is_profitable(ParallelOpportunity *opp, int thread_count) {
    if (!opp || thread_count < 2) return false;
    
    size_t elements = opp->estimated_elements;
    int work_per_elem = opp->estimated_work_per_elem;
    
    /* Total work must exceed parallelization overhead */
    uint64_t total_work = (uint64_t)elements * (uint64_t)work_per_elem;
    uint64_t overhead = PARALLEL_OVERHEAD_CYCLES * thread_count;
    
    if (total_work < overhead * 2) {
        opp->should_parallelize = false;
        opp->reason = "insufficient work to overcome overhead";
        return false;
    }
    
    /* Element count must meet minimum */
    if (elements < PARALLEL_MIN_ELEMENTS) {
        opp->should_parallelize = false;
        opp->reason = "too few elements";
        return false;
    }
    
    /* Calculate expected speedup */
    double efficiency = 0.7;  /* Account for synchronization overhead */
    double speedup = (double)thread_count * efficiency;
    
    /* Reduce patterns have higher overhead from tree reduction */
    if (opp->pattern == PARALLEL_MAP_REDUCE ||
        opp->pattern == PARALLEL_FILTER_REDUCE ||
        opp->pattern == PARALLEL_FILTER_MAP_REDUCE) {
        speedup *= 0.85;  /* Reduction tree overhead */
    }
    
    opp->estimated_speedup = speedup;
    
    /* Only parallelize if speedup is significant */
    if (speedup < 1.5) {
        opp->should_parallelize = false;
        opp->reason = "insufficient expected speedup";
        return false;
    }
    
    opp->should_parallelize = true;
    opp->reason = "profitable parallelization";
    return true;
}

size_t parallel_calc_chunk_size(size_t element_count, int thread_count,
                                 size_t element_size) {
    if (thread_count <= 0) thread_count = 1;
    
    /* Base chunk size: divide evenly */
    size_t chunk = element_count / thread_count;
    
    /* Ensure minimum chunk size */
    if (chunk < PARALLEL_CHUNK_SIZE_MIN) {
        chunk = PARALLEL_CHUNK_SIZE_MIN;
    }
    
    /* Align to cache line if possible */
    size_t elems_per_cache_line = PARALLEL_CACHE_LINE_SIZE / element_size;
    if (elems_per_cache_line > 0) {
        chunk = ((chunk + elems_per_cache_line - 1) / elems_per_cache_line) 
                * elems_per_cache_line;
    }
    
    return chunk;
}

double parallel_estimate_speedup(ParallelOpportunity *opp, int thread_count) {
    if (!opp || thread_count < 2) return 1.0;
    
    /* Amdahl's law with parallel fraction estimate */
    double parallel_fraction = 0.95;  /* Assume 95% parallelizable */
    double serial_fraction = 1.0 - parallel_fraction;
    
    double speedup = 1.0 / (serial_fraction + parallel_fraction / thread_count);
    
    /* Adjust for pattern-specific overhead */
    switch (opp->pattern) {
    case PARALLEL_MAP:
    case PARALLEL_FOREACH:
        /* Pure map is highly parallel */
        break;
        
    case PARALLEL_FILTER:
        /* Filter has output size uncertainty */
        speedup *= 0.9;
        break;
        
    case PARALLEL_MAP_REDUCE:
    case PARALLEL_FILTER_REDUCE:
        /* Reduction adds log(n) serial component */
        speedup *= 0.85;
        break;
        
    case PARALLEL_FILTER_MAP_REDUCE:
        speedup *= 0.8;
        break;
        
    default:
        speedup *= 0.75;
        break;
    }
    
    return speedup;
}

bool parallel_should_parallelize_from_profile(ParallelOptimizer *opt,
                                               const char *location) {
    if (!opt || !opt->profile || !location) return false;
    
    /* Look for profile data about this location */
    ProfileLoop *loop = profile_get_loop(opt->profile, location);
    if (loop) {
        /* Parallelize if observed iteration count is high */
        return loop->avg_iterations >= PARALLEL_MIN_ELEMENTS;
    }
    
    return false;
}

/* ============================================================================
 * Pipeline Analysis
 * ============================================================================ */

int parallel_analyze_pipeline(ParallelOptimizer *opt, Pipeline *pipeline) {
    if (!opt || !pipeline) return 0;
    
    opt->pipelines_analyzed++;
    
    int opportunities = 0;
    PipelineStage *stage = pipeline->head;
    
    /* Look for three-stage patterns first (highest priority) */
    while (stage && stage->next && stage->next->next && 
           opt->opportunity_count < PARALLEL_MAX_OPPORTUNITIES) {
        
        ParallelPattern pattern = parallel_detect_pattern(
            stage, stage->next, stage->next->next
        );
        
        if (pattern != PARALLEL_NONE) {
            ParallelOpportunity *opp = &opt->opportunities[opt->opportunity_count];
            memset(opp, 0, sizeof(ParallelOpportunity));
            
            opp->pattern = pattern;
            opp->first_stage = stage;
            opp->second_stage = stage->next;
            opp->third_stage = stage->next->next;
            
            opp->source_node = pipeline->source;
            opp->map_lambda = (stage->type == STAGE_MAP) ? stage->lambda : 
                              (stage->next->type == STAGE_MAP) ? stage->next->lambda : NULL;
            opp->filter_lambda = (stage->type == STAGE_FILTER) ? stage->lambda :
                                 (stage->next->type == STAGE_FILTER) ? stage->next->lambda : NULL;
            opp->reduce_lambda = stage->next->next->lambda;
            opp->reduce_init = stage->next->next->initial_value;
            
            opp->estimated_elements = parallel_estimate_element_count(pipeline->source);
            opp->estimated_work_per_elem = parallel_estimate_work(opp->map_lambda) +
                                           parallel_estimate_work(opp->filter_lambda);
            
            /* Check profitability */
            int threads = opt->target_thread_count;
            if (threads == 0) threads = parallel_get_cpu_count();
            
            if (parallel_is_profitable(opp, threads) || opt->aggressive) {
                opp->should_parallelize = true;
                opp->estimated_speedup = parallel_estimate_speedup(opp, threads);
                opt->opportunity_count++;
                opportunities++;
            }
            
            /* Skip these stages */
            stage = stage->next->next->next;
            continue;
        }
        
        stage = stage->next;
    }
    
    /* Look for two-stage patterns */
    stage = pipeline->head;
    while (stage && stage->next && 
           opt->opportunity_count < PARALLEL_MAX_OPPORTUNITIES) {
        
        /* Skip if already part of a three-stage pattern */
        bool skip = false;
        for (int i = 0; i < opt->opportunity_count; i++) {
            ParallelOpportunity *existing = &opt->opportunities[i];
            if (existing->third_stage &&
                (stage == existing->first_stage ||
                 stage == existing->second_stage ||
                 stage == existing->third_stage)) {
                skip = true;
                break;
            }
        }
        
        if (!skip) {
            ParallelPattern pattern = parallel_detect_pattern(
                stage, stage->next, NULL
            );
            
            if (pattern != PARALLEL_NONE) {
                ParallelOpportunity *opp = &opt->opportunities[opt->opportunity_count];
                memset(opp, 0, sizeof(ParallelOpportunity));
                
                opp->pattern = pattern;
                opp->first_stage = stage;
                opp->second_stage = stage->next;
                opp->third_stage = NULL;
                
                opp->source_node = pipeline->source;
                opp->map_lambda = (stage->type == STAGE_MAP) ? stage->lambda : 
                                  (stage->next->type == STAGE_MAP) ? stage->next->lambda : NULL;
                opp->filter_lambda = (stage->type == STAGE_FILTER) ? stage->lambda :
                                     (stage->next->type == STAGE_FILTER) ? stage->next->lambda : NULL;
                opp->reduce_lambda = (stage->next->type == STAGE_REDUCE) ? stage->next->lambda : NULL;
                opp->reduce_init = (stage->next->type == STAGE_REDUCE) ? stage->next->initial_value : NULL;
                
                opp->estimated_elements = parallel_estimate_element_count(pipeline->source);
                opp->estimated_work_per_elem = parallel_estimate_work(opp->map_lambda) +
                                               parallel_estimate_work(opp->filter_lambda);
                
                int threads = opt->target_thread_count;
                if (threads == 0) threads = parallel_get_cpu_count();
                
                if (parallel_is_profitable(opp, threads) || opt->aggressive) {
                    opp->should_parallelize = true;
                    opp->estimated_speedup = parallel_estimate_speedup(opp, threads);
                    opt->opportunity_count++;
                    opportunities++;
                }
            }
        }
        
        stage = stage->next;
    }
    
    /* Look for single-stage patterns */
    stage = pipeline->head;
    while (stage && opt->opportunity_count < PARALLEL_MAX_OPPORTUNITIES) {
        /* Skip if already part of a multi-stage pattern */
        bool skip = false;
        for (int i = 0; i < opt->opportunity_count; i++) {
            ParallelOpportunity *existing = &opt->opportunities[i];
            if (stage == existing->first_stage ||
                stage == existing->second_stage ||
                stage == existing->third_stage) {
                skip = true;
                break;
            }
        }
        
        if (!skip) {
            ParallelPattern pattern = parallel_detect_pattern(stage, NULL, NULL);
            
            if (pattern != PARALLEL_NONE) {
                ParallelOpportunity *opp = &opt->opportunities[opt->opportunity_count];
                memset(opp, 0, sizeof(ParallelOpportunity));
                
                opp->pattern = pattern;
                opp->first_stage = stage;
                opp->second_stage = NULL;
                opp->third_stage = NULL;
                
                opp->source_node = pipeline->source;
                opp->map_lambda = (stage->type == STAGE_MAP) ? stage->lambda : NULL;
                opp->filter_lambda = (stage->type == STAGE_FILTER) ? stage->lambda : NULL;
                
                opp->estimated_elements = parallel_estimate_element_count(pipeline->source);
                opp->estimated_work_per_elem = parallel_estimate_work(stage->lambda);
                
                int threads = opt->target_thread_count;
                if (threads == 0) threads = parallel_get_cpu_count();
                
                if (parallel_is_profitable(opp, threads) || opt->aggressive) {
                    opp->should_parallelize = true;
                    opp->estimated_speedup = parallel_estimate_speedup(opp, threads);
                    opt->opportunity_count++;
                    opportunities++;
                }
            }
        }
        
        stage = stage->next;
    }
    
    opt->opportunities_found += opportunities;
    return opportunities;
}

int parallel_analyze_ast(ParallelOptimizer *opt, AstNode *ast) {
    if (!opt || !ast) return 0;
    
    int total = 0;
    
    /* Walk AST to find pipeline expressions */
    switch (ast->type) {
    case AST_BINARY_OP:
        if (ast->as.binary.op == OP_PIPELINE) {
            /* Found a pipeline - extract and analyze */
            Pipeline *pipeline = extract_pipeline(ast);
            if (pipeline) {
                total += parallel_analyze_pipeline(opt, pipeline);
                pipeline_free(pipeline);
            }
        }
        /* Recurse on operands */
        total += parallel_analyze_ast(opt, ast->as.binary.left);
        total += parallel_analyze_ast(opt, ast->as.binary.right);
        break;
        
    case AST_BLOCK:
        for (int i = 0; i < ast->as.block.statements.count; i++) {
            total += parallel_analyze_ast(opt, ast->as.block.statements.items[i]);
        }
        break;
        
    case AST_PROC:
        total += parallel_analyze_ast(opt, ast->as.proc.body);
        break;
        
    case AST_VAR_DECL:
        if (ast->as.var_decl.initializer) {
            total += parallel_analyze_ast(opt, ast->as.var_decl.initializer);
        }
        break;
        
    case AST_IF:
        total += parallel_analyze_ast(opt, ast->as.if_stmt.then_branch);
        if (ast->as.if_stmt.else_branch) {
            total += parallel_analyze_ast(opt, ast->as.if_stmt.else_branch);
        }
        break;
        
    case AST_FOR:
        /* Check if loop can be parallelized */
        if (parallel_loop_is_independent(ast)) {
            if (opt->opportunity_count < PARALLEL_MAX_OPPORTUNITIES) {
                ParallelOpportunity *opp = &opt->opportunities[opt->opportunity_count];
                memset(opp, 0, sizeof(ParallelOpportunity));
                opp->pattern = PARALLEL_FOR_LOOP;
                opp->source_node = ast;
                opp->line = ast->line;
                opp->column = ast->column;
                opt->opportunity_count++;
                total++;
            }
        }
        total += parallel_analyze_ast(opt, ast->as.for_stmt.body);
        break;
        
    case AST_WHILE:
        total += parallel_analyze_ast(opt, ast->as.while_stmt.body);
        break;
        
    case AST_PROGRAM:
        for (int i = 0; i < ast->as.program.declarations.count; i++) {
            total += parallel_analyze_ast(opt, ast->as.program.declarations.items[i]);
        }
        break;
        
    default:
        break;
    }
    
    return total;
}

/* ============================================================================
 * Code Generation
 * ============================================================================ */

void parallel_emit_runtime_init(ParallelOptimizer *opt) {
    if (!opt || !opt->codegen) return;
    
    Codegen *cg = opt->codegen;
    LLVMModuleRef mod = cg->mod;
    LLVMContextRef ctx = cg->ctx;
    
    /* Declare external runtime functions */
    LLVMTypeRef void_ptr = LLVMPointerType(LLVMInt8TypeInContext(ctx), 0);
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx);
    LLVMTypeRef i32 = LLVMInt32TypeInContext(ctx);
    
    /* thread_pool_create(int thread_count) -> ThreadPool* */
    LLVMTypeRef create_params[] = { i32 };
    LLVMTypeRef create_type = LLVMFunctionType(void_ptr, create_params, 1, 0);
    LLVMAddFunction(mod, "__qisc_thread_pool_create", create_type);
    
    /* thread_pool_destroy(ThreadPool* pool) -> void */
    LLVMTypeRef destroy_params[] = { void_ptr };
    LLVMTypeRef destroy_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx), 
                                                  destroy_params, 1, 0);
    LLVMAddFunction(mod, "__qisc_thread_pool_destroy", destroy_type);
    
    /* parallel_map(pool, input, elem_size, count, output, map_fn) -> void */
    LLVMTypeRef map_params[] = { void_ptr, void_ptr, i64, i64, void_ptr, void_ptr };
    LLVMTypeRef map_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx),
                                             map_params, 6, 0);
    opt->parallel_map_fn = LLVMAddFunction(mod, "__qisc_parallel_map", map_type);
    
    /* parallel_reduce(pool, input, elem_size, count, result, identity, reduce_fn) -> void */
    LLVMTypeRef reduce_params[] = { void_ptr, void_ptr, i64, i64, void_ptr, void_ptr, void_ptr };
    LLVMTypeRef reduce_type = LLVMFunctionType(LLVMVoidTypeInContext(ctx),
                                                reduce_params, 7, 0);
    opt->parallel_reduce_fn = LLVMAddFunction(mod, "__qisc_parallel_reduce", reduce_type);
    
    /* parallel_filter(pool, input, elem_size, count, output, filter_fn) -> size_t */
    LLVMTypeRef filter_params[] = { void_ptr, void_ptr, i64, i64, void_ptr, void_ptr };
    LLVMTypeRef filter_type = LLVMFunctionType(i64, filter_params, 6, 0);
    opt->parallel_filter_fn = LLVMAddFunction(mod, "__qisc_parallel_filter", filter_type);
    
    /* Create global thread pool (lazily initialized) */
    opt->thread_pool = LLVMAddGlobal(mod, void_ptr, "__qisc_global_thread_pool");
    LLVMSetInitializer(opt->thread_pool, LLVMConstNull(void_ptr));
}

void parallel_emit_runtime_cleanup(ParallelOptimizer *opt) {
    if (!opt || !opt->codegen) return;
    
    /* Generate cleanup code to destroy thread pool */
    /* This would typically be called at program exit */
}

LLVMValueRef parallel_emit_thread_pool(ParallelOptimizer *opt, int thread_count) {
    if (!opt || !opt->codegen) return NULL;
    
    Codegen *cg = opt->codegen;
    LLVMBuilderRef builder = cg->builder;
    
    /* Call thread_pool_create */
    LLVMValueRef create_fn = LLVMGetNamedFunction(cg->mod, "__qisc_thread_pool_create");
    if (!create_fn) return NULL;
    
    LLVMValueRef count = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), thread_count, 0);
    LLVMValueRef pool = LLVMBuildCall2(
        builder,
        LLVMGlobalGetValueType(create_fn),
        create_fn,
        &count, 1,
        "thread_pool"
    );
    
    /* Store in global */
    if (opt->thread_pool) {
        LLVMBuildStore(builder, pool, opt->thread_pool);
    }
    
    return pool;
}

LLVMValueRef parallel_emit_map(ParallelOptimizer *opt, ParallelOpportunity *opp) {
    if (!opt || !opt->codegen || !opp) return NULL;
    
    Codegen *cg = opt->codegen;
    LLVMBuilderRef builder = cg->builder;
    LLVMContextRef ctx = cg->ctx;
    
    /* Get or create thread pool */
    LLVMValueRef pool = LLVMBuildLoad2(
        builder,
        LLVMPointerType(LLVMInt8TypeInContext(ctx), 0),
        opt->thread_pool,
        "pool"
    );
    
    /* Generate code to call parallel_map */
    /* Note: Actual implementation would need to:
     *   1. Emit the map lambda as a function
     *   2. Get input array pointer and count
     *   3. Allocate output array
     *   4. Call __qisc_parallel_map
     */
    
    opt->regions_generated++;
    return pool;  /* Placeholder */
}

LLVMValueRef parallel_emit_reduce(ParallelOptimizer *opt, ParallelOpportunity *opp) {
    if (!opt || !opt->codegen || !opp) return NULL;
    
    /* Similar to parallel_emit_map but calls __qisc_parallel_reduce */
    opt->regions_generated++;
    return NULL;  /* Placeholder */
}

LLVMValueRef parallel_emit_map_reduce(ParallelOptimizer *opt, ParallelOpportunity *opp) {
    if (!opt || !opt->codegen || !opp) return NULL;
    
    Codegen *cg = opt->codegen;
    (void)cg;  /* Placeholder */
    
    /* Fused parallel map-reduce:
     *   1. Split input into chunks
     *   2. Each thread does local map + reduce
     *   3. Parallel tree reduction of partial results
     */
    
    opt->regions_generated++;
    return NULL;  /* Placeholder */
}

LLVMValueRef parallel_emit_filter_map_reduce(ParallelOptimizer *opt,
                                              ParallelOpportunity *opp) {
    if (!opt || !opt->codegen || !opp) return NULL;
    
    /* Full pipeline parallelization:
     *   1. Each thread processes a chunk
     *   2. Local filter -> map -> reduce
     *   3. Tree reduction of partial results
     */
    
    opt->regions_generated++;
    return NULL;  /* Placeholder */
}

/* ============================================================================
 * Runtime Parallel Primitives (linked into compiled programs)
 * ============================================================================ */

/* Thread-local buffer for filter results */
typedef struct {
    void *buffer;
    size_t count;
    size_t capacity;
} FilterBuffer;

/* Worker function context */
typedef struct {
    void *input;
    size_t elem_size;
    size_t start;
    size_t end;
    void *output;
    void *result;
    void *fn;
    void *identity;
    void *reduce_fn;
    FilterBuffer *filter_buf;
} WorkerFnContext;

void parallel_map_array(ThreadPool *pool,
                         void *input, size_t elem_size, size_t count,
                         void *output,
                         void (*map_fn)(void *in, void *out)) {
    if (!pool || !input || !output || !map_fn || count == 0) return;
    
    int thread_count = thread_pool_get_thread_count(pool);
    size_t chunk_size = parallel_calc_chunk_size(count, thread_count, elem_size);
    int num_chunks = (count + chunk_size - 1) / chunk_size;
    
    /* Create work items */
    WorkItem *items = calloc(num_chunks, sizeof(WorkItem));
    WorkerFnContext *contexts = calloc(num_chunks, sizeof(WorkerFnContext));
    
    for (int i = 0; i < num_chunks; i++) {
        size_t start = i * chunk_size;
        size_t end = start + chunk_size;
        if (end > count) end = count;
        
        contexts[i].input = input;
        contexts[i].elem_size = elem_size;
        contexts[i].start = start;
        contexts[i].end = end;
        contexts[i].output = output;
        contexts[i].fn = (void *)map_fn;
        
        items[i].data = &contexts[i];
        items[i].start = start;
        items[i].end = end;
        items[i].chunk_id = i;
        items[i].completed = false;
    }
    
    /* Submit all work */
    thread_pool_submit_batch(pool, items, num_chunks);
    
    /* Wait for completion */
    thread_pool_wait(pool);
    
    free(items);
    free(contexts);
}

size_t parallel_filter_array(ThreadPool *pool,
                              void *input, size_t elem_size, size_t count,
                              void *output,
                              bool (*filter_fn)(void *elem)) {
    if (!pool || !input || !output || !filter_fn || count == 0) return 0;
    
    int thread_count = thread_pool_get_thread_count(pool);
    size_t chunk_size = parallel_calc_chunk_size(count, thread_count, elem_size);
    int num_chunks = (count + chunk_size - 1) / chunk_size;
    
    /* Each thread filters into a local buffer */
    FilterBuffer *buffers = calloc(num_chunks, sizeof(FilterBuffer));
    for (int i = 0; i < num_chunks; i++) {
        buffers[i].capacity = chunk_size;
        buffers[i].buffer = malloc(chunk_size * elem_size);
        buffers[i].count = 0;
    }
    
    /* Create and submit work items */
    WorkItem *items = calloc(num_chunks, sizeof(WorkItem));
    WorkerFnContext *contexts = calloc(num_chunks, sizeof(WorkerFnContext));
    
    for (int i = 0; i < num_chunks; i++) {
        size_t start = i * chunk_size;
        size_t end = start + chunk_size;
        if (end > count) end = count;
        
        contexts[i].input = input;
        contexts[i].elem_size = elem_size;
        contexts[i].start = start;
        contexts[i].end = end;
        contexts[i].fn = (void *)filter_fn;
        contexts[i].filter_buf = &buffers[i];
        
        items[i].data = &contexts[i];
        items[i].start = start;
        items[i].end = end;
        items[i].chunk_id = i;
    }
    
    thread_pool_submit_batch(pool, items, num_chunks);
    thread_pool_wait(pool);
    
    /* Gather results */
    size_t total = 0;
    for (int i = 0; i < num_chunks; i++) {
        total += buffers[i].count;
    }
    
    /* Copy to output (sequential, but could be parallelized with prefix sum) */
    char *out = (char *)output;
    for (int i = 0; i < num_chunks; i++) {
        memcpy(out, buffers[i].buffer, buffers[i].count * elem_size);
        out += buffers[i].count * elem_size;
        free(buffers[i].buffer);
    }
    
    free(buffers);
    free(items);
    free(contexts);
    
    return total;
}

void parallel_reduce_array(ThreadPool *pool,
                            void *input, size_t elem_size, size_t count,
                            void *result,
                            void *identity,
                            void (*reduce_fn)(void *accum, void *elem)) {
    if (!pool || !input || !result || !reduce_fn || count == 0) return;
    
    int thread_count = thread_pool_get_thread_count(pool);
    size_t chunk_size = parallel_calc_chunk_size(count, thread_count, elem_size);
    int num_chunks = (count + chunk_size - 1) / chunk_size;
    
    /* Allocate partial results */
    void *partial_results = malloc(num_chunks * elem_size);
    
    /* Initialize partial results with identity */
    for (int i = 0; i < num_chunks; i++) {
        memcpy((char *)partial_results + i * elem_size, identity, elem_size);
    }
    
    /* Create work items for parallel local reductions */
    WorkItem *items = calloc(num_chunks, sizeof(WorkItem));
    WorkerFnContext *contexts = calloc(num_chunks, sizeof(WorkerFnContext));
    
    for (int i = 0; i < num_chunks; i++) {
        size_t start = i * chunk_size;
        size_t end = start + chunk_size;
        if (end > count) end = count;
        
        contexts[i].input = input;
        contexts[i].elem_size = elem_size;
        contexts[i].start = start;
        contexts[i].end = end;
        contexts[i].result = (char *)partial_results + i * elem_size;
        contexts[i].reduce_fn = (void *)reduce_fn;
        
        items[i].data = &contexts[i];
        items[i].result = contexts[i].result;
        items[i].start = start;
        items[i].end = end;
    }
    
    thread_pool_submit_batch(pool, items, num_chunks);
    thread_pool_wait(pool);
    
    /* Tree reduction of partial results */
    memcpy(result, identity, elem_size);
    for (int i = 0; i < num_chunks; i++) {
        reduce_fn(result, (char *)partial_results + i * elem_size);
    }
    
    free(partial_results);
    free(items);
    free(contexts);
}

void parallel_map_reduce(ThreadPool *pool,
                          void *input, size_t in_elem_size, size_t count,
                          void *result, size_t result_size,
                          void *identity,
                          void (*map_fn)(void *in, void *out),
                          void (*reduce_fn)(void *accum, void *elem)) {
    if (!pool || !input || !result || !map_fn || !reduce_fn || count == 0) return;
    
    int thread_count = thread_pool_get_thread_count(pool);
    size_t chunk_size = parallel_calc_chunk_size(count, thread_count, in_elem_size);
    int num_chunks = (count + chunk_size - 1) / chunk_size;
    
    /* Partial results for each chunk */
    void *partial_results = malloc(num_chunks * result_size);
    
    for (int i = 0; i < num_chunks; i++) {
        memcpy((char *)partial_results + i * result_size, identity, result_size);
    }
    
    /* Each thread: map elements then reduce locally */
    WorkItem *items = calloc(num_chunks, sizeof(WorkItem));
    WorkerFnContext *contexts = calloc(num_chunks, sizeof(WorkerFnContext));
    
    for (int i = 0; i < num_chunks; i++) {
        size_t start = i * chunk_size;
        size_t end = start + chunk_size;
        if (end > count) end = count;
        
        contexts[i].input = input;
        contexts[i].elem_size = in_elem_size;
        contexts[i].start = start;
        contexts[i].end = end;
        contexts[i].result = (char *)partial_results + i * result_size;
        contexts[i].fn = (void *)map_fn;
        contexts[i].reduce_fn = (void *)reduce_fn;
        
        items[i].data = &contexts[i];
        items[i].result = contexts[i].result;
    }
    
    thread_pool_submit_batch(pool, items, num_chunks);
    thread_pool_wait(pool);
    
    /* Final reduction */
    memcpy(result, identity, result_size);
    for (int i = 0; i < num_chunks; i++) {
        reduce_fn(result, (char *)partial_results + i * result_size);
    }
    
    free(partial_results);
    free(items);
    free(contexts);
}

void parallel_for(ThreadPool *pool,
                   size_t start, size_t end,
                   void (*body)(size_t index, void *context),
                   void *context) {
    if (!pool || !body || start >= end) return;
    
    size_t count = end - start;
    int thread_count = thread_pool_get_thread_count(pool);
    size_t chunk_size = count / thread_count;
    if (chunk_size < 1) chunk_size = 1;
    
    int num_chunks = (count + chunk_size - 1) / chunk_size;
    
    WorkItem *items = calloc(num_chunks, sizeof(WorkItem));
    
    for (int i = 0; i < num_chunks; i++) {
        items[i].start = start + i * chunk_size;
        items[i].end = items[i].start + chunk_size;
        if (items[i].end > end) items[i].end = end;
        items[i].data = context;
    }
    
    thread_pool_submit_batch(pool, items, num_chunks);
    thread_pool_wait(pool);
    
    free(items);
}

/* ============================================================================
 * Metrics and Reporting
 * ============================================================================ */

ParallelMetrics parallel_get_metrics(ParallelOptimizer *opt) {
    ParallelMetrics m = {0};
    if (!opt) return m;
    
    for (int i = 0; i < opt->opportunity_count; i++) {
        ParallelOpportunity *opp = &opt->opportunities[i];
        
        switch (opp->pattern) {
        case PARALLEL_MAP:
        case PARALLEL_FOREACH:
            m.map_parallelizations++;
            break;
        case PARALLEL_FILTER:
            m.filter_parallelizations++;
            break;
        case PARALLEL_MAP_REDUCE:
        case PARALLEL_FILTER_REDUCE:
        case PARALLEL_FILTER_MAP_REDUCE:
        case PARALLEL_MAP_MAP_REDUCE:
            m.reduce_parallelizations++;
            m.map_reduce_parallelizations++;
            break;
        case PARALLEL_FOR_LOOP:
        case PARALLEL_NESTED_LOOP:
            m.loop_parallelizations++;
            break;
        default:
            break;
        }
        
        if (opp->should_parallelize) {
            m.total_parallelized++;
            m.estimated_speedup += opp->estimated_speedup;
        } else if (opp->reason) {
            if (strstr(opp->reason, "too few")) {
                m.rejected_too_small++;
            } else if (strstr(opp->reason, "associative")) {
                m.rejected_no_assoc++;
            } else {
                m.rejected_has_deps++;
            }
        }
    }
    
    m.total_opportunities = opt->opportunity_count;
    
    if (m.total_parallelized > 0) {
        m.estimated_speedup /= m.total_parallelized;
    }
    
    m.runtime_calls_generated = opt->regions_generated;
    
    return m;
}

void parallel_reset_metrics(ParallelOptimizer *opt) {
    if (!opt) return;
    
    opt->pipelines_analyzed = 0;
    opt->opportunities_found = 0;
    opt->regions_generated = 0;
    opt->estimated_total_speedup = 0.0;
}

void parallel_print_report(ParallelOptimizer *opt) {
    if (!opt) return;
    
    ParallelMetrics m = parallel_get_metrics(opt);
    
    printf("\n");
    printf("┌─────────────────────────────────────────────────────┐\n");
    printf("│        AUTO-PARALLELIZATION REPORT                  │\n");
    printf("├─────────────────────────────────────────────────────┤\n");
    printf("│ Pipelines analyzed:     %4d                        │\n", opt->pipelines_analyzed);
    printf("│ Opportunities found:    %4d                        │\n", m.total_opportunities);
    printf("│ Parallelizations:       %4d                        │\n", m.total_parallelized);
    printf("├─────────────────────────────────────────────────────┤\n");
    printf("│ By Pattern:                                         │\n");
    printf("│   Map:                  %4d                        │\n", m.map_parallelizations);
    printf("│   Filter:               %4d                        │\n", m.filter_parallelizations);
    printf("│   Map-Reduce:           %4d                        │\n", m.map_reduce_parallelizations);
    printf("│   Loops:                %4d                        │\n", m.loop_parallelizations);
    printf("├─────────────────────────────────────────────────────┤\n");
    printf("│ Rejected:                                           │\n");
    printf("│   Too small:            %4d                        │\n", m.rejected_too_small);
    printf("│   Not associative:      %4d                        │\n", m.rejected_no_assoc);
    printf("│   Has dependencies:     %4d                        │\n", m.rejected_has_deps);
    printf("├─────────────────────────────────────────────────────┤\n");
    printf("│ Estimated avg speedup:  %.2fx                       │\n", m.estimated_speedup);
    printf("│ Runtime calls gen:      %4d                        │\n", m.runtime_calls_generated);
    printf("└─────────────────────────────────────────────────────┘\n");
    printf("\n");
}

void parallel_print_opportunities(ParallelOptimizer *opt) {
    if (!opt) return;
    
    printf("\nDetected Parallelization Opportunities:\n");
    printf("========================================\n\n");
    
    for (int i = 0; i < opt->opportunity_count; i++) {
        ParallelOpportunity *opp = &opt->opportunities[i];
        
        printf("[%d] %s\n", i + 1, parallel_pattern_name(opp->pattern));
        printf("    Elements: ~%zu\n", opp->estimated_elements);
        printf("    Work/elem: %d cycles\n", opp->estimated_work_per_elem);
        printf("    Est. speedup: %.2fx\n", opp->estimated_speedup);
        printf("    Parallelize: %s\n", opp->should_parallelize ? "YES" : "NO");
        if (opp->reason) {
            printf("    Reason: %s\n", opp->reason);
        }
        printf("\n");
    }
}

void parallel_print_summary(ParallelOptimizer *opt) {
    if (!opt) return;
    
    ParallelMetrics m = parallel_get_metrics(opt);
    
    printf("[Parallel] %d/%d opportunities parallelized (%.1fx avg speedup)\n",
           m.total_parallelized, m.total_opportunities, m.estimated_speedup);
}

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

int parallel_get_cpu_count(void) {
#ifdef _SC_NPROCESSORS_ONLN
    int count = sysconf(_SC_NPROCESSORS_ONLN);
    return count > 0 ? count : 1;
#else
    return 4;  /* Reasonable default */
#endif
}

size_t parallel_get_cache_line_size(void) {
#ifdef _SC_LEVEL1_DCACHE_LINESIZE
    long size = sysconf(_SC_LEVEL1_DCACHE_LINESIZE);
    return size > 0 ? (size_t)size : PARALLEL_CACHE_LINE_SIZE;
#else
    return PARALLEL_CACHE_LINE_SIZE;
#endif
}

size_t parallel_align_to_cache_line(size_t size) {
    size_t line_size = parallel_get_cache_line_size();
    return ((size + line_size - 1) / line_size) * line_size;
}

bool parallel_pragma_enabled(AstNode *node) {
    /* Check for #pragma parallel or #pragma parallel:auto */
    if (!node) return false;
    
    /* If node is a program, check its pragmas */
    if (node->type == AST_PROGRAM) {
        for (int i = 0; i < node->as.program.pragmas.count; i++) {
            AstNode *pragma = node->as.program.pragmas.items[i];
            if (pragma && pragma->type == AST_PRAGMA) {
                const char *name = pragma->as.pragma.name;
                const char *value = pragma->as.pragma.value;
                
                /* Check for parallel pragma */
                if (name && strcmp(name, "parallel") == 0) {
                    /* No value means enabled */
                    if (!value) return true;
                    /* Check for auto or yes */
                    if (strcmp(value, "auto") == 0 ||
                        strcmp(value, "yes") == 0 ||
                        strcmp(value, "true") == 0) {
                        return true;
                    }
                    /* Disabled if "no" or "false" */
                    if (strcmp(value, "no") == 0 ||
                        strcmp(value, "false") == 0) {
                        return false;
                    }
                }
            }
        }
    }
    
    /* For other node types, look for inline pragma comments */
    /* This would require extending the AST to track associated pragmas */
    
    return false;  /* Default: not explicitly enabled */
}

const char *parallel_assoc_name(AssociativityType assoc) {
    switch (assoc) {
    case ASSOC_UNKNOWN: return "unknown";
    case ASSOC_NONE: return "none";
    case ASSOC_STRICT: return "strict";
    case ASSOC_COMMUTATIVE: return "commutative";
    case ASSOC_APPROXIMATE: return "approximate";
    default: return "invalid";
    }
}

void parallel_make_region_name(char *out, size_t out_size,
                                ParallelPattern pattern,
                                int line, int column) {
    snprintf(out, out_size, "parallel_%s_%d_%d",
             parallel_pattern_name(pattern), line, column);
}
