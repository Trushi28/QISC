/*
 * QISC Tail Call Optimization System — Implementation
 *
 * This implements detection and transformation of tail-recursive patterns
 * to eliminate stack growth and enable efficient recursion.
 *
 * Key insight: Tail recursion is iteration in disguise. We unmask it.
 */

#include "tail_call.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ============================================================================
 * String Constants for Reporting
 * ============================================================================ */

static const char *tail_call_type_names[] = {
    [TCO_NONE] = "not a tail call",
    [TCO_SELF_RECURSION] = "self-recursion",
    [TCO_MUTUAL_RECURSION] = "mutual recursion",
    [TCO_EXTERNAL_TAIL_CALL] = "external tail call",
    [TCO_INDIRECT_TAIL_CALL] = "indirect tail call",
};

static const char *transform_names[] = {
    [TCO_TRANSFORM_NONE] = "none",
    [TCO_TRANSFORM_LOOP] = "loop",
    [TCO_TRANSFORM_GOTO] = "goto",
    [TCO_TRANSFORM_MUSTTAIL] = "musttail",
    [TCO_TRANSFORM_TRAMPOLINE] = "trampoline",
};

static const char *invalid_reason_names[] = {
    [TCO_VALID] = "valid",
    [TCO_NOT_LAST_EXPR] = "not last expression",
    [TCO_HAS_POST_OPERATIONS] = "has operations after call",
    [TCO_WRAPPED_IN_EXPRESSION] = "wrapped in expression",
    [TCO_IN_TRY_BLOCK] = "inside try block",
    [TCO_DIFFERENT_RETURN_TYPE] = "return type mismatch",
    [TCO_CALLEE_NOT_FOUND] = "callee not found",
    [TCO_CLOSURE_CAPTURES] = "closure captures prevent TCO",
};

/* ============================================================================
 * Internal Helpers
 * ============================================================================ */

/* Get function name from a procedure AST node */
static const char *get_proc_name(AstNode *node) {
    if (!node || node->type != AST_PROC) return NULL;
    return node->as.proc.name;
}

/* Get callee name from a call AST node */
static const char *get_call_callee_name(AstNode *node) {
    if (!node || node->type != AST_CALL) return NULL;
    if (!node->as.call.callee) return NULL;
    if (node->as.call.callee->type == AST_IDENTIFIER) {
        return node->as.call.callee->as.identifier.name;
    }
    return NULL;
}

/* Check if two strings are equal (NULL-safe) */
static bool str_eq(const char *a, const char *b) {
    if (!a || !b) return false;
    return strcmp(a, b) == 0;
}

/* Allocate or grow tail call site array */
static bool grow_tail_calls(TailCallCandidate *candidate) {
    if (candidate->tail_call_count >= candidate->tail_call_capacity) {
        int new_cap = candidate->tail_call_capacity == 0 ? 8 : 
                      candidate->tail_call_capacity * 2;
        TailCallSite *new_sites = realloc(candidate->tail_calls,
                                          new_cap * sizeof(TailCallSite));
        if (!new_sites) return false;
        candidate->tail_calls = new_sites;
        candidate->tail_call_capacity = new_cap;
    }
    return true;
}

/* Find function by name in program */
static AstNode *find_function(AstNode *program, const char *name) {
    if (!program || program->type != AST_PROGRAM || !name) return NULL;
    
    for (int i = 0; i < program->as.program.declarations.count; i++) {
        AstNode *decl = program->as.program.declarations.items[i];
        if (decl && decl->type == AST_PROC) {
            if (str_eq(decl->as.proc.name, name)) {
                return decl;
            }
        }
    }
    return NULL;
}

/* Check if node is a return/give statement */
static bool is_return_stmt(AstNode *node) {
    return node && node->type == AST_GIVE;
}

/* Get the return value expression from a give statement */
static AstNode *get_return_value(AstNode *node) {
    if (!node || node->type != AST_GIVE) return NULL;
    return node->as.give_stmt.value;
}

/* ============================================================================
 * Tail Position Detection
 * ============================================================================ */

/* 
 * Check if an expression is the last expression in a block before return.
 * Handles nested if/else, when statements, and implicit returns.
 */
static bool is_in_tail_position_recursive(AstNode *node, AstNode *target,
                                           bool *found, int depth) {
    if (!node || !target || depth > 100) return false;
    
    /* Direct match */
    if (node == target) {
        *found = true;
        return true;
    }
    
    switch (node->type) {
    case AST_GIVE:
        /* give expr — expr is in tail position */
        return is_in_tail_position_recursive(node->as.give_stmt.value, target,
                                             found, depth + 1);
        
    case AST_BLOCK: {
        /* Last statement in block is in tail position */
        AstNodeArray *stmts = &node->as.block.statements;
        if (stmts->count == 0) return false;
        
        AstNode *last = stmts->items[stmts->count - 1];
        return is_in_tail_position_recursive(last, target, found, depth + 1);
    }
    
    case AST_IF: {
        /* Both branches of if are in tail position */
        bool in_then = false, in_else = false;
        
        if (node->as.if_stmt.then_branch) {
            in_then = is_in_tail_position_recursive(
                node->as.if_stmt.then_branch, target, found, depth + 1);
        }
        if (*found) return true;
        
        if (node->as.if_stmt.else_branch) {
            in_else = is_in_tail_position_recursive(
                node->as.if_stmt.else_branch, target, found, depth + 1);
        }
        
        return in_then || in_else;
    }
    
    case AST_WHEN: {
        /* All cases of when are in tail position */
        for (int i = 0; i < node->as.when_stmt.cases.count; i++) {
            if (is_in_tail_position_recursive(
                    node->as.when_stmt.cases.items[i], target, found, depth + 1)) {
                return true;
            }
        }
        return false;
    }
    
    case AST_CALL:
        /* Call itself — check if this is the target */
        return (node == target);
        
    case AST_BINARY_OP:
        /* Call in binary op is NOT in tail position (e.g., f(x) + 1) */
        return false;
        
    case AST_UNARY_OP:
        /* Call in unary op is NOT in tail position (e.g., -f(x)) */
        return false;
        
    default:
        return false;
    }
}

