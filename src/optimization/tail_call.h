/*
 * QISC Tail Call Optimization System
 *
 * Detects and transforms tail-recursive patterns in the AST,
 * converting them to iterative loops or applying LLVM musttail
 * attribute for guaranteed tail call optimization.
 *
 * Key transformations:
 *   - Self-recursion → iterative loop with goto
 *   - Mutual recursion → state machine or musttail calls
 *   - Accumulator patterns → direct iteration
 *
 * Example transformation:
 *   proc factorial(n: int, acc: int) -> int {
 *     if n <= 1 then acc
 *     else factorial(n - 1, n * acc)
 *   }
 *
 * Becomes (conceptually):
 *   int factorial(int n, int acc) {
 *     loop:
 *       if (n <= 1) return acc;
 *       acc = n * acc;
 *       n = n - 1;
 *       goto loop;
 *   }
 */

#ifndef QISC_OPTIMIZATION_TAIL_CALL_H
#define QISC_OPTIMIZATION_TAIL_CALL_H

#include "../parser/ast.h"
#include "../codegen/codegen.h"
#include "../ir/living_ir.h"
#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stdint.h>

/* Maximum items to track */
#define TCO_MAX_CANDIDATES       256
#define TCO_MAX_MUTUAL_GROUP     16
#define TCO_MAX_PARAM_BINDINGS   32

/* ============================================================================
 * Tail Call Classification
 * ============================================================================ */

typedef enum {
    TCO_NONE = 0,               /* Not a tail call */
    TCO_SELF_RECURSION,         /* Function calls itself in tail position */
    TCO_MUTUAL_RECURSION,       /* Part of mutual recursion cycle */
    TCO_EXTERNAL_TAIL_CALL,     /* Tail call to external/unknown function */
    TCO_INDIRECT_TAIL_CALL,     /* Tail call through function pointer */
} TailCallType;

/* Reasons why a call is NOT in tail position */
typedef enum {
    TCO_VALID = 0,                      /* Is valid tail call */
    TCO_NOT_LAST_EXPR,                  /* Not the last expression */
    TCO_HAS_POST_OPERATIONS,            /* Operations after call */
    TCO_WRAPPED_IN_EXPRESSION,          /* Call result is used in expression */
    TCO_IN_TRY_BLOCK,                   /* Inside try/catch block */
    TCO_DIFFERENT_RETURN_TYPE,          /* Return type mismatch */
    TCO_CALLEE_NOT_FOUND,               /* Cannot resolve callee */
    TCO_CLOSURE_CAPTURES,               /* Closure captures prevent TCO */
} TailCallInvalidReason;

/* Transformation strategy */
typedef enum {
    TCO_TRANSFORM_NONE = 0,
    TCO_TRANSFORM_LOOP,         /* Transform to iterative loop */
    TCO_TRANSFORM_GOTO,         /* Transform using labeled goto */
    TCO_TRANSFORM_MUSTTAIL,     /* Apply LLVM musttail attribute */
    TCO_TRANSFORM_TRAMPOLINE,   /* Use trampoline for mutual recursion */
} TailCallTransform;

/* ============================================================================
 * Data Structures
 * ============================================================================ */

/* Parameter binding for loop transformation */
typedef struct {
    const char *param_name;     /* Original parameter name */
    int param_index;            /* Index in parameter list */
    AstNode *new_value_expr;    /* Expression computing new value */
    bool is_accumulator;        /* True if accumulator pattern detected */
} ParamBinding;

/* Single tail call site */
typedef struct {
    TailCallType type;
    TailCallInvalidReason invalid_reason;
    TailCallTransform suggested_transform;
    
    /* Source location */
    AstNode *call_node;         /* The AST call node */
    AstNode *containing_func;   /* Function containing this call */
    int line;
    int column;
    
    /* Call details */
    const char *caller_name;    /* Name of containing function */
    const char *callee_name;    /* Name of called function */
    bool is_self_recursive;     /* caller == callee */
    
    /* Parameter bindings for transformation */
    ParamBinding bindings[TCO_MAX_PARAM_BINDINGS];
    int binding_count;
    
    /* Condition for conditional tail calls */
    AstNode *condition;         /* NULL if unconditional */
    bool in_else_branch;        /* True if in else branch of if */
    
    /* LLVM values (populated during codegen) */
    LLVMValueRef llvm_call;     /* The LLVM call instruction */
    LLVMBasicBlockRef loop_header; /* Loop header block for transformed code */
} TailCallSite;

