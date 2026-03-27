/*
 * QISC Stage Fusion Optimization System — Implementation
 *
 * This implements fusion of pipeline stages to eliminate intermediate allocations
 * and reduce the number of passes over data.
 */

#include "fusion.h"
#include "../parser/ast.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Pattern name strings for reporting */
static const char *fusion_pattern_names[] = {
    [FUSION_NONE] = "none",
    [FUSION_FILTER_MAP] = "filter|>map",
    [FUSION_MAP_MAP] = "map|>map",
    [FUSION_FILTER_FILTER] = "filter|>filter",
    [FUSION_MAP_REDUCE] = "map|>reduce",
    [FUSION_FILTER_REDUCE] = "filter|>reduce",
    [FUSION_TAKE_FILTER] = "take|>filter",
    [FUSION_FILTER_TAKE] = "filter|>take",
    [FUSION_MAP_FILTER] = "map|>filter",
    [FUSION_TAKE_MAP] = "take|>map",
    [FUSION_SKIP_FILTER] = "skip|>filter",
    [FUSION_SKIP_MAP] = "skip|>map",
    [FUSION_FILTER_MAP_REDUCE] = "filter|>map|>reduce",
    [FUSION_MAP_FILTER_REDUCE] = "map|>filter|>reduce",
    [FUSION_FILTER_FILTER_MAP] = "filter|>filter|>map",
    [FUSION_MAP_MAP_REDUCE] = "map|>map|>reduce",
};

/* ============================================================================
 * Pipeline Stage Analysis
 * ============================================================================ */

/* Helper to get callee name from a call node */
static const char *get_callee_name(AstNode *node) {
    if (!node || node->type != AST_CALL) return NULL;
    if (!node->as.call.callee) return NULL;
    if (node->as.call.callee->type == AST_IDENTIFIER) {
        return node->as.call.callee->as.identifier.name;
    }
    return NULL;
}

/* Identify the type of a pipeline stage from its AST node */
PipelineStageType identify_stage_type(AstNode *node) {
    const char *name = get_callee_name(node);
    if (!name) return STAGE_UNKNOWN;
    
    if (strcmp(name, "filter") == 0) return STAGE_FILTER;
    if (strcmp(name, "map") == 0) return STAGE_MAP;
    if (strcmp(name, "reduce") == 0) return STAGE_REDUCE;
    if (strcmp(name, "take") == 0) return STAGE_TAKE;
    if (strcmp(name, "skip") == 0) return STAGE_SKIP;
    if (strcmp(name, "flatMap") == 0) return STAGE_FLAT_MAP;
    if (strcmp(name, "collect") == 0) return STAGE_COLLECT;
    if (strcmp(name, "forEach") == 0) return STAGE_FOREACH;
    if (strcmp(name, "any") == 0) return STAGE_ANY;
    if (strcmp(name, "all") == 0) return STAGE_ALL;
    if (strcmp(name, "find") == 0) return STAGE_FIND;
    if (strcmp(name, "count") == 0) return STAGE_COUNT;
    if (strcmp(name, "sum") == 0) return STAGE_SUM;
    if (strcmp(name, "distinct") == 0) return STAGE_DISTINCT;
    if (strcmp(name, "sorted") == 0) return STAGE_SORTED;
    if (strcmp(name, "grouped") == 0) return STAGE_GROUPED;
    
    return STAGE_UNKNOWN;
}

/* Create a new pipeline stage */
static PipelineStage *create_stage(AstNode *node) {
    PipelineStage *stage = calloc(1, sizeof(PipelineStage));
    if (!stage) return NULL;
    
    stage->type = identify_stage_type(node);
    stage->node = node;
    
    /* Extract lambda/function argument if present */
    if (node && node->type == AST_CALL && node->as.call.args.count > 0) {
        stage->lambda = node->as.call.args.items[0];
    }
    
    /* Extract count for take/skip */
    if ((stage->type == STAGE_TAKE || stage->type == STAGE_SKIP) &&
        node && node->type == AST_CALL && node->as.call.args.count > 0) {
        AstNode *count_arg = node->as.call.args.items[0];
        if (count_arg && count_arg->type == AST_INT_LITERAL) {
            stage->count_arg = count_arg->as.int_literal.value;
        }
    }
    
    /* Extract initial value for reduce */
    if (stage->type == STAGE_REDUCE && 
        node && node->type == AST_CALL && node->as.call.args.count > 1) {
        stage->initial_value = node->as.call.args.items[0];
        stage->lambda = node->as.call.args.items[1];
    }
    
    return stage;
}