bool tco_is_tail_position(AstNode *func, AstNode *expr) {
    if (!func || !expr || func->type != AST_PROC) return false;
    
    AstNode *body = func->as.proc.body;
    if (!body) return false;
    
    bool found = false;
    return is_in_tail_position_recursive(body, expr, &found, 0);
}

bool tco_is_last_expression(AstNode *block, AstNode *expr) {
    if (!block || !expr) return false;
    
    if (block->type != AST_BLOCK) {
        /* Non-block body — the body IS the last expression */
        return block == expr;
    }
    
    AstNodeArray *stmts = &block->as.block.statements;
    if (stmts->count == 0) return false;
    
    return stmts->items[stmts->count - 1] == expr;
}

bool tco_has_post_operations(AstNode *parent, AstNode *expr) {
    if (!parent || !expr) return false;
    
    /* If parent is a binary op and expr is not the whole thing, there are post ops */
    if (parent->type == AST_BINARY_OP) {
        return true;  /* Any binary op wrapper means post-ops */
    }
    
    /* If parent is unary, there's a post-op */
    if (parent->type == AST_UNARY_OP) {
        return true;
    }
    
    return false;
}

bool tco_is_wrapped(AstNode *parent, AstNode *expr) {
    if (!parent || !expr) return false;
    
    switch (parent->type) {
    case AST_BINARY_OP:
        /* Check if expr is a child of binary op */
        return parent->as.binary.left == expr || 
               parent->as.binary.right == expr;
        
    case AST_UNARY_OP:
        return parent->as.unary.operand == expr;
        
    case AST_INDEX:
        return parent->as.index.object == expr ||
               parent->as.index.index == expr;
        
    case AST_MEMBER:
        return parent->as.member.object == expr;
        
    default:
        return false;
    }
}

/* Recursive helper to check for try blocks */
static bool check_in_try_block(AstNode *node, AstNode *target, bool in_try) {
    if (!node) return false;
    if (node == target) return in_try;
    
    switch (node->type) {
    case AST_TRY:
        /* Check try block with in_try = true */
        if (check_in_try_block(node->as.try_stmt.try_block, target, true)) {
            return true;
        }
        /* Check catch blocks (still in try context for TCO purposes) */
        for (int i = 0; i < node->as.try_stmt.catches.count; i++) {
            if (check_in_try_block(node->as.try_stmt.catches.items[i], target, true)) {
                return true;
            }
        }
        return false;
        
    case AST_BLOCK:
        for (int i = 0; i < node->as.block.statements.count; i++) {
            if (check_in_try_block(node->as.block.statements.items[i], target, in_try)) {
                return true;
            }
        }
        return false;
        
    case AST_IF:
        return check_in_try_block(node->as.if_stmt.condition, target, in_try) ||
               check_in_try_block(node->as.if_stmt.then_branch, target, in_try) ||
               check_in_try_block(node->as.if_stmt.else_branch, target, in_try);
        
    case AST_WHILE:
        return check_in_try_block(node->as.while_stmt.condition, target, in_try) ||
               check_in_try_block(node->as.while_stmt.body, target, in_try);
        
    case AST_FOR:
        return check_in_try_block(node->as.for_stmt.body, target, in_try);
        
    case AST_CALL:
        return (node == target) && in_try;
        
    default:
        return false;
    }
}

bool tco_in_try_block(AstNode *func, AstNode *call) {
    if (!func || !call || func->type != AST_PROC) return false;
    return check_in_try_block(func->as.proc.body, call, false);
}

/* ============================================================================
 * Call Classification
 * ============================================================================ */

TailCallType tco_classify_call(AstNode *func, AstNode *call,
                                TailCallInvalidReason *out_reason) {
    if (out_reason) *out_reason = TCO_VALID;
    
    if (!func || !call || func->type != AST_PROC || call->type != AST_CALL) {
        if (out_reason) *out_reason = TCO_CALLEE_NOT_FOUND;
        return TCO_NONE;
    }
    
    /* Check if call is in tail position */
    if (!tco_is_tail_position(func, call)) {
        if (out_reason) *out_reason = TCO_NOT_LAST_EXPR;
        return TCO_NONE;
    }
    
    /* Check if call is in try block */
    if (tco_in_try_block(func, call)) {
        if (out_reason) *out_reason = TCO_IN_TRY_BLOCK;
        return TCO_NONE;
    }
    
    /* Get callee name */
    const char *callee_name = get_call_callee_name(call);
    const char *caller_name = get_proc_name(func);
    
    if (!callee_name) {
        /* Indirect call through function pointer */
        return TCO_INDIRECT_TAIL_CALL;
    }
    
    /* Check for self-recursion */
    if (str_eq(caller_name, callee_name)) {
        return TCO_SELF_RECURSION;
    }
    
    /* External or potentially mutual recursion */
    return TCO_EXTERNAL_TAIL_CALL;
}