/* Mutual recursion group */
typedef struct {
    const char *function_names[TCO_MAX_MUTUAL_GROUP];
    AstNode *functions[TCO_MAX_MUTUAL_GROUP];
    int function_count;
    
    /* Call graph within group */
    bool calls[TCO_MAX_MUTUAL_GROUP][TCO_MAX_MUTUAL_GROUP];
    
    /* Transformation decision */
    TailCallTransform transform;
    bool can_use_musttail;      /* All calls compatible with musttail */
    bool needs_trampoline;      /* Requires trampoline dispatch */
} MutualRecursionGroup;

/* Tail call optimization candidate */
typedef struct {
    /* Function being analyzed */
    AstNode *function;
    const char *function_name;
    
    /* Detected tail calls */
    TailCallSite *tail_calls;
    int tail_call_count;
    int tail_call_capacity;
    
    /* Analysis results */
    bool has_self_recursion;
    bool has_mutual_recursion;
    bool all_returns_are_tail_calls;
    
    /* Transformation decision */
    TailCallTransform transform;
    bool transformation_applied;
    
    /* Mutual recursion group (if any) */
    MutualRecursionGroup *mutual_group;
    
    /* Statistics */
    int total_calls;
    int tail_calls_found;
    int calls_transformed;
} TailCallCandidate;

/* Tail call optimizer state */
typedef struct {
    /* Candidates detected */
    TailCallCandidate candidates[TCO_MAX_CANDIDATES];
    int candidate_count;
    
    /* Mutual recursion groups */
    MutualRecursionGroup *mutual_groups;
    int mutual_group_count;
    int mutual_group_capacity;
    
    /* Configuration */
    struct {
        bool enable_loop_transform;     /* Transform self-recursion to loops */
        bool enable_musttail;           /* Use LLVM musttail attribute */
        bool enable_trampoline;         /* Use trampolines for mutual recursion */
        bool aggressive_mode;           /* Apply more speculative transforms */
        int max_recursion_depth;        /* Depth limit for analysis */
    } config;
    
    /* Statistics */
    struct {
        int functions_analyzed;
        int self_recursive_found;
        int mutual_recursive_found;
        int loops_created;
        int musttail_applied;
        int trampolines_created;
        double estimated_stack_savings;
    } metrics;
    
    /* Living IR integration */
    LivingIR *living_ir;
    
    /* Error handling */
    bool had_error;
    char error_msg[512];
} TailCallOptimizer;

/* ============================================================================
 * Initialization and Cleanup
 * ============================================================================ */

/* Initialize tail call optimizer with default settings */
void tco_init(TailCallOptimizer *opt);

/* Initialize with custom configuration */
void tco_init_ex(TailCallOptimizer *opt, bool loop_transform, 
                 bool musttail, bool trampoline);

/* Free optimizer resources */
void tco_free(TailCallOptimizer *opt);

/* Reset optimizer state for new analysis pass */
void tco_reset(TailCallOptimizer *opt);

/* Set Living IR integration for profile-guided decisions */
void tco_set_living_ir(TailCallOptimizer *opt, LivingIR *ir);

/* ============================================================================
 * AST Analysis
 * ============================================================================ */

/* Analyze entire program for tail call opportunities */
int tco_analyze_program(TailCallOptimizer *opt, AstNode *program);

/* Analyze a single function for tail calls */
TailCallCandidate *tco_analyze_function(TailCallOptimizer *opt, AstNode *func);

/* Check if an expression is in tail position within a function */
bool tco_is_tail_position(AstNode *func, AstNode *expr);

/* Check if a call node is a valid tail call */
TailCallType tco_classify_call(AstNode *func, AstNode *call, 
                                TailCallInvalidReason *out_reason);

/* Detect mutual recursion groups in a program */
int tco_detect_mutual_recursion(TailCallOptimizer *opt, AstNode *program);