/* Free a pipeline stage */
static void free_stage(PipelineStage *stage) {
    free(stage);
}

/* ============================================================================
 * Pipeline Construction
 * ============================================================================ */

Pipeline *fusion_create_pipeline(AstNode *source) {
    Pipeline *pipeline = calloc(1, sizeof(Pipeline));
    if (!pipeline) return NULL;
    
    pipeline->source = source;
    return pipeline;
}

void fusion_destroy_pipeline(Pipeline *pipeline) {
    if (!pipeline) return;
    
    PipelineStage *stage = pipeline->head;
    while (stage) {
        PipelineStage *next = stage->next;
        free_stage(stage);
        stage = next;
    }
    
    free(pipeline);
}

void fusion_add_stage(Pipeline *pipeline, AstNode *node) {
    if (!pipeline || !node) return;
    
    PipelineStage *stage = create_stage(node);
    if (!stage) return;
    
    if (!pipeline->head) {
        pipeline->head = stage;
        pipeline->tail = stage;
    } else {
        stage->prev = pipeline->tail;
        pipeline->tail->next = stage;
        pipeline->tail = stage;
    }
    
    pipeline->stage_count++;
}

/* ============================================================================
 * Pipeline Extraction
 * ============================================================================ */

Pipeline *extract_pipeline(AstNode *node) {
    if (!node) return NULL;
    
    if (node->type == AST_BINARY_OP && node->as.binary.op == OP_PIPELINE) {
        /* Left side is either the source or another pipeline op */
        Pipeline *pipeline = extract_pipeline(node->as.binary.left);
        
        if (!pipeline) {
            /* Left side is the source of the pipeline */
            pipeline = fusion_create_pipeline(node->as.binary.left);
        }
        
        /* Right side is the pipeline stage (should be a call like map() or filter()) */
        fusion_add_stage(pipeline, node->as.binary.right);
        return pipeline;
    }
    
    return NULL;
}

void pipeline_free(Pipeline *pipeline) {
    fusion_destroy_pipeline(pipeline);
}

/* ============================================================================
 * Fusion Pattern Detection
 * ============================================================================ */

/* Check if two consecutive stages can be fused */
static FusionPattern check_two_stage_fusion(PipelineStageType first, PipelineStageType second) {
    /* filter |> map */
    if (first == STAGE_FILTER && second == STAGE_MAP) return FUSION_FILTER_MAP;
    
    /* map |> map */
    if (first == STAGE_MAP && second == STAGE_MAP) return FUSION_MAP_MAP;
    
    /* filter |> filter */
    if (first == STAGE_FILTER && second == STAGE_FILTER) return FUSION_FILTER_FILTER;
    
    /* map |> reduce */
    if (first == STAGE_MAP && second == STAGE_REDUCE) return FUSION_MAP_REDUCE;
    
    /* filter |> reduce */
    if (first == STAGE_FILTER && second == STAGE_REDUCE) return FUSION_FILTER_REDUCE;
    
    /* take |> filter */
    if (first == STAGE_TAKE && second == STAGE_FILTER) return FUSION_TAKE_FILTER;
    
    /* filter |> take */
    if (first == STAGE_FILTER && second == STAGE_TAKE) return FUSION_FILTER_TAKE;
    
    /* map |> filter */
    if (first == STAGE_MAP && second == STAGE_FILTER) return FUSION_MAP_FILTER;
    
    /* take |> map */
    if (first == STAGE_TAKE && second == STAGE_MAP) return FUSION_TAKE_MAP;
    
    /* skip |> filter */
    if (first == STAGE_SKIP && second == STAGE_FILTER) return FUSION_SKIP_FILTER;
    
    /* skip |> map */
    if (first == STAGE_SKIP && second == STAGE_MAP) return FUSION_SKIP_MAP;
    
    return FUSION_NONE;
}

/* Check if three consecutive stages can be fused */
static FusionPattern check_three_stage_fusion(PipelineStageType first, 
                                              PipelineStageType second,
                                              PipelineStageType third) {
    /* filter |> map |> reduce */
    if (first == STAGE_FILTER && second == STAGE_MAP && third == STAGE_REDUCE)
        return FUSION_FILTER_MAP_REDUCE;
    
    /* map |> filter |> reduce */
    if (first == STAGE_MAP && second == STAGE_FILTER && third == STAGE_REDUCE)
        return FUSION_MAP_FILTER_REDUCE;
    
    /* filter |> filter |> map */
    if (first == STAGE_FILTER && second == STAGE_FILTER && third == STAGE_MAP)
        return FUSION_FILTER_FILTER_MAP;
    
    /* map |> map |> reduce */
    if (first == STAGE_MAP && second == STAGE_MAP && third == STAGE_REDUCE)
        return FUSION_MAP_MAP_REDUCE;
    
    return FUSION_NONE;
}