/* ============================================================================
 * Parameter Binding Extraction
 * ============================================================================ */

int tco_extract_bindings(TailCallSite *site, AstNode *call, AstNode *func) {
    if (!site || !call || !func) return 0;
    if (call->type != AST_CALL || func->type != AST_PROC) return 0;
    
    AstNodeArray *params = &func->as.proc.params;
    AstNodeArray *args = &call->as.call.args;
    
    /* Must have same number of arguments as parameters */
    if (args->count != params->count) return 0;
    
    int count = 0;
    for (int i = 0; i < params->count && count < TCO_MAX_PARAM_BINDINGS; i++) {
        AstNode *param = params->items[i];
        AstNode *arg = args->items[i];
        
        if (!param) continue;
        
        /* Extract parameter name */
        const char *param_name = NULL;
        if (param->type == AST_VAR_DECL) {
            param_name = param->as.var_decl.name;
        } else if (param->type == AST_IDENTIFIER) {
            param_name = param->as.identifier.name;
        }
        
        if (!param_name) continue;
        
        ParamBinding *binding = &site->bindings[count];
        binding->param_name = param_name;
        binding->param_index = i;
        binding->new_value_expr = arg;
        binding->is_accumulator = tco_is_accumulator(func, param_name, arg);
        count++;
    }
    
    site->binding_count = count;
    return count;
}

bool tco_is_accumulator(AstNode *func, const char *param_name,
                        AstNode *new_value_expr) {
    if (!param_name || !new_value_expr) return false;
    
    /* Check if the new value uses the old value in a computation */
    /* Common patterns: acc * x, acc + x, x :: acc (cons) */
    
    if (new_value_expr->type == AST_BINARY_OP) {
        BinaryOp op = new_value_expr->as.binary.op;
        
        /* Check if either side is the parameter */
        bool left_is_param = false, right_is_param = false;
        
        if (new_value_expr->as.binary.left &&
            new_value_expr->as.binary.left->type == AST_IDENTIFIER) {
            left_is_param = str_eq(
                new_value_expr->as.binary.left->as.identifier.name,
                param_name);
        }
        
        if (new_value_expr->as.binary.right &&
            new_value_expr->as.binary.right->type == AST_IDENTIFIER) {
            right_is_param = str_eq(
                new_value_expr->as.binary.right->as.identifier.name,
                param_name);
        }
        
        /* Accumulator operations: +, *, min, max, etc. */
        if ((left_is_param || right_is_param) &&
            (op == OP_ADD || op == OP_MUL || op == OP_BIT_OR || 
             op == OP_BIT_AND || op == OP_BIT_XOR)) {
            return true;
        }
    }
    
    return false;
}

bool tco_detect_accumulator_pattern(AstNode *func) {
    if (!func || func->type != AST_PROC) return false;
    
    /* Look for accumulator parameter pattern:
     * - Function has >= 2 parameters
     * - One parameter is returned in base case
     * - Same parameter is updated in recursive call
     */
    
    AstNodeArray *params = &func->as.proc.params;
    if (params->count < 2) return false;
    
    /* Check last parameter — often the accumulator */
    AstNode *last_param = params->items[params->count - 1];
    if (!last_param) return false;
    
    const char *acc_name = NULL;
    if (last_param->type == AST_VAR_DECL) {
        acc_name = last_param->as.var_decl.name;
    } else if (last_param->type == AST_IDENTIFIER) {
        acc_name = last_param->as.identifier.name;
    }
    
    if (!acc_name) return false;
    
    /* Analyze function body for accumulator usage patterns */
    AstNode *body = func->as.proc.body;
    if (!body) return false;
    
    /* Check if function body references the accumulator in a return/give statement */
    bool found_acc_return = false;
    bool found_acc_update = false;
    
    /* Simple pattern: look for give statements that reference accumulator */
    if (body->type == AST_BLOCK) {
        for (int i = 0; i < body->as.block.statements.count; i++) {
            AstNode *stmt = body->as.block.statements.items[i];
            if (stmt->type == AST_GIVE && stmt->as.give_stmt.value) {
                AstNode *val = stmt->as.give_stmt.value;
                /* Check if returning the accumulator directly */
                if (val->type == AST_IDENTIFIER && 
                    strcmp(val->as.identifier.name, acc_name) == 0) {
                    found_acc_return = true;
                }
                /* Check if returning an operation on accumulator */
                if (val->type == AST_BINARY_OP) {
                    AstNode *left = val->as.binary.left;
                    AstNode *right = val->as.binary.right;
                    if ((left->type == AST_IDENTIFIER && 
                         strcmp(left->as.identifier.name, acc_name) == 0) ||
                        (right->type == AST_IDENTIFIER && 
                         strcmp(right->as.identifier.name, acc_name) == 0)) {
                        found_acc_update = true;
                    }
                }
            }
        }
    }
    
    /* Accumulator pattern confirmed if we found both usage types */
    return found_acc_return || found_acc_update;
}

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

void tco_init(TailCallOptimizer *opt) {
    tco_init_ex(opt, true, true, true);
}

void tco_init_ex(TailCallOptimizer *opt, bool loop_transform,
                 bool musttail, bool trampoline) {
    if (!opt) return;
    
    memset(opt, 0, sizeof(TailCallOptimizer));
    
    opt->config.enable_loop_transform = loop_transform;
    opt->config.enable_musttail = musttail;
    opt->config.enable_trampoline = trampoline;
    opt->config.aggressive_mode = false;
    opt->config.max_recursion_depth = 100;
    
    opt->mutual_groups = NULL;
    opt->mutual_group_count = 0;
    opt->mutual_group_capacity = 0;
}

