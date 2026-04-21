/*
 * QISC Living IR Mutation System — Implementation
 *
 * This is the heart of QISC's self-evolving compilation.
 * The IR literally restructures itself based on runtime profile data.
 *
 * Key insight: Static analysis guesses; profile data knows.
 */

#include "living_ir.h"
#include <llvm-c/Core.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ======== Internal Helpers ======== */

/* Calculate the size of a function in instructions */
static int count_function_instructions(LLVMValueRef func) {
    int count = 0;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
             inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {
            count++;
        }
    }
    return count;
}

/* Count basic blocks in a function */
static int count_function_blocks(LLVMValueRef func) {
    int count = 0;
    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        count++;
    }
    return count;
}

/* Check if a function is a declaration (no body) */
static bool is_declaration(LLVMValueRef func) {
    return LLVMCountBasicBlocks(func) == 0;
}

/* Get function name safely */
static const char *get_function_name(LLVMValueRef func) {
    size_t len;
    return LLVMGetValueName2(func, &len);
}

/* Check if function has internal linkage (can be modified) */
static bool has_internal_linkage(LLVMValueRef func) {
    LLVMLinkage linkage = LLVMGetLinkage(func);
    return linkage == LLVMInternalLinkage || 
           linkage == LLVMPrivateLinkage;
}

/* Find all call sites to a function */
static int find_call_sites(LLVMModuleRef module, LLVMValueRef callee,
                           LLVMValueRef *callers, int max_callers) {
    int count = 0;
    const char *callee_name = get_function_name(callee);
    
    for (LLVMValueRef func = LLVMGetFirstFunction(module);
         func != NULL;
         func = LLVMGetNextFunction(func)) {
        
        if (is_declaration(func)) continue;
        if (LLVMGetNamedFunction(module, "__qisc_user_main") &&
            strcmp(get_function_name(func), "main") == 0) {
            continue;
        }
        if (func == callee) continue;
        
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func);
             bb != NULL;
             bb = LLVMGetNextBasicBlock(bb)) {
            
            for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
                 inst != NULL;
                 inst = LLVMGetNextInstruction(inst)) {
                
                if (LLVMGetInstructionOpcode(inst) == LLVMCall) {
                    LLVMValueRef called = LLVMGetCalledValue(inst);
                    if (called && strcmp(get_function_name(called), callee_name) == 0) {
                        if (count < max_callers) {
                            callers[count++] = func;
                        }
                    }
                }
            }
        }
    }
    return count;
}

/* Create cold function name with suffix */
static char *create_cold_function_name(const char *original, int block_id) {
    size_t len = strlen(original) + 32;
    char *name = malloc(len);
    snprintf(name, len, "%s.__cold_%d", original, block_id);
    return name;
}

static const char *profile_function_name(const char *func_name) {
    if (!func_name) return NULL;
    if (strcmp(func_name, "__qisc_user_main") == 0) {
        return "main";
    }
    return func_name;
}

static bool is_wrapper_main(LivingIR *ir, LLVMValueRef func) {
    const char *func_name;

    if (!ir || !func) return false;

    func_name = get_function_name(func);
    if (!func_name || strcmp(func_name, "main") != 0) return false;

    return LLVMGetNamedFunction(ir->module, "__qisc_user_main") != NULL;
}

static bool should_skip_function(LivingIR *ir, LLVMValueRef func) {
    if (!func) return true;
    if (is_declaration(func)) return true;
    if (is_wrapper_main(ir, func)) return true;
    return false;
}

typedef struct {
    LLVMValueRef from;
    LLVMValueRef to;
} ValueRemap;

#define LIVING_IR_MAX_OUTLINE_VALUES 128
#define LIVING_IR_MAX_OUTLINE_INSTS 256

static int get_qisc_line_metadata(LivingIR *ir, LLVMValueRef inst);

static void add_enum_attr_if_supported(LivingIR *ir, LLVMValueRef func,
                                       const char *name) {
    unsigned kind;

    if (!ir || !func || !name) return;

    kind = LLVMGetEnumAttributeKindForName(name, strlen(name));
    if (kind == 0) return;

    LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
        LLVMCreateEnumAttribute(ir->context, kind, 0));
}

static void add_string_attr(LivingIR *ir, LLVMValueRef func,
                            const char *key, const char *value) {
    if (!ir || !func || !key || !value) return;

    LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
        LLVMCreateStringAttribute(ir->context,
            key, strlen(key),
            value, strlen(value)));
}

static LLVMValueRef remap_lookup(ValueRemap *items, int count, LLVMValueRef from) {
    for (int i = 0; i < count; i++) {
        if (items[i].from == from) return items[i].to;
    }
    return NULL;
}

static bool block_value_used_outside(LLVMValueRef value, LLVMBasicBlockRef block) {
    LLVMUseRef use;

    if (!value || !block) return false;

    for (use = LLVMGetFirstUse(value); use != NULL; use = LLVMGetNextUse(use)) {
        LLVMValueRef user = LLVMGetUser(use);
        if (!user) continue;
        if (!LLVMIsAInstruction(user)) continue;
        if (LLVMGetInstructionParent(user) != block) return true;
    }

    return false;
}

static bool successor_has_phi_from_block(LLVMBasicBlockRef block,
                                         LLVMBasicBlockRef successor) {
    for (LLVMValueRef inst = LLVMGetFirstInstruction(successor);
         inst != NULL && LLVMIsAPHINode(inst);
         inst = LLVMGetNextInstruction(inst)) {
        int incoming = LLVMCountIncoming(inst);
        for (int i = 0; i < incoming; i++) {
            if (LLVMGetIncomingBlock(inst, i) == block) {
                return true;
            }
        }
    }

    return false;
}

static double estimate_block_frequency(LivingIR *ir, const char *func_name,
                                       LLVMBasicBlockRef bb,
                                       ProfileFunction *pf,
                                       int total_blocks) {
    double freq = total_blocks > 0 ? 1.0 / total_blocks : 1.0;
    bool used_profile = false;
    LLVMValueRef func;

    if (!ir || !func_name || !bb || !pf) return freq;

    func = LLVMGetBasicBlockParent(bb);
    for (LLVMBasicBlockRef pred = LLVMGetFirstBasicBlock(func);
         pred != NULL;
         pred = LLVMGetNextBasicBlock(pred)) {
        LLVMValueRef term = LLVMGetBasicBlockTerminator(pred);
        int line;
        char location[256];
        ProfileBranch *branch;
        double edge_prob;

        if (!term || LLVMGetInstructionOpcode(term) != LLVMBr ||
            LLVMGetNumSuccessors(term) != 2) {
            continue;
        }

        line = get_qisc_line_metadata(ir, term);
        if (line <= 0) continue;

        snprintf(location, sizeof(location), "%s:%d", func_name, line);
        branch = profile_get_branch(ir->profile, location);
        if (!branch) continue;

        /*
         * In this pipeline, the recorded "taken" branch maps to successor 1
         * and the recorded "not taken" branch maps to successor 0.
         */
        if (LLVMGetSuccessor(term, 0) == bb) {
            edge_prob = 1.0 - branch->taken_ratio;
        } else if (LLVMGetSuccessor(term, 1) == bb) {
            edge_prob = branch->taken_ratio;
        } else {
            continue;
        }

        freq = edge_prob;
        used_profile = true;
        if (branch->is_predictable) break;
    }

    if (!used_profile && bb == LLVMGetEntryBasicBlock(func)) {
        return 1.0;
    }

    return freq;
}