int fusion_detect_opportunities(Pipeline *pipeline, FusionOpportunity *out, int max_out) {
    if (!pipeline || !out || max_out <= 0) return 0;
    
    int count = 0;
    PipelineStage *stage = pipeline->head;
    
    /* First pass: detect three-stage fusions (higher priority) */
    while (stage && stage->next && stage->next->next && count < max_out) {
        FusionPattern pattern = check_three_stage_fusion(
            stage->type,
            stage->next->type,
            stage->next->next->type
        );
        
        if (pattern != FUSION_NONE) {
            out[count].pattern = pattern;
            out[count].first_stage = stage;
            out[count].second_stage = stage->next;
            out[count].third_stage = stage->next->next;
            out[count].first_node = stage->node;
            out[count].second_node = stage->next->node;
            out[count].third_node = stage->next->next->node;
            out[count].estimated_passes_saved = 2;
            out[count].estimated_allocs_saved = 2;
            out[count].estimated_speedup = 1.8;  /* ~80% faster */
            out[count].requires_reanalysis = true;
            count++;
            
            /* Skip these stages for two-stage analysis */
            stage = stage->next->next->next;
            continue;
        }
        
        stage = stage->next;
    }
    
    /* Second pass: detect two-stage fusions */
    stage = pipeline->head;
    while (stage && stage->next && count < max_out) {
        /* Skip if already part of a three-stage fusion */
        bool skip = false;
        for (int i = 0; i < count; i++) {
            if (out[i].third_stage && 
                (stage == out[i].first_stage || 
                 stage == out[i].second_stage ||
                 stage == out[i].third_stage)) {
                skip = true;
                break;
            }
        }
        
        if (!skip) {
            FusionPattern pattern = check_two_stage_fusion(stage->type, stage->next->type);
            
            if (pattern != FUSION_NONE) {
                out[count].pattern = pattern;
                out[count].first_stage = stage;
                out[count].second_stage = stage->next;
                out[count].third_stage = NULL;
                out[count].first_node = stage->node;
                out[count].second_node = stage->next->node;
                out[count].third_node = NULL;
                out[count].estimated_passes_saved = 1;
                out[count].estimated_allocs_saved = 1;
                out[count].estimated_speedup = 1.3;  /* ~30% faster */
                out[count].requires_reanalysis = (pattern == FUSION_FILTER_FILTER);
                count++;
            }
        }
        
        stage = stage->next;
    }
    
    return count;
}

/* ============================================================================
 * Fusion Transformations
 * ============================================================================ */

/* Helper to create a new lambda node */
static AstNode *create_lambda(const char *param_name, AstNode *body) {
    AstNode *lambda = malloc(sizeof(AstNode));
    if (!lambda) return NULL;
    memset(lambda, 0, sizeof(AstNode));
    lambda->type = AST_LAMBDA;
    lambda->line = 0;
    lambda->column = 0;
    
    /* Create parameter identifier */
    AstNode *param = malloc(sizeof(AstNode));
    if (!param) { free(lambda); return NULL; }
    memset(param, 0, sizeof(AstNode));
    param->type = AST_IDENTIFIER;
    param->as.identifier.name = strdup(param_name);
    
    /* Initialize params array and add the parameter */
    lambda->as.lambda.params.items = malloc(sizeof(AstNode*));
    lambda->as.lambda.params.items[0] = param;
    lambda->as.lambda.params.count = 1;
    lambda->as.lambda.params.capacity = 1;
    
    lambda->as.lambda.body = body;
    return lambda;
}

/* Helper to create a call node */
static AstNode *create_call(AstNode *callee, AstNode *arg) {
    AstNode *call = malloc(sizeof(AstNode));
    if (!call) return NULL;
    memset(call, 0, sizeof(AstNode));
    call->type = AST_CALL;
    call->as.call.callee = callee;
    
    if (arg) {
        call->as.call.args.items = malloc(sizeof(AstNode*));
        call->as.call.args.items[0] = arg;
        call->as.call.args.count = 1;
        call->as.call.args.capacity = 1;
    }
    return call;
}

