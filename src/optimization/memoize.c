/*
 * QISC Automatic Memoization System — Implementation
 *
 * Profile-driven automatic memoization for pure functions.
 */

#include "memoize.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * Context Creation and Destruction
 * ============================================================================ */

MemoContext *memo_create(LLVMModuleRef module, QiscProfile *profile) {
    return memo_create_with_ir(module, profile, NULL);
}

MemoContext *memo_create_with_ir(LLVMModuleRef module, QiscProfile *profile,
                                  LivingIR *living_ir) {
    MemoContext *ctx = calloc(1, sizeof(MemoContext));
    if (!ctx) return NULL;
    
    ctx->module = module;
    ctx->llvm_ctx = module ? LLVMGetModuleContext(module) : NULL;
    ctx->profile = profile;
    ctx->living_ir = living_ir;
    
    /* Default configuration */
    ctx->config.min_call_count = MEMO_MIN_CALLS_FOR_CACHE;
    ctx->config.min_speedup = 1.2;
    ctx->config.max_cache_size = MEMO_LARGE_CACHE_SIZE;
    ctx->config.max_param_count = MEMO_MAX_PARAMS;
    ctx->config.enable_recursive = true;
    ctx->config.aggressive_mode = false;
    ctx->config.emit_statistics = true;
    ctx->config.memory_budget_mb = 16.0;
    
    return ctx;
}

void memo_destroy(MemoContext *ctx) {
    if (!ctx) return;
    free(ctx);
}

/* ============================================================================
 * Configuration
 * ============================================================================ */

void memo_set_min_calls(MemoContext *ctx, int min_calls) {
    if (ctx) ctx->config.min_call_count = min_calls;
}

void memo_set_min_speedup(MemoContext *ctx, double min_speedup) {
    if (ctx) ctx->config.min_speedup = min_speedup;
}

void memo_set_max_cache_size(MemoContext *ctx, int max_size) {
    if (ctx) ctx->config.max_cache_size = max_size;
}

void memo_set_memory_budget(MemoContext *ctx, double mb) {
    if (ctx) ctx->config.memory_budget_mb = mb;
}

void memo_set_aggressive(MemoContext *ctx, bool aggressive) {
    if (ctx) ctx->config.aggressive_mode = aggressive;
}

void memo_enable_recursive(MemoContext *ctx, bool enable) {
    if (ctx) ctx->config.enable_recursive = enable;
}

void memo_enable_statistics(MemoContext *ctx, bool enable) {
    if (ctx) ctx->config.emit_statistics = enable;
}

/* ============================================================================
 * Purity Analysis
 * ============================================================================ */

/* Check if an expression has side effects */
static uint32_t analyze_expr_effects(MemoContext *ctx, AstNode *expr) {
    if (!expr) return EFFECT_NONE;
    
    uint32_t effects = EFFECT_NONE;
    
    switch (expr->type) {
    case AST_CALL: {
        /* Check if callee is a known pure function */
        if (expr->as.call.callee && 
            expr->as.call.callee->type == AST_IDENTIFIER) {
            const char *name = expr->as.call.callee->as.identifier.name;
            if (!memo_call_is_pure(ctx, name)) {
                effects |= EFFECT_CALLS_IMPURE;
            }
        }
        /* Analyze arguments */
        for (int i = 0; i < expr->as.call.args.count; i++) {
            effects |= analyze_expr_effects(ctx, expr->as.call.args.items[i]);
        }
        break;
    }
    case AST_ASSIGN:
        effects |= EFFECT_WRITES_GLOBAL;
        if (expr->as.binary.left) {
            effects |= analyze_expr_effects(ctx, expr->as.binary.left);
        }
        if (expr->as.binary.right) {
            effects |= analyze_expr_effects(ctx, expr->as.binary.right);
        }
        break;
    case AST_BINARY_OP:
        effects |= analyze_expr_effects(ctx, expr->as.binary.left);
        effects |= analyze_expr_effects(ctx, expr->as.binary.right);
        break;
    case AST_UNARY_OP:
        effects |= analyze_expr_effects(ctx, expr->as.unary.operand);
        break;
    default:
        break;
    }
    
    return effects;
}