void tco_free(TailCallOptimizer *opt) {
    if (!opt) return;
    
    /* Free tail call sites in each candidate */
    for (int i = 0; i < opt->candidate_count; i++) {
        TailCallCandidate *c = &opt->candidates[i];
        if (c->tail_calls) {
            free(c->tail_calls);
            c->tail_calls = NULL;
        }
    }
    
    /* Free mutual recursion groups */
    if (opt->mutual_groups) {
        free(opt->mutual_groups);
        opt->mutual_groups = NULL;
    }
    
    memset(opt, 0, sizeof(TailCallOptimizer));
}

void tco_reset(TailCallOptimizer *opt) {
    if (!opt) return;
    
    /* Free and reinit */
    bool loop = opt->config.enable_loop_transform;
    bool musttail = opt->config.enable_musttail;
    bool tramp = opt->config.enable_trampoline;
    
    tco_free(opt);
    tco_init_ex(opt, loop, musttail, tramp);
}

void tco_set_living_ir(TailCallOptimizer *opt, LivingIR *ir) {
    if (opt) opt->living_ir = ir;
}

/* ============================================================================
 * AST Analysis - Helper to traverse and find tail calls
 * ============================================================================ */

/* Recursive helper to find all calls in an AST node */
static void find_calls_recursive(AstNode *node, AstNode *func,
                                  TailCallCandidate *candidate,
                                  AstNode *condition, bool in_else) {
    if (!node || !func || !candidate) return;
    
    switch (node->type) {
    case AST_CALL: {
        candidate->total_calls++;
        
        /* Check if this is a tail call */
        TailCallInvalidReason reason;
        TailCallType type = tco_classify_call(func, node, &reason);
        
        if (type != TCO_NONE) {
            if (!grow_tail_calls(candidate)) return;
            
            TailCallSite *site = &candidate->tail_calls[candidate->tail_call_count];
            memset(site, 0, sizeof(TailCallSite));
            
            site->type = type;
            site->invalid_reason = reason;
            site->call_node = node;
            site->containing_func = func;
            site->line = node->line;
            site->column = node->column;
            site->caller_name = get_proc_name(func);
            site->callee_name = get_call_callee_name(node);
            site->is_self_recursive = (type == TCO_SELF_RECURSION);
            site->condition = condition;
            site->in_else_branch = in_else;
            
            /* Extract parameter bindings */
            tco_extract_bindings(site, node, func);
            
            /* Suggest transformation */
            if (type == TCO_SELF_RECURSION) {
                site->suggested_transform = TCO_TRANSFORM_LOOP;
            } else if (type == TCO_MUTUAL_RECURSION) {
                site->suggested_transform = TCO_TRANSFORM_MUSTTAIL;
            } else {
                site->suggested_transform = TCO_TRANSFORM_MUSTTAIL;
            }
            
            candidate->tail_call_count++;
            candidate->tail_calls_found++;
            
            if (type == TCO_SELF_RECURSION) {
                candidate->has_self_recursion = true;
            } else if (type == TCO_MUTUAL_RECURSION) {
                candidate->has_mutual_recursion = true;
            }
        }
        
        /* Also check call arguments for nested calls */
        for (int i = 0; i < node->as.call.args.count; i++) {
            find_calls_recursive(node->as.call.args.items[i], func, candidate,
                                condition, in_else);
        }
        break;
    }
    
    case AST_BLOCK:
        for (int i = 0; i < node->as.block.statements.count; i++) {
            find_calls_recursive(node->as.block.statements.items[i], func, 
                                candidate, condition, in_else);
        }
        break;
        
    case AST_IF:
        /* Condition is not in tail position */
        find_calls_recursive(node->as.if_stmt.condition, func, candidate,
                            NULL, false);
        /* Then branch: passes condition */
        find_calls_recursive(node->as.if_stmt.then_branch, func, candidate,
                            node->as.if_stmt.condition, false);
        /* Else branch: passes negated condition */
        find_calls_recursive(node->as.if_stmt.else_branch, func, candidate,
                            node->as.if_stmt.condition, true);
        break;
        
    case AST_WHEN:
        find_calls_recursive(node->as.when_stmt.value, func, candidate,
                            NULL, false);
        for (int i = 0; i < node->as.when_stmt.cases.count; i++) {
            find_calls_recursive(node->as.when_stmt.cases.items[i], func, 
                                candidate, NULL, false);
        }
        break;
        
    case AST_WHILE:
        find_calls_recursive(node->as.while_stmt.condition, func, candidate,
                            NULL, false);
        find_calls_recursive(node->as.while_stmt.body, func, candidate,
                            NULL, false);
        break;
        
    case AST_FOR:
        find_calls_recursive(node->as.for_stmt.body, func, candidate,
                            NULL, false);
        break;
        
    case AST_GIVE:
        find_calls_recursive(node->as.give_stmt.value, func, candidate,
                            condition, in_else);
        break;
        
    case AST_BINARY_OP:
        find_calls_recursive(node->as.binary.left, func, candidate,
                            NULL, false);
        find_calls_recursive(node->as.binary.right, func, candidate,
                            NULL, false);
        break;
        
    case AST_UNARY_OP:
        find_calls_recursive(node->as.unary.operand, func, candidate,
                            NULL, false);
        break;
        
    case AST_VAR_DECL:
        find_calls_recursive(node->as.var_decl.initializer, func, candidate,
                            NULL, false);
        break;
        
    case AST_ASSIGN:
        find_calls_recursive(node->as.assign.value, func, candidate,
                            NULL, false);
        break;
        
    case AST_TRY:
        find_calls_recursive(node->as.try_stmt.try_block, func, candidate,
                            NULL, false);
        for (int i = 0; i < node->as.try_stmt.catches.count; i++) {
            find_calls_recursive(node->as.try_stmt.catches.items[i], func,
                                candidate, NULL, false);
        }
        break;
        
    default:
        /* Other node types don't contain calls */
        break;
    }
}