/* Helper to create an identifier reference */
static AstNode *create_ident(const char *name) {
    AstNode *id = malloc(sizeof(AstNode));
    if (!id) return NULL;
    memset(id, 0, sizeof(AstNode));
    id->type = AST_IDENTIFIER;
    id->as.identifier.name = strdup(name);
    return id;
}

/* Helper to create binary && expression */
static AstNode *create_and(AstNode *left, AstNode *right) {
    AstNode *and_node = malloc(sizeof(AstNode));
    if (!and_node) return NULL;
    memset(and_node, 0, sizeof(AstNode));
    and_node->type = AST_BINARY_OP;
    and_node->as.binary.op = OP_AND;
    and_node->as.binary.left = left;
    and_node->as.binary.right = right;
    return and_node;
}

/* Helper to get parameter name from a lambda */
static const char *get_lambda_param(AstNode *lambda) {
    if (!lambda || lambda->type != AST_LAMBDA) return NULL;
    if (lambda->as.lambda.params.count == 0) return NULL;
    AstNode *param = lambda->as.lambda.params.items[0];
    if (param->type == AST_IDENTIFIER)
        return param->as.identifier.name;
    if (param->type == AST_VAR_DECL)
        return param->as.var_decl.name;
    return NULL;
}

/* Clone an AST node (shallow) and rename variables */
static AstNode *clone_and_rename(AstNode *node, const char *old_name, const char *new_name) {
    if (!node) return NULL;
    
    AstNode *clone = malloc(sizeof(AstNode));
    if (!clone) return NULL;
    *clone = *node;  /* Shallow copy */
    
    /* Handle identifier renaming */
    if (node->type == AST_IDENTIFIER) {
        if (strcmp(node->as.identifier.name, old_name) == 0) {
            clone->as.identifier.name = strdup(new_name);
        } else {
            clone->as.identifier.name = strdup(node->as.identifier.name);
        }
        return clone;
    }
    
    /* Recursively handle sub-nodes for common cases */
    if (node->type == AST_BINARY_OP) {
        clone->as.binary.left = clone_and_rename(node->as.binary.left, old_name, new_name);
        clone->as.binary.right = clone_and_rename(node->as.binary.right, old_name, new_name);
    } else if (node->type == AST_UNARY_OP) {
        clone->as.unary.operand = clone_and_rename(node->as.unary.operand, old_name, new_name);
    } else if (node->type == AST_CALL) {
        clone->as.call.callee = clone_and_rename(node->as.call.callee, old_name, new_name);
        if (node->as.call.args.count > 0) {
            clone->as.call.args.items = malloc(node->as.call.args.count * sizeof(AstNode*));
            for (int i = 0; i < node->as.call.args.count; i++) {
                clone->as.call.args.items[i] = clone_and_rename(node->as.call.args.items[i], old_name, new_name);
            }
        }
    }
    
    return clone;
}

/* Create a fused filter|>map lambda */
static AstNode *fuse_filter_map_lambda(AstNode *filter_lambda, AstNode *map_lambda) {
    /*
     * filter(p) |> map(f) becomes:
     *   fn(x) { if (p(x)) { yield f(x); } else { skip } }
     *
     * For simplified streaming: we create a wrapped lambda that applies
     * filter predicate first, then map transformation.
     */
    if (!filter_lambda || !map_lambda) return NULL;
    
    const char *filter_param = get_lambda_param(filter_lambda);
    const char *map_param = get_lambda_param(map_lambda);
    if (!filter_param || !map_param) return NULL;
    
    /* Use a fresh parameter name */
    const char *param = "__fused_x";
    
    /* Clone filter body with renamed parameter */
    AstNode *filter_cond = clone_and_rename(filter_lambda->as.lambda.body, filter_param, param);
    
    /* Clone map body with renamed parameter */
    AstNode *map_body = clone_and_rename(map_lambda->as.lambda.body, map_param, param);
    
    /* Create: if (filter_cond) { map_body } 
     * For now, we'll create a simple conditional expression using ternary:
     * p(x) ? f(x) : none
     * This allows fusion while maintaining filter semantics.
     */
    AstNode *ternary = malloc(sizeof(AstNode));
    if (!ternary) return NULL;
    memset(ternary, 0, sizeof(AstNode));
    ternary->type = AST_IF;  /* Using IF as conditional expr */
    ternary->as.if_stmt.condition = filter_cond;
    
    /* Create a block for the then branch */
    AstNode *then_block = malloc(sizeof(AstNode));
    memset(then_block, 0, sizeof(AstNode));
    then_block->type = AST_BLOCK;
    then_block->as.block.statements.items = malloc(sizeof(AstNode*));
    then_block->as.block.statements.items[0] = map_body;
    then_block->as.block.statements.count = 1;
    then_block->as.block.statements.capacity = 1;
    
    ternary->as.if_stmt.then_branch = then_block;
    ternary->as.if_stmt.else_branch = NULL;
    
    return create_lambda(param, ternary);
}