/* Check if a function body is pure */
static uint32_t analyze_body_effects(MemoContext *ctx, AstNode *body) {
    if (!body) return EFFECT_NONE;
    
    uint32_t effects = EFFECT_NONE;
    
    if (body->type == AST_BLOCK) {
        for (int i = 0; i < body->as.block.statements.count; i++) {
            effects |= analyze_expr_effects(ctx, body->as.block.statements.items[i]);
        }
    } else {
        effects |= analyze_expr_effects(ctx, body);
    }
    
    return effects;
}

PurityAnalysis memo_analyze_purity(MemoContext *ctx, AstNode *func_node) {
    PurityAnalysis result = {0};
    
    if (!ctx || !func_node || func_node->type != AST_PROC) {
        result.impurity_reason = "Not a function";
        return result;
    }
    
    result.function_name = func_node->as.proc.name;
    result.purity_confidence = 0.9;
    
    /* Analyze function body for side effects */
    uint32_t effects = analyze_body_effects(ctx, func_node->as.proc.body);
    result.effects = effects;
    
    if (effects == EFFECT_NONE) {
        result.is_pure = true;
    } else {
        result.is_pure = false;
        if (effects & EFFECT_WRITES_GLOBAL) {
            result.impurity_reason = "Writes to global state";
        } else if (effects & EFFECT_IO) {
            result.impurity_reason = "Performs I/O";
        } else if (effects & EFFECT_CALLS_IMPURE) {
            result.impurity_reason = "Calls impure function";
        } else {
            result.impurity_reason = "Has side effects";
        }
    }
    
    return result;
}

PurityAnalysis memo_analyze_purity_llvm(MemoContext *ctx, LLVMValueRef func) {
    PurityAnalysis result = {0};
    
    if (!ctx || !func) {
        result.impurity_reason = "Invalid function";
        return result;
    }
    
    result.function_name = LLVMGetValueName(func);
    
    /* Check LLVM attributes */
    LLVMAttributeRef readonly_attr = LLVMGetEnumAttributeAtIndex(
        func, LLVMAttributeFunctionIndex,
        LLVMGetEnumAttributeKindForName("readonly", 8));
    
    LLVMAttributeRef readnone_attr = LLVMGetEnumAttributeAtIndex(
        func, LLVMAttributeFunctionIndex,
        LLVMGetEnumAttributeKindForName("readnone", 8));
    
    if (readnone_attr || readonly_attr) {
        result.is_pure = true;
        result.purity_confidence = 1.0;
    } else {
        /* Conservative: assume impure unless proven otherwise */
        result.is_pure = false;
        result.impurity_reason = "No purity attributes found";
        result.purity_confidence = 0.5;
    }
    
    return result;
}

bool memo_expr_has_effects(MemoContext *ctx, AstNode *expr) {
    return analyze_expr_effects(ctx, expr) != EFFECT_NONE;
}

/* Known pure functions */
static const char *KNOWN_PURE_FUNCTIONS[] = {
    "abs", "sqrt", "sin", "cos", "tan", "exp", "log", "pow",
    "floor", "ceil", "round", "min", "max", "len", "strlen",
    NULL
};

bool memo_call_is_pure(MemoContext *ctx, const char *callee_name) {
    (void)ctx;
    if (!callee_name) return false;
    
    for (int i = 0; KNOWN_PURE_FUNCTIONS[i]; i++) {
        if (strcmp(callee_name, KNOWN_PURE_FUNCTIONS[i]) == 0) {
            return true;
        }
    }
    
    /* Check purity cache */
    if (ctx) {
        for (int i = 0; i < ctx->purity_cache_count; i++) {
            if (ctx->purity_cache[i].function_name &&
                strcmp(ctx->purity_cache[i].function_name, callee_name) == 0) {
                return ctx->purity_cache[i].is_pure;
            }
        }
    }
    
    return false;
}