TailCallCandidate *tco_analyze_function(TailCallOptimizer *opt, AstNode *func) {
    if (!opt || !func || func->type != AST_PROC) return NULL;
    
    if (opt->candidate_count >= TCO_MAX_CANDIDATES) return NULL;
    
    TailCallCandidate *candidate = &opt->candidates[opt->candidate_count];
    memset(candidate, 0, sizeof(TailCallCandidate));
    
    candidate->function = func;
    candidate->function_name = get_proc_name(func);
    candidate->tail_calls = NULL;
    candidate->tail_call_count = 0;
    candidate->tail_call_capacity = 0;
    
    /* Traverse function body to find all calls */
    if (func->as.proc.body) {
        find_calls_recursive(func->as.proc.body, func, candidate, NULL, false);
    }
    
    /* Determine transformation strategy */
    if (candidate->has_self_recursion) {
        if (opt->config.enable_loop_transform) {
            candidate->transform = TCO_TRANSFORM_LOOP;
        } else if (opt->config.enable_musttail) {
            candidate->transform = TCO_TRANSFORM_MUSTTAIL;
        }
    } else if (candidate->has_mutual_recursion) {
        if (opt->config.enable_trampoline) {
            candidate->transform = TCO_TRANSFORM_TRAMPOLINE;
        } else if (opt->config.enable_musttail) {
            candidate->transform = TCO_TRANSFORM_MUSTTAIL;
        }
    }
    
    opt->candidate_count++;
    opt->metrics.functions_analyzed++;
    
    if (candidate->has_self_recursion) {
        opt->metrics.self_recursive_found++;
    }
    if (candidate->has_mutual_recursion) {
        opt->metrics.mutual_recursive_found++;
    }
    
    return candidate;
}

int tco_analyze_program(TailCallOptimizer *opt, AstNode *program) {
    if (!opt || !program || program->type != AST_PROGRAM) return 0;
    
    int count = 0;
    
    /* Analyze each function declaration */
    for (int i = 0; i < program->as.program.declarations.count; i++) {
        AstNode *decl = program->as.program.declarations.items[i];
        
        if (decl && decl->type == AST_PROC) {
            TailCallCandidate *candidate = tco_analyze_function(opt, decl);
            if (candidate && candidate->tail_call_count > 0) {
                count++;
            }
        }
    }
    
    /* Detect mutual recursion groups */
    tco_detect_mutual_recursion(opt, program);
    
    return count;
}

/* ============================================================================
 * Mutual Recursion Detection
 * ============================================================================ */

int tco_detect_mutual_recursion(TailCallOptimizer *opt, AstNode *program) {
    if (!opt || !program) return 0;
    
    /* Build a call graph among candidates */
    int n = opt->candidate_count;
    if (n < 2) return 0;
    
    /* Create adjacency matrix for tail calls between functions */
    bool (*calls)[TCO_MAX_CANDIDATES] = calloc(n, sizeof(*calls));
    if (!calls) return 0;
    
    for (int i = 0; i < n; i++) {
        TailCallCandidate *c = &opt->candidates[i];
        for (int j = 0; j < c->tail_call_count; j++) {
            TailCallSite *site = &c->tail_calls[j];
            if (!site->is_self_recursive) {
                /* Find callee in candidates */
                for (int k = 0; k < n; k++) {
                    if (k != i && str_eq(opt->candidates[k].function_name,
                                         site->callee_name)) {
                        calls[i][k] = true;
                        break;
                    }
                }
            }
        }
    }
    
    /* Find strongly connected components (simplified: look for cycles) */
    int groups_found = 0;
    bool *visited = calloc(n, sizeof(bool));
    bool *in_group = calloc(n, sizeof(bool));
    
    for (int i = 0; i < n && groups_found < 16; i++) {
        if (visited[i]) continue;
        
        /* DFS to find cycles */
        for (int j = 0; j < n; j++) {
            if (i != j && calls[i][j] && calls[j][i]) {
                /* Found mutual recursion: i <-> j */
                if (!in_group[i] && !in_group[j]) {
                    /* Allocate new group */
                    if (opt->mutual_group_count >= opt->mutual_group_capacity) {
                        int new_cap = opt->mutual_group_capacity == 0 ? 4 :
                                     opt->mutual_group_capacity * 2;
                        MutualRecursionGroup *new_groups = realloc(
                            opt->mutual_groups,
                            new_cap * sizeof(MutualRecursionGroup));
                        if (!new_groups) break;
                        opt->mutual_groups = new_groups;
                        opt->mutual_group_capacity = new_cap;
                    }
                    
                    MutualRecursionGroup *group = 
                        &opt->mutual_groups[opt->mutual_group_count];
                    memset(group, 0, sizeof(MutualRecursionGroup));
                    
                    group->function_names[0] = opt->candidates[i].function_name;
                    group->function_names[1] = opt->candidates[j].function_name;
                    group->functions[0] = opt->candidates[i].function;
                    group->functions[1] = opt->candidates[j].function;
                    group->function_count = 2;
                    group->calls[0][1] = true;
                    group->calls[1][0] = true;
                    group->can_use_musttail = true;
                    
                    /* Update candidates */
                    opt->candidates[i].has_mutual_recursion = true;
                    opt->candidates[j].has_mutual_recursion = true;
                    opt->candidates[i].mutual_group = group;
                    opt->candidates[j].mutual_group = group;
                    
                    /* Mark tail calls as mutual */
                    for (int k = 0; k < opt->candidates[i].tail_call_count; k++) {
                        TailCallSite *site = &opt->candidates[i].tail_calls[k];
                        if (str_eq(site->callee_name, 
                                  opt->candidates[j].function_name)) {
                            site->type = TCO_MUTUAL_RECURSION;
                        }
                    }
                    for (int k = 0; k < opt->candidates[j].tail_call_count; k++) {
                        TailCallSite *site = &opt->candidates[j].tail_calls[k];
                        if (str_eq(site->callee_name,
                                  opt->candidates[i].function_name)) {
                            site->type = TCO_MUTUAL_RECURSION;
                        }
                    }
                    
                    in_group[i] = true;
                    in_group[j] = true;
                    opt->mutual_group_count++;
                    groups_found++;
                }
            }
        }
        visited[i] = true;
    }
    
    free(calls);
    free(visited);
    free(in_group);
    
    return groups_found;
}