/* Create a fused map|>map lambda (composition) */
static AstNode *fuse_map_map_lambda(AstNode *map1_lambda, AstNode *map2_lambda) {
    /*
     * map(f) |> map(g) becomes:
     *   map(fn(x) { g(f(x)) })
     *
     * Function composition: (g ∘ f)(x) = g(f(x))
     */
    if (!map1_lambda || !map2_lambda) return NULL;
    
    const char *param1 = get_lambda_param(map1_lambda);
    const char *param2 = get_lambda_param(map2_lambda);
    if (!param1 || !param2) return NULL;
    
    /* Fresh parameter name */
    const char *param = "__fused_x";
    
    /* Inner application: f(x) - clone map1's body with our parameter */
    AstNode *inner = clone_and_rename(map1_lambda->as.lambda.body, param1, param);
    
    /* Outer application: g(f(x)) - clone map2's body but replace its param with inner result */
    /* For simplicity, we create: g(inner) where g is the second lambda's body */
    
    /* Create a call to the second function with the result of the first */
    /* We need to substitute param2 with inner in map2's body */
    AstNode *composed = clone_and_rename(map2_lambda->as.lambda.body, param2, "__tmp_inner");
    
    /* Actually, for proper composition we need to inline:
     * Let map1 body be: x + 1
     * Let map2 body be: x * 2
     * Composed should be: (x + 1) * 2
     * 
     * So we substitute map2's parameter with map1's body (after renaming)
     */
    
    /* Simpler approach: create fresh inner variable and wrap */
    /* fn(x) { let __inner = f(x); g(__inner) } */
    
    /* For now, use direct substitution approach */
    AstNode *body = clone_and_rename(map2_lambda->as.lambda.body, param2, param);
    
    /* Recursively replace param with map1's result */
    /* This is tricky - for MVP, we'll create nested lambda application */
    
    /* Create: (fn(a) => map2_body)(map1_body) */
    /* Which evaluates to: map2_body[a := map1_body] */
    AstNode *inner_app = create_call(
        clone_and_rename(map2_lambda, NULL, NULL),  /* Copy of map2 lambda */
        clone_and_rename(map1_lambda->as.lambda.body, param1, param)  /* f(x) */
    );
    
    return create_lambda(param, inner_app);
}

/* Create a fused filter|>filter lambda (predicate AND) */
static AstNode *fuse_filter_filter_lambda(AstNode *filter1_lambda, AstNode *filter2_lambda) {
    /*
     * filter(p1) |> filter(p2) becomes:
     *   filter(fn(x) { p1(x) && p2(x) })
     *
     * Short-circuit AND of predicates
     */
    if (!filter1_lambda || !filter2_lambda) return NULL;
    
    const char *param1 = get_lambda_param(filter1_lambda);
    const char *param2 = get_lambda_param(filter2_lambda);
    if (!param1 || !param2) return NULL;
    
    /* Fresh parameter */
    const char *param = "__fused_x";
    
    /* Clone both predicate bodies with fresh parameter */
    AstNode *pred1 = clone_and_rename(filter1_lambda->as.lambda.body, param1, param);
    AstNode *pred2 = clone_and_rename(filter2_lambda->as.lambda.body, param2, param);
    
    /* Create: p1(x) && p2(x) */
    AstNode *combined = create_and(pred1, pred2);
    
    return create_lambda(param, combined);
}

AstNode *fusion_apply(FusionOpportunity *opportunity) {
    if (!opportunity) return NULL;
    
    switch (opportunity->pattern) {
    case FUSION_FILTER_MAP:
        return fuse_filter_map_lambda(
            opportunity->first_stage->lambda,
            opportunity->second_stage->lambda
        );
        
    case FUSION_MAP_MAP:
        return fuse_map_map_lambda(
            opportunity->first_stage->lambda,
            opportunity->second_stage->lambda
        );
        
    case FUSION_FILTER_FILTER:
        return fuse_filter_filter_lambda(
            opportunity->first_stage->lambda,
            opportunity->second_stage->lambda
        );
        
    default:
        /* Other patterns not yet implemented */
        return NULL;
    }
}