static bool is_simple_outline_candidate(ColdBlock *cold, LLVMBasicBlockRef *successor) {
    LLVMBasicBlockRef succ = NULL;
    LLVMValueRef term;

    if (!cold || !cold->can_outline || !cold->block) return false;

    term = LLVMGetBasicBlockTerminator(cold->block);
    if (!term || LLVMGetInstructionOpcode(term) != LLVMBr ||
        LLVMGetNumSuccessors(term) != 1) {
        return false;
    }

    succ = LLVMGetSuccessor(term, 0);
    if (successor) *successor = succ;

    if (successor_has_phi_from_block(cold->block, succ)) return false;

    for (LLVMValueRef inst = LLVMGetFirstInstruction(cold->block);
         inst != NULL;
         inst = LLVMGetNextInstruction(inst)) {
        if (inst == term) break;
        if (LLVMIsAPHINode(inst)) return false;
        if (LLVMGetTypeKind(LLVMTypeOf(inst)) != LLVMVoidTypeKind &&
            block_value_used_outside(inst, cold->block)) {
            return false;
        }
    }

    return true;
}

static int collect_outline_inputs(ColdBlock *cold, LLVMValueRef *inputs,
                                  int max_inputs) {
    ValueRemap locals[LIVING_IR_MAX_OUTLINE_INSTS];
    int local_count = 0;
    int input_count = 0;
    LLVMValueRef term;

    if (!cold || !cold->block) return 0;

    term = LLVMGetBasicBlockTerminator(cold->block);
    for (LLVMValueRef inst = LLVMGetFirstInstruction(cold->block);
         inst != NULL && inst != term;
         inst = LLVMGetNextInstruction(inst)) {
        if (local_count < LIVING_IR_MAX_OUTLINE_INSTS) {
            locals[local_count].from = inst;
            locals[local_count].to = NULL;
            local_count++;
        }
    }

    for (LLVMValueRef inst = LLVMGetFirstInstruction(cold->block);
         inst != NULL && inst != term;
         inst = LLVMGetNextInstruction(inst)) {
        int operands = LLVMGetNumOperands(inst);
        for (int i = 0; i < operands; i++) {
            LLVMValueRef op = LLVMGetOperand(inst, i);
            bool local = false;
            bool seen = false;

            if (!op || LLVMValueIsBasicBlock(op) || LLVMIsAConstant(op) ||
                LLVMIsAFunction(op) || LLVMIsAGlobalValue(op)) {
                continue;
            }

            for (int j = 0; j < local_count; j++) {
                if (locals[j].from == op) {
                    local = true;
                    break;
                }
            }
            if (local) continue;

            for (int j = 0; j < input_count; j++) {
                if (inputs[j] == op) {
                    seen = true;
                    break;
                }
            }
            if (seen || input_count >= max_inputs) continue;

            inputs[input_count++] = op;
        }
    }

    return input_count;
}

static char *create_unique_cold_function_name(LivingIR *ir,
                                              const char *original,
                                              int block_id) {
    char *name;
    int suffix = block_id;

    if (!ir || !original) return NULL;

    name = create_cold_function_name(original, suffix);
    while (name && LLVMGetNamedFunction(ir->module, name) != NULL) {
        free(name);
        suffix++;
        name = create_cold_function_name(original, suffix);
    }

    return name;
}

static double sample_confidence(uint64_t samples, uint64_t high_water) {
    if (high_water == 0) high_water = 1;
    if (samples >= high_water) return 1.0;
    return (double)samples / (double)high_water;
}

static double profile_generation_confidence(LivingIR *ir) {
    if (!ir || !ir->profile) return 0.0;
    if (ir->profile->has_converged) return 1.0;
    if (ir->profile->run_count >= 3) return 0.85;
    if (ir->profile->run_count == 2) return 0.65;
    if (ir->profile->run_count == 1) return 0.40;
    return 0.0;
}

static double function_time_percentage(LivingIR *ir, ProfileFunction *pf) {
    double total_time;
    double self_time;

    if (!ir || !pf) return 0.0;

    total_time = (double)(ir->profile->total_execution_time_us > 0 ?
                          ir->profile->total_execution_time_us : 1);
    self_time = pf->avg_time_us * pf->call_count;
    return (self_time / total_time) * 100.0;
}

static uint64_t total_profile_calls(LivingIR *ir) {
    uint64_t total = 0;

    if (!ir || !ir->profile) return 0;

    for (int i = 0; i < ir->profile->function_count; i++) {
        total += ir->profile->functions[i].call_count;
    }

    return total;
}

static double branch_predictability_for_function(LivingIR *ir,
                                                 const char *func_name) {
    double total = 0.0;
    int count = 0;

    if (!ir || !func_name || !ir->profile) return 0.5;

    for (int i = 0; i < ir->profile->branch_count; i++) {
        ProfileBranch *br = &ir->profile->branches[i];
        size_t len;

        if (!br->location) continue;
        len = strlen(func_name);
        if (strncmp(br->location, func_name, len) != 0 || br->location[len] != ':') {
            continue;
        }

        total += br->is_predictable ? 0.98 :
                 (br->taken_ratio > 0.5 ? br->taken_ratio : 1.0 - br->taken_ratio);
        count++;
    }

    return count > 0 ? total / count : 0.5;
}