PurityAnalysis *memo_get_purity(MemoContext *ctx, const char *func_name) {
    if (!ctx || !func_name) return NULL;
    
    for (int i = 0; i < ctx->purity_cache_count; i++) {
        if (ctx->purity_cache[i].function_name &&
            strcmp(ctx->purity_cache[i].function_name, func_name) == 0) {
            return &ctx->purity_cache[i];
        }
    }
    
    return NULL;
}

/* ============================================================================
 * Candidate Detection
 * ============================================================================ */

/* Analyze a single function */
MemoCandidate *memo_analyze_function(MemoContext *ctx, AstNode *func_node) {
    if (!ctx || !func_node || func_node->type != AST_PROC) return NULL;
    if (ctx->candidate_count >= MEMO_MAX_FUNCTIONS) return NULL;
    
    const char *name = func_node->as.proc.name;
    
    /* Check purity */
    PurityAnalysis purity = memo_analyze_purity(ctx, func_node);
    
    /* Store in purity cache */
    if (ctx->purity_cache_count < MEMO_MAX_FUNCTIONS) {
        ctx->purity_cache[ctx->purity_cache_count++] = purity;
    }
    
    if (!purity.is_pure) {
        ctx->metrics.rejected_impure++;
        return NULL;
    }
    
    /* Check call count from profile */
    int call_count = 0;
    if (ctx->profile) {
        ProfileFunction *pf = profile_get_function(ctx->profile, name);
        if (pf) call_count = (int)pf->call_count;
    }
    
    if (call_count < ctx->config.min_call_count) {
        ctx->metrics.rejected_low_calls++;
        return NULL;
    }
    
    /* Create candidate */
    MemoCandidate *candidate = &ctx->candidates[ctx->candidate_count];
    memset(candidate, 0, sizeof(MemoCandidate));
    
    candidate->function_name = name;
    candidate->ast_node = func_node;
    candidate->purity.is_recursive = purity.is_recursive;
    
    /* Analyze parameters */
    int param_count = func_node->as.proc.params.count;
    if (param_count > MEMO_MAX_PARAMS) {
        ctx->metrics.rejected_complex_args++;
        return NULL;
    }
    
    candidate->param_count = param_count;
    for (int i = 0; i < param_count; i++) {
        candidate->params[i].index = i;
        candidate->params[i].type = PARAM_TYPE_INT; /* Simplified */
        candidate->params[i].bit_width = 64;
        candidate->params[i].is_nullable = false;
    }
    
    /* Calculate estimated benefit */
    candidate->call_count = call_count;
    candidate->recommended_cache_size = memo_recommend_cache_size(ctx, name);
    candidate->estimated_speedup = memo_estimate_speedup(ctx, candidate);
    
    if (candidate->estimated_speedup < ctx->config.min_speedup) {
        ctx->metrics.rejected_low_benefit++;
        return NULL;
    }
    
    candidate->should_memoize = true;
    ctx->candidate_count++;
    ctx->metrics.candidates_found++;
    
    return candidate;
}

int memo_find_candidates(MemoContext *ctx, AstNode *program) {
    if (!ctx || !program) return 0;
    
    ctx->metrics.functions_analyzed = 0;
    ctx->metrics.pure_functions_found = 0;
    ctx->metrics.candidates_found = 0;
    
    /* Walk AST looking for function definitions */
    if (program->type == AST_PROGRAM) {
        for (int i = 0; i < program->as.program.declarations.count; i++) {
            AstNode *decl = program->as.program.declarations.items[i];
            if (decl->type == AST_PROC) {
                ctx->metrics.functions_analyzed++;
                MemoCandidate *candidate = memo_analyze_function(ctx, decl);
                if (candidate) {
                    ctx->metrics.pure_functions_found++;
                }
            }
        }
    }
    
    return ctx->candidate_count;
}