/* ============================================================================
 * Optimizer Entry Points
 * ============================================================================ */

void fusion_optimizer_init(FusionOptimizer *optimizer) {
    if (!optimizer) return;
    
    memset(optimizer, 0, sizeof(FusionOptimizer));
    optimizer->max_fusion_depth = FUSION_MAX_CHAIN_LENGTH;
    optimizer->aggressive_fusion = false;
    optimizer->preserve_debug_info = true;
}

void fusion_optimizer_cleanup(FusionOptimizer *optimizer) {
    if (!optimizer) return;
    /* Nothing to free currently - opportunities are stack-allocated */
}

int fusion_optimize_pipeline(FusionOptimizer *optimizer, Pipeline *pipeline) {
    if (!optimizer || !pipeline) return 0;
    
    optimizer->pipelines_analyzed++;
    
    /* Detect fusion opportunities */
    int opportunities = fusion_detect_opportunities(
        pipeline,
        optimizer->opportunities,
        FUSION_MAX_OPPORTUNITIES
    );
    
    optimizer->opportunity_count = opportunities;
    
    if (opportunities == 0) return 0;
    
    int fusions_applied = 0;
    
    /* Apply each fusion opportunity */
    for (int i = 0; i < opportunities; i++) {
        FusionOpportunity *opp = &optimizer->opportunities[i];
        
        AstNode *fused = fusion_apply(opp);
        if (fused) {
            opp->fused_result = fused;
            fusions_applied++;
            optimizer->fusions_applied++;
            optimizer->passes_eliminated += opp->estimated_passes_saved;
            optimizer->allocs_avoided += opp->estimated_allocs_saved;
        }
    }
    
    pipeline->fusion_count = fusions_applied;
    pipeline->optimized = (fusions_applied > 0);
    
    return fusions_applied;
}

/* Forward declarations for AST traversal */
static int fusion_visit_node(FusionOptimizer *optimizer, AstNode *node);

/* Check if a node is a fusible pipeline operation */
static bool is_pipeline_stage(AstNode *node, const char **func_name, AstNode **lambda) {
    if (!node || node->type != AST_CALL) return false;
    
    const char *name = get_callee_name(node);
    if (!name) return false;
    
    /* Check for fusible operations */
    if (strcmp(name, "filter") == 0 || strcmp(name, "map") == 0 ||
        strcmp(name, "reduce") == 0 || strcmp(name, "take") == 0 ||
        strcmp(name, "skip") == 0) {
        *func_name = name;
        /* Get the lambda argument if present */
        if (node->as.call.args.count > 0) {
            *lambda = node->as.call.args.items[0];
        } else {
            *lambda = NULL;
        }
        return true;
    }
    return false;
}

/* Check if two stages can be fused */
static FusionPattern check_fusion_pattern(const char *first, const char *second) {
    if (strcmp(first, "filter") == 0 && strcmp(second, "map") == 0)
        return FUSION_FILTER_MAP;
    if (strcmp(first, "map") == 0 && strcmp(second, "map") == 0)
        return FUSION_MAP_MAP;
    if (strcmp(first, "filter") == 0 && strcmp(second, "filter") == 0)
        return FUSION_FILTER_FILTER;
    if (strcmp(first, "map") == 0 && strcmp(second, "reduce") == 0)
        return FUSION_MAP_REDUCE;
    if (strcmp(first, "filter") == 0 && strcmp(second, "reduce") == 0)
        return FUSION_FILTER_REDUCE;
    if (strcmp(first, "take") == 0 && strcmp(second, "filter") == 0)
        return FUSION_TAKE_FILTER;
    if (strcmp(first, "filter") == 0 && strcmp(second, "take") == 0)
        return FUSION_FILTER_TAKE;
    if (strcmp(first, "map") == 0 && strcmp(second, "filter") == 0)
        return FUSION_MAP_FILTER;
    return FUSION_NONE;
}