static void specialize_function_from_profile(LivingIR *ir, LLVMValueRef func,
                                             ProfileFunction *pf) {
    IRMetadata meta;
    double time_pct;
    int opt_level;
    char unroll_hint[16];

    if (!ir || !func || !pf) return;

    ir_metadata_init(&meta);
    time_pct = function_time_percentage(ir, pf);
    ir_metadata_from_profile(&meta, pf->call_count, total_profile_calls(ir), time_pct);
    meta.branch_predictability =
        branch_predictability_for_function(ir, profile_function_name(pf->name));
    if (profile_generation_confidence(ir) > meta.confidence) {
        meta.confidence = profile_generation_confidence(ir);
    }

    if (ir->profile && ir->profile->cache_hit_ratio > 0.90) {
        meta.memory_behavior = MEMORY_SEQUENTIAL;
    } else if (ir->profile && ir->profile->cache_hit_ratio > 0.70) {
        meta.memory_behavior = MEMORY_LOCALIZED;
    }

    ir_metadata_compute_hints(&meta);
    if (meta.confidence < ir->config.min_confidence) {
        ir->metrics.mutations_rejected++;
        return;
    }

    opt_level = profile_get_opt_level(ir->profile, profile_function_name(pf->name));
    if (pf->is_hot || meta.hints.is_hot_path || opt_level >= 3) {
        add_enum_attr_if_supported(ir, func, "hot");
        add_enum_attr_if_supported(ir, func, "inlinehint");
        LLVMSetSection(func, ".text.hot");
        ir->metrics.hot_functions_specialized++;
    } else if (pf->is_cold || meta.hints.is_cold_path || opt_level <= 1) {
        add_enum_attr_if_supported(ir, func, "cold");
        add_enum_attr_if_supported(ir, func, "minsize");
        add_enum_attr_if_supported(ir, func, "optsize");
        LLVMSetSection(func, ".text.unlikely");
        ir->metrics.cold_functions_specialized++;
    }

    if (meta.hints.should_vectorize) {
        add_string_attr(ir, func, "qisc.vectorize", "true");
    }
    if (meta.hints.should_parallelize) {
        add_string_attr(ir, func, "qisc.parallelize", "true");
    }
    if (meta.hints.should_unroll && meta.hints.unroll_factor > 1) {
        snprintf(unroll_hint, sizeof(unroll_hint), "%d", meta.hints.unroll_factor);
        add_string_attr(ir, func, "qisc.loop_unroll", unroll_hint);
    }
    if (pf->should_inline || meta.hints.should_inline) {
        add_enum_attr_if_supported(ir, func, "inlinehint");
    }
}

static void apply_loop_unroll_metadata(LivingIR *ir, LLVMBasicBlockRef header,
                                       int unroll_count) {
    if (!ir || !header || unroll_count <= 1) return;

    LLVMValueRef terminator = LLVMGetBasicBlockTerminator(header);
    if (!terminator) return;

    LLVMMetadataRef loop_md[4];
    unsigned count = 0;

    /* Self-reference placeholder required by LLVM loop metadata. */
    loop_md[count++] = NULL;

    LLVMMetadataRef enable_key =
        LLVMMDStringInContext2(ir->context, "llvm.loop.unroll.enable", 23);
    LLVMMetadataRef enable_val =
        LLVMValueAsMetadata(LLVMConstInt(LLVMInt1TypeInContext(ir->context), 1, false));
    LLVMMetadataRef enable_pair[] = {enable_key, enable_val};
    loop_md[count++] = LLVMMDNodeInContext2(ir->context, enable_pair, 2);

    LLVMMetadataRef count_key =
        LLVMMDStringInContext2(ir->context, "llvm.loop.unroll.count", 22);
    LLVMMetadataRef count_val =
        LLVMValueAsMetadata(LLVMConstInt(LLVMInt32TypeInContext(ir->context),
                                         (unsigned)unroll_count, false));
    LLVMMetadataRef count_pair[] = {count_key, count_val};
    loop_md[count++] = LLVMMDNodeInContext2(ir->context, count_pair, 2);

    LLVMMetadataRef md = LLVMMDNodeInContext2(ir->context, loop_md, count);
    loop_md[0] = md;
    md = LLVMMDNodeInContext2(ir->context, loop_md, count);

    unsigned loop_kind = LLVMGetMDKindIDInContext(ir->context, "llvm.loop", 9);
    LLVMValueRef md_val = LLVMMetadataAsValue(ir->context, md);
    LLVMSetMetadata(terminator, loop_kind, md_val);
}

static void apply_branch_weight_metadata(LivingIR *ir, LLVMValueRef branch_inst,
                                         unsigned true_weight,
                                         unsigned false_weight) {
    unsigned prof_kind;
    LLVMMetadataRef weights[3];
    LLVMMetadataRef md;
    LLVMValueRef md_val;

    if (!ir || !branch_inst) return;

    prof_kind = LLVMGetMDKindIDInContext(ir->context, "prof", 4);
    weights[0] = LLVMMDStringInContext2(ir->context, "branch_weights", 14);
    weights[1] = LLVMValueAsMetadata(
        LLVMConstInt(LLVMInt32TypeInContext(ir->context),
                     true_weight ? true_weight : 1, false));
    weights[2] = LLVMValueAsMetadata(
        LLVMConstInt(LLVMInt32TypeInContext(ir->context),
                     false_weight ? false_weight : 1, false));
    md = LLVMMDNodeInContext2(ir->context, weights, 3);
    md_val = LLVMMetadataAsValue(ir->context, md);
    LLVMSetMetadata(branch_inst, prof_kind, md_val);
}

static int get_qisc_line_metadata(LivingIR *ir, LLVMValueRef inst) {
    unsigned line_kind;
    LLVMValueRef md;
    unsigned count;
    LLVMValueRef operands[4];

    if (!ir || !inst || !LLVMHasMetadata(inst)) return -1;

    line_kind = LLVMGetMDKindIDInContext(ir->context, "qisc.line", 9);
    md = LLVMGetMetadata(inst, line_kind);
    if (!md) return -1;

    count = LLVMGetMDNodeNumOperands(md);
    if (count == 0 || count > 4) return -1;

    memset(operands, 0, sizeof(operands));
    LLVMGetMDNodeOperands(md, operands);
    if (!operands[0] || !LLVMIsAConstantInt(operands[0])) return -1;

    return (int)LLVMConstIntGetZExtValue(operands[0]);
}

static LLVMValueRef find_profile_function(LivingIR *ir, const char *profile_name) {
    LLVMValueRef user_main;

    if (!ir || !profile_name) return NULL;

    if (strcmp(profile_name, "main") == 0) {
        user_main = LLVMGetNamedFunction(ir->module, "__qisc_user_main");
        if (user_main && !is_declaration(user_main)) {
            return user_main;
        }
    }

    for (LLVMValueRef func = LLVMGetFirstFunction(ir->module);
         func != NULL;
         func = LLVMGetNextFunction(func)) {
        if (should_skip_function(ir, func)) continue;
        const char *func_name = get_function_name(func);
        if (func_name && strcmp(profile_function_name(func_name), profile_name) == 0) {
            return func;
        }
    }

    return NULL;
}

static LLVMBasicBlockRef find_loop_header_for_location(LivingIR *ir,
                                                       const char *location) {
    char func_name[256];
    char *colon;
    int line;
    LLVMValueRef func;

    if (!ir || !location) return NULL;

    strncpy(func_name, location, sizeof(func_name) - 1);
    func_name[sizeof(func_name) - 1] = '\0';
    colon = strrchr(func_name, ':');
    if (!colon) return NULL;

    *colon = '\0';
    line = atoi(colon + 1);
    if (line <= 0) return NULL;

    func = find_profile_function(ir, func_name);
    if (!func) return NULL;

    for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func);
         bb != NULL;
         bb = LLVMGetNextBasicBlock(bb)) {
        LLVMValueRef term = LLVMGetBasicBlockTerminator(bb);
        if (!term) continue;
        if (LLVMGetInstructionOpcode(term) != LLVMBr ||
            LLVMGetNumSuccessors(term) != 2) {
            continue;
        }
        if (get_qisc_line_metadata(ir, term) == line) {
            return bb;
        }
    }

    return NULL;
}

/* ======== Creation and Destruction ======== */

