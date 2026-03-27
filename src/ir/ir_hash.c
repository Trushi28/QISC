/*
 * QISC IR Hash Implementation
 * Structural hashing for convergence detection
 */

#include "ir_hash.h"
#include <llvm-c/Core.h>
#include <string.h>
#include <stdio.h>

/* FNV-1a hash constants */
#define FNV_OFFSET 14695981039346656037ULL
#define FNV_PRIME 1099511628211ULL

static uint64_t fnv1a_hash(const void *data, size_t len) {
    const uint8_t *bytes = (const uint8_t*)data;
    uint64_t hash = FNV_OFFSET;
    for (size_t i = 0; i < len; i++) {
        hash ^= bytes[i];
        hash *= FNV_PRIME;
    }
    return hash;
}

static uint64_t hash_combine(uint64_t h1, uint64_t h2) {
    return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
}

uint64_t ir_hash_block(LLVMBasicBlockRef block) {
    uint64_t hash = FNV_OFFSET;
    
    for (LLVMValueRef inst = LLVMGetFirstInstruction(block);
         inst; inst = LLVMGetNextInstruction(inst)) {
        /* Hash opcode */
        LLVMOpcode op = LLVMGetInstructionOpcode(inst);
        hash = hash_combine(hash, (uint64_t)op);
        
        /* Hash operand count */
        int num_ops = LLVMGetNumOperands(inst);
        hash = hash_combine(hash, (uint64_t)num_ops);
        
        /* Hash type of instruction result */
        LLVMTypeRef ty = LLVMTypeOf(inst);
        LLVMTypeKind kind = LLVMGetTypeKind(ty);
        hash = hash_combine(hash, (uint64_t)kind);
    }
    
    return hash;
}

uint64_t ir_hash_function(LLVMValueRef function) {
    uint64_t hash = FNV_OFFSET;
    
    /* Hash function name */
    const char *name = LLVMGetValueName(function);
    if (name && *name) {
        hash = hash_combine(hash, fnv1a_hash(name, strlen(name)));
    }
    
    /* Hash parameter count */
    unsigned param_count = LLVMCountParams(function);
    hash = hash_combine(hash, (uint64_t)param_count);
    
    /* Hash each basic block */
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(function);
         bb; bb = LLVMGetNextBasicBlock(bb)) {
        hash = hash_combine(hash, ir_hash_block(bb));
    }
    
    return hash;
}

uint64_t ir_hash_module(LLVMModuleRef module) {
    uint64_t hash = FNV_OFFSET;
    
    if (!module) return hash;
    
    /* Hash module identifier if present */
    size_t id_len = 0;
    const char *id = LLVMGetModuleIdentifier(module, &id_len);
    if (id && id_len > 0) {
        hash = hash_combine(hash, fnv1a_hash(id, id_len));
    }
    
    /* Hash all non-declaration functions */
    for (LLVMValueRef fn = LLVMGetFirstFunction(module);
         fn; fn = LLVMGetNextFunction(fn)) {
        if (!LLVMIsDeclaration(fn)) {
            hash = hash_combine(hash, ir_hash_function(fn));
        }
    }
    
    /* Hash global variables */
    for (LLVMValueRef gv = LLVMGetFirstGlobal(module);
         gv; gv = LLVMGetNextGlobal(gv)) {
        const char *gname = LLVMGetValueName(gv);
        if (gname && *gname) {
            hash = hash_combine(hash, fnv1a_hash(gname, strlen(gname)));
        }
    }
    
    return hash;
}

void convergence_init(ConvergenceMetrics *m) {
    m->current_hash = 0;
    m->previous_hash = 0;
    m->iterations = 0;
    m->converged = false;
    m->stability = 0.0;
    m->edit_distance = 1.0;
}

bool convergence_update(ConvergenceMetrics *m, uint64_t new_hash) {
    m->previous_hash = m->current_hash;
    m->current_hash = new_hash;
    m->iterations++;
    
    if (m->iterations > 1) {
        if (m->current_hash == m->previous_hash) {
            m->converged = true;
            m->stability = 1.0;
            m->edit_distance = 0.0;
        } else {
            /* Calculate a rough stability metric based on hash bit differences */
            uint64_t diff = m->current_hash ^ m->previous_hash;
            int bits_different = __builtin_popcountll(diff);
            m->edit_distance = (double)bits_different / 64.0;
            m->stability = 1.0 - m->edit_distance;
        }
    }
    
    return m->converged;
}

bool convergence_should_continue(ConvergenceMetrics *m, int max_iterations) {
    return !m->converged && m->iterations < max_iterations;
}

const char* convergence_summary(ConvergenceMetrics *m) {
    static char buf[256];
    snprintf(buf, sizeof(buf), 
             "Iterations: %d, Converged: %s, Stability: %.1f%%, Hash: 0x%016llx",
             m->iterations, 
             m->converged ? "yes" : "no",
             m->stability * 100.0,
             (unsigned long long)m->current_hash);
    return buf;
}