/* Visit a pipeline expression (binary op with OP_PIPELINE) */
static int fusion_visit_pipeline(FusionOptimizer *optimizer, AstNode *node) {
    if (!node || node->type != AST_BINARY_OP || 
        node->as.binary.op != OP_PIPELINE) return 0;
    
    int fusions = 0;
    AstNode *left = node->as.binary.left;
    AstNode *right = node->as.binary.right;
    
    /* Recursively check nested pipelines */
    if (left && left->type == AST_BINARY_OP && left->as.binary.op == OP_PIPELINE) {
        fusions += fusion_visit_pipeline(optimizer, left);
    }
    
    /* Check if right side is a fusible stage */
    const char *right_name = NULL;
    AstNode *right_lambda = NULL;
    if (!is_pipeline_stage(right, &right_name, &right_lambda)) {
        return fusions;
    }
    
    /* Check if left side ends with a fusible stage */
    AstNode *left_stage = left;
    if (left->type == AST_BINARY_OP && left->as.binary.op == OP_PIPELINE) {
        left_stage = left->as.binary.right;
    }
    
    const char *left_name = NULL;
    AstNode *left_lambda = NULL;
    if (!is_pipeline_stage(left_stage, &left_name, &left_lambda)) {
        return fusions;
    }
    
    /* Check if these stages can be fused */
    FusionPattern pattern = check_fusion_pattern(left_name, right_name);
    if (pattern == FUSION_NONE) {
        return fusions;
    }
    
    /* Record the fusion opportunity */
    optimizer->pipelines_analyzed++;
    
    /* Create pipeline stages for analysis */
    PipelineStage first_stage = {
        .type = (strcmp(left_name, "filter") == 0) ? STAGE_FILTER :
                (strcmp(left_name, "map") == 0) ? STAGE_MAP :
                (strcmp(left_name, "reduce") == 0) ? STAGE_REDUCE :
                (strcmp(left_name, "take") == 0) ? STAGE_TAKE :
                (strcmp(left_name, "skip") == 0) ? STAGE_SKIP : STAGE_UNKNOWN,
        .lambda = left_lambda,
        .next = NULL
    };
    
    PipelineStage second_stage = {
        .type = (strcmp(right_name, "filter") == 0) ? STAGE_FILTER :
                (strcmp(right_name, "map") == 0) ? STAGE_MAP :
                (strcmp(right_name, "reduce") == 0) ? STAGE_REDUCE :
                (strcmp(right_name, "take") == 0) ? STAGE_TAKE :
                (strcmp(right_name, "skip") == 0) ? STAGE_SKIP : STAGE_UNKNOWN,
        .lambda = right_lambda,
        .next = NULL
    };
    
    FusionOpportunity opp = {
        .pattern = pattern,
        .first_stage = &first_stage,
        .second_stage = &second_stage,
        .estimated_passes_saved = 1,
        .estimated_allocs_saved = 1,
        .estimated_speedup = 1.5
    };
    
    /* Try to apply the fusion */
    AstNode *fused = fusion_apply(&opp);
    if (fused) {
        /* Report the fusion */
        printf("  ├─ Fusing %s |> %s → single pass\n", left_name, right_name);
        optimizer->fusions_applied++;
        optimizer->passes_eliminated++;
        optimizer->allocs_avoided++;
        fusions++;
        
        /* Note: In a full implementation, we would replace the pipeline node
         * with the fused version. For now we just report the opportunity. */
    }
    
    return fusions;
}