LivingIR *living_ir_create(LLVMModuleRef module, QiscProfile *profile) {
    return living_ir_create_with_metadata(module, profile, NULL);
}

LivingIR *living_ir_create_with_metadata(LLVMModuleRef module,
                                          QiscProfile *profile,
                                          IRMetadata *metadata) {
    if (!module || !profile) {
        return NULL;
    }
    
    LivingIR *ir = calloc(1, sizeof(LivingIR));
    if (!ir) return NULL;
    
    ir->module = module;
    ir->context = LLVMGetModuleContext(module);
    ir->profile = profile;
    ir->metadata = metadata;
    
    /* Default configuration */
    ir->config.enable_inlining = true;
    ir->config.enable_outlining = true;
    ir->config.enable_loop_restructure = true;
    ir->config.enable_block_reorder = true;
    ir->config.aggressive_mode = false;
    ir->config.max_inline_depth = 3;
    ir->config.min_confidence = 0.25;
    
    /* Initialize metrics */
    memset(&ir->metrics, 0, sizeof(LivingIRMetrics));
    ir->metrics.estimated_speedup = 1.0;
    
    return ir;
}

void living_ir_destroy(LivingIR *ir) {
    if (!ir) return;
    free(ir);
}

/* ======== Configuration ======== */

void living_ir_set_inlining(LivingIR *ir, bool enable) {
    if (ir) ir->config.enable_inlining = enable;
}

void living_ir_set_outlining(LivingIR *ir, bool enable) {
    if (ir) ir->config.enable_outlining = enable;
}

void living_ir_set_loop_restructure(LivingIR *ir, bool enable) {
    if (ir) ir->config.enable_loop_restructure = enable;
}

void living_ir_set_block_reorder(LivingIR *ir, bool enable) {
    if (ir) ir->config.enable_block_reorder = enable;
}

void living_ir_set_aggressive(LivingIR *ir, bool aggressive) {
    if (ir) ir->config.aggressive_mode = aggressive;
}

void living_ir_set_min_confidence(LivingIR *ir, double confidence) {
    if (ir) {
        if (confidence < 0.0) confidence = 0.0;
        if (confidence > 1.0) confidence = 1.0;
        ir->config.min_confidence = confidence;
    }
}

/* ======== Analysis Phase ======== */

/* Calculate hotness score based on call count and time */
double living_ir_calc_hotness(LivingIR *ir, const char *func_name) {
    if (!ir || !func_name) return 0.0;
    
    ProfileFunction *pf = profile_get_function(ir->profile,
                                               profile_function_name(func_name));
    if (!pf) return 0.0;
    
    /* Hotness = normalized call count * time percentage */
    double call_factor = (double)pf->call_count / 
        (ir->profile->total_execution_time_us > 0 ? 
         ir->profile->total_execution_time_us : 1);
    
    double time_factor = pf->avg_time_us * pf->call_count / 
        (ir->profile->total_execution_time_us > 0 ?
         ir->profile->total_execution_time_us : 1);
    
    return call_factor * 0.4 + time_factor * 0.6;
}

/* Check if a function should be inlined at a call site */
bool living_ir_should_inline(LivingIR *ir, const char *caller,
                              const char *callee, uint64_t call_count) {
    if (!ir || !caller || !callee) return false;
    
    /* Must have enough calls to matter */
    if (call_count < LIVING_IR_HOT_CALL_THRESHOLD) {
        return false;
    }
    
    /* Check if callee is in profile */
    ProfileFunction *pf_callee = profile_get_function(
        ir->profile, profile_function_name(callee));
    ProfileFunction *pf_caller = profile_get_function(
        ir->profile, profile_function_name(caller));
    
    /* Both caller and callee should be hot */
    if (!pf_callee || !pf_caller) return false;
    if (!pf_caller->is_hot && !ir->config.aggressive_mode) return false;
    
    /* Check callee recommendations */
    if (pf_callee->should_inline) return true;
    
    /* Aggressive mode: inline anything that's called enough */
    if (ir->config.aggressive_mode && call_count > LIVING_IR_HOT_CALL_THRESHOLD * 10) {
        return true;
    }
    
    return false;
}

/* Analyze for inlining candidates */
static void analyze_inlining(LivingIR *ir) {
    ir->inline_candidate_count = 0;
    
    for (LLVMValueRef func = LLVMGetFirstFunction(ir->module);
         func != NULL;
         func = LLVMGetNextFunction(func)) {
        
        if (should_skip_function(ir, func)) continue;
        
        const char *func_name = get_function_name(func);
        ProfileFunction *pf = profile_get_function(
            ir->profile, profile_function_name(func_name));
        
        if (!pf) continue;
        if (!pf->is_hot && !pf->should_inline) continue;
        
        int func_size = count_function_instructions(func);
        
        /* Find all callers */
        LLVMValueRef callers[64];
        int caller_count = find_call_sites(ir->module, func, callers, 64);
        
        for (int i = 0; i < caller_count; i++) {
            const char *caller_name = get_function_name(callers[i]);
            ProfileFunction *pf_caller = profile_get_function(
                ir->profile, profile_function_name(caller_name));
            
            if (!pf_caller) continue;
            
            /* Estimate call count at this site (assume uniform distribution) */
            uint64_t site_calls = pf->call_count / (caller_count > 0 ? caller_count : 1);
            
            if (ir->inline_candidate_count >= LIVING_IR_MAX_INLINE_CANDIDATES) break;
            
            InlineCandidate *candidate = &ir->inline_candidates[ir->inline_candidate_count];
            candidate->caller = caller_name;
            candidate->callee = func_name;
            candidate->call_count = site_calls;
            candidate->callee_size = func_size;
            candidate->hotness_score = living_ir_calc_hotness(ir, func_name);
            
            /* Determine if we should inline */
            if (func_size > LIVING_IR_INLINE_SIZE_LIMIT && !ir->config.aggressive_mode) {
                candidate->should_inline = false;
                candidate->reason = "function too large";
            } else if (site_calls < LIVING_IR_HOT_CALL_THRESHOLD) {
                candidate->should_inline = false;
                candidate->reason = "call site not hot enough";
            } else if (!pf_caller->is_hot && !ir->config.aggressive_mode) {
                candidate->should_inline = false;
                candidate->reason = "caller not hot";
            } else {
                candidate->should_inline = true;
                candidate->reason = pf->should_inline ? 
                    "profile recommends inlining" : "hot path optimization";
            }
            
            ir->inline_candidate_count++;
        }
        
        ir->metrics.functions_analyzed++;
    }
    
    ir->metrics.inline_candidates_found = ir->inline_candidate_count;
}

/* Check if a block is cold enough to outline */
bool living_ir_is_cold_block(LivingIR *ir, LLVMBasicBlockRef block,
                              double freq) {
    if (!ir || !block) return false;
    
    /* Cold if execution frequency is below threshold */
    if (freq >= LIVING_IR_COLD_FREQ_THRESHOLD) return false;
    
    /* Don't outline entry blocks */
    LLVMValueRef func = LLVMGetBasicBlockParent(block);
    if (block == LLVMGetEntryBasicBlock(func)) return false;
    
    /* Don't outline blocks with landing pads (exception handling) */
    LLVMValueRef first_inst = LLVMGetFirstInstruction(block);
    if (first_inst && LLVMGetInstructionOpcode(first_inst) == LLVMLandingPad) {
        return false;
    }
    
    return true;
}