bool memo_should_memoize(MemoContext *ctx, const char *func_name) {
    MemoCandidate *candidate = memo_get_candidate(ctx, func_name);
    return candidate && candidate->should_memoize;
}

MemoCandidate *memo_get_candidate(MemoContext *ctx, const char *func_name) {
    if (!ctx || !func_name) return NULL;
    
    for (int i = 0; i < ctx->candidate_count; i++) {
        if (strcmp(ctx->candidates[i].function_name, func_name) == 0) {
            return &ctx->candidates[i];
        }
    }
    
    return NULL;
}

/* ============================================================================
 * Cache Sizing
 * ============================================================================ */

int memo_recommend_cache_size(MemoContext *ctx, const char *func_name) {
    if (!ctx || !func_name) return MEMO_DEFAULT_CACHE_SIZE;
    
    int call_count = 0;
    if (ctx->profile) {
        ProfileFunction *pf = profile_get_function(ctx->profile, func_name);
        if (pf) call_count = (int)pf->call_count;
    }
    
    if (call_count < MEMO_THRESHOLD_SMALL) {
        return MEMO_SMALL_CACHE_SIZE;
    } else if (call_count < MEMO_THRESHOLD_MEDIUM) {
        return MEMO_DEFAULT_CACHE_SIZE;
    } else if (call_count < MEMO_THRESHOLD_LARGE) {
        return MEMO_MEDIUM_CACHE_SIZE;
    } else if (call_count < MEMO_THRESHOLD_HUGE) {
        return MEMO_LARGE_CACHE_SIZE;
    } else {
        return MEMO_HUGE_CACHE_SIZE;
    }
}

double memo_estimate_hit_rate(MemoContext *ctx, MemoCandidate *candidate,
                               int cache_size) {
    (void)ctx;
    if (!candidate || cache_size <= 0) return 0.0;
    
    /* Simple model: hit rate approaches 1.0 as cache grows */
    /* Assume some locality in arguments */
    double unique_args = candidate->call_count * 0.3; /* 30% unique */
    if (unique_args < cache_size) {
        return 0.9; /* Most will hit */
    }
    
    return (double)cache_size / unique_args;
}

double memo_estimate_speedup(MemoContext *ctx, MemoCandidate *candidate) {
    if (!candidate) return 1.0;
    
    double hit_rate = memo_estimate_hit_rate(ctx, candidate, 
                                              candidate->recommended_cache_size);
    
    /* Assume function body is 10x slower than cache lookup */
    double cache_cost = 0.1;
    double compute_cost = 1.0;
    
    double avg_cost = hit_rate * cache_cost + (1.0 - hit_rate) * compute_cost;
    
    return compute_cost / avg_cost;
}

size_t memo_estimate_memory(MemoContext *ctx, MemoCandidate *candidate,
                             int cache_size) {
    (void)ctx;
    if (!candidate || cache_size <= 0) return 0;
    
    /* Each entry: hash + args + result + metadata */
    size_t entry_size = 8; /* hash */
    for (int i = 0; i < candidate->param_count; i++) {
        entry_size += candidate->params[i].bit_width / 8;
    }
    entry_size += 8; /* result */
    entry_size += 16; /* LRU metadata */
    
    return entry_size * cache_size;
}

/* ============================================================================
 * Hash Function Generation
 * ============================================================================ */

LLVMValueRef memo_gen_hash_int(MemoContext *ctx, LLVMBuilderRef builder,
                                LLVMValueRef value) {
    /* FNV-1a style hash for integers */
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->llvm_ctx);
    
    /* hash = value * 0x517cc1b727220a95 */
    LLVMValueRef multiplier = LLVMConstInt(i64, 0x517cc1b727220a95ULL, false);
    LLVMValueRef extended = LLVMBuildZExt(builder, value, i64, "hash_ext");
    
    return LLVMBuildMul(builder, extended, multiplier, "hash_int");
}

