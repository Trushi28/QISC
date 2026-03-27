/*
 * QISC IR Hash System
 * Structural hashing for convergence detection
 */

#ifndef QISC_IR_HASH_H
#define QISC_IR_HASH_H

#include <stdint.h>
#include <stdbool.h>
#include <llvm-c/Core.h>

/* Hash an LLVM module (structural hash of all functions) */
uint64_t ir_hash_module(LLVMModuleRef module);

/* Hash a single LLVM function */
uint64_t ir_hash_function(LLVMValueRef function);

/* Hash a basic block */
uint64_t ir_hash_block(LLVMBasicBlockRef block);

/* Convergence metrics */
typedef struct {
    uint64_t current_hash;
    uint64_t previous_hash;
    int iterations;
    bool converged;
    double stability;       /* 0.0-1.0, how stable is the IR */
    double edit_distance;   /* Normalized difference from previous */
} ConvergenceMetrics;

/* Initialize convergence tracking */
void convergence_init(ConvergenceMetrics *metrics);

/* Update with new IR hash, returns true if converged */
bool convergence_update(ConvergenceMetrics *metrics, uint64_t new_hash);

/* Check if should continue iterating */
bool convergence_should_continue(ConvergenceMetrics *metrics, int max_iterations);

/* Get convergence summary */
const char* convergence_summary(ConvergenceMetrics *metrics);

#endif /* QISC_IR_HASH_H */