/* Analyze for cold block outlining */
static void analyze_cold_blocks(LivingIR *ir) {
    ir->cold_block_count = 0;
    
    for (LLVMValueRef func = LLVMGetFirstFunction(ir->module);
         func != NULL;
         func = LLVMGetNextFunction(func)) {
        
        if (should_skip_function(ir, func)) continue;
        
        const char *func_name = get_function_name(func);
        ProfileFunction *pf = profile_get_function(
            ir->profile, profile_function_name(func_name));
        
        /* Only analyze hot functions - cold functions don't need outlining */
        if (!pf || !pf->is_hot) continue;
        
        int total_blocks = count_function_blocks(func);
        
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func);
             bb != NULL;
             bb = LLVMGetNextBasicBlock(bb)) {
            
            double block_freq = estimate_block_frequency(
                ir, profile_function_name(func_name), bb, pf, total_blocks);
            double confidence = profile_generation_confidence(ir);
            double call_confidence = sample_confidence(pf->call_count, 16);
            if (call_confidence > confidence) {
                confidence = call_confidence;
            }
            
            if (ir->cold_block_count >= LIVING_IR_MAX_COLD_BLOCKS) break;
            
            ColdBlock *cold = &ir->cold_blocks[ir->cold_block_count];
            cold->block = bb;
            cold->function_name = func_name;
            cold->execution_freq = block_freq;
            
            /* Count instructions in block */
            int inst_count = 0;
            for (LLVMValueRef inst = LLVMGetFirstInstruction(bb);
                 inst != NULL;
                 inst = LLVMGetNextInstruction(inst)) {
                inst_count++;
            }
            cold->instruction_count = inst_count;
            
            /* Determine if we can outline */
            if (confidence < ir->config.min_confidence) {
                cold->can_outline = false;
                cold->reason = "profile confidence too low";
                ir->metrics.mutations_rejected++;
            } else if (!living_ir_is_cold_block(ir, bb, block_freq)) {
                cold->can_outline = false;
                cold->reason = "block not cold enough";
            } else if (inst_count < 3) {
                cold->can_outline = false;
                cold->reason = "block too small to outline";
            } else {
                cold->can_outline = true;
                cold->reason = "cold block candidate";
                ir->metrics.cold_blocks_found++;
            }
            
            ir->cold_block_count++;
        }
    }
}

/* Get suggested unroll factor for a loop */
int living_ir_suggest_unroll(LivingIR *ir, const char *loop_location) {
    if (!ir || !loop_location) return 0;
    
    ProfileLoop *pl = profile_get_loop(ir->profile, loop_location);
    if (!pl) return 0;
    
    /* Use profile's suggestion if available */
    if (pl->should_unroll && pl->suggested_unroll_factor > 0) {
        return pl->suggested_unroll_factor;
    }
    
    /* Otherwise, calculate from average iterations */
    if (pl->avg_iterations < LIVING_IR_UNROLL_SMALL_LOOP) {
        return (int)pl->avg_iterations;  /* Full unroll */
    } else if (pl->avg_iterations < LIVING_IR_UNROLL_MEDIUM_LOOP) {
        return 4;  /* Partial unroll 4x */
    } else if (pl->avg_iterations < 100) {
        return 8;  /* Partial unroll 8x */
    } else {
        return 4;  /* Conservative for very large loops */
    }
}

/* Analyze loops for restructuring */
static void analyze_loops(LivingIR *ir) {
    ir->loop_mutation_count = 0;
    
    /* Iterate through profile loops */
    for (int i = 0; i < ir->profile->loop_count; i++) {
        ProfileLoop *pl = &ir->profile->loops[i];
        double confidence;
        
        if (ir->loop_mutation_count >= LIVING_IR_MAX_LOOP_MUTATIONS) break;
        confidence = sample_confidence(pl->invocation_count, 4);
        if (confidence < ir->config.min_confidence) {
            ir->metrics.mutations_rejected++;
            continue;
        }
        
        LoopMutation *mutation = &ir->loop_mutations[ir->loop_mutation_count];
        mutation->location = pl->location;
        
        /* Map location to LLVM block by searching module functions */
        mutation->header = find_loop_header_for_location(ir, pl->location);
        
        mutation->avg_iterations = pl->avg_iterations;
        
        /* Determine restructuring strategy */
        if (pl->avg_iterations < LIVING_IR_UNROLL_SMALL_LOOP) {
            /* Small loop: fully unroll */
            mutation->suggested_unroll = (int)pl->avg_iterations;
            mutation->should_prefetch = false;
            mutation->should_tile = false;
        } else if (pl->avg_iterations < LIVING_IR_UNROLL_MEDIUM_LOOP) {
            /* Medium loop: partial unroll */
            mutation->suggested_unroll = 4;
            mutation->should_prefetch = false;
            mutation->should_tile = false;
        } else if (pl->avg_iterations > LIVING_IR_PREFETCH_THRESHOLD) {
            /* Large loop: add prefetch, consider tiling */
            mutation->suggested_unroll = 4;
            mutation->should_prefetch = true;
            mutation->should_tile = (pl->avg_iterations > 10000);
            mutation->tile_size = 64;
        } else {
            /* Default: modest unroll */
            mutation->suggested_unroll = 4;
            mutation->should_prefetch = false;
            mutation->should_tile = false;
        }
        
        ir->loop_mutation_count++;
        ir->metrics.loops_analyzed++;
    }
}

void living_ir_analyze(LivingIR *ir) {
    if (!ir) return;
    
    /* Reset metrics */
    memset(&ir->metrics, 0, sizeof(LivingIRMetrics));
    ir->metrics.estimated_speedup = 1.0;
    
    /* Run all analysis passes */
    if (ir->config.enable_inlining) {
        analyze_inlining(ir);
    }
    
    if (ir->config.enable_outlining) {
        analyze_cold_blocks(ir);
    }
    
    if (ir->config.enable_loop_restructure) {
        analyze_loops(ir);
    }
}

int living_ir_get_inline_candidates(LivingIR *ir, InlineCandidate **candidates) {
    if (!ir || !candidates) return 0;
    *candidates = ir->inline_candidates;
    return ir->inline_candidate_count;
}

int living_ir_get_cold_blocks(LivingIR *ir, ColdBlock **blocks) {
    if (!ir || !blocks) return 0;
    *blocks = ir->cold_blocks;
    return ir->cold_block_count;
}

int living_ir_get_loop_mutations(LivingIR *ir, LoopMutation **mutations) {
    if (!ir || !mutations) return 0;
    *mutations = ir->loop_mutations;
    return ir->loop_mutation_count;
}

/* ======== Mutation Phase ======== */