/* ============================================================================
 * Transformation Decision
 * ============================================================================ */

TailCallTransform tco_decide_transform(TailCallOptimizer *opt,
                                        TailCallCandidate *candidate) {
    if (!opt || !candidate) return TCO_TRANSFORM_NONE;
    
    /* Self-recursion: prefer loop transform */
    if (candidate->has_self_recursion) {
        if (opt->config.enable_loop_transform) {
            return TCO_TRANSFORM_LOOP;
        }
        if (opt->config.enable_musttail) {
            return TCO_TRANSFORM_MUSTTAIL;
        }
    }
    
    /* Mutual recursion: prefer trampoline, fall back to musttail */
    if (candidate->has_mutual_recursion) {
        if (opt->config.enable_trampoline) {
            return TCO_TRANSFORM_TRAMPOLINE;
        }
        if (opt->config.enable_musttail) {
            return TCO_TRANSFORM_MUSTTAIL;
        }
    }
    
    /* External tail calls: use musttail if enabled */
    for (int i = 0; i < candidate->tail_call_count; i++) {
        if (candidate->tail_calls[i].type == TCO_EXTERNAL_TAIL_CALL ||
            candidate->tail_calls[i].type == TCO_INDIRECT_TAIL_CALL) {
            if (opt->config.enable_musttail) {
                return TCO_TRANSFORM_MUSTTAIL;
            }
        }
    }
    
    return TCO_TRANSFORM_NONE;
}

int tco_get_recursion_depth_hint(TailCallOptimizer *opt, const char *func_name) {
    if (!opt || !opt->living_ir || !func_name) return -1;
    
    /* Query Living IR profile data for recursion patterns */
    ProfileFunction *pf = profile_get_function(opt->living_ir->profile, func_name);
    if (!pf) return -1;
    
    /* Use call count as rough estimate of recursion depth */
    /* In practice, this would need more sophisticated tracking */
    return (int)(pf->call_count > 10000 ? 10000 : pf->call_count);
}

/* ============================================================================
 * LLVM Code Generation
 * ============================================================================ */

void tco_apply_musttail(Codegen *cg, LLVMValueRef call) {
    if (!cg || !call) return;
    
    /* Check if it's a call instruction */
    if (LLVMGetInstructionOpcode(call) != LLVMCall) return;
    
    /* Set the tail call kind to musttail */
    LLVMSetTailCallKind(call, LLVMTailCallKindMustTail);
}

void tco_mark_function(Codegen *cg, LLVMValueRef func, TailCallTransform transform) {
    if (!cg || !func) return;
    
    /* Add function attributes based on transform type */
    unsigned kind;
    
    switch (transform) {
    case TCO_TRANSFORM_LOOP:
        /* For loop transform, we might want to hint that the function is hot */
        kind = LLVMGetEnumAttributeKindForName("hot", 3);
        if (kind != 0) {
            LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
                LLVMCreateEnumAttribute(cg->ctx, kind, 0));
        }
        break;
        
    case TCO_TRANSFORM_MUSTTAIL:
        /* Disable frame pointer for tail call optimization */
        kind = LLVMGetEnumAttributeKindForName("nounwind", 8);
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
            LLVMCreateEnumAttribute(cg->ctx, kind, 0));
        break;
        
    case TCO_TRANSFORM_TRAMPOLINE:
        /* Mark as inlineable (trampolines benefit from inlining) */
        kind = LLVMGetEnumAttributeKindForName("inlinehint", 10);
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
            LLVMCreateEnumAttribute(cg->ctx, kind, 0));
        break;
        
    default:
        break;
    }
    
    /* Add metadata indicating TCO was applied */
    char md_str[128];
    snprintf(md_str, sizeof(md_str), "tco:%s", tco_transform_name(transform));
    LLVMValueRef md_string = LLVMMDString(md_str, strlen(md_str));
    LLVMValueRef md_node = LLVMMDNode(&md_string, 1);
    LLVMAddNamedMetadataOperand(cg->mod, "qisc.tco", md_node);
}