/* ============================================================================
 * Tail Position Detection Helpers
 * ============================================================================ */

/* Check if expression is the last thing before function returns */
bool tco_is_last_expression(AstNode *block, AstNode *expr);

/* Check if there are any operations after an expression */
bool tco_has_post_operations(AstNode *parent, AstNode *expr);

/* Check if expression is wrapped (e.g., 1 + f(x) — f(x) is not tail) */
bool tco_is_wrapped(AstNode *parent, AstNode *expr);

/* Check if call is inside try/catch (prevents TCO) */
bool tco_in_try_block(AstNode *func, AstNode *call);

/* Get the containing return/give statement for an expression */
AstNode *tco_get_containing_return(AstNode *func, AstNode *expr);

/* ============================================================================
 * Parameter Binding Analysis
 * ============================================================================ */

/* Extract parameter bindings from a tail call */
int tco_extract_bindings(TailCallSite *site, AstNode *call, AstNode *func);

/* Check if a parameter follows accumulator pattern */
bool tco_is_accumulator(AstNode *func, const char *param_name, 
                        AstNode *new_value_expr);

/* Detect common accumulator patterns (factorial, fold, etc.) */
bool tco_detect_accumulator_pattern(AstNode *func);

/* ============================================================================
 * AST Transformation
 * ============================================================================ */

/* Apply all detected transformations to AST */
int tco_transform_ast(TailCallOptimizer *opt, AstNode *program);

/* Transform a single self-recursive function to iterative loop */
AstNode *tco_transform_to_loop(TailCallCandidate *candidate);

/* Transform mutual recursion group using trampoline */
AstNode *tco_create_trampoline(MutualRecursionGroup *group);

/* Create parameter update statements for loop body */
AstNode *tco_create_param_updates(TailCallSite *site);

/* Replace recursive call with parameter updates and continue */
void tco_replace_call_with_continue(AstNode *func, TailCallSite *site,
                                     LLVMBasicBlockRef loop_header);

/* ============================================================================
 * LLVM Code Generation
 * ============================================================================ */

/* Emit optimized code for a tail-recursive function */
LLVMValueRef tco_emit_function(Codegen *cg, TailCallCandidate *candidate);

/* Emit iterative loop for self-recursive function */
LLVMValueRef tco_emit_loop_transform(Codegen *cg, TailCallCandidate *candidate);

/* Emit function with musttail calls */
LLVMValueRef tco_emit_musttail(Codegen *cg, TailCallCandidate *candidate);

/* Emit trampoline dispatch for mutual recursion */
LLVMValueRef tco_emit_trampoline(Codegen *cg, MutualRecursionGroup *group);

/* Apply musttail attribute to a call instruction */
void tco_apply_musttail(Codegen *cg, LLVMValueRef call);

/* Mark function for TCO in LLVM */
void tco_mark_function(Codegen *cg, LLVMValueRef func, TailCallTransform transform);

/* ============================================================================
 * Living IR Integration
 * ============================================================================ */

/* Register tail call transformations with Living IR */
void tco_register_with_living_ir(TailCallOptimizer *opt, LivingIR *ir);

/* Check profile data for recursion depth hints */
int tco_get_recursion_depth_hint(TailCallOptimizer *opt, const char *func_name);

/* Decide transformation based on profile data */
TailCallTransform tco_decide_transform(TailCallOptimizer *opt, 
                                        TailCallCandidate *candidate);

/* ============================================================================
 * Metrics and Reporting
 * ============================================================================ */

/* Print optimization report */
void tco_print_report(TailCallOptimizer *opt);

/* Print compact summary */
void tco_print_summary(TailCallOptimizer *opt);

/* Dump detailed analysis results */
void tco_dump_analysis(TailCallOptimizer *opt);

/* Get human-readable name for tail call type */
const char *tco_type_name(TailCallType type);

/* Get human-readable name for transform strategy */
const char *tco_transform_name(TailCallTransform transform);

/* Get human-readable reason for invalid tail call */
const char *tco_invalid_reason_name(TailCallInvalidReason reason);

#endif /* QISC_OPTIMIZATION_TAIL_CALL_H */