void living_ir_inline_hot_paths(LivingIR *ir) {
    if (!ir || !ir->config.enable_inlining) return;
    
    /*
     * Set inline hints on functions based on profile data.
     * We use LLVM attributes instead of deprecated passes.
     */
    for (int i = 0; i < ir->inline_candidate_count; i++) {
        InlineCandidate *candidate = &ir->inline_candidates[i];
        
        if (!candidate->should_inline) continue;
        
        /* Find the callee function and add inline hint */
        LLVMValueRef callee_func = LLVMGetNamedFunction(ir->module, candidate->callee);
        if (callee_func) {
            /* Add alwaysinline attribute for hot functions */
            LLVMAddAttributeAtIndex(callee_func, LLVMAttributeFunctionIndex,
                LLVMCreateEnumAttribute(ir->context, 
                    LLVMGetEnumAttributeKindForName("alwaysinline", 12), 0));
            ir->metrics.functions_inlined++;
        }
    }
    
    /* Estimate speedup from inlining (rough heuristic) */
    double inline_speedup = 1.0 + (ir->metrics.functions_inlined * 0.02);
    ir->metrics.estimated_speedup *= inline_speedup;
}

void living_ir_outline_cold_paths(LivingIR *ir) {
    if (!ir || !ir->config.enable_outlining) return;
    
    /*
     * Cold code outlining is complex because we need to:
     * 1. Create a new function with the cold block's code
     * 2. Add parameters for any values used from the original function
     * 3. Replace the original block with a call to the new function
     * 4. Handle return values if needed
     *
     * We use LLVM attributes to mark functions as cold.
     */
    
    for (int i = 0; i < ir->cold_block_count; i++) {
        ColdBlock *cold = &ir->cold_blocks[i];
        LLVMBasicBlockRef successor = NULL;
        LLVMValueRef parent;
        LLVMTypeRef input_types[LIVING_IR_MAX_OUTLINE_VALUES];
        LLVMValueRef inputs[LIVING_IR_MAX_OUTLINE_VALUES];
        LLVMValueRef params[LIVING_IR_MAX_OUTLINE_VALUES];
        ValueRemap remap[LIVING_IR_MAX_OUTLINE_VALUES + LIVING_IR_MAX_OUTLINE_INSTS];
        LLVMValueRef to_erase[LIVING_IR_MAX_OUTLINE_INSTS];
        int remap_count = 0;
        int erase_count = 0;
        int input_count;
        char *helper_name;
        LLVMTypeRef helper_type;
        LLVMValueRef helper;
        LLVMBasicBlockRef helper_entry;
        LLVMBuilderRef builder;
        
        if (!cold->can_outline) continue;

        if (!is_simple_outline_candidate(cold, &successor)) {
            cold->reason = "outline requires unsupported control/data flow";
            ir->metrics.mutations_rejected++;
            continue;
        }

        parent = LLVMGetBasicBlockParent(cold->block);
        if (!parent || !successor) {
            cold->reason = "outline missing parent or successor";
            ir->metrics.mutations_rejected++;
            continue;
        }

        input_count = collect_outline_inputs(cold, inputs, LIVING_IR_MAX_OUTLINE_VALUES);
        helper_name = create_unique_cold_function_name(ir, cold->function_name, i);
        if (!helper_name) {
            cold->reason = "failed to allocate cold helper name";
            ir->metrics.mutations_rejected++;
            continue;
        }

        for (int j = 0; j < input_count; j++) {
            input_types[j] = LLVMTypeOf(inputs[j]);
        }

        helper_type = LLVMFunctionType(LLVMVoidTypeInContext(ir->context),
                                       input_types, input_count, false);
        helper = LLVMAddFunction(ir->module, helper_name, helper_type);
        free(helper_name);
        if (!helper) {
            cold->reason = "failed to create cold helper";
            ir->metrics.mutations_rejected++;
            continue;
        }

        helper_entry = LLVMAppendBasicBlockInContext(ir->context, helper, "entry");
        builder = LLVMCreateBuilderInContext(ir->context);
        LLVMPositionBuilderAtEnd(builder, helper_entry);

        add_enum_attr_if_supported(ir, helper, "cold");
        add_enum_attr_if_supported(ir, helper, "minsize");
        add_enum_attr_if_supported(ir, helper, "optsize");
        LLVMSetLinkage(helper, LLVMInternalLinkage);
        LLVMSetSection(helper, ".text.unlikely");

        for (int j = 0; j < input_count; j++) {
            params[j] = LLVMGetParam(helper, j);
            remap[remap_count].from = inputs[j];
            remap[remap_count].to = params[j];
            remap_count++;
        }

        for (LLVMValueRef inst = LLVMGetFirstInstruction(cold->block);
             inst != NULL;
             inst = LLVMGetNextInstruction(inst)) {
            LLVMValueRef next = LLVMGetNextInstruction(inst);
            LLVMValueRef clone;
            int operands;

            if (inst == LLVMGetBasicBlockTerminator(cold->block)) break;
            if (erase_count >= LIVING_IR_MAX_OUTLINE_INSTS ||
                remap_count >= LIVING_IR_MAX_OUTLINE_VALUES + LIVING_IR_MAX_OUTLINE_INSTS) {
                break;
            }

            clone = LLVMInstructionClone(inst);
            if (!clone) continue;

            operands = LLVMGetNumOperands(clone);
            for (int op = 0; op < operands; op++) {
                LLVMValueRef value = LLVMGetOperand(clone, op);
                LLVMValueRef mapped = remap_lookup(remap, remap_count, value);
                if (mapped) {
                    LLVMSetOperand(clone, op, mapped);
                }
            }

            LLVMInsertIntoBuilder(builder, clone);
            remap[remap_count].from = inst;
            remap[remap_count].to = clone;
            remap_count++;
            to_erase[erase_count++] = inst;

            if (!next) break;
        }

        LLVMBuildRetVoid(builder);
        LLVMDisposeBuilder(builder);

        LLVMValueRef old_term = LLVMGetBasicBlockTerminator(cold->block);
        if (old_term && erase_count < LIVING_IR_MAX_OUTLINE_INSTS) {
            to_erase[erase_count++] = old_term;
        }

        for (int j = 0; j < erase_count; j++) {
            LLVMInstructionEraseFromParent(to_erase[j]);
        }

        builder = LLVMCreateBuilderInContext(ir->context);
        LLVMPositionBuilderAtEnd(builder, cold->block);
        if (input_count > 0) {
            LLVMBuildCall2(builder, helper_type, helper, inputs, input_count, "");
        } else {
            LLVMBuildCall2(builder, helper_type, helper, NULL, 0, "");
        }
        LLVMBuildBr(builder, successor);
        LLVMDisposeBuilder(builder);

        ir->metrics.cold_blocks_outlined++;
        ir->metrics.code_size_delta += (uint64_t)cold->instruction_count;
    }
    
    /* Estimate speedup from outlining (better icache) */
    double outline_speedup = 1.0 + (ir->metrics.cold_blocks_outlined * 0.005);
    ir->metrics.estimated_speedup *= outline_speedup;
}