LLVMValueRef memo_gen_hash_float(MemoContext *ctx, LLVMBuilderRef builder,
                                  LLVMValueRef value) {
    /* Bit-cast float to int, then hash as int */
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->llvm_ctx);
    
    LLVMValueRef bits = LLVMBuildBitCast(builder, value, i64, "float_bits");
    return memo_gen_hash_int(ctx, builder, bits);
}

LLVMValueRef memo_gen_hash_combine(MemoContext *ctx, LLVMBuilderRef builder,
                                    LLVMValueRef *hashes, int count) {
    if (count <= 0) return LLVMConstInt(LLVMInt64TypeInContext(ctx->llvm_ctx), 0, false);
    
    LLVMValueRef combined = hashes[0];
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->llvm_ctx);
    LLVMValueRef prime = LLVMConstInt(i64, 0x00000100000001B3ULL, false);
    
    for (int i = 1; i < count; i++) {
        /* combined = combined * prime ^ hashes[i] */
        combined = LLVMBuildMul(builder, combined, prime, "hash_mul");
        combined = LLVMBuildXor(builder, combined, hashes[i], "hash_xor");
    }
    
    return combined;
}

LLVMValueRef memo_gen_hash_function(MemoContext *ctx, MemoCandidate *candidate) {
    if (!ctx || !candidate || !ctx->module) return NULL;
    
    /* Create hash function: uint64_t hash_<func>(params...) */
    char name[256];
    snprintf(name, sizeof(name), "memo_hash_%s", candidate->function_name);
    
    LLVMTypeRef i64 = LLVMInt64TypeInContext(ctx->llvm_ctx);
    LLVMTypeRef *param_types = malloc(sizeof(LLVMTypeRef) * candidate->param_count);
    
    for (int i = 0; i < candidate->param_count; i++) {
        param_types[i] = i64; /* Simplified: all params as i64 */
    }
    
    LLVMTypeRef func_type = LLVMFunctionType(i64, param_types, 
                                              candidate->param_count, false);
    LLVMValueRef func = LLVMAddFunction(ctx->module, name, func_type);
    
    free(param_types);
    
    /* Build function body */
    LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(ctx->llvm_ctx, func, "entry");
    LLVMBuilderRef builder = LLVMCreateBuilderInContext(ctx->llvm_ctx);
    LLVMPositionBuilderAtEnd(builder, entry);
    
    LLVMValueRef *param_hashes = malloc(sizeof(LLVMValueRef) * candidate->param_count);
    for (int i = 0; i < candidate->param_count; i++) {
        param_hashes[i] = memo_gen_hash_int(ctx, builder, LLVMGetParam(func, i));
    }
    
    LLVMValueRef final_hash = memo_gen_hash_combine(ctx, builder, param_hashes, 
                                                     candidate->param_count);
    LLVMBuildRet(builder, final_hash);
    
    free(param_hashes);
    LLVMDisposeBuilder(builder);
    
    return func;
}

/* ============================================================================
 * Memoized Function Generation
 * ============================================================================ */

int memo_generate_all(MemoContext *ctx) {
    if (!ctx) return 0;
    
    int generated = 0;
    
    for (int i = 0; i < ctx->candidate_count; i++) {
        MemoCandidate *candidate = &ctx->candidates[i];
        if (!candidate->should_memoize) continue;
        
        /* Generate hash function */
        LLVMValueRef hash_func = memo_gen_hash_function(ctx, candidate);
        if (!hash_func) continue;
        
        /* Store result */
        MemoizedFunction *memoized = &ctx->memoized[ctx->memoized_count];
        memoized->original_name = candidate->function_name;
        memoized->memoized_name = memoized->original_name; /* Will generate proper name */
        memoized->hash_func = hash_func;
        memoized->cache_size = candidate->recommended_cache_size;
        
        ctx->memoized_count++;
        generated++;
    }
    
    ctx->metrics.functions_memoized = generated;
    return generated;
}

/* ============================================================================
 * Living IR Integration
 * ============================================================================ */

void memo_apply_to_living_ir(MemoContext *ctx, LivingIR *ir) {
    if (!ctx || !ir) return;
    
    /* Mark memoized functions in Living IR */
    for (int i = 0; i < ctx->memoized_count; i++) {
        /* Living IR could track memoization opportunities */
    }
}