LLVMValueRef tco_emit_loop_transform(Codegen *cg, TailCallCandidate *candidate) {
    if (!cg || !candidate || !candidate->function) return NULL;
    
    AstNode *func = candidate->function;
    const char *func_name = candidate->function_name;
    
    /* Get the existing function (already declared) */
    LLVMValueRef llvm_func = LLVMGetNamedFunction(cg->mod, func_name);
    if (!llvm_func) return NULL;
    
    /* Create entry block */
    LLVMBasicBlockRef entry_bb = LLVMAppendBasicBlockInContext(
        cg->ctx, llvm_func, "entry");
    
    /* Create loop header block */
    LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(
        cg->ctx, llvm_func, "tail_loop");
    
    /* Create exit block */
    LLVMBasicBlockRef exit_bb = LLVMAppendBasicBlockInContext(
        cg->ctx, llvm_func, "return");
    
    /* Position builder at entry */
    LLVMPositionBuilderAtEnd(cg->builder, entry_bb);
    
    /* Create allocas for all parameters */
    int param_count = LLVMCountParams(llvm_func);
    LLVMValueRef *param_allocas = calloc(param_count, sizeof(LLVMValueRef));
    
    for (int i = 0; i < param_count; i++) {
        LLVMValueRef param = LLVMGetParam(llvm_func, i);
        LLVMTypeRef param_type = LLVMTypeOf(param);
        
        char name[64];
        snprintf(name, sizeof(name), "p%d.addr", i);
        param_allocas[i] = LLVMBuildAlloca(cg->builder, param_type, name);
        LLVMBuildStore(cg->builder, param, param_allocas[i]);
    }
    
    /* Jump to loop header */
    LLVMBuildBr(cg->builder, loop_bb);
    
    /* Position at loop header */
    LLVMPositionBuilderAtEnd(cg->builder, loop_bb);
    
    /* Store loop header for tail call sites */
    for (int i = 0; i < candidate->tail_call_count; i++) {
        candidate->tail_calls[i].loop_header = loop_bb;
    }
    
    /* Mark function for TCO */
    tco_mark_function(cg, llvm_func, TCO_TRANSFORM_LOOP);
    
    free(param_allocas);
    
    return llvm_func;
}

LLVMValueRef tco_emit_musttail(Codegen *cg, TailCallCandidate *candidate) {
    if (!cg || !candidate) return NULL;
    
    /* The function is emitted normally, but all tail calls get musttail */
    LLVMValueRef llvm_func = LLVMGetNamedFunction(cg->mod, 
                                                   candidate->function_name);
    if (!llvm_func) return NULL;
    
    /* Apply musttail to all relevant call instructions */
    /* This needs to be done after the calls are emitted */
    /* We mark the function so codegen knows to apply musttail later */
    tco_mark_function(cg, llvm_func, TCO_TRANSFORM_MUSTTAIL);
    
    return llvm_func;
}

LLVMValueRef tco_emit_trampoline(Codegen *cg, MutualRecursionGroup *group) {
    if (!cg || !group || group->function_count < 2) return NULL;
    
    /* Create trampoline dispatch function */
    /*
     * The trampoline pattern:
     * 
     * struct TrampolineResult {
     *   int func_id;    // Which function to call next (-1 = done)
     *   int64_t result; // Result value if done
     *   int64_t args[N]; // Arguments for next call
     * };
     * 
     * int64_t trampoline(int func_id, args...) {
     *   loop:
     *     switch (func_id) {
     *       case 0: result = f0_impl(args); break;
     *       case 1: result = f1_impl(args); break;
     *       ...
     *     }
     *     if (result.func_id < 0) return result.result;
     *     func_id = result.func_id;
     *     args = result.args;
     *     goto loop;
     * }
     */
    
    char tramp_name[256];
    snprintf(tramp_name, sizeof(tramp_name), "__tco_trampoline_%s_%s",
             group->function_names[0], group->function_names[1]);
    
    /* Create trampoline function type */
    /* Simplified: assumes all functions have same signature */
    LLVMValueRef first_func = LLVMGetNamedFunction(cg->mod, 
                                                    group->function_names[0]);
    if (!first_func) return NULL;
    
    LLVMTypeRef func_type = LLVMGlobalGetValueType(first_func);
    LLVMTypeRef ret_type = LLVMGetReturnType(func_type);
    
    /* Build param types: func_id + original params */
    unsigned param_count = LLVMCountParamTypes(func_type);
    LLVMTypeRef *param_types = calloc(param_count + 1, sizeof(LLVMTypeRef));
    param_types[0] = LLVMInt32TypeInContext(cg->ctx);  /* func_id */
    LLVMGetParamTypes(func_type, &param_types[1]);
    
    LLVMTypeRef tramp_type = LLVMFunctionType(ret_type, param_types, 
                                               param_count + 1, false);
    LLVMValueRef tramp_func = LLVMAddFunction(cg->mod, tramp_name, tramp_type);
    
    free(param_types);
    
    /* Mark trampoline function */
    tco_mark_function(cg, tramp_func, TCO_TRANSFORM_TRAMPOLINE);
    
    return tramp_func;
}

/* ============================================================================
 * Living IR Integration
 * ============================================================================ */