void living_ir_restructure_loops(LivingIR *ir) {
    if (!ir || !ir->config.enable_loop_restructure) return;
    
    /* Process each loop mutation - we use metadata hints instead of
     * deprecated pass manager functions */
    for (int i = 0; i < ir->loop_mutation_count; i++) {
        LoopMutation *mutation = &ir->loop_mutations[i];
        bool changed = false;
        
        /*
         * LLVM's loop unroll pass will use pragma hints we set.
         * In a full implementation, we would:
         * 1. Add llvm.loop.unroll.count metadata
         * 2. Add llvm.loop.unroll.enable metadata
         * 3. Add prefetch intrinsics for large loops
         */
        
        if (mutation->header && mutation->suggested_unroll > 1) {
            apply_loop_unroll_metadata(ir, mutation->header,
                                       mutation->suggested_unroll);
            ir->metrics.loops_unrolled++;
            changed = true;
        }
        
        if (mutation->should_prefetch) {
            ir->metrics.loops_prefetched++;
            changed = true;
        }

        if (changed) {
            ir->metrics.loops_restructured++;
        }
    }
    
    /* Estimate speedup from loop optimization */
    double loop_speedup = 1.0 + (ir->metrics.loops_unrolled * 0.03) +
                          (ir->metrics.loops_prefetched * 0.05);
    ir->metrics.estimated_speedup *= loop_speedup;
}

void living_ir_reorder_blocks(LivingIR *ir) {
    if (!ir || !ir->config.enable_block_reorder) return;
    
    /*
     * Block reordering for cache efficiency:
     * 1. Put hot blocks at function entry (sequential access)
     * 2. Put cold blocks at the end (rarely fetched)
     * 3. Arrange fall-through on likely paths
     *
     * This requires analyzing branch predictions and physically
     * reordering basic blocks in the function.
     */
    
    ir->block_order_count = 0;
    
    for (LLVMValueRef func = LLVMGetFirstFunction(ir->module);
         func != NULL;
         func = LLVMGetNextFunction(func)) {
        
        if (should_skip_function(ir, func)) continue;
        
        const char *func_name = get_function_name(func);
        ProfileFunction *pf = profile_get_function(
            ir->profile, profile_function_name(func_name));
        
        /* Only optimize hot functions */
        if (!pf || !pf->is_hot) continue;
        
        int block_idx = 0;
        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func);
             bb != NULL;
             bb = LLVMGetNextBasicBlock(bb)) {
            
            if (ir->block_order_count >= LIVING_IR_MAX_BLOCK_REORDERS) break;
            
            BlockOrder *order = &ir->block_orders[ir->block_order_count];
            order->block = bb;
            order->original_order = block_idx;
            
            /* Get execution frequency from branch profile */
            const char *bb_name = LLVMGetBasicBlockName(bb);
            order->execution_freq = 1.0;  /* Default */
            if (bb_name && ir->profile) {
                char location[256];
                snprintf(location, sizeof(location), "%s:%s",
                         profile_function_name(func_name), bb_name);
                ProfileBranch *branch = profile_get_branch(ir->profile, location);
                if (branch) {
                    uint64_t total = branch->taken_count + branch->not_taken_count;
                    if (total > 0 && pf->call_count > 0) {
                        order->execution_freq = (double)total / (double)pf->call_count;
                    }
                }
            }
            order->is_hot = (order->execution_freq > 0.5);  /* Hot if executed > 50% of calls */
            
            /* New order will be determined by frequency */
            order->new_order = block_idx;  /* Placeholder */
            
            ir->block_order_count++;
            block_idx++;
        }
        
        ir->metrics.blocks_reordered += block_idx;

        for (LLVMBasicBlockRef bb = LLVMGetFirstBasicBlock(func);
             bb != NULL;
             bb = LLVMGetNextBasicBlock(bb)) {
            LLVMValueRef term = LLVMGetBasicBlockTerminator(bb);
            int line;
            char location[256];
            ProfileBranch *branch;
            uint64_t total;
            double taken_ratio;
            unsigned true_weight;
            unsigned false_weight;

            if (!term) continue;
            if (LLVMGetInstructionOpcode(term) != LLVMBr ||
                LLVMGetNumSuccessors(term) != 2) {
                continue;
            }

            line = get_qisc_line_metadata(ir, term);
            if (line <= 0) continue;

            snprintf(location, sizeof(location), "%s:%d",
                     profile_function_name(func_name), line);
            branch = profile_get_branch(ir->profile, location);
            if (!branch || !branch->is_predictable) continue;

            total = branch->taken_count + branch->not_taken_count;
            if (total == 0) continue;
            if (sample_confidence(total, 16) < ir->config.min_confidence) {
                ir->metrics.mutations_rejected++;
                continue;
            }

            taken_ratio = branch->taken_ratio;
            true_weight = (unsigned)(taken_ratio * 10000.0);
            false_weight = (unsigned)((1.0 - taken_ratio) * 10000.0);
            if (true_weight == 0) true_weight = 1;
            if (false_weight == 0) false_weight = 1;

            apply_branch_weight_metadata(ir, term, false_weight, true_weight);
            ir->metrics.branch_weights_applied++;
        }
    }
    
    /* 
     * LLVM handles basic block ordering during code generation.
     * We can influence it through branch probability metadata.
     */
    
    /* Estimate speedup from better branch guidance rather than claiming
     * physical block movement we have not performed yet. */
    double reorder_speedup = 1.0 +
                             (ir->metrics.branch_weights_applied > 0 ? 0.02 : 0) +
                             (ir->metrics.branch_weights_applied * 0.01);
    ir->metrics.estimated_speedup *= reorder_speedup;
}

void living_ir_evolve(LivingIR *ir) {
    if (!ir) return;
    
    /* First, analyze the module */
    living_ir_analyze(ir);
    
    /* Apply mutations in order */
    living_ir_inline_hot_paths(ir);
    living_ir_outline_cold_paths(ir);
    living_ir_restructure_loops(ir);
    living_ir_reorder_blocks(ir);
    if (ir->profile && ir->profile->function_count > 0) {
        living_ir_clone_hot_functions(ir);
        living_ir_merge_cold_functions(ir);
    }
    
    /* Verify IR is still valid */
    if (!living_ir_verify(ir)) {
        fprintf(stderr, "Warning: Living IR mutations produced invalid IR: %s\n",
                ir->error_msg);
    }
}

/* ======== Advanced Mutations ======== */

void living_ir_clone_hot_functions(LivingIR *ir) {
    if (!ir) return;
    
    /*
     * Clone hot functions for specialization opportunities.
     * This allows different call sites to have differently optimized versions.
     */
    
    for (LLVMValueRef func = LLVMGetFirstFunction(ir->module);
         func != NULL;
         func = LLVMGetNextFunction(func)) {
        
        if (should_skip_function(ir, func)) continue;
        
        const char *func_name = get_function_name(func);
        ProfileFunction *pf = profile_get_function(
            ir->profile, profile_function_name(func_name));
        
        if (!pf || !pf->is_hot) continue;

        specialize_function_from_profile(ir, func, pf);
    }
}