/* ============================================================================
 * Reporting
 * ============================================================================ */

MemoMetrics memo_get_metrics(MemoContext *ctx) {
    return ctx ? ctx->metrics : (MemoMetrics){0};
}

void memo_print_report(MemoContext *ctx) {
    if (!ctx) return;
    
    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║         MEMOIZATION OPTIMIZATION REPORT          ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Functions analyzed:     %5d                    ║\n", ctx->metrics.functions_analyzed);
    printf("║  Pure functions found:   %5d                    ║\n", ctx->metrics.pure_functions_found);
    printf("║  Memoization candidates: %5d                    ║\n", ctx->metrics.candidates_found);
    printf("║  Functions memoized:     %5d                    ║\n", ctx->metrics.functions_memoized);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║  Rejected (impure):      %5d                    ║\n", ctx->metrics.rejected_impure);
    printf("║  Rejected (low calls):   %5d                    ║\n", ctx->metrics.rejected_low_calls);
    printf("║  Rejected (low benefit): %5d                    ║\n", ctx->metrics.rejected_low_benefit);
    printf("║  Rejected (complex args):%5d                    ║\n", ctx->metrics.rejected_complex_args);
    printf("╚══════════════════════════════════════════════════╝\n\n");
}

void memo_print_candidates(MemoContext *ctx) {
    if (!ctx) return;
    
    printf("Memoization Candidates:\n");
    for (int i = 0; i < ctx->candidate_count; i++) {
        MemoCandidate *c = &ctx->candidates[i];
        size_t mem = memo_estimate_memory(ctx, c, c->recommended_cache_size);
        printf("  %s: calls=%lu, speedup=%.2fx, cache=%d, memory=%zu bytes\n",
               c->function_name, (unsigned long)c->call_count, c->estimated_speedup,
               c->recommended_cache_size, mem);
    }
}

/* ============================================================================
 * Verification
 * ============================================================================ */

bool memo_verify(MemoContext *ctx) {
    if (!ctx || !ctx->module) return false;
    
    char *error = NULL;
    if (LLVMVerifyModule(ctx->module, LLVMReturnStatusAction, &error)) {
        snprintf(ctx->error_msg, sizeof(ctx->error_msg), "%s", error);
        LLVMDisposeMessage(error);
        ctx->had_error = true;
        return false;
    }
    
    return true;
}

const char *memo_get_error(MemoContext *ctx) {
    return ctx ? ctx->error_msg : "No context";
}

/* ============================================================================
 * Utilities
 * ============================================================================ */

void memo_make_name(char *out, size_t out_size, const char *base_name) {
    snprintf(out, out_size, "memoized_%s", base_name);
}

const char *memo_param_type_name(MemoParamType type) {
    switch (type) {
    case PARAM_TYPE_INT: return "int";
    case PARAM_TYPE_FLOAT: return "float";
    case PARAM_TYPE_BOOL: return "bool";
    case PARAM_TYPE_STRING: return "string";
    case PARAM_TYPE_ARRAY: return "array";
    case PARAM_TYPE_STRUCT: return "struct";
    case PARAM_TYPE_POINTER: return "pointer";
    default: return "unknown";
    }
}

const char *memo_effect_name(SideEffect effect) {
    switch (effect) {
    case EFFECT_NONE: return "none";
    case EFFECT_WRITES_GLOBAL: return "writes_global";
    case EFFECT_READS_MUTABLE: return "reads_mutable";
    case EFFECT_IO: return "io";
    case EFFECT_ALLOC: return "alloc";
    case EFFECT_THROWS: return "throws";
    case EFFECT_CALLS_IMPURE: return "calls_impure";
    case EFFECT_RANDOM: return "random";
    case EFFECT_TIME: return "time";
    case EFFECT_MODIFIES_PARAM: return "modifies_param";
    default: return "unknown";
    }
}