void tco_register_with_living_ir(TailCallOptimizer *opt, LivingIR *ir) {
    if (!opt || !ir) return;
    
    opt->living_ir = ir;
    
    /* Register optimized functions for potential further optimization */
    for (int i = 0; i < opt->candidate_count; i++) {
        TailCallCandidate *c = &opt->candidates[i];
        if (c->transformation_applied && c->function_name) {
            /* Add metadata about the transformation */
            LLVMValueRef func = LLVMGetNamedFunction(ir->module, c->function_name);
            if (func) {
                /* Mark function as TCO'd in IR metadata */
                unsigned kind = LLVMGetMDKindIDInContext(ir->context, 
                                                         "qisc.tco.applied", 16);
                LLVMValueRef md_str = LLVMMDString(
                    tco_transform_name(c->transform),
                    strlen(tco_transform_name(c->transform)));
                LLVMValueRef md_node = LLVMMDNode(&md_str, 1);
                LLVMSetMetadata(func, kind, md_node);
            }
        }
    }
}

/* ============================================================================
 * Reporting
 * ============================================================================ */

const char *tco_type_name(TailCallType type) {
    if (type >= 0 && type <= TCO_INDIRECT_TAIL_CALL) {
        return tail_call_type_names[type];
    }
    return "unknown";
}

const char *tco_transform_name(TailCallTransform transform) {
    if (transform >= 0 && transform <= TCO_TRANSFORM_TRAMPOLINE) {
        return transform_names[transform];
    }
    return "unknown";
}

const char *tco_invalid_reason_name(TailCallInvalidReason reason) {
    if (reason >= 0 && reason <= TCO_CLOSURE_CAPTURES) {
        return invalid_reason_names[reason];
    }
    return "unknown";
}

void tco_print_report(TailCallOptimizer *opt) {
    if (!opt) return;
    
    printf("\n");
    printf("┌─────────────────────────────────────────────────────────────┐\n");
    printf("│           TAIL CALL OPTIMIZATION REPORT                     │\n");
    printf("├─────────────────────────────────────────────────────────────┤\n");
    printf("│ Functions analyzed:        %4d                             │\n",
           opt->metrics.functions_analyzed);
    printf("│ Self-recursive functions:  %4d                             │\n",
           opt->metrics.self_recursive_found);
    printf("│ Mutual recursion groups:   %4d                             │\n",
           opt->mutual_group_count);
    printf("│ Loop transforms applied:   %4d                             │\n",
           opt->metrics.loops_created);
    printf("│ Musttail calls applied:    %4d                             │\n",
           opt->metrics.musttail_applied);
    printf("│ Trampolines created:       %4d                             │\n",
           opt->metrics.trampolines_created);
    printf("│ Est. stack savings:        %.1f%%                           │\n",
           opt->metrics.estimated_stack_savings * 100);
    printf("└─────────────────────────────────────────────────────────────┘\n");
    
    /* Print candidate details */
    if (opt->candidate_count > 0) {
        printf("\nTail Call Candidates:\n");
        for (int i = 0; i < opt->candidate_count; i++) {
            TailCallCandidate *c = &opt->candidates[i];
            if (c->tail_call_count > 0) {
                printf("  %s: %d tail call(s)", c->function_name, c->tail_call_count);
                if (c->has_self_recursion) printf(" [self-recursive]");
                if (c->has_mutual_recursion) printf(" [mutual]");
                printf(" → %s\n", tco_transform_name(c->transform));
            }
        }
    }
    
    /* Print mutual recursion groups */
    if (opt->mutual_group_count > 0) {
        printf("\nMutual Recursion Groups:\n");
        for (int i = 0; i < opt->mutual_group_count; i++) {
            MutualRecursionGroup *g = &opt->mutual_groups[i];
            printf("  Group %d: ", i + 1);
            for (int j = 0; j < g->function_count; j++) {
                printf("%s", g->function_names[j]);
                if (j < g->function_count - 1) printf(" <-> ");
            }
            printf("\n");
        }
    }
    
    printf("\n");
}

void tco_print_summary(TailCallOptimizer *opt) {
    if (!opt) return;
    
    printf("[TCO] %d functions, %d self-recursive, %d mutual groups, "
           "%d transforms applied\n",
           opt->metrics.functions_analyzed,
           opt->metrics.self_recursive_found,
           opt->mutual_group_count,
           opt->metrics.loops_created + opt->metrics.musttail_applied +
           opt->metrics.trampolines_created);
}

void tco_dump_analysis(TailCallOptimizer *opt) {
    if (!opt) return;
    
    printf("\n=== TAIL CALL OPTIMIZATION ANALYSIS ===\n\n");
    
    for (int i = 0; i < opt->candidate_count; i++) {
        TailCallCandidate *c = &opt->candidates[i];
        
        printf("Function: %s\n", c->function_name);
        printf("  Total calls: %d\n", c->total_calls);
        printf("  Tail calls: %d\n", c->tail_call_count);
        printf("  Self-recursive: %s\n", c->has_self_recursion ? "yes" : "no");
        printf("  Mutual recursive: %s\n", c->has_mutual_recursion ? "yes" : "no");
        printf("  Transform: %s\n", tco_transform_name(c->transform));
        
        if (c->tail_call_count > 0) {
            printf("  Tail call sites:\n");
            for (int j = 0; j < c->tail_call_count; j++) {
                TailCallSite *s = &c->tail_calls[j];
                printf("    [%d:%d] %s -> %s (%s)\n",
                       s->line, s->column,
                       s->caller_name, s->callee_name,
                       tco_type_name(s->type));
                
                if (s->binding_count > 0) {
                    printf("      Bindings: ");
                    for (int k = 0; k < s->binding_count; k++) {
                        printf("%s", s->bindings[k].param_name);
                        if (s->bindings[k].is_accumulator) printf("(acc)");
                        if (k < s->binding_count - 1) printf(", ");
                    }
                    printf("\n");
                }
            }
        }
        printf("\n");
    }
}