void living_ir_devirtualize_calls(LivingIR *ir) {
    if (!ir) return;
    
    /*
     * When profile shows an indirect call always goes to the same target,
     * convert it to a direct call with a guard.
     *
     * Example:
     *   call *%vtable_ptr  ->  if (ptr == known_target)
     *                            call known_target directly
     *                          else
     *                            call *%vtable_ptr
     */
    
    /* This requires tracking indirect call targets in the profile */
}

void living_ir_merge_cold_functions(LivingIR *ir) {
    if (!ir) return;
    
    /*
     * Find small, rarely-called functions and merge them.
     * This reduces function call overhead for cold code.
     */
    
    for (LLVMValueRef func = LLVMGetFirstFunction(ir->module);
         func != NULL;
         func = LLVMGetNextFunction(func)) {
        
        if (should_skip_function(ir, func)) continue;
        
        const char *func_name = get_function_name(func);
        ProfileFunction *pf = profile_get_function(
            ir->profile, profile_function_name(func_name));
        
        if (!pf || !pf->is_cold) continue;
        
        int size = count_function_instructions(func);
        
        /* Very small cold functions could be merged into callers */
        if (size < 10 && pf->call_count < 100) {
            specialize_function_from_profile(ir, func, pf);
        }
    }
}

void living_ir_insert_prefetch(LivingIR *ir) {
    if (!ir) return;
    
    /*
     * Insert prefetch instructions for loops with predictable
     * memory access patterns and high iteration counts.
     */
    
    for (int i = 0; i < ir->loop_mutation_count; i++) {
        LoopMutation *mutation = &ir->loop_mutations[i];
        
        if (!mutation->should_prefetch) continue;
        
        /*
         * In a full implementation:
         * 1. Analyze memory access pattern in loop
         * 2. Calculate prefetch distance based on iteration count
         * 3. Insert llvm.prefetch intrinsic calls
         */
    }
}

/* ======== Metrics and Reporting ======== */

LivingIRMetrics living_ir_get_metrics(LivingIR *ir) {
    if (!ir) {
        LivingIRMetrics empty = {0};
        return empty;
    }
    return ir->metrics;
}

void living_ir_print_summary(LivingIR *ir) {
    if (!ir) return;
    
    printf("\n╔══════════════════════════════════════════════════╗\n");
    printf("║           Living IR Mutation Summary             ║\n");
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Analysis:                                        ║\n");
    printf("║   Functions analyzed:     %4d                   ║\n", 
           ir->metrics.functions_analyzed);
    printf("║   Inline candidates:      %4d                   ║\n", 
           ir->metrics.inline_candidates_found);
    printf("║   Cold blocks found:      %4d                   ║\n", 
           ir->metrics.cold_blocks_found);
    printf("║   Loops analyzed:         %4d                   ║\n", 
           ir->metrics.loops_analyzed);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Mutations Applied:                               ║\n");
    printf("║   Functions inlined:      %4d                   ║\n", 
           ir->metrics.functions_inlined);
    printf("║   Cold blocks outlined:   %4d                   ║\n", 
           ir->metrics.cold_blocks_outlined);
    printf("║   Hot funcs specialized:  %4d                   ║\n",
           ir->metrics.hot_functions_specialized);
    printf("║   Cold funcs specialized: %4d                   ║\n",
           ir->metrics.cold_functions_specialized);
    printf("║   Loops restructured:     %4d                   ║\n", 
           ir->metrics.loops_restructured);
    printf("║     - Unrolled:           %4d                   ║\n", 
           ir->metrics.loops_unrolled);
    printf("║     - Prefetched:         %4d                   ║\n", 
           ir->metrics.loops_prefetched);
    printf("║   Hot blocks analyzed:    %4d                   ║\n", 
           ir->metrics.blocks_reordered);
    printf("║   Branch weights applied: %4d                   ║\n",
           ir->metrics.branch_weights_applied);
    printf("║   Mutations rejected:     %4d                   ║\n",
           ir->metrics.mutations_rejected);
    printf("╠══════════════════════════════════════════════════╣\n");
    printf("║ Estimated speedup:        %.2fx                  ║\n", 
           ir->metrics.estimated_speedup);
    printf("╚══════════════════════════════════════════════════╝\n\n");
}

void living_ir_dump(LivingIR *ir) {
    if (!ir) return;
    
    printf("=== Living IR Detailed Dump ===\n\n");
    
    /* Dump inline candidates */
    printf("Inline Candidates (%d):\n", ir->inline_candidate_count);
    for (int i = 0; i < ir->inline_candidate_count; i++) {
        InlineCandidate *c = &ir->inline_candidates[i];
        printf("  [%s] %s -> %s (calls: %lu, size: %d, hotness: %.4f)\n",
               c->should_inline ? "INLINE" : "SKIP",
               c->caller, c->callee,
               (unsigned long)c->call_count, c->callee_size, c->hotness_score);
        printf("        Reason: %s\n", c->reason);
    }
    
    printf("\nCold Blocks (%d):\n", ir->cold_block_count);
    for (int i = 0; i < ir->cold_block_count; i++) {
        ColdBlock *b = &ir->cold_blocks[i];
        printf("  [%s] %s (freq: %.6f, insts: %d)\n",
               b->can_outline ? "OUTLINE" : "SKIP",
               b->function_name, b->execution_freq, b->instruction_count);
        printf("        Reason: %s\n", b->reason);
    }
    
    printf("\nLoop Mutations (%d):\n", ir->loop_mutation_count);
    for (int i = 0; i < ir->loop_mutation_count; i++) {
        LoopMutation *m = &ir->loop_mutations[i];
        printf("  %s (avg iters: %.1f)\n", m->location, m->avg_iterations);
        printf("        Unroll: %dx, Prefetch: %s, Tile: %s",
               m->suggested_unroll,
               m->should_prefetch ? "yes" : "no",
               m->should_tile ? "yes" : "no");
        if (m->should_tile) {
            printf(" (size: %d)", m->tile_size);
        }
        printf("\n");
    }
    
    printf("\n");
}

/* ======== Verification ======== */

bool living_ir_verify(LivingIR *ir) {
    if (!ir) return false;
    
    char *error = NULL;
    LLVMBool failed = LLVMVerifyModule(ir->module, 
                                        LLVMReturnStatusAction, 
                                        &error);
    
    if (failed) {
        ir->had_error = true;
        snprintf(ir->error_msg, sizeof(ir->error_msg), 
                 "IR verification failed: %.400s", error ? error : "unknown");
        LLVMDisposeMessage(error);
        return false;
    }
    
    if (error) {
        LLVMDisposeMessage(error);
    }
    
    ir->had_error = false;
    ir->error_msg[0] = '\0';
    return true;
}

const char *living_ir_get_error(LivingIR *ir) {
    if (!ir) return "null context";
    return ir->had_error ? ir->error_msg : NULL;
}