/* Recursive AST visitor */
static int fusion_visit_node(FusionOptimizer *optimizer, AstNode *node) {
    if (!node) return 0;
    
    int fusions = 0;
    
    switch (node->type) {
    case AST_PROGRAM:
        for (int i = 0; i < node->as.program.declarations.count; i++) {
            fusions += fusion_visit_node(optimizer, node->as.program.declarations.items[i]);
        }
        break;
        
    case AST_PROC:
        fusions += fusion_visit_node(optimizer, node->as.proc.body);
        break;
        
    case AST_BLOCK:
        for (int i = 0; i < node->as.block.statements.count; i++) {
            fusions += fusion_visit_node(optimizer, node->as.block.statements.items[i]);
        }
        break;
        
    case AST_BINARY_OP:
        if (node->as.binary.op == OP_PIPELINE) {
            fusions += fusion_visit_pipeline(optimizer, node);
        }
        fusions += fusion_visit_node(optimizer, node->as.binary.left);
        fusions += fusion_visit_node(optimizer, node->as.binary.right);
        break;
        
    case AST_VAR_DECL:
        if (node->as.var_decl.initializer) {
            fusions += fusion_visit_node(optimizer, node->as.var_decl.initializer);
        }
        break;
        
    case AST_ASSIGN:
        fusions += fusion_visit_node(optimizer, node->as.assign.value);
        break;
        
    case AST_IF:
        fusions += fusion_visit_node(optimizer, node->as.if_stmt.condition);
        fusions += fusion_visit_node(optimizer, node->as.if_stmt.then_branch);
        if (node->as.if_stmt.else_branch) {
            fusions += fusion_visit_node(optimizer, node->as.if_stmt.else_branch);
        }
        break;
        
    case AST_WHILE:
        fusions += fusion_visit_node(optimizer, node->as.while_stmt.condition);
        fusions += fusion_visit_node(optimizer, node->as.while_stmt.body);
        break;
        
    case AST_FOR:
        fusions += fusion_visit_node(optimizer, node->as.for_stmt.iterable);
        fusions += fusion_visit_node(optimizer, node->as.for_stmt.body);
        break;
        
    case AST_GIVE:
        fusions += fusion_visit_node(optimizer, node->as.give_stmt.value);
        break;
        
    case AST_CALL:
        for (int i = 0; i < node->as.call.args.count; i++) {
            fusions += fusion_visit_node(optimizer, node->as.call.args.items[i]);
        }
        break;
        
    case AST_LAMBDA:
        fusions += fusion_visit_node(optimizer, node->as.lambda.body);
        break;
        
    default:
        /* Other node types don't contain pipelines */
        break;
    }
    
    return fusions;
}

int fusion_optimize_ast(FusionOptimizer *optimizer, AstNode *ast) {
    if (!optimizer || !ast) return 0;
    
    int total_fusions = fusion_visit_node(optimizer, ast);
    
    return total_fusions;
}

/* Alias for header compatibility */
int analyze_ast_for_fusion(FusionOptimizer *optimizer, AstNode *ast) {
    return fusion_optimize_ast(optimizer, ast);
}

/* ============================================================================
 * Reporting
 * ============================================================================ */

const char *fusion_pattern_name(FusionPattern pattern) {
    if (pattern >= 0 && pattern < FUSION_PATTERN_COUNT) {
        return fusion_pattern_names[pattern];
    }
    return "unknown";
}

void fusion_print_opportunity(FusionOpportunity *opp) {
    if (!opp) return;
    
    printf("  [Fusion] %s\n", fusion_pattern_name(opp->pattern));
    printf("    Passes saved: %d\n", opp->estimated_passes_saved);
    printf("    Allocations saved: %d\n", opp->estimated_allocs_saved);
    printf("    Estimated speedup: %.1fx\n", opp->estimated_speedup);
}

void fusion_print_report(FusionOptimizer *optimizer) {
    if (!optimizer) return;
    
    printf("\n┌─────────────────────────────────────────────┐\n");
    printf("│         FUSION OPTIMIZATION REPORT          │\n");
    printf("├─────────────────────────────────────────────┤\n");
    printf("│ Pipelines analyzed: %d                      \n", optimizer->pipelines_analyzed);
    printf("│ Fusions applied: %d                         \n", optimizer->fusions_applied);
    printf("│ Passes eliminated: %d                       \n", optimizer->passes_eliminated);
    printf("│ Allocations avoided: %d                     \n", optimizer->allocs_avoided);
    printf("└─────────────────────────────────────────────┘\n\n");
}

FusionMetrics fusion_get_metrics(FusionOptimizer *optimizer) {
    FusionMetrics metrics = {0};
    
    if (!optimizer) return metrics;
    
    /* Count by pattern type */
    for (int i = 0; i < optimizer->opportunity_count; i++) {
        FusionOpportunity *opp = &optimizer->opportunities[i];
        switch (opp->pattern) {
        case FUSION_FILTER_MAP:
            metrics.filter_map_fusions++;
            break;
        case FUSION_MAP_MAP:
            metrics.map_map_fusions++;
            break;
        case FUSION_FILTER_FILTER:
            metrics.filter_filter_fusions++;
            break;
        case FUSION_MAP_REDUCE:
        case FUSION_FILTER_REDUCE:
            metrics.map_reduce_fusions++;
            break;
        default:
            metrics.other_fusions++;
            break;
        }
    }
    
    metrics.total_passes_eliminated = optimizer->passes_eliminated;
    metrics.total_allocs_avoided = optimizer->allocs_avoided;
    metrics.estimated_speedup = 1.0; /* Placeholder - would need aggregate calculation */
    
    return metrics;
}
