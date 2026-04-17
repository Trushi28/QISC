/*
 * QISC LLVM Code Generator — Implementation
 *
 * Phase 1: int/float/bool literals, binary/unary ops, variables,
 *          functions, if/else, while, for, give, print/str builtins.
 */

#include "codegen.h"
#include "../personality/easter_eggs.h"
#include "../pragma/pragma.h"
#include "qisc.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ======== Debug Personality System ======== */

/* Debug-only messages for curious debugger users */
static const char *debug_only_messages[] = {
    "If you're reading this in a debugger, hi!",
    "Breakpoint here means you're curious. I respect that.",
    "This variable changed. Suspicious.",
    "Stack trace leads back to here. Good luck!",
    "You found the secret debug message. Achievement unlocked!",
    "Debugging at 3 AM? We've all been there.",
    "The bug is always in the last place you look.",
    "Have you tried turning it off and on again?",
};

/* Debug section comments for various contexts */
static const char *debug_section_comments[] = {
    "; Here be dragons (and also your variables)",
    "; Stack frame - handle with care",
    "; Local variables - they grow up so fast",
    "; Return address - one way ticket home",
    "; Function prologue - setting up shop",
    "; Function epilogue - packing up to leave",
    "; Loop body - round and round we go",
    "; Branch taken - the road less traveled",
};

/* Generate funny symbol suffix for cryptic mode */
static const char *funny_symbol_suffix(const char *func_name) {
    static char buffer[256];
    
    if (!func_name) return "";
    
    if (strcmp(func_name, "main") == 0) {
        snprintf(buffer, sizeof(buffer), 
                 "main ; The adventure begins here");
        return buffer;
    }
    if (strstr(func_name, "sort")) {
        snprintf(buffer, sizeof(buffer), 
                 "%s ; In theory, O(n log n). In practice, O(coffee)", func_name);
        return buffer;
    }
    if (strstr(func_name, "loop") || strstr(func_name, "Loop")) {
        snprintf(buffer, sizeof(buffer), 
                 "%s ; Loop-de-loop", func_name);
        return buffer;
    }
    if (strstr(func_name, "init") || strstr(func_name, "Init")) {
        snprintf(buffer, sizeof(buffer),
                 "%s ; Once upon a time...", func_name);
        return buffer;
    }
    if (strstr(func_name, "parse") || strstr(func_name, "Parse")) {
        snprintf(buffer, sizeof(buffer),
                 "%s ; Making sense of chaos", func_name);
        return buffer;
    }
    if (strstr(func_name, "error") || strstr(func_name, "Error")) {
        snprintf(buffer, sizeof(buffer),
                 "%s ; This is fine (narrator: it was not fine)", func_name);
        return buffer;
    }
    if (strstr(func_name, "free") || strstr(func_name, "Free") ||
        strstr(func_name, "cleanup") || strstr(func_name, "Cleanup")) {
        snprintf(buffer, sizeof(buffer),
                 "%s ; Farewell, memory. You served us well.", func_name);
        return buffer;
    }
    if (strstr(func_name, "test") || strstr(func_name, "Test")) {
        snprintf(buffer, sizeof(buffer),
                 "%s ; Trust, but verify", func_name);
        return buffer;
    }
    if (strstr(func_name, "calc") || strstr(func_name, "Calc") ||
        strstr(func_name, "compute") || strstr(func_name, "Compute")) {
        snprintf(buffer, sizeof(buffer),
                 "%s ; Math is math, until it's a bug", func_name);
        return buffer;
    }
    if (strstr(func_name, "print") || strstr(func_name, "Print") ||
        strstr(func_name, "log") || strstr(func_name, "Log")) {
        snprintf(buffer, sizeof(buffer),
                 "%s ; Shouting into the void", func_name);
        return buffer;
    }
    if (strstr(func_name, "read") || strstr(func_name, "Read") ||
        strstr(func_name, "load") || strstr(func_name, "Load")) {
        snprintf(buffer, sizeof(buffer),
                 "%s ; Absorbing knowledge", func_name);
        return buffer;
    }
    if (strstr(func_name, "write") || strstr(func_name, "Write") ||
        strstr(func_name, "save") || strstr(func_name, "Save")) {
        snprintf(buffer, sizeof(buffer),
                 "%s ; Committing to permanent record", func_name);
        return buffer;
    }
    
    return func_name;
}

/* Get debug section comment based on section type */
static const char *get_section_comment(int section_type) {
    if (section_type < 0 || section_type >= 8) {
        return "; Unknown territory - proceed with caution";
    }
    return debug_section_comments[section_type];
}

/* Get random debug message for curious debuggers */
static const char *get_debug_easter_egg(void) {
    static bool seeded = false;
    if (!seeded) {
        srand((unsigned int)time(NULL));
        seeded = true;
    }
    int idx = rand() % (sizeof(debug_only_messages) / sizeof(debug_only_messages[0]));
    return debug_only_messages[idx];
}

/* Add debug personality comment as module-level metadata */
static void add_debug_personality_comment(Codegen *cg, const char *section, int section_type) {
    if (!cg || !cg->debug_personality_enabled) return;
    
    char comment[512];
    const char *section_comment = get_section_comment(section_type);
    snprintf(comment, sizeof(comment), "%s\n%s", section, section_comment);
    
    LLVMValueRef md_string = LLVMMDStringInContext(cg->ctx, comment, strlen(comment));
    LLVMValueRef md_node = LLVMMDNodeInContext(cg->ctx, &md_string, 1);
    LLVMAddNamedMetadataOperand(cg->mod, "qisc.debug.comments", md_node);
}

/* Add compilation metadata with personality */
static void add_compilation_metadata(Codegen *cg) {
    if (!cg) return;
    
    char metadata[1024];
    time_t now = time(NULL);
    char *time_str = ctime(&now);
    if (time_str) {
        time_str[strlen(time_str) - 1] = '\0';  /* Remove trailing newline */
    }
    
    bool cryptic = (cg->personality == QISC_PERSONALITY_CRYPTIC);
    int easter_eggs = cryptic ? (rand() % 10 + 1) : 0;
    
    const char *mood;
    switch (cg->personality) {
        case QISC_PERSONALITY_CRYPTIC:  mood = "Mysterious"; break;
        case QISC_PERSONALITY_SNARKY:   mood = "Witty"; break;
        case QISC_PERSONALITY_SAGE:     mood = "Contemplative"; break;
        case QISC_PERSONALITY_FRIENDLY: mood = "Cheerful"; break;
        case QISC_PERSONALITY_MINIMAL:  mood = "Focused"; break;
        default:                        mood = "Professional"; break;
    }
    
    snprintf(metadata, sizeof(metadata),
        "============================================\n"
        "Compiled by QISC v%s\n"
        "Date: %s\n"
        "Mood: %s\n"
        "Optimizations applied: %d\n"
        "Warnings ignored: 0 (we take those seriously)\n"
        "Easter eggs hidden: %d\n"
        "============================================",
        QISC_VERSION_STRING,
        time_str ? time_str : "unknown",
        mood,
        cg->optimization_count,
        easter_eggs
    );
    
    LLVMValueRef md_string = LLVMMDStringInContext(cg->ctx, metadata, strlen(metadata));
    LLVMValueRef md_node = LLVMMDNodeInContext(cg->ctx, &md_string, 1);
    LLVMAddNamedMetadataOperand(cg->mod, "qisc.metadata", md_node);
}

/* Add line number personality notes as metadata */
static void add_line_personality(Codegen *cg, int line) {
    if (!cg || !cg->debug_personality_enabled) return;
    if (cg->personality != QISC_PERSONALITY_CRYPTIC &&
        cg->personality != QISC_PERSONALITY_SNARKY) return;
    
    const char *note = NULL;
    char note_buf[256];
    
    if (line == 42) {
        note = "Line 42 - The Answer to Life, the Universe, and Everything";
    } else if (line == 404) {
        note = "Line 404 - Hope this code is found";
    } else if (line == 500) {
        note = "Line 500 - Internal server error vibes";
    } else if (line == 418) {
        note = "Line 418 - I'm a teapot (RFC 2324)";
    } else if (line == 666) {
        note = "Line 666 - The number of the beast... function";
    } else if (line == 1337) {
        note = "Line 1337 - L33t code detected";
    } else if (line == 80) {
        note = "Line 80 - Classic column limit. Respect.";
    } else if (line == 100) {
        note = "Line 100 - A century of code";
    } else if (line == 200) {
        note = "Line 200 - OK (HTTP would be proud)";
    } else if (line == 256) {
        note = "Line 256 - A byte's worth of lines";
    } else if (line == 512) {
        note = "Line 512 - Half a kilobyte of lines";
    } else if (line == 1024) {
        note = "Line 1024 - A kilobyte of code lines";
    } else if (line % 100 == 0 && line > 0) {
        snprintf(note_buf, sizeof(note_buf), "Line %d - Milestone reached!", line);
        note = note_buf;
    }
    
    if (note) {
        LLVMValueRef md_string = LLVMMDStringInContext(cg->ctx, note, strlen(note));
        LLVMValueRef md_node = LLVMMDNodeInContext(cg->ctx, &md_string, 1);
        LLVMAddNamedMetadataOperand(cg->mod, "qisc.debug.line_notes", md_node);
    }
}

/* Add function entry/exit debug comments */
static void add_function_debug_comments(Codegen *cg, LLVMValueRef func, const char *name) {
    if (!cg || !cg->debug_personality_enabled) return;
    if (cg->personality != QISC_PERSONALITY_CRYPTIC &&
        cg->personality != QISC_PERSONALITY_SNARKY) return;
    
    char entry_comment[512];
    char exit_comment[512];
    
    /* Get funny symbol name for cryptic mode */
    const char *funny_name = (cg->personality == QISC_PERSONALITY_CRYPTIC) 
                             ? funny_symbol_suffix(name) : name;
    
    /* Entry comment */
    if (cg->personality == QISC_PERSONALITY_CRYPTIC) {
        snprintf(entry_comment, sizeof(entry_comment),
                 "Function '%s' checking in. Ready to compute. | %s",
                 name, get_debug_easter_egg());
    } else {
        snprintf(entry_comment, sizeof(entry_comment),
                 "Entering function '%s'. Let's see what happens.", name);
    }
    
    /* Exit comment */
    snprintf(exit_comment, sizeof(exit_comment),
             "%s signing off. Mission accomplished (probably).", name);
    
    /* Add as function-level metadata */
    LLVMValueRef entry_md = LLVMMDStringInContext(cg->ctx, entry_comment, strlen(entry_comment));
    LLVMValueRef exit_md = LLVMMDStringInContext(cg->ctx, exit_comment, strlen(exit_comment));
    LLVMValueRef funny_md = LLVMMDStringInContext(cg->ctx, funny_name, strlen(funny_name));
    
    LLVMValueRef entry_node = LLVMMDNodeInContext(cg->ctx, &entry_md, 1);
    LLVMValueRef exit_node = LLVMMDNodeInContext(cg->ctx, &exit_md, 1);
    LLVMValueRef funny_node = LLVMMDNodeInContext(cg->ctx, &funny_md, 1);
    
    /* Attach metadata to function */
    unsigned entry_kind = LLVMGetMDKindIDInContext(cg->ctx, "qisc.fn.entry", 14);
    unsigned exit_kind = LLVMGetMDKindIDInContext(cg->ctx, "qisc.fn.exit", 12);
    unsigned symbol_kind = LLVMGetMDKindIDInContext(cg->ctx, "qisc.fn.symbol", 14);
    
    LLVMSetMetadata(func, entry_kind, entry_node);
    LLVMSetMetadata(func, exit_kind, exit_node);
    LLVMSetMetadata(func, symbol_kind, funny_node);
}

/* Add debug-only global string (visible in debugger) */
static void add_debug_easter_egg_string(Codegen *cg) {
    if (!cg || !cg->debug_personality_enabled) return;
    if (cg->personality != QISC_PERSONALITY_CRYPTIC) return;
    
    const char *easter_egg = get_debug_easter_egg();
    
    /* Create a global string constant that's only visible in debug info */
    LLVMValueRef str = LLVMBuildGlobalStringPtr(cg->builder, 
                                                 easter_egg, 
                                                 "__qisc_debug_easter_egg");
    LLVMSetGlobalConstant(str, true);
    LLVMSetLinkage(str, LLVMPrivateLinkage);
    
    /* Add debug greeting message */
    LLVMValueRef greeting = LLVMBuildGlobalStringPtr(cg->builder,
        "Welcome, debugger! You've found the QISC debug zone.",
        "__qisc_debug_greeting");
    LLVMSetGlobalConstant(greeting, true);
    LLVMSetLinkage(greeting, LLVMPrivateLinkage);
}

/* ======== Error Handling ======== */

static void cg_error(Codegen *cg, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  vsnprintf(cg->error_msg, sizeof(cg->error_msg), fmt, args);
  va_end(args);
  cg->had_error = true;
  fprintf(stderr, "[codegen] Error: %s\n", cg->error_msg);
}

/* ======== Scope Management ======== */

static void cg_push_scope(Codegen *cg) {
  if (cg->scope_depth >= CG_MAX_SCOPES - 1) {
    cg_error(cg, "Too many nested scopes");
    return;
  }
  cg->scope_depth++;
  cg->scopes[cg->scope_depth].count = 0;
}

static void cg_pop_scope(Codegen *cg) {
  if (cg->scope_depth <= 0)
    return;
  /* Free symbol names */
  CgScope *s = &cg->scopes[cg->scope_depth];
  for (int i = 0; i < s->count; i++) {
    free(s->symbols[i].name);
  }
  s->count = 0;
  cg->scope_depth--;
}

static void cg_define(Codegen *cg, const char *name, LLVMValueRef alloca,
                      LLVMTypeRef type) {
  CgScope *s = &cg->scopes[cg->scope_depth];
  if (s->count >= CG_MAX_SYMBOLS)
    return;
  int idx = s->count++;
  s->symbols[idx].name = strdup(name);
  s->symbols[idx].alloca = alloca;
  s->symbols[idx].type = type;
  s->symbols[idx].is_callable = false;
  s->symbols[idx].callable_param_count = 0;
  s->symbols[idx].callable_return_type = NULL;
  for (int i = 0; i < CG_MAX_CALLABLE_PARAMS; i++) {
    s->symbols[idx].callable_param_types[i] = NULL;
  }
}

static CgSymbol *cg_lookup(Codegen *cg, const char *name) {
  for (int d = cg->scope_depth; d >= 0; d--) {
    CgScope *s = &cg->scopes[d];
    for (int i = s->count - 1; i >= 0; i--) {
      if (strcmp(s->symbols[i].name, name) == 0) {
        return &s->symbols[i];
      }
    }
  }
  return NULL;
}

static void cg_set_callable_metadata(CgSymbol *sym, LLVMTypeRef return_type,
                                     LLVMTypeRef *param_types,
                                     int param_count) {
  if (!sym)
    return;
  sym->is_callable = true;
  sym->callable_param_count =
      param_count > CG_MAX_CALLABLE_PARAMS ? CG_MAX_CALLABLE_PARAMS : param_count;
  sym->callable_return_type = return_type;
  for (int i = 0; i < CG_MAX_CALLABLE_PARAMS; i++) {
    sym->callable_param_types[i] =
        i < sym->callable_param_count ? param_types[i] : NULL;
  }
}

static AstNode *cg_find_proc_decl(Codegen *cg, const char *name) {
  if (!cg || !name || !cg->program_ast ||
      cg->program_ast->type != AST_PROGRAM)
    return NULL;

  for (int i = 0; i < cg->program_ast->as.program.declarations.count; i++) {
    AstNode *decl = cg->program_ast->as.program.declarations.items[i];
    if (decl && decl->type == AST_PROC && decl->as.proc.name &&
        strcmp(decl->as.proc.name, name) == 0) {
      return decl;
    }
  }
  return NULL;
}

/* ======== Type Helpers ======== */

typedef enum {
  CG_STREAM_NONE = 0,
  CG_STREAM_INT = 1,
  CG_STREAM_STRING = 2,
} CgStreamKind;

typedef enum {
  CG_VALUE_UNKNOWN = 0,
  CG_VALUE_INT = 1,
  CG_VALUE_STRING = 2,
  CG_VALUE_BOOL = 3,
} CgValueKind;

typedef struct {
  const char *name;
  CgSymbol *symbol;
} CgLambdaCapture;

typedef struct {
  const char *name;
  CgValueKind kind;
} CgLocalKind;

typedef struct {
  LLVMValueRef fn;
  LLVMValueRef ctx;
  bool uses_ctx;
} CgStreamCallable;

static LLVMValueRef emit_expr(Codegen *cg, AstNode *node);
static void emit_block(Codegen *cg, AstNode *node);
static LLVMTypeRef cg_type_from_name(Codegen *cg, const char *name);
static LLVMTypeRef cg_return_type(Codegen *cg, AstNode *proc);
static CgValueKind cg_lambda_body_kind(Codegen *cg, AstNode *node,
                                       const char *param_name,
                                       CgValueKind param_kind);
static LLVMValueRef cg_coerce_value(Codegen *cg, LLVMValueRef value,
                                    LLVMTypeRef target_type);
static CgStreamKind cg_stream_kind_from_callable(Codegen *cg, AstNode *node,
                                                 CgStreamKind input_kind);
static LLVMValueRef cg_emit_general_closure_value(Codegen *cg, AstNode *lambda,
                                                  LLVMTypeRef *param_types,
                                                  int param_count,
                                                  LLVMTypeRef return_type);
static LLVMValueRef cg_emit_closure_call(Codegen *cg, LLVMValueRef closure,
                                         LLVMTypeRef *arg_types,
                                         LLVMValueRef *args, int argc,
                                         LLVMTypeRef return_type,
                                         const char *name);

static bool cg_expr_is_array_like(Codegen *cg, AstNode *node) {
  if (!cg || !node)
    return false;

  switch (node->type) {
  case AST_ARRAY_LITERAL:
  case AST_PIPELINE:
    return true;
  case AST_IDENTIFIER: {
    char len_name[300];
    snprintf(len_name, sizeof(len_name), "__%s__len", node->as.identifier.name);
    return cg_lookup(cg, len_name) != NULL;
  }
  case AST_CALL:
    if (node->as.call.callee && node->as.call.callee->type == AST_IDENTIFIER) {
      const char *name = node->as.call.callee->as.identifier.name;
      return strcmp(name, "range") == 0 || strcmp(name, "map") == 0 ||
             strcmp(name, "filter") == 0 || strcmp(name, "collect") == 0 ||
             strcmp(name, "stream_collect") == 0 ||
             strcmp(name, "stdin_lines") == 0 || strcmp(name, "file_lines") == 0 ||
             strcmp(name, "take") == 0 || strcmp(name, "skip") == 0;
    }
    return false;
  default:
    return false;
  }
}

static CgStreamKind cg_expr_stream_kind(Codegen *cg, AstNode *node) {
  if (!cg || !node)
    return CG_STREAM_NONE;

  switch (node->type) {
  case AST_IDENTIFIER: {
    char int_name[300];
    char string_name[300];
    snprintf(int_name, sizeof(int_name), "__%s__stream_int",
             node->as.identifier.name);
    snprintf(string_name, sizeof(string_name), "__%s__stream_string",
             node->as.identifier.name);
    if (cg_lookup(cg, int_name))
      return CG_STREAM_INT;
    if (cg_lookup(cg, string_name))
      return CG_STREAM_STRING;
    return CG_STREAM_NONE;
  }
  case AST_CALL:
    if (node->as.call.callee && node->as.call.callee->type == AST_IDENTIFIER) {
      const char *name = node->as.call.callee->as.identifier.name;
      if (strcmp(name, "stream_range") == 0)
        return CG_STREAM_INT;
      if (strcmp(name, "stream_file_lines") == 0)
        return CG_STREAM_STRING;
      if ((strcmp(name, "stream_take") == 0 || strcmp(name, "stream_skip") == 0 ||
           strcmp(name, "stream_filter") == 0) &&
          node->as.call.args.count >= 1)
        return cg_expr_stream_kind(cg, node->as.call.args.items[0]);
      if (strcmp(name, "stream_map") == 0 &&
          node->as.call.args.count >= 2) {
        CgStreamKind result_kind =
            cg_stream_kind_from_callable(cg, node->as.call.args.items[1],
                                         cg_expr_stream_kind(cg, node->as.call.args.items[0]));
        if (result_kind != CG_STREAM_NONE)
          return result_kind;
        return cg_expr_stream_kind(cg, node->as.call.args.items[0]);
      }
    }
    return CG_STREAM_NONE;
  case AST_BINARY_OP:
    if (node->as.binary.op == OP_PIPELINE) {
      CgStreamKind left_kind = cg_expr_stream_kind(cg, node->as.binary.left);
      AstNode *rhs = node->as.binary.right;
      if (left_kind != CG_STREAM_NONE && rhs && rhs->type == AST_CALL &&
          rhs->as.call.callee &&
          rhs->as.call.callee->type == AST_IDENTIFIER) {
        const char *name = rhs->as.call.callee->as.identifier.name;
        if (strcmp(name, "stream_take") == 0 || strcmp(name, "stream_skip") == 0 ||
            strcmp(name, "stream_filter") == 0)
          return left_kind;
        if (strcmp(name, "stream_map") == 0 && rhs->as.call.args.count >= 1) {
          CgStreamKind result_kind =
              cg_stream_kind_from_callable(cg, rhs->as.call.args.items[0],
                                           left_kind);
          return result_kind != CG_STREAM_NONE ? result_kind : left_kind;
        }
      }
    }
    return CG_STREAM_NONE;
  default:
    return CG_STREAM_NONE;
  }
}

static bool cg_expr_is_stream_like(Codegen *cg, AstNode *node) {
  return cg_expr_stream_kind(cg, node) != CG_STREAM_NONE;
}

static bool cg_ast_is_probably_callable(Codegen *cg, AstNode *node) {
  if (!cg || !node)
    return false;
  if (node->type == AST_LAMBDA)
    return true;
  if (node->type == AST_IDENTIFIER) {
    if (cg->program_ast && cg->program_ast->type == AST_PROGRAM) {
      for (int i = 0; i < cg->program_ast->as.program.declarations.count; i++) {
        AstNode *decl = cg->program_ast->as.program.declarations.items[i];
        if (decl->type == AST_PROC && decl->as.proc.name &&
            strcmp(decl->as.proc.name, node->as.identifier.name) == 0) {
          return true;
        }
      }
    }
  }
  return false;
}

static CgStreamKind cg_stream_kind_from_type_name(const char *name) {
  if (!name)
    return CG_STREAM_NONE;
  if (strncmp(name, "stream(", 7) == 0) {
    size_t len = strlen(name);
    if (len > 8 && strcmp(name + 7, "int)") == 0)
      return CG_STREAM_INT;
    if (len > 11 && strcmp(name + 7, "string)") == 0)
      return CG_STREAM_STRING;
  }
  if (strcmp(name, "int") == 0 || strcmp(name, "i64") == 0)
    return CG_STREAM_INT;
  if (strcmp(name, "string") == 0)
    return CG_STREAM_STRING;
  return CG_STREAM_NONE;
}

static CgValueKind cg_value_kind_from_type_name(const char *name) {
  if (!name)
    return CG_VALUE_UNKNOWN;
  if (strcmp(name, "int") == 0 || strcmp(name, "i64") == 0)
    return CG_VALUE_INT;
  if (strcmp(name, "string") == 0)
    return CG_VALUE_STRING;
  if (strcmp(name, "bool") == 0)
    return CG_VALUE_BOOL;
  return CG_VALUE_UNKNOWN;
}

static CgValueKind cg_value_kind_from_stream_kind(CgStreamKind kind) {
  if (kind == CG_STREAM_INT)
    return CG_VALUE_INT;
  if (kind == CG_STREAM_STRING)
    return CG_VALUE_STRING;
  return CG_VALUE_UNKNOWN;
}

static CgValueKind cg_value_kind_from_llvm_type(Codegen *cg,
                                                LLVMTypeRef type) {
  if (!cg || !type)
    return CG_VALUE_UNKNOWN;
  if (type == cg->i64_type)
    return CG_VALUE_INT;
  if (type == cg->i8ptr_type)
    return CG_VALUE_STRING;
  if (type == cg->i1_type)
    return CG_VALUE_BOOL;
  return CG_VALUE_UNKNOWN;
}

static CgValueKind cg_merge_value_kind(CgValueKind left, CgValueKind right) {
  if (left == CG_VALUE_UNKNOWN)
    return right;
  if (right == CG_VALUE_UNKNOWN)
    return left;
  if (left == right)
    return left;
  return CG_VALUE_UNKNOWN;
}

static bool cg_local_kind_present(CgLocalKind *locals, int count,
                                  const char *name) {
  for (int i = 0; i < count; i++) {
    if (locals[i].name && name && strcmp(locals[i].name, name) == 0)
      return true;
  }
  return false;
}

static CgValueKind cg_lookup_local_kind(CgLocalKind *locals, int count,
                                        const char *name) {
  for (int i = count - 1; i >= 0; i--) {
    if (locals[i].name && name && strcmp(locals[i].name, name) == 0)
      return locals[i].kind;
  }
  return CG_VALUE_UNKNOWN;
}

static void cg_locals_add(CgLocalKind *locals, int *count, const char *name,
                          CgValueKind kind) {
  if (!locals || !count || !name || *count >= 64)
    return;
  locals[*count].name = name;
  locals[*count].kind = kind;
  (*count)++;
}

static CgValueKind cg_lambda_stmt_kind(Codegen *cg, AstNode *node,
                                       const char *param_name,
                                       CgValueKind param_kind,
                                       CgLocalKind *locals, int local_count);
static const char *cg_lambda_param_name(AstNode *param);

static LLVMTypeRef cg_llvm_type_from_value_kind(Codegen *cg, CgValueKind kind) {
  switch (kind) {
  case CG_VALUE_INT:
    return cg->i64_type;
  case CG_VALUE_STRING:
    return cg->i8ptr_type;
  case CG_VALUE_BOOL:
    return cg->i1_type;
  default:
    return NULL;
  }
}

static void cg_fill_proc_callable_types(Codegen *cg, AstNode *proc,
                                        LLVMTypeRef *return_type,
                                        LLVMTypeRef *param_types,
                                        int *param_count) {
  int count = 0;

  if (!cg || !proc || proc->type != AST_PROC)
    return;

  if (return_type)
    *return_type = cg_return_type(cg, proc);

  if (param_types && param_count) {
    count = proc->as.proc.params.count;
    if (count > CG_MAX_CALLABLE_PARAMS)
      count = CG_MAX_CALLABLE_PARAMS;
    for (int i = 0; i < count; i++) {
      AstNode *param = proc->as.proc.params.items[i];
      if (param && param->type == AST_VAR_DECL && param->as.var_decl.type_info) {
        param_types[i] =
            cg_type_from_name(cg, param->as.var_decl.type_info->name);
      } else {
        param_types[i] = cg->i64_type;
      }
    }
    *param_count = count;
  }
}

static LLVMTypeRef cg_lambda_general_return_type(Codegen *cg, AstNode *lambda) {
  const char *param_name = NULL;
  CgValueKind kind;
  LLVMTypeRef inferred;

  if (!cg || !lambda || lambda->type != AST_LAMBDA || !lambda->as.lambda.body)
    return cg ? cg->i64_type : NULL;

  if (lambda->as.lambda.params.count >= 1) {
    AstNode *param = lambda->as.lambda.params.items[0];
    param_name = cg_lambda_param_name(param);
  }

  kind = cg_lambda_body_kind(cg, lambda->as.lambda.body, param_name,
                             CG_VALUE_INT);
  inferred = cg_llvm_type_from_value_kind(cg, kind);
  return inferred ? inferred : cg->i64_type;
}

static void cg_attach_callable_metadata_from_initializer(Codegen *cg,
                                                         const char *name,
                                                         AstNode *initializer) {
  CgSymbol *sym;

  if (!cg || !name || !initializer)
    return;

  sym = cg_lookup(cg, name);
  if (!sym)
    return;

  if (initializer->type == AST_LAMBDA) {
    LLVMTypeRef param_types[CG_MAX_CALLABLE_PARAMS];
    int param_count = initializer->as.lambda.params.count;
    if (param_count > CG_MAX_CALLABLE_PARAMS)
      param_count = CG_MAX_CALLABLE_PARAMS;
    for (int i = 0; i < param_count; i++)
      param_types[i] = cg->i64_type;
    cg_set_callable_metadata(sym, cg_lambda_general_return_type(cg, initializer),
                             param_types, param_count);
    return;
  }

  if (initializer->type == AST_IDENTIFIER) {
    CgSymbol *source_sym = cg_lookup(cg, initializer->as.identifier.name);
    if (source_sym && source_sym->is_callable) {
      cg_set_callable_metadata(sym, source_sym->callable_return_type,
                               source_sym->callable_param_types,
                               source_sym->callable_param_count);
      return;
    }

    AstNode *proc = cg_find_proc_decl(cg, initializer->as.identifier.name);
    if (proc) {
      LLVMTypeRef param_types[CG_MAX_CALLABLE_PARAMS];
      LLVMTypeRef return_type = cg->i64_type;
      int param_count = 0;
      cg_fill_proc_callable_types(cg, proc, &return_type, param_types,
                                  &param_count);
      cg_set_callable_metadata(sym, return_type, param_types, param_count);
    }
  }
}

static LLVMValueRef cg_emit_symbol_callable_call(Codegen *cg, CgSymbol *sym,
                                                 LLVMValueRef callable,
                                                 LLVMValueRef *args, int argc,
                                                 bool is_closure,
                                                 const char *call_name) {
  LLVMTypeRef *param_types;
  LLVMValueRef *coerced_args;
  LLVMTypeRef return_type;
  LLVMValueRef result;

  if (!cg || !sym || !sym->is_callable || !callable)
    return LLVMConstInt(cg->i64_type, 0, false);

  if (argc != sym->callable_param_count) {
    cg_error(cg, "Callable '%s' expects %d argument(s) but got %d",
             sym->name ? sym->name : "<callable>", sym->callable_param_count,
             argc);
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  param_types = argc > 0 ? calloc(argc, sizeof(LLVMTypeRef)) : NULL;
  coerced_args = argc > 0 ? calloc(argc, sizeof(LLVMValueRef)) : NULL;
  for (int i = 0; i < argc; i++) {
    param_types[i] = sym->callable_param_types[i]
                         ? sym->callable_param_types[i]
                         : cg->i64_type;
    coerced_args[i] = cg_coerce_value(cg, args[i], param_types[i]);
  }
  return_type = sym->callable_return_type ? sym->callable_return_type
                                          : cg->i64_type;

  if (is_closure) {
    result = cg_emit_closure_call(cg, callable, param_types, coerced_args, argc,
                                  return_type, call_name);
  } else {
    LLVMTypeRef fn_type = LLVMFunctionType(return_type, param_types, argc, false);
    result = LLVMBuildCall2(cg->builder, fn_type, callable, coerced_args, argc,
                            call_name ? call_name : "");
  }

  free(param_types);
  free(coerced_args);
  return result;
}

static CgValueKind cg_lambda_body_kind(Codegen *cg, AstNode *node,
                                       const char *param_name,
                                       CgValueKind param_kind) {
  CgLocalKind locals[64];
  int local_count = 0;

  if (param_name)
    cg_locals_add(locals, &local_count, param_name, param_kind);

  return cg_lambda_stmt_kind(cg, node, param_name, param_kind, locals,
                             local_count);
}

static CgValueKind cg_lambda_stmt_kind(Codegen *cg, AstNode *node,
                                       const char *param_name,
                                       CgValueKind param_kind,
                                       CgLocalKind *locals, int local_count) {
  if (!cg || !node)
    return CG_VALUE_UNKNOWN;

  switch (node->type) {
  case AST_INT_LITERAL:
    return CG_VALUE_INT;
  case AST_STRING_LITERAL:
    return CG_VALUE_STRING;
  case AST_BOOL_LITERAL:
    return CG_VALUE_BOOL;
  case AST_IDENTIFIER:
    if (param_name && strcmp(node->as.identifier.name, param_name) == 0)
      return param_kind;
    {
      CgValueKind local_kind =
          cg_lookup_local_kind(locals, local_count, node->as.identifier.name);
      if (local_kind != CG_VALUE_UNKNOWN)
        return local_kind;
      CgSymbol *sym = cg_lookup(cg, node->as.identifier.name);
      if (sym)
        return cg_value_kind_from_llvm_type(cg, sym->type);
    }
    return CG_VALUE_UNKNOWN;
  case AST_UNARY_OP:
    if (node->as.unary.op == OP_NOT)
      return CG_VALUE_BOOL;
    return cg_lambda_stmt_kind(cg, node->as.unary.operand, param_name,
                               param_kind, locals, local_count);
  case AST_BINARY_OP: {
    if (node->as.binary.op == OP_EQ || node->as.binary.op == OP_NE ||
        node->as.binary.op == OP_LT || node->as.binary.op == OP_GT ||
        node->as.binary.op == OP_LE || node->as.binary.op == OP_GE ||
        node->as.binary.op == OP_AND || node->as.binary.op == OP_OR)
      return CG_VALUE_BOOL;

    CgValueKind left =
        cg_lambda_stmt_kind(cg, node->as.binary.left, param_name, param_kind,
                            locals, local_count);
    CgValueKind right =
        cg_lambda_stmt_kind(cg, node->as.binary.right, param_name, param_kind,
                            locals, local_count);

    if (node->as.binary.op == OP_ADD &&
        (left == CG_VALUE_STRING || right == CG_VALUE_STRING))
      return CG_VALUE_STRING;
    if (left == CG_VALUE_INT && right == CG_VALUE_INT)
      return CG_VALUE_INT;
    return CG_VALUE_UNKNOWN;
  }
  case AST_CALL:
    if (node->as.call.callee && node->as.call.callee->type == AST_IDENTIFIER) {
      const char *name = node->as.call.callee->as.identifier.name;
      if (strcmp(name, "str") == 0)
        return CG_VALUE_STRING;
      if (strcmp(name, "len") == 0)
        return CG_VALUE_INT;
      if (strcmp(name, "stdin_text") == 0 || strcmp(name, "read_file") == 0)
        return CG_VALUE_STRING;
      if (strcmp(name, "int_parse") == 0)
        return CG_VALUE_INT;
      if (strcmp(name, "abs") == 0 || strcmp(name, "min") == 0 ||
          strcmp(name, "max") == 0)
        return CG_VALUE_INT;
      if (cg->program_ast && cg->program_ast->type == AST_PROGRAM) {
        for (int i = 0; i < cg->program_ast->as.program.declarations.count; i++) {
          AstNode *decl = cg->program_ast->as.program.declarations.items[i];
          if (decl->type == AST_PROC && decl->as.proc.name &&
              strcmp(decl->as.proc.name, name) == 0) {
            if (!decl->as.proc.return_type)
              return CG_VALUE_UNKNOWN;
            return cg_value_kind_from_type_name(decl->as.proc.return_type->name);
          }
        }
      }
    }
    return CG_VALUE_UNKNOWN;
  case AST_GIVE:
    if (!node->as.give_stmt.value)
      return CG_VALUE_UNKNOWN;
    return cg_lambda_stmt_kind(cg, node->as.give_stmt.value, param_name,
                               param_kind, locals, local_count);
  case AST_VAR_DECL: {
    int scoped_count = local_count;
    CgValueKind init_kind = CG_VALUE_UNKNOWN;
    if (node->as.var_decl.initializer) {
      init_kind = cg_lambda_stmt_kind(cg, node->as.var_decl.initializer,
                                      param_name, param_kind, locals,
                                      scoped_count);
    }
    cg_locals_add(locals, &scoped_count, node->as.var_decl.name, init_kind);
    return CG_VALUE_UNKNOWN;
  }
  case AST_ASSIGN:
    return cg_lambda_stmt_kind(cg, node->as.assign.value, param_name, param_kind,
                               locals, local_count);
  case AST_BLOCK: {
    int scoped_count = local_count;
    CgValueKind result = CG_VALUE_UNKNOWN;
    for (int i = 0; i < node->as.block.statements.count; i++) {
      AstNode *stmt = node->as.block.statements.items[i];
      if (stmt && stmt->type == AST_VAR_DECL) {
        CgValueKind init_kind = CG_VALUE_UNKNOWN;
        if (stmt->as.var_decl.initializer) {
          init_kind = cg_lambda_stmt_kind(
              cg, stmt->as.var_decl.initializer, param_name, param_kind, locals,
              scoped_count);
        }
        cg_locals_add(locals, &scoped_count, stmt->as.var_decl.name, init_kind);
        continue;
      }
      result = cg_merge_value_kind(
          result, cg_lambda_stmt_kind(cg, stmt, param_name, param_kind, locals,
                                      scoped_count));
    }
    return result;
  }
  case AST_IF: {
    CgValueKind then_kind = CG_VALUE_UNKNOWN;
    CgValueKind else_kind = CG_VALUE_UNKNOWN;
    if (node->as.if_stmt.then_branch)
      then_kind = cg_lambda_stmt_kind(cg, node->as.if_stmt.then_branch,
                                      param_name, param_kind, locals,
                                      local_count);
    if (node->as.if_stmt.else_branch)
      else_kind = cg_lambda_stmt_kind(cg, node->as.if_stmt.else_branch,
                                      param_name, param_kind, locals,
                                      local_count);
    return cg_merge_value_kind(then_kind, else_kind);
  }
  case AST_WHILE:
    if (node->as.while_stmt.body)
      return cg_lambda_stmt_kind(cg, node->as.while_stmt.body, param_name,
                                 param_kind, locals, local_count);
    return CG_VALUE_UNKNOWN;
  case AST_FOR: {
    CgLocalKind scoped_locals[64];
    int scoped_count = local_count;
    memcpy(scoped_locals, locals, sizeof(scoped_locals));
    if (node->as.for_stmt.init && node->as.for_stmt.init->type == AST_VAR_DECL) {
      CgValueKind init_kind = CG_VALUE_UNKNOWN;
      if (node->as.for_stmt.init->as.var_decl.initializer) {
        init_kind = cg_lambda_stmt_kind(
            cg, node->as.for_stmt.init->as.var_decl.initializer, param_name,
            param_kind, scoped_locals, scoped_count);
      }
      cg_locals_add(scoped_locals, &scoped_count,
                    node->as.for_stmt.init->as.var_decl.name, init_kind);
    }
    if (node->as.for_stmt.var_name) {
      cg_locals_add(scoped_locals, &scoped_count, node->as.for_stmt.var_name,
                    CG_VALUE_UNKNOWN);
    }
    if (node->as.for_stmt.body)
      return cg_lambda_stmt_kind(cg, node->as.for_stmt.body, param_name,
                                 param_kind, scoped_locals, scoped_count);
    return CG_VALUE_UNKNOWN;
  }
  default:
    return CG_VALUE_UNKNOWN;
  }
}

static CgStreamKind cg_stream_kind_from_callable(Codegen *cg, AstNode *node,
                                                 CgStreamKind input_kind) {
  if (!cg || !node)
    return CG_STREAM_NONE;

  if (node->type == AST_IDENTIFIER && cg->program_ast &&
      cg->program_ast->type == AST_PROGRAM) {
    const char *name = node->as.identifier.name;
    for (int i = 0; i < cg->program_ast->as.program.declarations.count; i++) {
      AstNode *decl = cg->program_ast->as.program.declarations.items[i];
      if (decl->type == AST_PROC && decl->as.proc.name &&
          strcmp(decl->as.proc.name, name) == 0) {
        if (!decl->as.proc.return_type)
          return CG_STREAM_NONE;
        return cg_stream_kind_from_type_name(decl->as.proc.return_type->name);
      }
    }
  }

  if (node->type == AST_LAMBDA) {
    const char *param_name = NULL;
    CgValueKind param_kind = cg_value_kind_from_stream_kind(input_kind);
    CgValueKind body_kind;

    if (node->as.lambda.params.count != 1 || !node->as.lambda.body)
      return CG_STREAM_NONE;

    AstNode *param = node->as.lambda.params.items[0];
    if (param->type == AST_VAR_DECL)
      param_name = param->as.var_decl.name;
    else if (param->type == AST_IDENTIFIER)
      param_name = param->as.identifier.name;

    body_kind =
        cg_lambda_body_kind(cg, node->as.lambda.body, param_name, param_kind);
    if (body_kind == CG_VALUE_INT)
      return CG_STREAM_INT;
    if (body_kind == CG_VALUE_STRING)
      return CG_STREAM_STRING;
  }

  return CG_STREAM_NONE;
}

static LLVMValueRef cg_coerce_value(Codegen *cg, LLVMValueRef value,
                                    LLVMTypeRef target_type) {
  LLVMTypeRef source_type;

  if (!cg || !value || !target_type)
    return value;

  source_type = LLVMTypeOf(value);
  if (source_type == target_type)
    return value;

  if (target_type == cg->i1_type) {
    if (source_type == cg->i64_type)
      return LLVMBuildICmp(cg->builder, LLVMIntNE, value,
                           LLVMConstInt(cg->i64_type, 0, false), "boolcast");
    if (source_type == cg->i8ptr_type)
      return LLVMBuildICmp(cg->builder, LLVMIntNE, value,
                           LLVMConstNull(cg->i8ptr_type), "boolcast");
  }

  if (target_type == cg->i64_type && source_type == cg->i1_type)
    return LLVMBuildZExt(cg->builder, value, cg->i64_type, "intcast");

  if (target_type == cg->i8ptr_type && source_type == cg->i8ptr_type)
    return value;

  char *src = LLVMPrintTypeToString(source_type);
  char *dst = LLVMPrintTypeToString(target_type);
  cg_error(cg, "Unsupported lambda return coercion (%s -> %s)", src, dst);
  LLVMDisposeMessage(src);
  LLVMDisposeMessage(dst);
  return value;
}

static bool cg_lambda_capture_present(CgLambdaCapture *captures, int count,
                                      const char *name) {
  for (int i = 0; i < count; i++) {
    if (strcmp(captures[i].name, name) == 0)
      return true;
  }
  return false;
}

static const char *cg_lambda_param_name(AstNode *param) {
  if (!param)
    return NULL;
  if (param->type == AST_VAR_DECL)
    return param->as.var_decl.name;
  if (param->type == AST_IDENTIFIER)
    return param->as.identifier.name;
  return NULL;
}

static void cg_collect_lambda_captures_impl(Codegen *cg, AstNode *node,
                                            CgLambdaCapture *captures,
                                            int *count, bool *unsupported,
                                            CgLocalKind *locals,
                                            int *local_count) {
  if (!cg || !node || !count || !unsupported || *unsupported)
    return;

  switch (node->type) {
  case AST_IDENTIFIER: {
    const char *name = node->as.identifier.name;
    CgSymbol *sym;
    if (!name || cg_local_kind_present(locals, *local_count, name))
      return;
    sym = cg_lookup(cg, name);
    if (!sym)
      return;
    if (!cg_lambda_capture_present(captures, *count, name)) {
      captures[*count].name = name;
      captures[*count].symbol = sym;
      (*count)++;
    }
    return;
  }
  case AST_CALL:
    if (node->as.call.callee &&
        node->as.call.callee->type != AST_IDENTIFIER) {
      cg_collect_lambda_captures_impl(cg, node->as.call.callee, captures, count,
                                      unsupported, locals, local_count);
    }
    for (int i = 0; i < node->as.call.args.count; i++) {
      cg_collect_lambda_captures_impl(cg, node->as.call.args.items[i], captures,
                                      count, unsupported, locals, local_count);
    }
    return;
  case AST_BINARY_OP:
    cg_collect_lambda_captures_impl(cg, node->as.binary.left, captures, count,
                                    unsupported, locals, local_count);
    cg_collect_lambda_captures_impl(cg, node->as.binary.right, captures, count,
                                    unsupported, locals, local_count);
    return;
  case AST_UNARY_OP:
    cg_collect_lambda_captures_impl(cg, node->as.unary.operand, captures, count,
                                    unsupported, locals, local_count);
    return;
  case AST_ARRAY_LITERAL:
    for (int i = 0; i < node->as.array_literal.elements.count; i++) {
      cg_collect_lambda_captures_impl(
          cg, node->as.array_literal.elements.items[i], captures, count,
          unsupported, locals, local_count);
    }
    return;
  case AST_MEMBER:
    cg_collect_lambda_captures_impl(cg, node->as.member.object, captures, count,
                                    unsupported, locals, local_count);
    return;
  case AST_INDEX:
    cg_collect_lambda_captures_impl(cg, node->as.index.object, captures, count,
                                    unsupported, locals, local_count);
    cg_collect_lambda_captures_impl(cg, node->as.index.index, captures, count,
                                    unsupported, locals, local_count);
    return;
  case AST_VAR_DECL:
    if (node->as.var_decl.initializer)
      cg_collect_lambda_captures_impl(cg, node->as.var_decl.initializer,
                                      captures, count, unsupported, locals,
                                      local_count);
    cg_locals_add(locals, local_count, node->as.var_decl.name,
                  CG_VALUE_UNKNOWN);
    return;
  case AST_ASSIGN:
    cg_collect_lambda_captures_impl(cg, node->as.assign.target, captures, count,
                                    unsupported, locals, local_count);
    cg_collect_lambda_captures_impl(cg, node->as.assign.value, captures, count,
                                    unsupported, locals, local_count);
    return;
  case AST_GIVE:
    if (node->as.give_stmt.value)
      cg_collect_lambda_captures_impl(cg, node->as.give_stmt.value, captures,
                                      count, unsupported, locals, local_count);
    return;
  case AST_BLOCK: {
    int scoped_count = *local_count;
    for (int i = 0; i < node->as.block.statements.count; i++) {
      cg_collect_lambda_captures_impl(cg, node->as.block.statements.items[i],
                                      captures, count, unsupported, locals,
                                      &scoped_count);
      if (*unsupported)
        return;
    }
    return;
  }
  case AST_IF:
    if (node->as.if_stmt.condition)
      cg_collect_lambda_captures_impl(cg, node->as.if_stmt.condition, captures,
                                      count, unsupported, locals, local_count);
    if (node->as.if_stmt.then_branch) {
      int branch_locals = *local_count;
      cg_collect_lambda_captures_impl(cg, node->as.if_stmt.then_branch, captures,
                                      count, unsupported, locals,
                                      &branch_locals);
    }
    if (node->as.if_stmt.else_branch) {
      int branch_locals = *local_count;
      cg_collect_lambda_captures_impl(cg, node->as.if_stmt.else_branch, captures,
                                      count, unsupported, locals,
                                      &branch_locals);
    }
    return;
  case AST_WHILE:
    if (node->as.while_stmt.condition)
      cg_collect_lambda_captures_impl(cg, node->as.while_stmt.condition,
                                      captures, count, unsupported, locals,
                                      local_count);
    if (node->as.while_stmt.body) {
      int body_locals = *local_count;
      cg_collect_lambda_captures_impl(cg, node->as.while_stmt.body, captures,
                                      count, unsupported, locals, &body_locals);
    }
    return;
  case AST_FOR: {
    int scoped_count = *local_count;
    if (node->as.for_stmt.init)
      cg_collect_lambda_captures_impl(cg, node->as.for_stmt.init, captures,
                                      count, unsupported, locals, &scoped_count);
    if (node->as.for_stmt.var_name)
      cg_locals_add(locals, &scoped_count, node->as.for_stmt.var_name,
                    CG_VALUE_UNKNOWN);
    if (node->as.for_stmt.condition)
      cg_collect_lambda_captures_impl(cg, node->as.for_stmt.condition, captures,
                                      count, unsupported, locals, &scoped_count);
    if (node->as.for_stmt.update)
      cg_collect_lambda_captures_impl(cg, node->as.for_stmt.update, captures,
                                      count, unsupported, locals, &scoped_count);
    if (node->as.for_stmt.iterable)
      cg_collect_lambda_captures_impl(cg, node->as.for_stmt.iterable, captures,
                                      count, unsupported, locals, &scoped_count);
    if (node->as.for_stmt.body)
      cg_collect_lambda_captures_impl(cg, node->as.for_stmt.body, captures,
                                      count, unsupported, locals, &scoped_count);
    return;
  }
  case AST_LAMBDA:
    return;
  default:
    return;
  }
}

static void cg_collect_lambda_captures(Codegen *cg, AstNode *node,
                                       const char *param_name,
                                       CgLambdaCapture *captures, int *count,
                                       bool *unsupported) {
  CgLocalKind locals[64];
  int local_count = 0;

  if (param_name)
    cg_locals_add(locals, &local_count, param_name, CG_VALUE_UNKNOWN);

  cg_collect_lambda_captures_impl(cg, node, captures, count, unsupported,
                                  locals, &local_count);
}

static void cg_collect_lambda_captures_for_params(
    Codegen *cg, AstNode *node, const char **param_names, int param_count,
    CgLambdaCapture *captures, int *count, bool *unsupported) {
  CgLocalKind locals[64];
  int local_count = 0;

  for (int i = 0; i < param_count; i++) {
    if (param_names && param_names[i])
      cg_locals_add(locals, &local_count, param_names[i], CG_VALUE_UNKNOWN);
  }

  cg_collect_lambda_captures_impl(cg, node, captures, count, unsupported,
                                  locals, &local_count);
}

static bool cg_expr_is_closure_like(Codegen *cg, AstNode *node) {
  if (!cg || !node)
    return false;
  if (node->type == AST_LAMBDA)
    return true;
  if (node->type == AST_IDENTIFIER) {
    char marker_name[300];
    snprintf(marker_name, sizeof(marker_name), "__%s__closure",
             node->as.identifier.name);
    return cg_lookup(cg, marker_name) != NULL;
  }
  return false;
}

static LLVMTypeRef cg_closure_struct_type(Codegen *cg) {
  return LLVMStructTypeInContext(
      cg->ctx, (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
}

static LLVMValueRef cg_build_closure_object(Codegen *cg, LLVMValueRef fn,
                                            LLVMValueRef ctx) {
  LLVMTypeRef closure_type;
  LLVMTypeRef closure_ptr_type;
  LLVMValueRef fn_malloc;
  LLVMTypeRef malloc_type;
  LLVMValueRef raw;
  LLVMValueRef typed;
  LLVMValueRef fn_field;
  LLVMValueRef ctx_field;

  if (!cg || !fn)
    return LLVMConstNull(cg ? cg->i8ptr_type : LLVMInt8Type());

  closure_type = cg_closure_struct_type(cg);
  closure_ptr_type = LLVMPointerType(closure_type, 0);
  fn_malloc = LLVMGetNamedFunction(cg->mod, "malloc");
  if (!fn_malloc) {
    malloc_type = LLVMFunctionType(cg->i8ptr_type,
                                   (LLVMTypeRef[]){cg->i64_type}, 1, false);
    fn_malloc = LLVMAddFunction(cg->mod, "malloc", malloc_type);
  }
  malloc_type = LLVMFunctionType(cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type},
                                 1, false);
  raw = LLVMBuildCall2(cg->builder, malloc_type, fn_malloc,
                       (LLVMValueRef[]){LLVMSizeOf(closure_type)}, 1,
                       "closure_mem");
  typed = LLVMBuildBitCast(cg->builder, raw, closure_ptr_type, "closure");
  fn_field =
      LLVMBuildStructGEP2(cg->builder, closure_type, typed, 0, "closure_fn");
  ctx_field =
      LLVMBuildStructGEP2(cg->builder, closure_type, typed, 1, "closure_ctx");
  LLVMBuildStore(cg->builder,
                 LLVMBuildBitCast(cg->builder, fn, cg->i8ptr_type, "closure_fn_cast"),
                 fn_field);
  LLVMBuildStore(cg->builder,
                 ctx ? ctx : LLVMConstNull(cg->i8ptr_type), ctx_field);
  return raw;
}

static LLVMValueRef cg_emit_closure_call(Codegen *cg, LLVMValueRef closure,
                                         LLVMTypeRef *arg_types,
                                         LLVMValueRef *args, int argc,
                                         LLVMTypeRef return_type,
                                         const char *name) {
  LLVMTypeRef closure_type;
  LLVMTypeRef closure_ptr_type;
  LLVMValueRef typed;
  LLVMValueRef fn_raw;
  LLVMValueRef ctx;
  LLVMTypeRef *param_types;
  LLVMTypeRef fn_type;
  LLVMValueRef fn_ptr;
  LLVMValueRef *call_args;
  LLVMValueRef result;

  if (!cg || !closure)
    return LLVMConstInt(cg->i64_type, 0, false);

  closure_type = cg_closure_struct_type(cg);
  closure_ptr_type = LLVMPointerType(closure_type, 0);
  typed = LLVMBuildBitCast(cg->builder, closure, closure_ptr_type, "closure_t");
  fn_raw = LLVMBuildLoad2(
      cg->builder, cg->i8ptr_type,
      LLVMBuildStructGEP2(cg->builder, closure_type, typed, 0, "closure_fn_ptr"),
      "closure_fn");
  ctx = LLVMBuildLoad2(
      cg->builder, cg->i8ptr_type,
      LLVMBuildStructGEP2(cg->builder, closure_type, typed, 1, "closure_ctx_ptr"),
      "closure_ctx");

  param_types = calloc(argc + 1, sizeof(LLVMTypeRef));
  param_types[0] = cg->i8ptr_type;
  for (int i = 0; i < argc; i++)
    param_types[i + 1] = arg_types[i];
  fn_type = LLVMFunctionType(return_type, param_types, argc + 1, false);
  free(param_types);
  fn_ptr = LLVMBuildBitCast(cg->builder, fn_raw, LLVMPointerType(fn_type, 0),
                            "closure_call_fn");
  call_args = calloc(argc + 1, sizeof(LLVMValueRef));
  call_args[0] = ctx;
  for (int i = 0; i < argc; i++)
    call_args[i + 1] = args[i];
  result = LLVMBuildCall2(cg->builder, fn_type, fn_ptr, call_args, argc + 1,
                          name ? name : "");
  free(call_args);
  return result;
}

static LLVMValueRef cg_emit_general_closure_value(Codegen *cg, AstNode *lambda,
                                                  LLVMTypeRef *param_types,
                                                  int param_count,
                                                  LLVMTypeRef return_type) {
  CgLambdaCapture captures[16];
  int capture_count = 0;
  bool unsupported = false;
  const char *param_names[8] = {0};
  static int closure_lambda_id = 0;
  char lname[64];
  LLVMValueRef saved_fn;
  const char *saved_fn_name;
  LLVMBasicBlockRef saved_bb;
  LLVMTypeRef *field_types = NULL;
  LLVMTypeRef ctx_struct = NULL;
  LLVMTypeRef ctx_ptr_type = NULL;
  LLVMValueRef raw_ctx = LLVMConstNull(cg->i8ptr_type);
  LLVMValueRef fn;

  if (!cg || !lambda || lambda->type != AST_LAMBDA)
    return LLVMConstNull(cg ? cg->i8ptr_type : LLVMInt8Type());

  if (param_count > 8)
    param_count = 8;
  for (int i = 0; i < param_count; i++)
    param_names[i] = cg_lambda_param_name(lambda->as.lambda.params.items[i]);

  cg_collect_lambda_captures_for_params(cg, lambda->as.lambda.body, param_names,
                                        param_count, captures, &capture_count,
                                        &unsupported);

  saved_fn = cg->current_fn;
  saved_fn_name = cg->current_fn_name;
  saved_bb = LLVMGetInsertBlock(cg->builder);

  if (capture_count > 0) {
    field_types = calloc(capture_count, sizeof(LLVMTypeRef));
    for (int i = 0; i < capture_count; i++)
      field_types[i] = captures[i].symbol->type;
    ctx_struct =
        LLVMStructTypeInContext(cg->ctx, field_types, capture_count, false);
    free(field_types);
    ctx_ptr_type = LLVMPointerType(ctx_struct, 0);

    LLVMValueRef fn_malloc = LLVMGetNamedFunction(cg->mod, "malloc");
    if (!fn_malloc) {
      LLVMTypeRef malloc_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
      fn_malloc = LLVMAddFunction(cg->mod, "malloc", malloc_type);
    }
    LLVMTypeRef malloc_type = LLVMFunctionType(
        cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
    raw_ctx = LLVMBuildCall2(cg->builder, malloc_type, fn_malloc,
                             (LLVMValueRef[]){LLVMSizeOf(ctx_struct)}, 1,
                             "lambda_ctx");
    LLVMValueRef ctx_alloc =
        LLVMBuildBitCast(cg->builder, raw_ctx, ctx_ptr_type, "lambda_ctx_t");
    for (int i = 0; i < capture_count; i++) {
      LLVMValueRef field =
          LLVMBuildStructGEP2(cg->builder, ctx_struct, ctx_alloc, i, "cap");
      LLVMValueRef loaded =
          LLVMBuildLoad2(cg->builder, captures[i].symbol->type,
                         captures[i].symbol->alloca, captures[i].name);
      LLVMBuildStore(cg->builder, loaded, field);
    }
  }

  snprintf(lname, sizeof(lname), "__closure_lambda_%d", closure_lambda_id++);
  LLVMTypeRef *fn_params = calloc(param_count + 1, sizeof(LLVMTypeRef));
  fn_params[0] = cg->i8ptr_type;
  for (int i = 0; i < param_count; i++)
    fn_params[i + 1] = param_types[i];
  LLVMTypeRef fn_type =
      LLVMFunctionType(return_type, fn_params, param_count + 1, false);
  free(fn_params);
  fn = LLVMAddFunction(cg->mod, lname, fn_type);
  LLVMSetLinkage(fn, LLVMPrivateLinkage);

  LLVMBasicBlockRef entry =
      LLVMAppendBasicBlockInContext(cg->ctx, fn, "entry");
  LLVMPositionBuilderAtEnd(cg->builder, entry);
  cg->current_fn = fn;
  cg->current_fn_name = lname;
  cg_push_scope(cg);

  if (capture_count > 0 && ctx_ptr_type) {
    LLVMValueRef ctx_arg = LLVMBuildBitCast(cg->builder, LLVMGetParam(fn, 0),
                                            ctx_ptr_type, "ctx");
    for (int i = 0; i < capture_count; i++) {
      LLVMValueRef field =
          LLVMBuildStructGEP2(cg->builder, ctx_struct, ctx_arg, i, "cap");
      LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, captures[i].symbol->type,
                                           field, captures[i].name);
      LLVMValueRef alloca =
          LLVMBuildAlloca(cg->builder, captures[i].symbol->type,
                          captures[i].name);
      LLVMBuildStore(cg->builder, loaded, alloca);
      cg_define(cg, captures[i].name, alloca, captures[i].symbol->type);
    }
  }

  for (int i = 0; i < param_count; i++) {
    const char *pname = param_names[i] ? param_names[i] : "arg";
    LLVMValueRef param_alloc =
        LLVMBuildAlloca(cg->builder, param_types[i], pname);
    LLVMBuildStore(cg->builder, LLVMGetParam(fn, i + 1), param_alloc);
    cg_define(cg, pname, param_alloc, param_types[i]);
  }

  if (lambda->as.lambda.body && lambda->as.lambda.body->type == AST_BLOCK) {
    emit_block(cg, lambda->as.lambda.body);
  } else if (lambda->as.lambda.body) {
    LLVMValueRef result = emit_expr(cg, lambda->as.lambda.body);
    result = cg_coerce_value(cg, result, return_type);
    LLVMBuildRet(cg->builder, result);
  }
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
    LLVMValueRef fallback =
        return_type == cg->i8ptr_type ? LLVMConstNull(cg->i8ptr_type)
                                      : LLVMConstInt(return_type, 0, false);
    LLVMBuildRet(cg->builder, fallback);
  }

  cg_pop_scope(cg);
  cg->current_fn = saved_fn;
  cg->current_fn_name = saved_fn_name;
  LLVMPositionBuilderAtEnd(cg->builder, saved_bb);

  return cg_build_closure_object(cg, fn,
                                 capture_count > 0 ? raw_ctx
                                                   : LLVMConstNull(cg->i8ptr_type));
}

static CgStreamCallable cg_emit_stream_callable(Codegen *cg, AstNode *callable,
                                                CgStreamKind input_kind,
                                                LLVMTypeRef expected_return_type) {
  CgStreamCallable result = {0};
  bool saved_active;
  LLVMTypeRef saved_param;
  LLVMTypeRef saved_return;
  LLVMTypeRef param_type;
  const char *param_name = NULL;

  if (!cg || !callable)
    return result;

  if (callable->type != AST_LAMBDA) {
    result.fn = emit_expr(cg, callable);
    return result;
  }

  if (callable->as.lambda.params.count != 1) {
    cg_error(cg, "Lazy stream lambdas must take exactly one parameter");
    result.fn = LLVMConstNull(cg->i8ptr_type);
    return result;
  }

  AstNode *param = callable->as.lambda.params.items[0];
  if (param->type == AST_VAR_DECL)
    param_name = param->as.var_decl.name;
  else if (param->type == AST_IDENTIFIER)
    param_name = param->as.identifier.name;

  if (callable->as.lambda.body) {
    CgLambdaCapture captures[16];
    int capture_count = 0;
    bool unsupported = false;
    cg_collect_lambda_captures(cg, callable->as.lambda.body, param_name, captures,
                               &capture_count, &unsupported);
    if (capture_count > 0 && !unsupported) {
      static int stream_lambda_id = 0;
      char lname[64];
      LLVMValueRef saved_fn = cg->current_fn;
      const char *saved_fn_name = cg->current_fn_name;
      LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(cg->builder);
      LLVMTypeRef *field_types = calloc(capture_count, sizeof(LLVMTypeRef));
      LLVMTypeRef ctx_struct;
      LLVMTypeRef ctx_ptr_type;
      LLVMValueRef fn;
      LLVMValueRef raw_ctx;
      LLVMValueRef ctx_arg;

      for (int i = 0; i < capture_count; i++)
        field_types[i] = captures[i].symbol->type;
      ctx_struct = LLVMStructTypeInContext(cg->ctx, field_types, capture_count,
                                           false);
      free(field_types);
      ctx_ptr_type = LLVMPointerType(ctx_struct, 0);

      snprintf(lname, sizeof(lname), "__stream_lambda_%d", stream_lambda_id++);
      param_type = input_kind == CG_STREAM_STRING ? cg->i8ptr_type : cg->i64_type;
      LLVMTypeRef fn_type =
          LLVMFunctionType(expected_return_type,
                           (LLVMTypeRef[]){cg->i8ptr_type, param_type}, 2, false);
      fn = LLVMAddFunction(cg->mod, lname, fn_type);
      LLVMSetLinkage(fn, LLVMPrivateLinkage);

      LLVMValueRef fn_malloc = LLVMGetNamedFunction(cg->mod, "malloc");
      if (!fn_malloc) {
        LLVMTypeRef malloc_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
        fn_malloc = LLVMAddFunction(cg->mod, "malloc", malloc_type);
      }
      LLVMTypeRef malloc_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
      raw_ctx = LLVMBuildCall2(cg->builder, malloc_type, fn_malloc,
                               (LLVMValueRef[]){LLVMSizeOf(ctx_struct)}, 1,
                               "lambda_ctx");
      LLVMValueRef ctx_alloc =
          LLVMBuildBitCast(cg->builder, raw_ctx, ctx_ptr_type, "lambda_ctx_t");
      for (int i = 0; i < capture_count; i++) {
        LLVMValueRef field =
            LLVMBuildStructGEP2(cg->builder, ctx_struct, ctx_alloc, i, "cap");
        LLVMValueRef loaded =
            LLVMBuildLoad2(cg->builder, captures[i].symbol->type,
                           captures[i].symbol->alloca, captures[i].name);
        LLVMBuildStore(cg->builder, loaded, field);
      }

      LLVMBasicBlockRef entry =
          LLVMAppendBasicBlockInContext(cg->ctx, fn, "entry");
      LLVMPositionBuilderAtEnd(cg->builder, entry);
      cg->current_fn = fn;
      cg->current_fn_name = lname;
      cg_push_scope(cg);

      ctx_arg = LLVMBuildBitCast(cg->builder, LLVMGetParam(fn, 0), ctx_ptr_type,
                                 "ctx");
      for (int i = 0; i < capture_count; i++) {
        LLVMValueRef field =
            LLVMBuildStructGEP2(cg->builder, ctx_struct, ctx_arg, i, "cap");
        LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, captures[i].symbol->type,
                                             field, captures[i].name);
        LLVMValueRef alloca =
            LLVMBuildAlloca(cg->builder, captures[i].symbol->type,
                            captures[i].name);
        LLVMBuildStore(cg->builder, loaded, alloca);
        cg_define(cg, captures[i].name, alloca, captures[i].symbol->type);
      }

      const char *pname =
          (param->type == AST_VAR_DECL)
              ? param->as.var_decl.name
              : (param->type == AST_IDENTIFIER ? param->as.identifier.name : "arg");
      LLVMValueRef param_alloc = LLVMBuildAlloca(cg->builder, param_type, pname);
      LLVMBuildStore(cg->builder, LLVMGetParam(fn, 1), param_alloc);
      cg_define(cg, pname, param_alloc, param_type);

      if (callable->as.lambda.body->type == AST_BLOCK) {
        emit_block(cg, callable->as.lambda.body);
      } else {
        LLVMValueRef body_result = emit_expr(cg, callable->as.lambda.body);
        body_result = cg_coerce_value(cg, body_result, expected_return_type);
        LLVMBuildRet(cg->builder, body_result);
      }
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
        LLVMValueRef fallback =
            expected_return_type == cg->i8ptr_type
                ? LLVMConstNull(cg->i8ptr_type)
                : LLVMConstInt(expected_return_type, 0, false);
        LLVMBuildRet(cg->builder, fallback);
      }
      cg_pop_scope(cg);
      cg->current_fn = saved_fn;
      cg->current_fn_name = saved_fn_name;
      LLVMPositionBuilderAtEnd(cg->builder, saved_bb);

      result.fn = fn;
      result.ctx = raw_ctx;
      result.uses_ctx = true;
      return result;
    }
  }

  param_type = input_kind == CG_STREAM_STRING ? cg->i8ptr_type : cg->i64_type;
  saved_active = cg->lambda_hint_active;
  saved_param = cg->lambda_hint_param_type;
  saved_return = cg->lambda_hint_return_type;

  cg->lambda_hint_active = true;
  cg->lambda_hint_param_type = param_type;
  cg->lambda_hint_return_type = expected_return_type;
  result.fn = emit_expr(cg, callable);
  cg->lambda_hint_active = saved_active;
  cg->lambda_hint_param_type = saved_param;
  cg->lambda_hint_return_type = saved_return;
  return result;
}

static CgStreamCallable cg_emit_stream_reduce_callable(Codegen *cg,
                                                       AstNode *callable,
                                                       CgStreamKind input_kind) {
  CgStreamCallable result = {0};
  const char *param_names[2] = {NULL, NULL};
  LLVMTypeRef elem_type;

  if (!cg || !callable)
    return result;

  if (callable->type != AST_LAMBDA) {
    result.fn = emit_expr(cg, callable);
    return result;
  }

  if (callable->as.lambda.params.count != 2) {
    cg_error(cg, "Lazy stream reduce lambdas must take exactly two parameters");
    result.fn = LLVMConstNull(cg->i8ptr_type);
    return result;
  }

  for (int i = 0; i < 2; i++)
    param_names[i] = cg_lambda_param_name(callable->as.lambda.params.items[i]);
  elem_type = input_kind == CG_STREAM_STRING ? cg->i8ptr_type : cg->i64_type;

  if (callable->as.lambda.body) {
    CgLambdaCapture captures[16];
    int capture_count = 0;
    bool unsupported = false;
    cg_collect_lambda_captures_for_params(cg, callable->as.lambda.body,
                                          param_names, 2, captures,
                                          &capture_count, &unsupported);
    if (capture_count > 0 && !unsupported) {
      static int stream_reduce_lambda_id = 0;
      char lname[64];
      LLVMValueRef saved_fn = cg->current_fn;
      const char *saved_fn_name = cg->current_fn_name;
      LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(cg->builder);
      LLVMTypeRef *field_types = calloc(capture_count, sizeof(LLVMTypeRef));
      LLVMTypeRef ctx_struct;
      LLVMTypeRef ctx_ptr_type;
      LLVMValueRef fn;
      LLVMValueRef raw_ctx;
      LLVMValueRef ctx_arg;

      for (int i = 0; i < capture_count; i++)
        field_types[i] = captures[i].symbol->type;
      ctx_struct = LLVMStructTypeInContext(cg->ctx, field_types, capture_count,
                                           false);
      free(field_types);
      ctx_ptr_type = LLVMPointerType(ctx_struct, 0);

      snprintf(lname, sizeof(lname), "__stream_reduce_lambda_%d",
               stream_reduce_lambda_id++);
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i64_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type, elem_type}, 3,
          false);
      fn = LLVMAddFunction(cg->mod, lname, fn_type);
      LLVMSetLinkage(fn, LLVMPrivateLinkage);

      LLVMValueRef fn_malloc = LLVMGetNamedFunction(cg->mod, "malloc");
      if (!fn_malloc) {
        LLVMTypeRef malloc_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
        fn_malloc = LLVMAddFunction(cg->mod, "malloc", malloc_type);
      }
      LLVMTypeRef malloc_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
      raw_ctx = LLVMBuildCall2(cg->builder, malloc_type, fn_malloc,
                               (LLVMValueRef[]){LLVMSizeOf(ctx_struct)}, 1,
                               "lambda_ctx");
      LLVMValueRef ctx_alloc =
          LLVMBuildBitCast(cg->builder, raw_ctx, ctx_ptr_type, "lambda_ctx_t");
      for (int i = 0; i < capture_count; i++) {
        LLVMValueRef field =
            LLVMBuildStructGEP2(cg->builder, ctx_struct, ctx_alloc, i, "cap");
        LLVMValueRef loaded =
            LLVMBuildLoad2(cg->builder, captures[i].symbol->type,
                           captures[i].symbol->alloca, captures[i].name);
        LLVMBuildStore(cg->builder, loaded, field);
      }

      LLVMBasicBlockRef entry =
          LLVMAppendBasicBlockInContext(cg->ctx, fn, "entry");
      LLVMPositionBuilderAtEnd(cg->builder, entry);
      cg->current_fn = fn;
      cg->current_fn_name = lname;
      cg_push_scope(cg);

      ctx_arg = LLVMBuildBitCast(cg->builder, LLVMGetParam(fn, 0), ctx_ptr_type,
                                 "ctx");
      for (int i = 0; i < capture_count; i++) {
        LLVMValueRef field =
            LLVMBuildStructGEP2(cg->builder, ctx_struct, ctx_arg, i, "cap");
        LLVMValueRef loaded = LLVMBuildLoad2(cg->builder, captures[i].symbol->type,
                                             field, captures[i].name);
        LLVMValueRef alloca =
            LLVMBuildAlloca(cg->builder, captures[i].symbol->type,
                            captures[i].name);
        LLVMBuildStore(cg->builder, loaded, alloca);
        cg_define(cg, captures[i].name, alloca, captures[i].symbol->type);
      }

      for (int i = 0; i < 2; i++) {
        const char *pname = param_names[i] ? param_names[i] : "arg";
        LLVMTypeRef param_type = i == 0 ? cg->i64_type : elem_type;
        LLVMValueRef param_alloc =
            LLVMBuildAlloca(cg->builder, param_type, pname);
        LLVMBuildStore(cg->builder, LLVMGetParam(fn, i + 1), param_alloc);
        cg_define(cg, pname, param_alloc, param_type);
      }

      if (callable->as.lambda.body->type == AST_BLOCK) {
        emit_block(cg, callable->as.lambda.body);
      } else {
        LLVMValueRef body_result = emit_expr(cg, callable->as.lambda.body);
        body_result = cg_coerce_value(cg, body_result, cg->i64_type);
        LLVMBuildRet(cg->builder, body_result);
      }
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
        LLVMBuildRet(cg->builder, LLVMConstInt(cg->i64_type, 0, false));
      }
      cg_pop_scope(cg);
      cg->current_fn = saved_fn;
      cg->current_fn_name = saved_fn_name;
      LLVMPositionBuilderAtEnd(cg->builder, saved_bb);

      result.fn = fn;
      result.ctx = raw_ctx;
      result.uses_ctx = true;
      return result;
    }
  }

  {
    static int stream_reduce_lambda_id = 0;
    char lname[64];
    LLVMValueRef saved_fn = cg->current_fn;
    const char *saved_fn_name = cg->current_fn_name;
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(cg->builder);
    LLVMValueRef fn;

    snprintf(lname, sizeof(lname), "__stream_reduce_lambda_%d",
             stream_reduce_lambda_id++);
    LLVMTypeRef fn_type = LLVMFunctionType(
        cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type, elem_type},
        3, false);
    fn = LLVMAddFunction(cg->mod, lname, fn_type);
    LLVMSetLinkage(fn, LLVMPrivateLinkage);

    LLVMBasicBlockRef entry =
        LLVMAppendBasicBlockInContext(cg->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry);
    cg->current_fn = fn;
    cg->current_fn_name = lname;
    cg_push_scope(cg);

    for (int i = 0; i < 2; i++) {
      const char *pname = param_names[i] ? param_names[i] : "arg";
      LLVMTypeRef param_type = i == 0 ? cg->i64_type : elem_type;
      LLVMValueRef param_alloc =
          LLVMBuildAlloca(cg->builder, param_type, pname);
      LLVMBuildStore(cg->builder, LLVMGetParam(fn, i + 1), param_alloc);
      cg_define(cg, pname, param_alloc, param_type);
    }

    if (callable->as.lambda.body->type == AST_BLOCK) {
      emit_block(cg, callable->as.lambda.body);
    } else {
      LLVMValueRef body_result = emit_expr(cg, callable->as.lambda.body);
      body_result = cg_coerce_value(cg, body_result, cg->i64_type);
      LLVMBuildRet(cg->builder, body_result);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
      LLVMBuildRet(cg->builder, LLVMConstInt(cg->i64_type, 0, false));
    }
    cg_pop_scope(cg);
    cg->current_fn = saved_fn;
    cg->current_fn_name = saved_fn_name;
    LLVMPositionBuilderAtEnd(cg->builder, saved_bb);

    result.fn = fn;
    result.ctx = LLVMConstNull(cg->i8ptr_type);
    result.uses_ctx = true;
    return result;
  }
}

static const char *cg_stream_map_runtime_name(CgStreamKind source_kind,
                                              CgStreamKind target_kind,
                                              bool use_ctx) {
  if (use_ctx) {
    if (source_kind == CG_STREAM_INT && target_kind == CG_STREAM_STRING)
      return "__qisc_stream_map_i64_to_strings_ctx";
    if (source_kind == CG_STREAM_STRING && target_kind == CG_STREAM_INT)
      return "__qisc_stream_map_strings_to_i64_ctx";
    if (source_kind == CG_STREAM_STRING)
      return "__qisc_stream_map_strings_ctx";
    return "__qisc_stream_map_i64_ctx";
  }
  if (source_kind == CG_STREAM_INT && target_kind == CG_STREAM_STRING)
    return "__qisc_stream_map_i64_to_strings";
  if (source_kind == CG_STREAM_STRING && target_kind == CG_STREAM_INT)
    return "__qisc_stream_map_strings_to_i64";
  if (source_kind == CG_STREAM_STRING)
    return "__qisc_stream_map_strings";
  return "__qisc_stream_map_i64";
}

static const char *cg_stream_filter_runtime_name(CgStreamKind kind,
                                                 bool use_ctx) {
  if (use_ctx)
    return kind == CG_STREAM_STRING ? "__qisc_stream_filter_strings_ctx"
                                    : "__qisc_stream_filter_i64_ctx";
  return kind == CG_STREAM_STRING ? "__qisc_stream_filter_strings"
                                  : "__qisc_stream_filter_i64";
}

static const char *cg_stream_reduce_runtime_name(CgStreamKind input_kind,
                                                 bool use_ctx) {
  if (input_kind == CG_STREAM_STRING) {
    return use_ctx ? "__qisc_stream_reduce_strings_to_i64_ctx"
                   : "__qisc_stream_reduce_strings_to_i64";
  }
  return use_ctx ? "__qisc_stream_reduce_i64_ctx" : "__qisc_stream_reduce_i64";
}

static LLVMValueRef cg_emit_strlen(Codegen *cg, LLVMValueRef val) {
  LLVMValueRef fn_strlen = LLVMGetNamedFunction(cg->mod, "strlen");
  if (!fn_strlen) {
    LLVMTypeRef st = LLVMFunctionType(cg->i64_type,
                                      (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
    fn_strlen = LLVMAddFunction(cg->mod, "strlen", st);
  }

  LLVMTypeRef st = LLVMFunctionType(cg->i64_type,
                                    (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
  return LLVMBuildCall2(cg->builder, st, fn_strlen, (LLVMValueRef[]){val}, 1,
                        "slen");
}

static LLVMTypeRef cg_type_from_name(Codegen *cg, const char *name) {
  size_t len;
  if (!name)
    return cg->i64_type;
  len = strlen(name);
  if (strncmp(name, "proc(", 5) == 0)
    return cg->i8ptr_type;
  if (strncmp(name, "stream(", 7) == 0)
    return cg->i8ptr_type;
  if (len >= 2 && strcmp(name + len - 2, "[]") == 0)
    return cg->i8ptr_type;
  if (strcmp(name, "int") == 0 || strcmp(name, "i64") == 0)
    return cg->i64_type;
  if (strcmp(name, "float") == 0 || strcmp(name, "f64") == 0 ||
      strcmp(name, "double") == 0)
    return cg->f64_type;
  if (strcmp(name, "bool") == 0)
    return cg->i1_type;
  if (strcmp(name, "string") == 0)
    return cg->i8ptr_type;
  if (strcmp(name, "void") == 0)
    return cg->void_type;
  /* Check registered struct types — return pointer to struct */
  for (int i = 0; i < cg->struct_count; i++) {
    if (strcmp(cg->structs[i].name, name) == 0)
      return LLVMPointerType(cg->structs[i].llvm_type, 0);
  }
  /* Default to i64 for unknown */
  return cg->i64_type;
}

/* Struct lookup helper */
static CgStructType *cg_find_struct(Codegen *cg, const char *name) {
  for (int i = 0; i < cg->struct_count; i++) {
    if (strcmp(cg->structs[i].name, name) == 0)
      return &cg->structs[i];
  }
  return NULL;
}

/* Register a struct type from an AST_STRUCT declaration */
static void cg_register_struct(Codegen *cg, AstNode *decl) {
  if (cg->struct_count >= CG_MAX_STRUCTS)
    return;
  const char *sname = decl->as.struct_decl.name;
  int fc = decl->as.struct_decl.fields.count;
  CgStructType *st = &cg->structs[cg->struct_count++];
  st->name = (char *)sname;
  st->field_count = fc;

  LLVMTypeRef *ftypes = calloc(fc, sizeof(LLVMTypeRef));
  for (int i = 0; i < fc; i++) {
    AstNode *f = decl->as.struct_decl.fields.items[i];
    st->field_names[i] = (f->type == AST_VAR_DECL) ? f->as.var_decl.name : "";
    LLVMTypeRef ft = cg->i64_type;
    if (f->type == AST_VAR_DECL && f->as.var_decl.type_info)
      ft = cg_type_from_name(cg, f->as.var_decl.type_info->name);
    st->field_types[i] = ft;
    ftypes[i] = ft;
  }

  st->llvm_type = LLVMStructCreateNamed(cg->ctx, sname);
  LLVMStructSetBody(st->llvm_type, ftypes, fc, false);
  free(ftypes);
}

static LLVMTypeRef cg_return_type(Codegen *cg, AstNode *proc) {
  if (proc->as.proc.return_type) {
    const char *rt = proc->as.proc.return_type->name;
    /* Check for arrays of structs: Person[] → ptr */
    int len = strlen(rt);
    if (len > 2 && rt[len - 1] == ']' && rt[len - 2] == '[') {
      return cg->i8ptr_type; /* array of structs is a pointer */
    }
    return cg_type_from_name(cg, rt);
  }
  return cg->void_type;
}

/* ======== Forward Declarations ======== */

static LLVMValueRef emit_expr(Codegen *cg, AstNode *node);
static void emit_stmt(Codegen *cg, AstNode *node);
static void emit_block(Codegen *cg, AstNode *node);
static void emit_profile_call(Codegen *cg, LLVMValueRef fn_profile, const char *name);
static void emit_profile_branch_call(Codegen *cg, const char *location, bool taken);
static void emit_profile_loop_call(Codegen *cg, const char *location,
                                   LLVMValueRef iterations);

/* ======== Context-Specific Compilation ======== */

/* Get LLVM code generation level based on context */
static LLVMCodeGenOptLevel cg_get_context_opt_level(Codegen *cg) {
  switch (cg->pragma_opts.context) {
    case CG_CONTEXT_CLI:
      return LLVMCodeGenLevelDefault;  /* O2 equivalent */
    case CG_CONTEXT_SERVER:
      return LLVMCodeGenLevelAggressive;  /* O3 equivalent */
    case CG_CONTEXT_EMBEDDED:
      return LLVMCodeGenLevelLess;  /* O1 for size */
    case CG_CONTEXT_WEB:
      return LLVMCodeGenLevelDefault;  /* O2 with size focus */
    case CG_CONTEXT_NOTEBOOK:
      return LLVMCodeGenLevelLess;  /* O1 for fast compile */
    default:
      return LLVMCodeGenLevelDefault;
  }
}

void codegen_set_syntax_mode(Codegen *cg, SyntaxProfile *profile) {
  if (!cg)
    return;

  if (cg->syntax_profile) {
    syntax_profile_free(cg->syntax_profile);
    cg->syntax_profile = NULL;
  }
  if (cg->ir_hints) {
    ir_hints_free(cg->ir_hints);
    cg->ir_hints = NULL;
  }

  cg->syntax_mode = CG_SYNTAX_MODE_DEFAULT;

  if (!profile)
    return;

  cg->syntax_profile = syntax_profile_clone(profile);
  cg->ir_hints = ir_hints_from_profile(profile);

  if (cg->ir_hints) {
    switch (cg->ir_hints->mode) {
    case IR_MODE_STREAM:
      cg->syntax_mode = CG_SYNTAX_MODE_PIPELINE;
      break;
    case IR_MODE_DATAFLOW:
      cg->syntax_mode = CG_SYNTAX_MODE_FUNCTIONAL;
      break;
    case IR_MODE_CONTROLFLOW:
      cg->syntax_mode = CG_SYNTAX_MODE_IMPERATIVE;
      break;
    case IR_MODE_DEFAULT:
    default:
      cg->syntax_mode = CG_SYNTAX_MODE_DEFAULT;
      break;
    }
  }

  OptStrategy *strategy = strategy_for_syntax(profile);
  if (!strategy)
    return;

  if (strategy->enable_parallel)
    cg->pragma_opts.enable_parallel = true;
  if (strategy->enable_vectorization || strategy->enable_simd)
    cg->pragma_opts.enable_vectorize = true;
  if (strategy->enable_inlining)
    cg->pragma_opts.enable_inline = true;
  if (strategy->enable_memoization)
    cg->pragma_opts.enable_memoize = true;

  opt_strategy_free(strategy);
}

void codegen_set_syntax_mode_from_pragma(Codegen *cg, const char *style) {
  if (!cg || !style)
    return;

  SyntaxProfile *profile = parse_syntax_pragma("style", style);
  if (!profile)
    return;

  codegen_set_syntax_mode(cg, profile);
  syntax_profile_free(profile);
}

/* Apply context-specific function attributes */
static void cg_apply_context_attrs(Codegen *cg, LLVMValueRef func) {
  CgPragmaOpts *opts = &cg->pragma_opts;
  unsigned kind;
  
  switch (opts->context) {
    case CG_CONTEXT_CLI:
      /*
       * CLI: Optimize startup, small binary
       * - Minimize static initialization
       * - Favor code size over speed
       * - Fast main() entry
       */
      kind = LLVMGetEnumAttributeKindForName("optsize", 7);
      LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
          LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      
      /* Avoid aggressive inlining to reduce startup overhead */
      if (!opts->mark_hot_path) {
        kind = LLVMGetEnumAttributeKindForName("noinline", 8);
        if (kind != 0) {
          /* Only hint, don't force noinline */
        }
      }
      break;
      
    case CG_CONTEXT_SERVER:
      /*
       * Server: Optimize throughput, allow larger binary
       * - Aggressive inlining
       * - Loop unrolling
       * - Cache optimization
       */
      if (opts->enable_inline && !opts->mark_cold_path) {
        kind = LLVMGetEnumAttributeKindForName("inlinehint", 10);
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
            LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      }
      
      /* Enable speculative execution hints */
      kind = LLVMGetEnumAttributeKindForName("speculatable", 12);
      if (kind != 0) {
        /* Only add to pure functions - skip for now as we can't detect purity */
      }
      
      /* Willreturn for better optimization */
      kind = LLVMGetEnumAttributeKindForName("willreturn", 10);
      if (kind != 0) {
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
            LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      }
      break;
      
    case CG_CONTEXT_EMBEDDED:
      /*
       * Embedded: Optimize size, energy
       * - Minimal code size
       * - Avoid floating point if possible
       * - Stack size optimization
       */
      kind = LLVMGetEnumAttributeKindForName("minsize", 7);
      LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
          LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      
      /* Prevent inlining to minimize code size */
      if (!opts->mark_hot_path) {
        kind = LLVMGetEnumAttributeKindForName("noinline", 8);
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
            LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      }
      
      /* nounwind for smaller exception handling tables */
      kind = LLVMGetEnumAttributeKindForName("nounwind", 8);
      LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
          LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      
      /* norecurse hint for stack analysis */
      kind = LLVMGetEnumAttributeKindForName("norecurse", 9);
      if (kind != 0) {
        /* Only add if we can verify no recursion - skip for safety */
      }
      break;
      
    case CG_CONTEXT_WEB:
      /*
       * Web: Optimize for WASM/size
       * - Small binary
       * - Fast startup
       * - Minimal runtime
       */
      kind = LLVMGetEnumAttributeKindForName("minsize", 7);
      LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
          LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      
      /* nounwind for WASM compatibility */
      kind = LLVMGetEnumAttributeKindForName("nounwind", 8);
      LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
          LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      
      /* Memory attributes for WASM safety */
      kind = LLVMGetEnumAttributeKindForName("nofree", 6);
      if (kind != 0) {
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
            LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      }
      break;
      
    case CG_CONTEXT_NOTEBOOK:
      /*
       * Notebook: Optimize for interactive use
       * - Fast incremental compilation
       * - Keep debug info
       * - REPL-friendly
       */
      /* No aggressive optimizations - prioritize compile speed */
      /* Keep debug info via separate mechanism */
      
      /* Willreturn for REPL safety */
      kind = LLVMGetEnumAttributeKindForName("willreturn", 10);
      if (kind != 0) {
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
            LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      }
      break;
      
    default:
      break;
  }
}

/* ======== Pragma → LLVM Attribute Helpers ======== */

/* Apply function-level pragma attributes to LLVM function */
static void cg_apply_pragma_attrs(Codegen *cg, LLVMValueRef func) {
  CgPragmaOpts *opts = &cg->pragma_opts;
  unsigned kind;
  
  /* Inline control */
  if (opts->enable_inline && opts->mark_hot_path) {
    /* #pragma inline:always or hot_path → alwaysinline */
    kind = LLVMGetEnumAttributeKindForName("alwaysinline", 12);
    LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
        LLVMCreateEnumAttribute(cg->ctx, kind, 0));
  } else if (!opts->enable_inline) {
    /* #pragma inline:never → noinline */
    kind = LLVMGetEnumAttributeKindForName("noinline", 8);
    LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
        LLVMCreateEnumAttribute(cg->ctx, kind, 0));
  }
  
  /* Optimization focus attributes */
  switch (opts->opt_focus) {
    case CG_OPT_SIZE:
    case CG_OPT_MEMORY:
      /* optimize:size/memory → optsize */
      kind = LLVMGetEnumAttributeKindForName("optsize", 7);
      LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
          LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      break;
    case CG_OPT_LATENCY:
    case CG_OPT_THROUGHPUT:
      /* optimize:latency/throughput → disable optsize, enable inlining */
      if (opts->enable_inline) {
        kind = LLVMGetEnumAttributeKindForName("inlinehint", 10);
        LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
            LLVMCreateEnumAttribute(cg->ctx, kind, 0));
      }
      break;
    default:
      break;
  }
  
  /* Cold path → cold attribute (deprioritizes optimization) */
  if (opts->mark_cold_path) {
    kind = LLVMGetEnumAttributeKindForName("cold", 4);
    LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
        LLVMCreateEnumAttribute(cg->ctx, kind, 0));
    /* Also set minsize for cold paths */
    kind = LLVMGetEnumAttributeKindForName("minsize", 7);
    LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
        LLVMCreateEnumAttribute(cg->ctx, kind, 0));
  }
  
  /* Hot path → hot attribute (enables aggressive optimization) */
  if (opts->mark_hot_path && !opts->mark_cold_path) {
    kind = LLVMGetEnumAttributeKindForName("hot", 3);
    if (kind != 0) {
      LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
          LLVMCreateEnumAttribute(cg->ctx, kind, 0));
    }
  }
  
  /* Context-based function attributes */
  cg_apply_context_attrs(cg, func);

  /* Bounds checking disabled → nosanitize (no UBSan instrumentation) */
  if (opts->disable_bounds) {
    /* Note: LLVM doesn't have a direct "disable bounds" attr, but we can add
     * attributes that affect sanitizer behavior. For now we use nosync/nofree 
     * which signals the function has no unusual semantics. */
    kind = LLVMGetEnumAttributeKindForName("nofree", 6);
    if (kind != 0) {
      LLVMAddAttributeAtIndex(func, LLVMAttributeFunctionIndex,
          LLVMCreateEnumAttribute(cg->ctx, kind, 0));
    }
  }
}

/* Create loop vectorization/unroll metadata */
static LLVMMetadataRef cg_create_loop_metadata(Codegen *cg, bool vectorize, bool unroll) {
  LLVMMetadataRef loop_md[4];
  unsigned count = 0;
  
  /* Self-reference placeholder (first element is the loop ID itself) */
  loop_md[count++] = NULL;  /* Will be replaced by self-ref */
  
  if (vectorize) {
    /* llvm.loop.vectorize.enable = true */
    LLVMMetadataRef key = LLVMMDStringInContext2(cg->ctx, "llvm.loop.vectorize.enable", 26);
    LLVMMetadataRef val = LLVMValueAsMetadata(LLVMConstInt(cg->i1_type, 1, false));
    LLVMMetadataRef pair[2] = {key, val};
    loop_md[count++] = LLVMMDNodeInContext2(cg->ctx, pair, 2);
  }
  
  if (unroll) {
    /* llvm.loop.unroll.enable = true */
    LLVMMetadataRef key = LLVMMDStringInContext2(cg->ctx, "llvm.loop.unroll.enable", 23);
    LLVMMetadataRef val = LLVMValueAsMetadata(LLVMConstInt(cg->i1_type, 1, false));
    LLVMMetadataRef pair[2] = {key, val};
    loop_md[count++] = LLVMMDNodeInContext2(cg->ctx, pair, 2);
  }
  
  if (count <= 1) return NULL;  /* No meaningful metadata to add */
  
  /* Create the loop metadata node */
  LLVMMetadataRef md = LLVMMDNodeInContext2(cg->ctx, loop_md, count);
  
  /* Replace self-reference (LLVM requires this for loop metadata) */
  /* Note: Due to C API limitations, we create a simple node that works */
  return md;
}

/* Apply loop-level pragma metadata to a branch instruction */
static void cg_apply_loop_pragmas(Codegen *cg, LLVMValueRef branch_inst) {
  if (!branch_inst) return;
  
  CgPragmaOpts *opts = &cg->pragma_opts;
  
  /* Create loop metadata if vectorize is enabled */
  if (opts->enable_vectorize) {
    LLVMMetadataRef loop_md = cg_create_loop_metadata(cg, true, false);
    if (loop_md) {
      unsigned loop_kind = LLVMGetMDKindIDInContext(cg->ctx, "llvm.loop", 9);
      LLVMValueRef md_val = LLVMMetadataAsValue(cg->ctx, loop_md);
      LLVMSetMetadata(branch_inst, loop_kind, md_val);
    }
  }
}

/* Add branch weight metadata for likely/unlikely hints */
static void cg_apply_branch_weights(Codegen *cg, LLVMValueRef branch_inst, bool likely_true) {
  if (!branch_inst) return;
  
  unsigned prof_kind = LLVMGetMDKindIDInContext(cg->ctx, "prof", 4);
  
  /* Create branch_weights metadata: !{!"branch_weights", i32 heavy, i32 light} */
  LLVMValueRef weights[3];
  weights[0] = LLVMMDString("branch_weights", 14);
  
  if (likely_true) {
    weights[1] = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 2000, false);
    weights[2] = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 1, false);
  } else {
    weights[1] = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 1, false);
    weights[2] = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 2000, false);
  }
  
  LLVMValueRef md = LLVMMDNode(weights, 3);
  LLVMSetMetadata(branch_inst, prof_kind, md);
}

/* ======== Expression Emission ======== */

static LLVMValueRef emit_int_literal(Codegen *cg, AstNode *node) {
  return LLVMConstInt(cg->i64_type,
                      (unsigned long long)node->as.int_literal.value, true);
}

static LLVMValueRef emit_float_literal(Codegen *cg, AstNode *node) {
  return LLVMConstReal(cg->f64_type, node->as.float_literal.value);
}

static LLVMValueRef emit_bool_literal(Codegen *cg, AstNode *node) {
  return LLVMConstInt(cg->i1_type, node->as.bool_literal.value ? 1 : 0, false);
}

static LLVMValueRef emit_string_literal(Codegen *cg, AstNode *node) {
  /* Create global string constant */
  return LLVMBuildGlobalStringPtr(cg->builder, node->as.string_literal.value,
                                  "str");
}

static LLVMValueRef emit_none_literal(Codegen *cg,
                                      AstNode *node __attribute__((unused))) {
  return LLVMConstInt(cg->i64_type, 0, false);
}

static LLVMValueRef emit_identifier(Codegen *cg, AstNode *node) {
  const char *name = node->as.identifier.name;

  /* Wildcard: _ is always 0 (discard) */
  if (strcmp(name, "_") == 0)
    return LLVMConstInt(cg->i64_type, 0, false);

  CgSymbol *sym = cg_lookup(cg, name);
  if (!sym) {
    /* Could be a function reference */
    LLVMValueRef fn = LLVMGetNamedFunction(cg->mod, name);
    if (fn)
      return fn;
    cg_error(cg, "Undefined variable: %s", name);
    return LLVMConstInt(cg->i64_type, 0, false);
  }
  return LLVMBuildLoad2(cg->builder, sym->type, sym->alloca, name);
}

static LLVMValueRef emit_binary(Codegen *cg, AstNode *node) {
  BinaryOp op = node->as.binary.op;

  /* Pipeline: a >> f(x) → f(a, x) — must intercept before generic eval */
  if (op == OP_PIPELINE) {
    LLVMValueRef left_val = emit_expr(cg, node->as.binary.left);
    if (cg->had_error)
      return left_val;

    AstNode *rhs = node->as.binary.right;
    if (rhs && rhs->type == AST_CALL) {
      /* Desugar: prepend left as first arg of the call */
      const char *fname = NULL;
      if (rhs->as.call.callee && rhs->as.call.callee->type == AST_IDENTIFIER)
        fname = rhs->as.call.callee->as.identifier.name;

      if (fname && strcmp(fname, "stream_take") == 0 &&
          rhs->as.call.args.count >= 1) {
        LLVMValueRef amount = emit_expr(cg, rhs->as.call.args.items[0]);
        LLVMValueRef fn_stream =
            LLVMGetNamedFunction(cg->mod, "__qisc_stream_take_i64");
        if (!fn_stream) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2,
              false);
          fn_stream =
              LLVMAddFunction(cg->mod, "__qisc_stream_take_i64", fn_type);
        }
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2,
            false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){left_val, amount}, 2,
                              "stream_take");
      }

      if (fname && strcmp(fname, "stream_skip") == 0 &&
          rhs->as.call.args.count >= 1) {
        LLVMValueRef amount = emit_expr(cg, rhs->as.call.args.items[0]);
        LLVMValueRef fn_stream =
            LLVMGetNamedFunction(cg->mod, "__qisc_stream_skip_i64");
        if (!fn_stream) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2,
              false);
          fn_stream =
              LLVMAddFunction(cg->mod, "__qisc_stream_skip_i64", fn_type);
        }
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2,
            false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){left_val, amount}, 2,
                              "stream_skip");
      }

      if (fname && strcmp(fname, "stream_map") == 0 &&
          rhs->as.call.args.count >= 1) {
        CgStreamKind source_kind = cg_expr_stream_kind(cg, node->as.binary.left);
        CgStreamKind target_kind = source_kind;
        CgStreamCallable mapper;
        if (rhs->as.call.args.count >= 1) {
          CgStreamKind inferred =
              cg_stream_kind_from_callable(cg, rhs->as.call.args.items[0],
                                           source_kind);
          if (inferred != CG_STREAM_NONE)
            target_kind = inferred;
        }
        LLVMTypeRef target_type =
            target_kind == CG_STREAM_STRING ? cg->i8ptr_type : cg->i64_type;
        mapper = cg_emit_stream_callable(
            cg, rhs->as.call.args.items[0], source_kind, target_type);
        const char *rt_name =
            cg_stream_map_runtime_name(source_kind, target_kind,
                                       mapper.uses_ctx);
        LLVMValueRef fn_stream = LLVMGetNamedFunction(cg->mod, rt_name);
        if (!fn_stream) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i8ptr_type,
              mapper.uses_ctx
                  ? (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type,
                                    cg->i8ptr_type}
                  : (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type},
              mapper.uses_ctx ? 3 : 2, false);
          fn_stream = LLVMAddFunction(cg->mod, rt_name, fn_type);
        }
        if (mapper.uses_ctx) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i8ptr_type,
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type, cg->i8ptr_type},
              3, false);
          return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                                (LLVMValueRef[]){left_val, mapper.fn, mapper.ctx},
                                3, "stream_map");
        }
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2,
            false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){left_val, mapper.fn}, 2,
                              "stream_map");
      }

      if (fname && strcmp(fname, "stream_filter") == 0 &&
          rhs->as.call.args.count >= 1) {
        CgStreamKind kind = cg_expr_stream_kind(cg, node->as.binary.left);
        CgStreamCallable pred = cg_emit_stream_callable(
            cg, rhs->as.call.args.items[0], kind, cg->i1_type);
        const char *rt_name = cg_stream_filter_runtime_name(kind, pred.uses_ctx);
        LLVMValueRef fn_stream = LLVMGetNamedFunction(cg->mod, rt_name);
        if (!fn_stream) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i8ptr_type,
              pred.uses_ctx
                  ? (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type,
                                    cg->i8ptr_type}
                  : (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type},
              pred.uses_ctx ? 3 : 2, false);
          fn_stream = LLVMAddFunction(cg->mod, rt_name, fn_type);
        }
        if (pred.uses_ctx) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i8ptr_type,
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type, cg->i8ptr_type},
              3, false);
          return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                                (LLVMValueRef[]){left_val, pred.fn, pred.ctx}, 3,
                                "stream_filter");
        }
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2,
            false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){left_val, pred.fn}, 2,
                              "stream_filter");
      }

      if (fname && strcmp(fname, "stream_count") == 0) {
        LLVMValueRef fn_stream =
            LLVMGetNamedFunction(cg->mod, "__qisc_stream_count_i64");
        if (!fn_stream) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          fn_stream = LLVMAddFunction(cg->mod, "__qisc_stream_count_i64", fn_type);
        }
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){left_val}, 1, "stream_count");
      }

      if (fname && strcmp(fname, "stream_first") == 0) {
        CgStreamKind kind = cg_expr_stream_kind(cg, node->as.binary.left);
        if (kind == CG_STREAM_STRING) {
          LLVMValueRef fn_stream =
              LLVMGetNamedFunction(cg->mod, "__qisc_stream_first_string");
          if (!fn_stream) {
            LLVMTypeRef fn_type = LLVMFunctionType(
                cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
            fn_stream =
                LLVMAddFunction(cg->mod, "__qisc_stream_first_string", fn_type);
          }
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                                (LLVMValueRef[]){left_val}, 1, "stream_first");
        }

        LLVMValueRef fn_stream =
            LLVMGetNamedFunction(cg->mod, "__qisc_stream_first_i64");
        if (!fn_stream) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          fn_stream = LLVMAddFunction(cg->mod, "__qisc_stream_first_i64", fn_type);
        }
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){left_val}, 1, "stream_first");
      }

      if (fname && strcmp(fname, "stream_collect") == 0) {
        CgStreamKind kind = cg_expr_stream_kind(cg, node->as.binary.left);
        const char *rt_name = kind == CG_STREAM_STRING
                                  ? "__qisc_stream_collect_strings"
                                  : "__qisc_stream_collect_i64";
        LLVMValueRef fn_stream = LLVMGetNamedFunction(cg->mod, rt_name);
        if (!fn_stream) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          fn_stream = LLVMAddFunction(cg->mod, rt_name, fn_type);
        }
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){left_val}, 1, "stream_collect");
      }

      if (fname && strcmp(fname, "stream_reduce") == 0 &&
          rhs->as.call.args.count >= 2) {
        AstNode *fn_arg = rhs->as.call.args.items[0];
        AstNode *init_arg = rhs->as.call.args.items[1];
        if (!cg_ast_is_probably_callable(cg, fn_arg) &&
            cg_ast_is_probably_callable(cg, init_arg)) {
          fn_arg = rhs->as.call.args.items[1];
          init_arg = rhs->as.call.args.items[0];
        }
        CgStreamKind kind = cg_expr_stream_kind(cg, node->as.binary.left);
        if (kind != CG_STREAM_INT && kind != CG_STREAM_STRING) {
          cg_error(cg,
                   "stream_reduce currently supports int and string streams only");
          return LLVMConstInt(cg->i64_type, 0, false);
        }
        CgStreamCallable reducer =
            cg_emit_stream_reduce_callable(cg, fn_arg, kind);
        LLVMValueRef initial = emit_expr(cg, init_arg);
        initial = cg_coerce_value(cg, initial, cg->i64_type);
        const char *rt_name =
            cg_stream_reduce_runtime_name(kind, reducer.uses_ctx);
        LLVMValueRef fn_stream = LLVMGetNamedFunction(cg->mod, rt_name);
        if (!fn_stream) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i64_type,
              reducer.uses_ctx
                  ? (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type,
                                    cg->i8ptr_type, cg->i64_type}
                  : (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type,
                                    cg->i64_type},
              reducer.uses_ctx ? 4 : 3, false);
          fn_stream = LLVMAddFunction(cg->mod, rt_name, fn_type);
        }
        if (reducer.uses_ctx) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i64_type,
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type, cg->i8ptr_type,
                              cg->i64_type},
              4, false);
          return LLVMBuildCall2(
              cg->builder, fn_type, fn_stream,
              (LLVMValueRef[]){left_val, reducer.fn, reducer.ctx, initial}, 4,
              "stream_reduce");
        }
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i64_type,
            (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type, cg->i64_type}, 3,
            false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){left_val, reducer.fn, initial},
                              3, "stream_reduce");
      }

      /* Check if this is a builtin that takes array as first arg */
      if (fname && (strcmp(fname, "filter") == 0 || strcmp(fname, "map") == 0 || 
                    strcmp(fname, "reduce") == 0 || strcmp(fname, "collect") == 0)) {
        /* Create a modified call node with left_val prepended */
        /* For builtins, we need to handle them specially */
        
        if (strcmp(fname, "filter") == 0 && rhs->as.call.args.count >= 1) {
          /* filter(predicate) with left_val as array */
          AstNode *pred_node = rhs->as.call.args.items[0];
          bool pred_is_closure = cg_expr_is_closure_like(cg, pred_node);
          LLVMValueRef pred_fn = LLVMConstNull(cg->i8ptr_type);
          if (pred_is_closure) {
            if (pred_node->type == AST_LAMBDA) {
              LLVMTypeRef param_types[1] = {cg->i64_type};
              pred_fn = cg_emit_general_closure_value(cg, pred_node, param_types,
                                                      1, cg->i64_type);
            } else {
              pred_fn = emit_expr(cg, pred_node);
            }
          } else {
            pred_fn = emit_expr(cg, pred_node);
          }
          
          /* Get array length */
          LLVMValueRef fn_array_len = LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
          LLVMTypeRef arr_len_type = LLVMFunctionType(cg->i64_type,
              (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          LLVMValueRef len = LLVMBuildCall2(cg->builder, arr_len_type, fn_array_len,
              (LLVMValueRef[]){left_val}, 1, "filter_len");
          
          /* Create result array */
          LLVMValueRef fn_array_new = LLVMGetNamedFunction(cg->mod, "__qisc_array_new");
          LLVMTypeRef arr_new_type = LLVMFunctionType(cg->i8ptr_type,
              (LLVMTypeRef[]){cg->i64_type, cg->i64_type}, 2, false);
          LLVMValueRef elem_size = LLVMConstInt(cg->i64_type, 8, false);
          LLVMValueRef result_arr = LLVMBuildCall2(cg->builder, arr_new_type, fn_array_new,
              (LLVMValueRef[]){elem_size, len}, 2, "filter_result");
          
          /* Loop through and filter */
          LLVMValueRef idx_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "filter_idx");
          LLVMValueRef res_alloca = LLVMBuildAlloca(cg->builder, cg->i8ptr_type, "filter_res");
          LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false), idx_alloca);
          LLVMBuildStore(cg->builder, result_arr, res_alloca);
          
          LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "pfilter.loop");
          LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "pfilter.body");
          LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "pfilter.then");
          LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "pfilter.cont");
          LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "pfilter.end");
          
          LLVMBuildBr(cg->builder, loop_bb);
          LLVMPositionBuilderAtEnd(cg->builder, loop_bb);
          
          LLVMValueRef idx = LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "i");
          LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, idx, len, "filter_cond");
          LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
          
          LLVMPositionBuilderAtEnd(cg->builder, body_bb);
          
          /* Get element at index */
          LLVMValueRef fn_array_get = LLVMGetNamedFunction(cg->mod, "__qisc_array_get");
          LLVMTypeRef arr_get_type = LLVMFunctionType(cg->i8ptr_type,
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2, false);
          LLVMValueRef elem_ptr = LLVMBuildCall2(cg->builder, arr_get_type, fn_array_get,
              (LLVMValueRef[]){left_val, idx}, 2, "elem_ptr");
          LLVMValueRef elem = LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "elem");
          
          /* Call predicate(elem) */
          LLVMValueRef pred_result;
          if (pred_is_closure) {
            LLVMTypeRef arg_types[1] = {cg->i64_type};
            LLVMValueRef arg_values[1] = {elem};
            pred_result = cg_emit_closure_call(cg, pred_fn, arg_types, arg_values,
                                               1, cg->i64_type, "pred_result");
          } else {
            LLVMTypeRef pred_fn_type = LLVMFunctionType(cg->i64_type,
                (LLVMTypeRef[]){cg->i64_type}, 1, false);
            pred_result = LLVMBuildCall2(cg->builder, pred_fn_type, pred_fn,
                (LLVMValueRef[]){elem}, 1, "pred_result");
          }
          
          LLVMValueRef is_truthy = LLVMBuildICmp(cg->builder, LLVMIntNE, pred_result,
              LLVMConstInt(cg->i64_type, 0, false), "is_truthy");
          LLVMBuildCondBr(cg->builder, is_truthy, then_bb, cont_bb);
          
          LLVMPositionBuilderAtEnd(cg->builder, then_bb);
          
          LLVMValueRef val_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "filter_val");
          LLVMBuildStore(cg->builder, elem, val_alloca);
          
          LLVMValueRef fn_array_push = LLVMGetNamedFunction(cg->mod, "__qisc_array_push");
          LLVMTypeRef arr_push_type = LLVMFunctionType(cg->i8ptr_type,
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
          LLVMValueRef cur_res = LLVMBuildLoad2(cg->builder, cg->i8ptr_type, res_alloca, "cur_res");
          LLVMValueRef new_res = LLVMBuildCall2(cg->builder, arr_push_type, fn_array_push,
              (LLVMValueRef[]){cur_res, val_alloca}, 2, "pushed");
          LLVMBuildStore(cg->builder, new_res, res_alloca);
          LLVMBuildBr(cg->builder, cont_bb);
          
          LLVMPositionBuilderAtEnd(cg->builder, cont_bb);
          LLVMValueRef next_idx = LLVMBuildAdd(cg->builder, idx, LLVMConstInt(cg->i64_type, 1, false), "next");
          LLVMBuildStore(cg->builder, next_idx, idx_alloca);
          LLVMBuildBr(cg->builder, loop_bb);
          
          LLVMPositionBuilderAtEnd(cg->builder, end_bb);
          return LLVMBuildLoad2(cg->builder, cg->i8ptr_type, res_alloca, "filter_final");
        }
        
        if (strcmp(fname, "map") == 0 && rhs->as.call.args.count >= 1) {
          /* map(fn) with left_val as array */
          AstNode *map_node = rhs->as.call.args.items[0];
          bool map_is_closure = cg_expr_is_closure_like(cg, map_node);
          LLVMValueRef map_fn = LLVMConstNull(cg->i8ptr_type);
          if (map_is_closure) {
            if (map_node->type == AST_LAMBDA) {
              LLVMTypeRef param_types[1] = {cg->i64_type};
              map_fn = cg_emit_general_closure_value(cg, map_node, param_types, 1,
                                                     cg->i64_type);
            } else {
              map_fn = emit_expr(cg, map_node);
            }
          } else {
            map_fn = emit_expr(cg, map_node);
          }
          
          LLVMValueRef fn_array_len = LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
          LLVMTypeRef arr_len_type = LLVMFunctionType(cg->i64_type,
              (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          LLVMValueRef len = LLVMBuildCall2(cg->builder, arr_len_type, fn_array_len,
              (LLVMValueRef[]){left_val}, 1, "map_len");
          
          LLVMValueRef fn_array_new = LLVMGetNamedFunction(cg->mod, "__qisc_array_new");
          LLVMTypeRef arr_new_type = LLVMFunctionType(cg->i8ptr_type,
              (LLVMTypeRef[]){cg->i64_type, cg->i64_type}, 2, false);
          LLVMValueRef elem_size = LLVMConstInt(cg->i64_type, 8, false);
          LLVMValueRef result_arr = LLVMBuildCall2(cg->builder, arr_new_type, fn_array_new,
              (LLVMValueRef[]){elem_size, len}, 2, "map_result");
          
          LLVMValueRef idx_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "map_idx");
          LLVMValueRef res_alloca = LLVMBuildAlloca(cg->builder, cg->i8ptr_type, "map_res");
          LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false), idx_alloca);
          LLVMBuildStore(cg->builder, result_arr, res_alloca);
          
          LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "pmap.loop");
          LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "pmap.body");
          LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "pmap.end");
          
          LLVMBuildBr(cg->builder, loop_bb);
          LLVMPositionBuilderAtEnd(cg->builder, loop_bb);
          
          LLVMValueRef idx = LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "i");
          LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, idx, len, "map_cond");
          LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
          
          LLVMPositionBuilderAtEnd(cg->builder, body_bb);
          
          LLVMValueRef fn_array_get = LLVMGetNamedFunction(cg->mod, "__qisc_array_get");
          LLVMTypeRef arr_get_type = LLVMFunctionType(cg->i8ptr_type,
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2, false);
          LLVMValueRef elem_ptr = LLVMBuildCall2(cg->builder, arr_get_type, fn_array_get,
              (LLVMValueRef[]){left_val, idx}, 2, "elem_ptr");
          LLVMValueRef elem = LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "elem");
          
          LLVMValueRef mapped;
          if (map_is_closure) {
            LLVMTypeRef arg_types[1] = {cg->i64_type};
            LLVMValueRef arg_values[1] = {elem};
            mapped = cg_emit_closure_call(cg, map_fn, arg_types, arg_values, 1,
                                          cg->i64_type, "mapped");
          } else {
            LLVMTypeRef map_fn_type = LLVMFunctionType(cg->i64_type,
                (LLVMTypeRef[]){cg->i64_type}, 1, false);
            mapped = LLVMBuildCall2(cg->builder, map_fn_type, map_fn,
                (LLVMValueRef[]){elem}, 1, "mapped");
          }
          
          LLVMValueRef val_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "map_val");
          LLVMBuildStore(cg->builder, mapped, val_alloca);
          
          LLVMValueRef fn_array_push = LLVMGetNamedFunction(cg->mod, "__qisc_array_push");
          LLVMTypeRef arr_push_type = LLVMFunctionType(cg->i8ptr_type,
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
          LLVMValueRef cur_res = LLVMBuildLoad2(cg->builder, cg->i8ptr_type, res_alloca, "cur_res");
          LLVMValueRef new_res = LLVMBuildCall2(cg->builder, arr_push_type, fn_array_push,
              (LLVMValueRef[]){cur_res, val_alloca}, 2, "pushed");
          LLVMBuildStore(cg->builder, new_res, res_alloca);
          
          LLVMValueRef next_idx = LLVMBuildAdd(cg->builder, idx, LLVMConstInt(cg->i64_type, 1, false), "next");
          LLVMBuildStore(cg->builder, next_idx, idx_alloca);
          LLVMBuildBr(cg->builder, loop_bb);
          
          LLVMPositionBuilderAtEnd(cg->builder, end_bb);
          return LLVMBuildLoad2(cg->builder, cg->i8ptr_type, res_alloca, "map_final");
        }
        
        if (strcmp(fname, "collect") == 0) {
          /* collect() just returns the array as-is (materializes lazy stream) */
          return left_val;
        }
        
        if (strcmp(fname, "reduce") == 0 && rhs->as.call.args.count >= 2) {
          /* reduce(fn, initial) with left_val as array */
          AstNode *reduce_node = rhs->as.call.args.items[0];
          bool reduce_is_closure = cg_expr_is_closure_like(cg, reduce_node);
          LLVMValueRef reduce_fn = LLVMConstNull(cg->i8ptr_type);
          if (reduce_is_closure) {
            if (reduce_node->type == AST_LAMBDA) {
              LLVMTypeRef param_types[2] = {cg->i64_type, cg->i64_type};
              reduce_fn = cg_emit_general_closure_value(cg, reduce_node,
                                                        param_types, 2,
                                                        cg->i64_type);
            } else {
              reduce_fn = emit_expr(cg, reduce_node);
            }
          } else {
            reduce_fn = emit_expr(cg, reduce_node);
          }
          LLVMValueRef initial = emit_expr(cg, rhs->as.call.args.items[1]);
          
          LLVMValueRef fn_array_len = LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
          LLVMTypeRef arr_len_type = LLVMFunctionType(cg->i64_type,
              (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          LLVMValueRef len = LLVMBuildCall2(cg->builder, arr_len_type, fn_array_len,
              (LLVMValueRef[]){left_val}, 1, "reduce_len");
          
          LLVMValueRef idx_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "reduce_idx");
          LLVMValueRef acc_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "reduce_acc");
          LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false), idx_alloca);
          LLVMBuildStore(cg->builder, initial, acc_alloca);
          
          LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "preduce.loop");
          LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "preduce.body");
          LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "preduce.end");
          
          LLVMBuildBr(cg->builder, loop_bb);
          LLVMPositionBuilderAtEnd(cg->builder, loop_bb);
          
          LLVMValueRef idx = LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "i");
          LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, idx, len, "reduce_cond");
          LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
          
          LLVMPositionBuilderAtEnd(cg->builder, body_bb);
          
          LLVMValueRef fn_array_get = LLVMGetNamedFunction(cg->mod, "__qisc_array_get");
          LLVMTypeRef arr_get_type = LLVMFunctionType(cg->i8ptr_type,
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2, false);
          LLVMValueRef elem_ptr = LLVMBuildCall2(cg->builder, arr_get_type, fn_array_get,
              (LLVMValueRef[]){left_val, idx}, 2, "elem_ptr");
          LLVMValueRef elem = LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "elem");
          
          LLVMValueRef acc = LLVMBuildLoad2(cg->builder, cg->i64_type, acc_alloca, "acc");
          
          LLVMValueRef new_acc;
          if (reduce_is_closure) {
            LLVMTypeRef arg_types[2] = {cg->i64_type, cg->i64_type};
            LLVMValueRef arg_values[2] = {acc, elem};
            new_acc = cg_emit_closure_call(cg, reduce_fn, arg_types, arg_values, 2,
                                           cg->i64_type, "new_acc");
          } else {
            LLVMTypeRef reduce_fn_type = LLVMFunctionType(cg->i64_type,
                (LLVMTypeRef[]){cg->i64_type, cg->i64_type}, 2, false);
            new_acc = LLVMBuildCall2(cg->builder, reduce_fn_type, reduce_fn,
                (LLVMValueRef[]){acc, elem}, 2, "new_acc");
          }
          
          LLVMBuildStore(cg->builder, new_acc, acc_alloca);
          
          LLVMValueRef next_idx = LLVMBuildAdd(cg->builder, idx, LLVMConstInt(cg->i64_type, 1, false), "next");
          LLVMBuildStore(cg->builder, next_idx, idx_alloca);
          LLVMBuildBr(cg->builder, loop_bb);
          
          LLVMPositionBuilderAtEnd(cg->builder, end_bb);
          return LLVMBuildLoad2(cg->builder, cg->i64_type, acc_alloca, "reduce_final");
        }
      }

      int orig_argc = rhs->as.call.args.count;
      int new_argc = 1 + orig_argc;
      LLVMValueRef *args = calloc(new_argc, sizeof(LLVMValueRef));
      args[0] = left_val;
      for (int i = 0; i < orig_argc; i++)
        args[i + 1] = emit_expr(cg, rhs->as.call.args.items[i]);

      /* Look up function */
      LLVMValueRef fn = fname ? LLVMGetNamedFunction(cg->mod, fname) : NULL;
      if (fn) {
        LLVMTypeRef fn_type = LLVMGlobalGetValueType(fn);
        LLVMTypeRef ret_type = LLVMGetReturnType(fn_type);
        const char *call_name = (ret_type == cg->void_type) ? "" : "pipe";
        LLVMValueRef result =
            LLVMBuildCall2(cg->builder, fn_type, fn, args, new_argc, call_name);
        free(args);
        return result;
      }

      /* Maybe a lambda variable */
      if (fname) {
        CgSymbol *sym = cg_lookup(cg, fname);
        if (sym) {
          bool is_closure = false;
          char marker_name[300];
          snprintf(marker_name, sizeof(marker_name), "__%s__closure", fname);
          is_closure = cg_lookup(cg, marker_name) != NULL;
          if (sym->is_callable) {
            LLVMValueRef callable =
                LLVMBuildLoad2(cg->builder, sym->type, sym->alloca,
                               is_closure ? "pipe_closure" : "pipe_fn");
            LLVMValueRef result = cg_emit_symbol_callable_call(
                cg, sym, callable, args, new_argc, is_closure, "pipe");
            free(args);
            return result;
          }
          LLVMValueRef fn_ptr =
              LLVMBuildLoad2(cg->builder, sym->type, sym->alloca, "pipe_fn");
          LLVMTypeRef *ptypes = calloc(new_argc, sizeof(LLVMTypeRef));
          for (int i = 0; i < new_argc; i++)
            ptypes[i] = cg->i64_type;
          LLVMTypeRef ft =
              LLVMFunctionType(cg->i64_type, ptypes, new_argc, false);
          free(ptypes);
          LLVMValueRef result =
              LLVMBuildCall2(cg->builder, ft, fn_ptr, args, new_argc, "pipe");
          free(args);
          return result;
        }
      }

      free(args);
      cg_error(cg, "Pipeline: undefined function: %s",
               fname ? fname : "<expr>");
      return left_val;
    }

    /* If right side is not a call, just return left (identity pipe) */
    return left_val;
  }

  LLVMValueRef left = emit_expr(cg, node->as.binary.left);
  LLVMValueRef right = emit_expr(cg, node->as.binary.right);

  if (cg->had_error)
    return left;

  LLVMTypeRef lt = LLVMTypeOf(left);
  LLVMTypeRef rt = LLVMTypeOf(right);

  /* String concatenation: ptr + ptr → malloc + sprintf */
  if (op == OP_ADD && lt == cg->i8ptr_type && rt == cg->i8ptr_type) {
    /* Declare strlen if needed */
    LLVMValueRef fn_strlen = LLVMGetNamedFunction(cg->mod, "strlen");
    if (!fn_strlen) {
      LLVMTypeRef strlen_t = LLVMFunctionType(
          cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      fn_strlen = LLVMAddFunction(cg->mod, "strlen", strlen_t);
    }
    /* Declare malloc if needed */
    LLVMValueRef fn_malloc = LLVMGetNamedFunction(cg->mod, "malloc");
    if (!fn_malloc) {
      LLVMTypeRef malloc_t = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
      fn_malloc = LLVMAddFunction(cg->mod, "malloc", malloc_t);
    }
    /* Declare sprintf if needed */
    LLVMValueRef fn_sprintf = LLVMGetNamedFunction(cg->mod, "sprintf");
    if (!fn_sprintf) {
      LLVMTypeRef sprintf_t = LLVMFunctionType(
          LLVMInt32TypeInContext(cg->ctx),
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, true);
      fn_sprintf = LLVMAddFunction(cg->mod, "sprintf", sprintf_t);
    }

    /* len = strlen(left) + strlen(right) + 1 */
    LLVMTypeRef strlen_t = LLVMFunctionType(
        cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
    LLVMValueRef len_l = LLVMBuildCall2(cg->builder, strlen_t, fn_strlen,
                                        (LLVMValueRef[]){left}, 1, "len_l");
    LLVMValueRef len_r = LLVMBuildCall2(cg->builder, strlen_t, fn_strlen,
                                        (LLVMValueRef[]){right}, 1, "len_r");
    LLVMValueRef total = LLVMBuildAdd(cg->builder, len_l, len_r, "total");
    total = LLVMBuildAdd(cg->builder, total,
                         LLVMConstInt(cg->i64_type, 1, false), "total1");

    /* buf = malloc(total) */
    LLVMTypeRef malloc_t = LLVMFunctionType(
        cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
    LLVMValueRef buf = LLVMBuildCall2(cg->builder, malloc_t, fn_malloc,
                                      (LLVMValueRef[]){total}, 1, "concat_buf");

    /* sprintf(buf, "%s%s", left, right) */
    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(cg->builder, "%s%s", "cat_fmt");
    LLVMTypeRef sprintf_t = LLVMFunctionType(
        LLVMInt32TypeInContext(cg->ctx),
        (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, true);
    LLVMBuildCall2(cg->builder, sprintf_t, fn_sprintf,
                   (LLVMValueRef[]){buf, fmt, left, right}, 4, "");

    return buf;
  }

  /* Float promotion: if either is float, promote both */
  bool is_float = (lt == cg->f64_type || rt == cg->f64_type);
  if (is_float) {
    if (lt != cg->f64_type)
      left = LLVMBuildSIToFP(cg->builder, left, cg->f64_type, "promo_l");
    if (rt != cg->f64_type)
      right = LLVMBuildSIToFP(cg->builder, right, cg->f64_type, "promo_r");
  }

  /* Boolean promotion for logical ops */
  if (lt == cg->i1_type && op != OP_AND && op != OP_OR && op != OP_EQ &&
      op != OP_NE) {
    left = LLVMBuildZExt(cg->builder, left, cg->i64_type, "ext_l");
    lt = cg->i64_type;
  }
  if (rt == cg->i1_type && op != OP_AND && op != OP_OR && op != OP_EQ &&
      op != OP_NE) {
    right = LLVMBuildZExt(cg->builder, right, cg->i64_type, "ext_r");
    rt = cg->i64_type;
  }

  switch (op) {
  case OP_ADD:
    return is_float ? LLVMBuildFAdd(cg->builder, left, right, "fadd")
                    : LLVMBuildAdd(cg->builder, left, right, "add");
  case OP_SUB:
    return is_float ? LLVMBuildFSub(cg->builder, left, right, "fsub")
                    : LLVMBuildSub(cg->builder, left, right, "sub");
  case OP_MUL:
    return is_float ? LLVMBuildFMul(cg->builder, left, right, "fmul")
                    : LLVMBuildMul(cg->builder, left, right, "mul");
  case OP_DIV:
    return is_float ? LLVMBuildFDiv(cg->builder, left, right, "fdiv")
                    : LLVMBuildSDiv(cg->builder, left, right, "div");
  case OP_MOD:
    return is_float ? LLVMBuildFRem(cg->builder, left, right, "fmod")
                    : LLVMBuildSRem(cg->builder, left, right, "mod");
  case OP_EQ:
    return is_float
               ? LLVMBuildFCmp(cg->builder, LLVMRealOEQ, left, right, "feq")
               : LLVMBuildICmp(cg->builder, LLVMIntEQ, left, right, "eq");
  case OP_NE:
    return is_float
               ? LLVMBuildFCmp(cg->builder, LLVMRealONE, left, right, "fne")
               : LLVMBuildICmp(cg->builder, LLVMIntNE, left, right, "ne");
  case OP_LT:
    return is_float
               ? LLVMBuildFCmp(cg->builder, LLVMRealOLT, left, right, "flt")
               : LLVMBuildICmp(cg->builder, LLVMIntSLT, left, right, "lt");
  case OP_GT:
    return is_float
               ? LLVMBuildFCmp(cg->builder, LLVMRealOGT, left, right, "fgt")
               : LLVMBuildICmp(cg->builder, LLVMIntSGT, left, right, "gt");
  case OP_LE:
    return is_float
               ? LLVMBuildFCmp(cg->builder, LLVMRealOLE, left, right, "fle")
               : LLVMBuildICmp(cg->builder, LLVMIntSLE, left, right, "le");
  case OP_GE:
    return is_float
               ? LLVMBuildFCmp(cg->builder, LLVMRealOGE, left, right, "fge")
               : LLVMBuildICmp(cg->builder, LLVMIntSGE, left, right, "ge");
  case OP_AND:
    return LLVMBuildAnd(cg->builder, left, right, "and");
  case OP_OR:
    return LLVMBuildOr(cg->builder, left, right, "or");
  case OP_BIT_AND:
    return LLVMBuildAnd(cg->builder, left, right, "band");
  case OP_BIT_OR:
    return LLVMBuildOr(cg->builder, left, right, "bor");
  case OP_BIT_XOR:
    return LLVMBuildXor(cg->builder, left, right, "bxor");
  case OP_LSHIFT:
    return LLVMBuildShl(cg->builder, left, right, "shl");
  case OP_RSHIFT:
    return LLVMBuildAShr(cg->builder, left, right, "shr");
  case OP_HAS:
    /* 'has _' checks if maybe value is not null/none */
    /* For now: treat as non-zero check */
    return LLVMBuildICmp(cg->builder, LLVMIntNE, left, 
        LLVMConstInt(LLVMTypeOf(left), 0, false), "has");
  default:
    cg_error(cg, "Unsupported binary op: %d", op);
    return LLVMConstInt(cg->i64_type, 0, false);
  }
}

static LLVMValueRef emit_unary(Codegen *cg, AstNode *node) {
  LLVMValueRef operand = emit_expr(cg, node->as.unary.operand);
  if (cg->had_error)
    return operand;

  switch (node->as.unary.op) {
  case OP_NEG:
    if (LLVMTypeOf(operand) == cg->f64_type)
      return LLVMBuildFNeg(cg->builder, operand, "fneg");
    return LLVMBuildNeg(cg->builder, operand, "neg");
  case OP_NOT:
    return LLVMBuildNot(cg->builder, operand, "not");
  case OP_BIT_NOT:
    return LLVMBuildNot(cg->builder, operand, "bnot");
  case OP_INC:
  case OP_DEC: {
    /* Postfix ++ / -- : operand must be an identifier */
    if (node->as.unary.operand->type != AST_IDENTIFIER) {
      cg_error(cg, "Inc/dec requires lvalue");
      return operand;
    }
    const char *vname = node->as.unary.operand->as.identifier.name;
    CgSymbol *sym = cg_lookup(cg, vname);
    if (!sym) {
      cg_error(cg, "Undefined variable for inc/dec: %s", vname);
      return operand;
    }
    LLVMValueRef old_val = operand; /* already loaded */
    LLVMValueRef one;
    LLVMValueRef new_val;
    if (sym->type == cg->f64_type) {
      one = LLVMConstReal(cg->f64_type, 1.0);
      new_val = (node->as.unary.op == OP_INC)
                    ? LLVMBuildFAdd(cg->builder, old_val, one, "finc")
                    : LLVMBuildFSub(cg->builder, old_val, one, "fdec");
    } else {
      one = LLVMConstInt(cg->i64_type, 1, false);
      new_val = (node->as.unary.op == OP_INC)
                    ? LLVMBuildAdd(cg->builder, old_val, one, "inc")
                    : LLVMBuildSub(cg->builder, old_val, one, "dec");
    }
    LLVMBuildStore(cg->builder, new_val, sym->alloca);
    return old_val; /* postfix: return old value */
  }
  default:
    cg_error(cg, "Unsupported unary op: %d", node->as.unary.op);
    return operand;
  }
}

/* Emit a call to printf */
static void emit_print_call(Codegen *cg, LLVMValueRef value) {
  LLVMTypeRef vtype = LLVMTypeOf(value);

  if (vtype == cg->i64_type) {
    LLVMValueRef fmt =
        LLVMBuildGlobalStringPtr(cg->builder, "%lld\n", "fmt_int");
    LLVMTypeRef printf_type =
        LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx),
                         (LLVMTypeRef[]){cg->i8ptr_type}, 1, true);
    LLVMValueRef args[] = {fmt, value};
    LLVMBuildCall2(cg->builder, printf_type, cg->fn_printf, args, 2, "");
  } else if (vtype == cg->f64_type) {
    LLVMValueRef fmt =
        LLVMBuildGlobalStringPtr(cg->builder, "%f\n", "fmt_float");
    LLVMTypeRef printf_type =
        LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx),
                         (LLVMTypeRef[]){cg->i8ptr_type}, 1, true);
    LLVMValueRef args[] = {fmt, value};
    LLVMBuildCall2(cg->builder, printf_type, cg->fn_printf, args, 2, "");
  } else if (vtype == cg->i1_type) {
    /* Bool: convert to "true"/"false" string */
    LLVMValueRef t = LLVMBuildGlobalStringPtr(cg->builder, "true\n", "s_true");
    LLVMValueRef f =
        LLVMBuildGlobalStringPtr(cg->builder, "false\n", "s_false");
    LLVMValueRef sel = LLVMBuildSelect(cg->builder, value, t, f, "boolstr");
    LLVMTypeRef printf_type =
        LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx),
                         (LLVMTypeRef[]){cg->i8ptr_type}, 1, true);
    LLVMValueRef args[] = {sel};
    LLVMBuildCall2(cg->builder, printf_type, cg->fn_printf, args, 1, "");
  } else if (vtype == cg->i8ptr_type) {
    /* String: print with %s\n */
    LLVMValueRef fmt = LLVMBuildGlobalStringPtr(cg->builder, "%s\n", "fmt_str");
    LLVMTypeRef printf_type =
        LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx),
                         (LLVMTypeRef[]){cg->i8ptr_type}, 1, true);
    LLVMValueRef args[] = {fmt, value};
    LLVMBuildCall2(cg->builder, printf_type, cg->fn_printf, args, 2, "");
  } else {
    /* Fallback: print as int */
    LLVMValueRef fmt =
        LLVMBuildGlobalStringPtr(cg->builder, "%lld\n", "fmt_def");
    LLVMTypeRef printf_type =
        LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx),
                         (LLVMTypeRef[]){cg->i8ptr_type}, 1, true);
    LLVMValueRef args[] = {fmt, value};
    LLVMBuildCall2(cg->builder, printf_type, cg->fn_printf, args, 2, "");
  }
}

/* Emit str() builtin — converts int/float to string via sprintf */
static LLVMValueRef emit_str_call(Codegen *cg, LLVMValueRef value) {
  /* If already a string (pointer), return as-is */
  if (LLVMTypeOf(value) == cg->i8ptr_type)
    return value;
  /* Return a heap-backed string so the result can safely escape the caller. */
  LLVMValueRef fn_malloc = LLVMGetNamedFunction(cg->mod, "malloc");
  if (!fn_malloc) {
    LLVMTypeRef malloc_type = LLVMFunctionType(
        cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
    fn_malloc = LLVMAddFunction(cg->mod, "malloc", malloc_type);
  }
  LLVMTypeRef malloc_type = LLVMFunctionType(
      cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
  LLVMValueRef buf = LLVMBuildCall2(
      cg->builder, malloc_type, fn_malloc,
      (LLVMValueRef[]){LLVMConstInt(cg->i64_type, 64, false)}, 1, "strbuf");

  LLVMTypeRef vtype = LLVMTypeOf(value);
  LLVMValueRef fmt;
  if (vtype == cg->f64_type) {
    fmt = LLVMBuildGlobalStringPtr(cg->builder, "%f", "sfmt_f");
  } else {
    fmt = LLVMBuildGlobalStringPtr(cg->builder, "%lld", "sfmt_i");
  }

  /* Call sprintf(buf, fmt, value) */
  LLVMValueRef fn_sprintf = LLVMGetNamedFunction(cg->mod, "sprintf");
  if (!fn_sprintf) {
    LLVMTypeRef sprintf_type = LLVMFunctionType(
        LLVMInt32TypeInContext(cg->ctx),
        (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, true);
    fn_sprintf = LLVMAddFunction(cg->mod, "sprintf", sprintf_type);
  }
  LLVMTypeRef sprintf_type = LLVMFunctionType(
      LLVMInt32TypeInContext(cg->ctx),
      (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, true);
  LLVMValueRef args[] = {buf, fmt, value};
  LLVMBuildCall2(cg->builder, sprintf_type, fn_sprintf, args, 3, "");
  return buf;
}

static LLVMValueRef emit_call(Codegen *cg, AstNode *node) {
  if (!node->as.call.callee)
    return LLVMConstInt(cg->i64_type, 0, false);

  /* Get function name */
  const char *fname = NULL;
  if (node->as.call.callee->type == AST_IDENTIFIER)
    fname = node->as.call.callee->as.identifier.name;

  if (!fname) {
    cg_error(cg, "Cannot call non-identifier");
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  int argc = node->as.call.args.count;

  /* Handle builtin: print() */
  if (strcmp(fname, "print") == 0) {
    if (argc >= 1) {
      LLVMValueRef val = emit_expr(cg, node->as.call.args.items[0]);
      emit_print_call(cg, val);
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: str() */
  if (strcmp(fname, "str") == 0) {
    if (argc >= 1) {
      LLVMValueRef val = emit_expr(cg, node->as.call.args.items[0]);
      return emit_str_call(cg, val);
    }
    return LLVMBuildGlobalStringPtr(cg->builder, "", "empty");
  }

  /* Handle builtin: stdin_text() -> string */
  if (strcmp(fname, "stdin_text") == 0) {
    LLVMValueRef fn_stdin = LLVMGetNamedFunction(cg->mod, "__qisc_io_read_stdin");
    if (!fn_stdin) {
      LLVMTypeRef fn_type = LLVMFunctionType(cg->i8ptr_type, NULL, 0, false);
      fn_stdin = LLVMAddFunction(cg->mod, "__qisc_io_read_stdin", fn_type);
    }
    LLVMTypeRef fn_type = LLVMFunctionType(cg->i8ptr_type, NULL, 0, false);
    return LLVMBuildCall2(cg->builder, fn_type, fn_stdin, NULL, 0, "stdin_text");
  }

  /* Handle builtin: stdin_lines() -> array of string pointers */
  if (strcmp(fname, "stdin_lines") == 0) {
    LLVMValueRef fn_stdin_lines =
        LLVMGetNamedFunction(cg->mod, "__qisc_io_read_stdin_lines");
    if (!fn_stdin_lines) {
      LLVMTypeRef fn_type = LLVMFunctionType(cg->i8ptr_type, NULL, 0, false);
      fn_stdin_lines =
          LLVMAddFunction(cg->mod, "__qisc_io_read_stdin_lines", fn_type);
    }
    LLVMTypeRef fn_type = LLVMFunctionType(cg->i8ptr_type, NULL, 0, false);
    return LLVMBuildCall2(cg->builder, fn_type, fn_stdin_lines, NULL, 0,
                          "stdin_lines");
  }

  /* Handle builtin: read_file(path) -> string */
  if (strcmp(fname, "read_file") == 0) {
    if (argc >= 1) {
      LLVMValueRef path = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef fn_read = LLVMGetNamedFunction(cg->mod, "__qisc_io_read_file");
      if (!fn_read) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        fn_read = LLVMAddFunction(cg->mod, "__qisc_io_read_file", fn_type);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_read, (LLVMValueRef[]){path},
                            1, "file_text");
    }
    return LLVMBuildGlobalStringPtr(cg->builder, "", "empty");
  }

  /* Handle builtin: file_lines(path) -> array of string pointers */
  if (strcmp(fname, "file_lines") == 0) {
    if (argc >= 1) {
      LLVMValueRef path = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef fn_lines =
          LLVMGetNamedFunction(cg->mod, "__qisc_io_read_file_lines");
      if (!fn_lines) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        fn_lines =
            LLVMAddFunction(cg->mod, "__qisc_io_read_file_lines", fn_type);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_lines,
                            (LLVMValueRef[]){path}, 1, "file_lines");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: stream_range(start, end[, step]) -> lazy int stream */
  if (strcmp(fname, "stream_range") == 0) {
    if (argc >= 2) {
      LLVMValueRef start = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef end = emit_expr(cg, node->as.call.args.items[1]);
      LLVMValueRef step =
          argc >= 3 ? emit_expr(cg, node->as.call.args.items[2])
                    : LLVMConstInt(cg->i64_type, 1, false);
      LLVMValueRef fn_stream = LLVMGetNamedFunction(cg->mod, "__qisc_stream_range_i64");
      if (!fn_stream) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type,
            (LLVMTypeRef[]){cg->i64_type, cg->i64_type, cg->i64_type}, 3, false);
        fn_stream = LLVMAddFunction(cg->mod, "__qisc_stream_range_i64", fn_type);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i64_type, cg->i64_type, cg->i64_type}, 3, false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                            (LLVMValueRef[]){start, end, step}, 3,
                            "stream_range");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: stream_file_lines(path) -> lazy string stream */
  if (strcmp(fname, "stream_file_lines") == 0) {
    if (argc >= 1) {
      LLVMValueRef path = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef fn_stream =
          LLVMGetNamedFunction(cg->mod, "__qisc_stream_file_lines_open");
      if (!fn_stream) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        fn_stream =
            LLVMAddFunction(cg->mod, "__qisc_stream_file_lines_open", fn_type);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                            (LLVMValueRef[]){path}, 1, "stream_lines");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: stream_take(stream, n) / stream_skip(stream, n) */
  if (strcmp(fname, "stream_take") == 0 || strcmp(fname, "stream_skip") == 0) {
    if (argc >= 2) {
      LLVMValueRef stream = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef amount = emit_expr(cg, node->as.call.args.items[1]);
      const char *rt_name = strcmp(fname, "stream_take") == 0
                                ? "__qisc_stream_take_i64"
                                : "__qisc_stream_skip_i64";
      LLVMValueRef fn_stream = LLVMGetNamedFunction(cg->mod, rt_name);
      if (!fn_stream) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2,
            false);
        fn_stream = LLVMAddFunction(cg->mod, rt_name, fn_type);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2,
          false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                            (LLVMValueRef[]){stream, amount}, 2, fname);
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: stream_map(stream, fn) / stream_filter(stream, fn) */
  if (strcmp(fname, "stream_map") == 0 || strcmp(fname, "stream_filter") == 0) {
    if (argc >= 2) {
      LLVMValueRef stream = emit_expr(cg, node->as.call.args.items[0]);
      CgStreamKind kind = cg_expr_stream_kind(cg, node->as.call.args.items[0]);
      CgStreamKind target_kind = kind;
      const char *rt_name;
      CgStreamCallable callable;
      if (strcmp(fname, "stream_map") == 0) {
        CgStreamKind inferred =
            cg_stream_kind_from_callable(cg, node->as.call.args.items[1],
                                         kind);
        if (inferred != CG_STREAM_NONE)
          target_kind = inferred;
        callable = cg_emit_stream_callable(
            cg, node->as.call.args.items[1], kind,
            target_kind == CG_STREAM_STRING ? cg->i8ptr_type : cg->i64_type);
        rt_name =
            cg_stream_map_runtime_name(kind, target_kind, callable.uses_ctx);
      } else {
        callable = cg_emit_stream_callable(cg, node->as.call.args.items[1], kind,
                                           cg->i1_type);
        rt_name = cg_stream_filter_runtime_name(kind, callable.uses_ctx);
      }
      LLVMValueRef fn_stream = LLVMGetNamedFunction(cg->mod, rt_name);
      if (!fn_stream) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type,
            callable.uses_ctx
                ? (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type,
                                  cg->i8ptr_type}
                : (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type},
            callable.uses_ctx ? 3 : 2, false);
        fn_stream = LLVMAddFunction(cg->mod, rt_name, fn_type);
      }
      if (callable.uses_ctx) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type,
            (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type, cg->i8ptr_type}, 3,
            false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){stream, callable.fn, callable.ctx},
                              3, fname);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2,
          false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                            (LLVMValueRef[]){stream, callable.fn}, 2, fname);
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: stream_reduce(stream, fn, initial) -> int */
  if (strcmp(fname, "stream_reduce") == 0) {
    if (argc >= 3) {
      AstNode *stream_arg = node->as.call.args.items[0];
      AstNode *fn_arg = node->as.call.args.items[1];
      AstNode *init_arg = node->as.call.args.items[2];
      CgStreamKind kind = cg_expr_stream_kind(cg, stream_arg);
      if (kind != CG_STREAM_INT && kind != CG_STREAM_STRING) {
        cg_error(cg, "stream_reduce currently supports int and string streams only");
        return LLVMConstInt(cg->i64_type, 0, false);
      }
      LLVMValueRef stream = emit_expr(cg, stream_arg);
      CgStreamCallable reducer =
          cg_emit_stream_reduce_callable(cg, fn_arg, kind);
      LLVMValueRef initial = emit_expr(cg, init_arg);
      initial = cg_coerce_value(cg, initial, cg->i64_type);
      const char *rt_name =
          cg_stream_reduce_runtime_name(kind, reducer.uses_ctx);
      LLVMValueRef fn_stream = LLVMGetNamedFunction(cg->mod, rt_name);
      if (!fn_stream) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i64_type,
            reducer.uses_ctx
                ? (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type,
                                  cg->i8ptr_type, cg->i64_type}
                : (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type,
                                  cg->i64_type},
            reducer.uses_ctx ? 4 : 3, false);
        fn_stream = LLVMAddFunction(cg->mod, rt_name, fn_type);
      }
      if (reducer.uses_ctx) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i64_type,
            (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type, cg->i8ptr_type,
                            cg->i64_type},
            4, false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){stream, reducer.fn, reducer.ctx,
                                              initial},
                              4, "stream_reduce");
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i64_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type, cg->i64_type}, 3,
          false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                            (LLVMValueRef[]){stream, reducer.fn, initial}, 3,
                            "stream_reduce");
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: stream_count(stream) -> int */
  if (strcmp(fname, "stream_count") == 0) {
    if (argc >= 1) {
      LLVMValueRef stream = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef fn_stream =
          LLVMGetNamedFunction(cg->mod, "__qisc_stream_count_i64");
      if (!fn_stream) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        fn_stream = LLVMAddFunction(cg->mod, "__qisc_stream_count_i64", fn_type);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                            (LLVMValueRef[]){stream}, 1, "stream_count");
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: stream_first(stream) -> first element */
  if (strcmp(fname, "stream_first") == 0) {
    if (argc >= 1) {
      AstNode *stream_node = node->as.call.args.items[0];
      LLVMValueRef stream = emit_expr(cg, stream_node);
      CgStreamKind kind = cg_expr_stream_kind(cg, stream_node);
      if (kind == CG_STREAM_STRING) {
        LLVMValueRef fn_stream =
            LLVMGetNamedFunction(cg->mod, "__qisc_stream_first_string");
        if (!fn_stream) {
          LLVMTypeRef fn_type = LLVMFunctionType(
              cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          fn_stream =
              LLVMAddFunction(cg->mod, "__qisc_stream_first_string", fn_type);
        }
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                              (LLVMValueRef[]){stream}, 1, "stream_first");
      }

      LLVMValueRef fn_stream =
          LLVMGetNamedFunction(cg->mod, "__qisc_stream_first_i64");
      if (!fn_stream) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        fn_stream = LLVMAddFunction(cg->mod, "__qisc_stream_first_i64", fn_type);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                            (LLVMValueRef[]){stream}, 1, "stream_first");
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: stream_collect(stream) -> eager array */
  if (strcmp(fname, "stream_collect") == 0) {
    if (argc >= 1) {
      AstNode *stream_node = node->as.call.args.items[0];
      LLVMValueRef stream = emit_expr(cg, stream_node);
      CgStreamKind kind = cg_expr_stream_kind(cg, stream_node);
      const char *rt_name = kind == CG_STREAM_STRING
                                ? "__qisc_stream_collect_strings"
                                : "__qisc_stream_collect_i64";
      LLVMValueRef fn_stream = LLVMGetNamedFunction(cg->mod, rt_name);
      if (!fn_stream) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        fn_stream = LLVMAddFunction(cg->mod, rt_name, fn_type);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_stream,
                            (LLVMValueRef[]){stream}, 1, "stream_collect");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: stdout_write(text), stderr_write(text) */
  if (strcmp(fname, "stdout_write") == 0 || strcmp(fname, "stderr_write") == 0) {
    if (argc >= 1) {
      LLVMValueRef text = emit_expr(cg, node->as.call.args.items[0]);
      const char *rt_name =
          strcmp(fname, "stdout_write") == 0 ? "__qisc_io_write_stdout"
                                             : "__qisc_io_write_stderr";
      LLVMValueRef fn_write = LLVMGetNamedFunction(cg->mod, rt_name);
      if (!fn_write) {
        LLVMTypeRef fn_type = LLVMFunctionType(
            cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        fn_write = LLVMAddFunction(cg->mod, rt_name, fn_type);
      }
      if (LLVMTypeOf(text) != cg->i8ptr_type) {
        text = emit_str_call(cg, text);
      }
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      return LLVMBuildCall2(cg->builder, fn_type, fn_write, (LLVMValueRef[]){text},
                            1, "written");
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: sizeof() — return a constant */
  if (strcmp(fname, "sizeof") == 0) {
    if (argc >= 1) {
      LLVMValueRef val = emit_expr(cg, node->as.call.args.items[0]);
      LLVMTypeRef vt = LLVMTypeOf(val);
      if (vt == cg->i8ptr_type && !cg_expr_is_array_like(cg, node->as.call.args.items[0])) {
        return cg_emit_strlen(cg, val);
      }
      /* Numeric: return 8 (64-bit) */
      return LLVMConstInt(cg->i64_type, 8, false);
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: typeof() — return type name string */
  if (strcmp(fname, "typeof") == 0) {
    if (argc >= 1) {
      LLVMValueRef val = emit_expr(cg, node->as.call.args.items[0]);
      LLVMTypeRef vt = LLVMTypeOf(val);
      const char *tn = "unknown";
      if (vt == cg->i64_type)
        tn = "int";
      else if (vt == cg->f64_type)
        tn = "float";
      else if (vt == cg->i1_type)
        tn = "bool";
      else if (vt == cg->i8ptr_type)
        tn = "string";
      return LLVMBuildGlobalStringPtr(cg->builder, tn, "typename");
    }
    return LLVMBuildGlobalStringPtr(cg->builder, "none", "typename");
  }

  /* Handle builtin: len(array) — call __qisc_array_len for runtime length */
  if (strcmp(fname, "len") == 0) {
    if (argc >= 1) {
      AstNode *arg_node = node->as.call.args.items[0];
      LLVMValueRef value = emit_expr(cg, arg_node);

      if (!cg_expr_is_array_like(cg, arg_node)) {
        return cg_emit_strlen(cg, value);
      }

      LLVMValueRef fn_array_len = LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
      LLVMTypeRef arr_len_type = LLVMFunctionType(cg->i64_type,
          (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      return LLVMBuildCall2(cg->builder, arr_len_type, fn_array_len,
          (LLVMValueRef[]){value}, 1, "len");
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: take(array, n) -> array slice [0, n) */
  if (strcmp(fname, "take") == 0) {
    if (argc >= 2) {
      LLVMValueRef arr = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef end = emit_expr(cg, node->as.call.args.items[1]);
      LLVMValueRef zero = LLVMConstInt(cg->i64_type, 0, false);
      LLVMValueRef fn_array_len =
          LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
      LLVMTypeRef arr_len_type = LLVMFunctionType(
          cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      LLVMValueRef len = LLVMBuildCall2(cg->builder, arr_len_type, fn_array_len,
                                        (LLVMValueRef[]){arr}, 1, "take_len");
      LLVMValueRef negative =
          LLVMBuildICmp(cg->builder, LLVMIntSLT, end, zero, "take_neg");
      LLVMValueRef clamped_low =
          LLVMBuildSelect(cg->builder, negative, zero, end, "take_clamped_low");
      LLVMValueRef too_high =
          LLVMBuildICmp(cg->builder, LLVMIntSGT, clamped_low, len, "take_high");
      LLVMValueRef clamped_end =
          LLVMBuildSelect(cg->builder, too_high, len, clamped_low, "take_end");
      LLVMValueRef fn_slice =
          LLVMGetNamedFunction(cg->mod, "__qisc_array_slice");
      if (!fn_slice) {
        LLVMTypeRef slice_type = LLVMFunctionType(
            cg->i8ptr_type,
            (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type, cg->i64_type}, 3,
            false);
        fn_slice = LLVMAddFunction(cg->mod, "__qisc_array_slice", slice_type);
      }
      LLVMTypeRef slice_type = LLVMFunctionType(
          cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type, cg->i64_type}, 3, false);
      return LLVMBuildCall2(cg->builder, slice_type, fn_slice,
                            (LLVMValueRef[]){arr, zero, clamped_end}, 3,
                            "taken");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: skip(array, n) -> array slice [n, len) */
  if (strcmp(fname, "skip") == 0) {
    if (argc >= 2) {
      LLVMValueRef arr = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef start = emit_expr(cg, node->as.call.args.items[1]);
      LLVMValueRef zero = LLVMConstInt(cg->i64_type, 0, false);
      LLVMValueRef fn_array_len =
          LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
      LLVMTypeRef arr_len_type = LLVMFunctionType(
          cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      LLVMValueRef len = LLVMBuildCall2(cg->builder, arr_len_type, fn_array_len,
                                        (LLVMValueRef[]){arr}, 1, "skip_len");
      LLVMValueRef negative =
          LLVMBuildICmp(cg->builder, LLVMIntSLT, start, zero, "skip_neg");
      LLVMValueRef clamped_low =
          LLVMBuildSelect(cg->builder, negative, zero, start, "skip_clamped_low");
      LLVMValueRef too_high =
          LLVMBuildICmp(cg->builder, LLVMIntSGT, clamped_low, len, "skip_high");
      LLVMValueRef clamped_start =
          LLVMBuildSelect(cg->builder, too_high, len, clamped_low, "skip_start");
      LLVMValueRef fn_slice =
          LLVMGetNamedFunction(cg->mod, "__qisc_array_slice");
      if (!fn_slice) {
        LLVMTypeRef slice_type = LLVMFunctionType(
            cg->i8ptr_type,
            (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type, cg->i64_type}, 3,
            false);
        fn_slice = LLVMAddFunction(cg->mod, "__qisc_array_slice", slice_type);
      }
      LLVMTypeRef slice_type = LLVMFunctionType(
          cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type, cg->i64_type}, 3, false);
      return LLVMBuildCall2(cg->builder, slice_type, fn_slice,
                            (LLVMValueRef[]){arr, clamped_start, len}, 3,
                            "skipped");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: push(array, element) — call __qisc_array_push */
  if (strcmp(fname, "push") == 0) {
    if (argc >= 2) {
      LLVMValueRef arr = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef elem = emit_expr(cg, node->as.call.args.items[1]);
      
      /* Store element value to temp alloca for passing by pointer */
      LLVMValueRef elem_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "push_elem");
      LLVMBuildStore(cg->builder, elem, elem_alloca);
      
      LLVMValueRef fn_array_push = LLVMGetNamedFunction(cg->mod, "__qisc_array_push");
      LLVMTypeRef arr_push_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
      return LLVMBuildCall2(cg->builder, arr_push_type, fn_array_push,
          (LLVMValueRef[]){arr, elem_alloca}, 2, "pushed");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: pop(array) — call __qisc_array_pop and load value */
  if (strcmp(fname, "pop") == 0) {
    if (argc >= 1) {
      LLVMValueRef arr = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef fn_array_pop = LLVMGetNamedFunction(cg->mod, "__qisc_array_pop");
      LLVMTypeRef arr_pop_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      LLVMValueRef elem_ptr = LLVMBuildCall2(cg->builder, arr_pop_type, fn_array_pop,
          (LLVMValueRef[]){arr}, 1, "pop_ptr");
      /* Load the i64 value from the returned pointer */
      return LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "popped");
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: file_exists(path) — calls access() to check if file exists */
  if (strcmp(fname, "file_exists") == 0) {
    if (argc >= 1) {
      LLVMValueRef path = emit_expr(cg, node->as.call.args.items[0]);
      
      /* Declare access() if not already declared */
      LLVMValueRef fn_access = LLVMGetNamedFunction(cg->mod, "access");
      if (!fn_access) {
        LLVMTypeRef access_type = LLVMFunctionType(
            LLVMInt32TypeInContext(cg->ctx),
            (LLVMTypeRef[]){cg->i8ptr_type, LLVMInt32TypeInContext(cg->ctx)}, 2, false);
        fn_access = LLVMAddFunction(cg->mod, "access", access_type);
      }
      LLVMTypeRef access_type = LLVMFunctionType(
          LLVMInt32TypeInContext(cg->ctx),
          (LLVMTypeRef[]){cg->i8ptr_type, LLVMInt32TypeInContext(cg->ctx)}, 2, false);
      
      /* Call access(path, F_OK=0) - returns 0 if file exists */
      LLVMValueRef f_ok = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, false);
      LLVMValueRef result = LLVMBuildCall2(cg->builder, access_type, fn_access,
          (LLVMValueRef[]){path, f_ok}, 2, "access_result");
      
      /* Convert: access returns 0 on success, we want 1 (true) on success */
      LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, false);
      LLVMValueRef exists = LLVMBuildICmp(cg->builder, LLVMIntEQ, result, zero, "exists");
      return LLVMBuildZExt(cg->builder, exists, cg->i64_type, "file_exists");
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: range(start, end) — creates an array from start to end-1 with length tracking */
  if (strcmp(fname, "range") == 0) {
    if (argc >= 2) {
      LLVMValueRef start = emit_expr(cg, node->as.call.args.items[0]);
      LLVMValueRef end = emit_expr(cg, node->as.call.args.items[1]);
      
      /* Calculate length = end - start */
      LLVMValueRef len = LLVMBuildSub(cg->builder, end, start, "range_len");
      
      /* Create array using __qisc_array_new(elem_size=8, capacity=len) */
      LLVMValueRef fn_array_new = LLVMGetNamedFunction(cg->mod, "__qisc_array_new");
      LLVMTypeRef arr_new_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i64_type, cg->i64_type}, 2, false);
      LLVMValueRef elem_size = LLVMConstInt(cg->i64_type, 8, false);
      LLVMValueRef arr = LLVMBuildCall2(cg->builder, arr_new_type, fn_array_new,
          (LLVMValueRef[]){elem_size, len}, 2, "range_arr");
      
      /* Fill array with values from start to end-1 using push
       * Simple loop: for (i = 0; i < len; i++) push(arr, start + i) */
      LLVMValueRef idx_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "ridx");
      LLVMValueRef arr_alloca = LLVMBuildAlloca(cg->builder, cg->i8ptr_type, "rarr");
      LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false), idx_alloca);
      LLVMBuildStore(cg->builder, arr, arr_alloca);
      
      LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "range.loop");
      LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "range.body");
      LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "range.end");
      
      LLVMBuildBr(cg->builder, loop_bb);
      LLVMPositionBuilderAtEnd(cg->builder, loop_bb);
      
      LLVMValueRef idx = LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "i");
      LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, idx, len, "rcond");
      LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, body_bb);
      LLVMValueRef val = LLVMBuildAdd(cg->builder, start, idx, "rval");
      
      /* Store val in temp alloca for push */
      LLVMValueRef val_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "rval_tmp");
      LLVMBuildStore(cg->builder, val, val_alloca);
      
      /* Call push(arr, &val) */
      LLVMValueRef fn_array_push = LLVMGetNamedFunction(cg->mod, "__qisc_array_push");
      LLVMTypeRef arr_push_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
      LLVMValueRef cur_arr = LLVMBuildLoad2(cg->builder, cg->i8ptr_type, arr_alloca, "cur_arr");
      LLVMValueRef new_arr = LLVMBuildCall2(cg->builder, arr_push_type, fn_array_push,
          (LLVMValueRef[]){cur_arr, val_alloca}, 2, "pushed");
      LLVMBuildStore(cg->builder, new_arr, arr_alloca);
      
      LLVMValueRef next_idx = LLVMBuildAdd(cg->builder, idx, LLVMConstInt(cg->i64_type, 1, false), "rnext");
      LLVMBuildStore(cg->builder, next_idx, idx_alloca);
      LLVMBuildBr(cg->builder, loop_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, end_bb);
      return LLVMBuildLoad2(cg->builder, cg->i8ptr_type, arr_alloca, "final_arr");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: map(array, fn) -> new array with fn applied to each element */
  if (strcmp(fname, "map") == 0) {
    if (argc >= 2) {
      LLVMValueRef arr = emit_expr(cg, node->as.call.args.items[0]);
      AstNode *fn_node = node->as.call.args.items[1];
      bool fn_is_closure = cg_expr_is_closure_like(cg, fn_node);
      LLVMValueRef fn_ptr = LLVMConstNull(cg->i8ptr_type);
      if (fn_is_closure) {
        if (fn_node->type == AST_LAMBDA) {
          LLVMTypeRef param_types[1] = {cg->i64_type};
          fn_ptr = cg_emit_general_closure_value(cg, fn_node, param_types, 1,
                                                 cg->i64_type);
        } else {
          fn_ptr = emit_expr(cg, fn_node);
        }
      } else {
        fn_ptr = emit_expr(cg, fn_node);
      }
      
      /* Get array length */
      LLVMValueRef fn_array_len = LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
      LLVMTypeRef arr_len_type = LLVMFunctionType(cg->i64_type,
          (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      LLVMValueRef len = LLVMBuildCall2(cg->builder, arr_len_type, fn_array_len,
          (LLVMValueRef[]){arr}, 1, "map_len");
      
      /* Create result array using __qisc_array_new(elem_size=8, capacity=len) */
      LLVMValueRef fn_array_new = LLVMGetNamedFunction(cg->mod, "__qisc_array_new");
      LLVMTypeRef arr_new_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i64_type, cg->i64_type}, 2, false);
      LLVMValueRef elem_size = LLVMConstInt(cg->i64_type, 8, false);
      LLVMValueRef result_arr = LLVMBuildCall2(cg->builder, arr_new_type, fn_array_new,
          (LLVMValueRef[]){elem_size, len}, 2, "map_result");
      
      /* Loop: for (i = 0; i < len; i++) { result[i] = fn(arr[i]); } */
      LLVMValueRef idx_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "map_idx");
      LLVMValueRef res_alloca = LLVMBuildAlloca(cg->builder, cg->i8ptr_type, "map_res");
      LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false), idx_alloca);
      LLVMBuildStore(cg->builder, result_arr, res_alloca);
      
      LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "map.loop");
      LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "map.body");
      LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "map.end");
      
      LLVMBuildBr(cg->builder, loop_bb);
      LLVMPositionBuilderAtEnd(cg->builder, loop_bb);
      
      LLVMValueRef idx = LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "i");
      LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, idx, len, "map_cond");
      LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, body_bb);
      
      /* Get element at index from source array: __qisc_array_get(arr, idx) */
      LLVMValueRef fn_array_get = LLVMGetNamedFunction(cg->mod, "__qisc_array_get");
      LLVMTypeRef arr_get_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2, false);
      LLVMValueRef elem_ptr = LLVMBuildCall2(cg->builder, arr_get_type, fn_array_get,
          (LLVMValueRef[]){arr, idx}, 2, "elem_ptr");
      LLVMValueRef elem = LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "elem");
      
      /* Call fn(elem) */
      LLVMValueRef mapped;
      if (fn_is_closure) {
        LLVMTypeRef arg_types[1] = {cg->i64_type};
        LLVMValueRef arg_values[1] = {elem};
        mapped = cg_emit_closure_call(cg, fn_ptr, arg_types, arg_values, 1,
                                      cg->i64_type, "mapped");
      } else {
        LLVMTypeRef map_fn_type = LLVMFunctionType(cg->i64_type,
            (LLVMTypeRef[]){cg->i64_type}, 1, false);
        mapped = LLVMBuildCall2(cg->builder, map_fn_type, fn_ptr,
            (LLVMValueRef[]){elem}, 1, "mapped");
      }
      
      /* Store mapped value in temp alloca for push */
      LLVMValueRef val_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "map_val");
      LLVMBuildStore(cg->builder, mapped, val_alloca);
      
      /* Push to result array */
      LLVMValueRef fn_array_push = LLVMGetNamedFunction(cg->mod, "__qisc_array_push");
      LLVMTypeRef arr_push_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
      LLVMValueRef cur_res = LLVMBuildLoad2(cg->builder, cg->i8ptr_type, res_alloca, "cur_res");
      LLVMValueRef new_res = LLVMBuildCall2(cg->builder, arr_push_type, fn_array_push,
          (LLVMValueRef[]){cur_res, val_alloca}, 2, "pushed");
      LLVMBuildStore(cg->builder, new_res, res_alloca);
      
      /* Increment index */
      LLVMValueRef next_idx = LLVMBuildAdd(cg->builder, idx, LLVMConstInt(cg->i64_type, 1, false), "map_next");
      LLVMBuildStore(cg->builder, next_idx, idx_alloca);
      LLVMBuildBr(cg->builder, loop_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, end_bb);
      return LLVMBuildLoad2(cg->builder, cg->i8ptr_type, res_alloca, "map_final");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: filter(array, predicate) -> array of elements where predicate is true */
  if (strcmp(fname, "filter") == 0) {
    if (argc >= 2) {
      LLVMValueRef arr = emit_expr(cg, node->as.call.args.items[0]);
      AstNode *pred_node = node->as.call.args.items[1];
      bool pred_is_closure = cg_expr_is_closure_like(cg, pred_node);
      LLVMValueRef pred_fn = LLVMConstNull(cg->i8ptr_type);
      if (pred_is_closure) {
        if (pred_node->type == AST_LAMBDA) {
          LLVMTypeRef param_types[1] = {cg->i64_type};
          pred_fn = cg_emit_general_closure_value(cg, pred_node, param_types, 1,
                                                  cg->i64_type);
        } else {
          pred_fn = emit_expr(cg, pred_node);
        }
      } else {
        pred_fn = emit_expr(cg, pred_node);
      }
      
      /* Get array length */
      LLVMValueRef fn_array_len = LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
      LLVMTypeRef arr_len_type = LLVMFunctionType(cg->i64_type,
          (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      LLVMValueRef len = LLVMBuildCall2(cg->builder, arr_len_type, fn_array_len,
          (LLVMValueRef[]){arr}, 1, "filter_len");
      
      /* Create result array (start with capacity = len, will only keep matching) */
      LLVMValueRef fn_array_new = LLVMGetNamedFunction(cg->mod, "__qisc_array_new");
      LLVMTypeRef arr_new_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i64_type, cg->i64_type}, 2, false);
      LLVMValueRef elem_size = LLVMConstInt(cg->i64_type, 8, false);
      LLVMValueRef result_arr = LLVMBuildCall2(cg->builder, arr_new_type, fn_array_new,
          (LLVMValueRef[]){elem_size, len}, 2, "filter_result");
      
      /* Loop variables */
      LLVMValueRef idx_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "filter_idx");
      LLVMValueRef res_alloca = LLVMBuildAlloca(cg->builder, cg->i8ptr_type, "filter_res");
      LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false), idx_alloca);
      LLVMBuildStore(cg->builder, result_arr, res_alloca);
      
      LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "filter.loop");
      LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "filter.body");
      LLVMBasicBlockRef then_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "filter.then");
      LLVMBasicBlockRef cont_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "filter.cont");
      LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "filter.end");
      
      LLVMBuildBr(cg->builder, loop_bb);
      LLVMPositionBuilderAtEnd(cg->builder, loop_bb);
      
      LLVMValueRef idx = LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "i");
      LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, idx, len, "filter_cond");
      LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, body_bb);
      
      /* Get element at index */
      LLVMValueRef fn_array_get = LLVMGetNamedFunction(cg->mod, "__qisc_array_get");
      LLVMTypeRef arr_get_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2, false);
      LLVMValueRef elem_ptr = LLVMBuildCall2(cg->builder, arr_get_type, fn_array_get,
          (LLVMValueRef[]){arr, idx}, 2, "elem_ptr");
      LLVMValueRef elem = LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "elem");
      
      /* Call predicate(elem) */
      LLVMValueRef pred_result;
      if (pred_is_closure) {
        LLVMTypeRef arg_types[1] = {cg->i64_type};
        LLVMValueRef arg_values[1] = {elem};
        pred_result = cg_emit_closure_call(cg, pred_fn, arg_types, arg_values, 1,
                                           cg->i64_type, "pred_result");
      } else {
        LLVMTypeRef pred_fn_type = LLVMFunctionType(cg->i64_type,
            (LLVMTypeRef[]){cg->i64_type}, 1, false);
        pred_result = LLVMBuildCall2(cg->builder, pred_fn_type, pred_fn,
            (LLVMValueRef[]){elem}, 1, "pred_result");
      }
      
      /* If predicate is truthy (non-zero), add to result */
      LLVMValueRef is_truthy = LLVMBuildICmp(cg->builder, LLVMIntNE, pred_result,
          LLVMConstInt(cg->i64_type, 0, false), "is_truthy");
      LLVMBuildCondBr(cg->builder, is_truthy, then_bb, cont_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, then_bb);
      
      /* Push elem to result array */
      LLVMValueRef val_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "filter_val");
      LLVMBuildStore(cg->builder, elem, val_alloca);
      
      LLVMValueRef fn_array_push = LLVMGetNamedFunction(cg->mod, "__qisc_array_push");
      LLVMTypeRef arr_push_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
      LLVMValueRef cur_res = LLVMBuildLoad2(cg->builder, cg->i8ptr_type, res_alloca, "cur_res");
      LLVMValueRef new_res = LLVMBuildCall2(cg->builder, arr_push_type, fn_array_push,
          (LLVMValueRef[]){cur_res, val_alloca}, 2, "pushed");
      LLVMBuildStore(cg->builder, new_res, res_alloca);
      LLVMBuildBr(cg->builder, cont_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, cont_bb);
      
      /* Increment index */
      LLVMValueRef next_idx = LLVMBuildAdd(cg->builder, idx, LLVMConstInt(cg->i64_type, 1, false), "filter_next");
      LLVMBuildStore(cg->builder, next_idx, idx_alloca);
      LLVMBuildBr(cg->builder, loop_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, end_bb);
      return LLVMBuildLoad2(cg->builder, cg->i8ptr_type, res_alloca, "filter_final");
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Handle builtin: reduce(array, fn, initial) -> single value */
  if (strcmp(fname, "reduce") == 0) {
    if (argc >= 3) {
      LLVMValueRef arr = emit_expr(cg, node->as.call.args.items[0]);
      AstNode *reduce_node = node->as.call.args.items[1];
      bool reduce_is_closure = cg_expr_is_closure_like(cg, reduce_node);
      LLVMValueRef reduce_fn = LLVMConstNull(cg->i8ptr_type);
      if (reduce_is_closure) {
        if (reduce_node->type == AST_LAMBDA) {
          LLVMTypeRef param_types[2] = {cg->i64_type, cg->i64_type};
          reduce_fn = cg_emit_general_closure_value(cg, reduce_node, param_types,
                                                    2, cg->i64_type);
        } else {
          reduce_fn = emit_expr(cg, reduce_node);
        }
      } else {
        reduce_fn = emit_expr(cg, reduce_node);
      }
      LLVMValueRef initial = emit_expr(cg, node->as.call.args.items[2]);
      
      /* Get array length */
      LLVMValueRef fn_array_len = LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
      LLVMTypeRef arr_len_type = LLVMFunctionType(cg->i64_type,
          (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      LLVMValueRef len = LLVMBuildCall2(cg->builder, arr_len_type, fn_array_len,
          (LLVMValueRef[]){arr}, 1, "reduce_len");
      
      /* Loop: acc = initial; for (i = 0; i < len; i++) acc = fn(acc, arr[i]); */
      LLVMValueRef idx_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "reduce_idx");
      LLVMValueRef acc_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, "reduce_acc");
      LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false), idx_alloca);
      LLVMBuildStore(cg->builder, initial, acc_alloca);
      
      LLVMBasicBlockRef loop_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "reduce.loop");
      LLVMBasicBlockRef body_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "reduce.body");
      LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "reduce.end");
      
      LLVMBuildBr(cg->builder, loop_bb);
      LLVMPositionBuilderAtEnd(cg->builder, loop_bb);
      
      LLVMValueRef idx = LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "i");
      LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, idx, len, "reduce_cond");
      LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, body_bb);
      
      /* Get element at index */
      LLVMValueRef fn_array_get = LLVMGetNamedFunction(cg->mod, "__qisc_array_get");
      LLVMTypeRef arr_get_type = LLVMFunctionType(cg->i8ptr_type,
          (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2, false);
      LLVMValueRef elem_ptr = LLVMBuildCall2(cg->builder, arr_get_type, fn_array_get,
          (LLVMValueRef[]){arr, idx}, 2, "elem_ptr");
      LLVMValueRef elem = LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "elem");
      
      /* Load current accumulator */
      LLVMValueRef acc = LLVMBuildLoad2(cg->builder, cg->i64_type, acc_alloca, "acc");
      
      /* Call fn(acc, elem) */
      LLVMValueRef new_acc;
      if (reduce_is_closure) {
        LLVMTypeRef arg_types[2] = {cg->i64_type, cg->i64_type};
        LLVMValueRef arg_values[2] = {acc, elem};
        new_acc = cg_emit_closure_call(cg, reduce_fn, arg_types, arg_values, 2,
                                       cg->i64_type, "new_acc");
      } else {
        LLVMTypeRef reduce_fn_type = LLVMFunctionType(cg->i64_type,
            (LLVMTypeRef[]){cg->i64_type, cg->i64_type}, 2, false);
        new_acc = LLVMBuildCall2(cg->builder, reduce_fn_type, reduce_fn,
            (LLVMValueRef[]){acc, elem}, 2, "new_acc");
      }
      
      /* Store new accumulator */
      LLVMBuildStore(cg->builder, new_acc, acc_alloca);
      
      /* Increment index */
      LLVMValueRef next_idx = LLVMBuildAdd(cg->builder, idx, LLVMConstInt(cg->i64_type, 1, false), "reduce_next");
      LLVMBuildStore(cg->builder, next_idx, idx_alloca);
      LLVMBuildBr(cg->builder, loop_bb);
      
      LLVMPositionBuilderAtEnd(cg->builder, end_bb);
      return LLVMBuildLoad2(cg->builder, cg->i64_type, acc_alloca, "reduce_final");
    }
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  /* Handle builtin: collect(stream_or_array) -> array (materializes lazy evaluation) */
  if (strcmp(fname, "collect") == 0) {
    if (argc >= 1) {
      /* For now, collect just returns its argument as arrays are already eager */
      return emit_expr(cg, node->as.call.args.items[0]);
    }
    return LLVMConstNull(cg->i8ptr_type);
  }

  /* Regular function call */
  LLVMValueRef fn = LLVMGetNamedFunction(cg->mod, fname);
  if (!fn) {
    /* Maybe it's a lambda variable (function pointer) */
    CgSymbol *sym = cg_lookup(cg, fname);
    if (sym) {
      char marker_name[300];
      snprintf(marker_name, sizeof(marker_name), "__%s__closure", fname);
      bool is_closure = cg_lookup(cg, marker_name) != NULL;
      if (sym->is_callable) {
        LLVMValueRef *args = NULL;
        if (argc > 0) {
          args = calloc(argc, sizeof(LLVMValueRef));
          for (int i = 0; i < argc; i++)
            args[i] = emit_expr(cg, node->as.call.args.items[i]);
        }
        LLVMValueRef callable =
            LLVMBuildLoad2(cg->builder, sym->type, sym->alloca,
                           is_closure ? "closure_ptr" : "fn_ptr");
        LLVMValueRef result = cg_emit_symbol_callable_call(
            cg, sym, callable, args, argc, is_closure, "lcall");
        free(args);
        return result;
      }
      /* Load the function pointer from the variable */
      LLVMValueRef fn_ptr =
          LLVMBuildLoad2(cg->builder, sym->type, sym->alloca, "fn_ptr");

      /* Build a function type based on arg count, all i64 -> i64 */
      LLVMTypeRef *param_types = NULL;
      if (argc > 0) {
        param_types = calloc(argc, sizeof(LLVMTypeRef));
        for (int i = 0; i < argc; i++)
          param_types[i] = cg->i64_type;
      }
      LLVMTypeRef fn_type =
          LLVMFunctionType(cg->i64_type, param_types, argc, false);
      free(param_types);

      /* Emit arguments */
      LLVMValueRef *args = NULL;
      if (argc > 0) {
        args = calloc(argc, sizeof(LLVMValueRef));
        for (int i = 0; i < argc; i++)
          args[i] = emit_expr(cg, node->as.call.args.items[i]);
      }
      LLVMValueRef result =
          LLVMBuildCall2(cg->builder, fn_type, fn_ptr, args, argc, "lcall");
      free(args);
      return result;
    }
    cg_error(cg, "Undefined function: %s", fname);
    return LLVMConstInt(cg->i64_type, 0, false);
  }

  LLVMTypeRef fn_type = LLVMGlobalGetValueType(fn);

  /* Emit arguments */
  LLVMValueRef *args = NULL;
  if (argc > 0) {
    args = calloc(argc, sizeof(LLVMValueRef));
    for (int i = 0; i < argc; i++) {
      args[i] = emit_expr(cg, node->as.call.args.items[i]);
    }
  }

  LLVMTypeRef ret_type = LLVMGetReturnType(fn_type);
  const char *call_name = (ret_type == cg->void_type) ? "" : "call";

  LLVMValueRef result =
      LLVMBuildCall2(cg->builder, fn_type, fn, args, argc, call_name);
  free(args);
  return result;
}

/* Emit array literal: [a, b, c] → __qisc_array_from for length tracking */
static LLVMValueRef emit_array_literal(Codegen *cg, AstNode *node) {
  int count = node->as.array_literal.elements.count;

  /* Allocate temp stack space for elements */
  LLVMValueRef fn_malloc = LLVMGetNamedFunction(cg->mod, "malloc");
  if (!fn_malloc) {
    LLVMTypeRef mt = LLVMFunctionType(cg->i8ptr_type,
                                      (LLVMTypeRef[]){cg->i64_type}, 1, false);
    fn_malloc = LLVMAddFunction(cg->mod, "malloc", mt);
  }
  
  /* Allocate temporary buffer for element values */
  LLVMValueRef temp_size = LLVMConstInt(cg->i64_type, count * 8, false);
  LLVMTypeRef mt =
      LLVMFunctionType(cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
  LLVMValueRef temp_buf = LLVMBuildCall2(cg->builder, mt, fn_malloc,
                                    (LLVMValueRef[]){temp_size}, 1, "temp_buf");

  /* Store elements into temp buffer */
  for (int i = 0; i < count; i++) {
    LLVMValueRef val = emit_expr(cg, node->as.array_literal.elements.items[i]);
    LLVMValueRef idx = LLVMConstInt(cg->i64_type, i, false);
    LLVMValueRef ptr =
        LLVMBuildGEP2(cg->builder, cg->i64_type, temp_buf, &idx, 1, "elem_ptr");
    LLVMBuildStore(cg->builder, val, ptr);
  }

  /* Call __qisc_array_from(temp_buf, elem_size=8, count) */
  LLVMValueRef fn_array_from = LLVMGetNamedFunction(cg->mod, "__qisc_array_from");
  LLVMTypeRef arr_from_type = LLVMFunctionType(cg->i8ptr_type,
      (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type, cg->i64_type}, 3, false);
  LLVMValueRef elem_size = LLVMConstInt(cg->i64_type, 8, false);
  LLVMValueRef elem_count = LLVMConstInt(cg->i64_type, count, false);
  LLVMValueRef arr = LLVMBuildCall2(cg->builder, arr_from_type, fn_array_from,
      (LLVMValueRef[]){temp_buf, elem_size, elem_count}, 3, "arr");

  /* Free temp buffer */
  LLVMValueRef fn_free = LLVMGetNamedFunction(cg->mod, "free");
  if (!fn_free) {
    LLVMTypeRef free_t = LLVMFunctionType(cg->void_type,
                                          (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
    fn_free = LLVMAddFunction(cg->mod, "free", free_t);
  }
  LLVMTypeRef free_t = LLVMFunctionType(cg->void_type,
                                        (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
  LLVMBuildCall2(cg->builder, free_t, fn_free, (LLVMValueRef[]){temp_buf}, 1, "");

  return arr;
}

/* Emit array index: array runtime values go through __qisc_array_get. */
static LLVMValueRef emit_index(Codegen *cg, AstNode *node) {
  LLVMValueRef obj = emit_expr(cg, node->as.index.object);
  LLVMValueRef idx = emit_expr(cg, node->as.index.index);
  if (cg->had_error)
    return obj;

  if (cg_expr_is_array_like(cg, node->as.index.object)) {
    LLVMValueRef fn_array_get =
        LLVMGetNamedFunction(cg->mod, "__qisc_array_get");
    if (!fn_array_get) {
      LLVMTypeRef fn_type = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2,
          false);
      fn_array_get = LLVMAddFunction(cg->mod, "__qisc_array_get", fn_type);
    }
    LLVMTypeRef fn_type = LLVMFunctionType(
        cg->i8ptr_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2,
        false);
    LLVMValueRef elem_ptr = LLVMBuildCall2(
        cg->builder, fn_type, fn_array_get, (LLVMValueRef[]){obj, idx}, 2,
        "elem_ptr");
    return LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "idx_val");
  }

  LLVMValueRef ptr =
      LLVMBuildGEP2(cg->builder, cg->i64_type, obj, &idx, 1, "idx_ptr");
  return LLVMBuildLoad2(cg->builder, cg->i64_type, ptr, "idx_val");
}

/* Emit member access: obj.member — handles enum variants */
static LLVMValueRef emit_member(Codegen *cg, AstNode *node) {
  /* For enum variants like Color.Red → lookup the numeric value */
  if (node->as.member.object &&
      node->as.member.object->type == AST_IDENTIFIER) {
    const char *type_name = node->as.member.object->as.identifier.name;
    const char *variant = node->as.member.member;
    /* Search for the enum variant in scope */
    char full_name[256];
    snprintf(full_name, sizeof(full_name), "%s.%s", type_name, variant);
    CgSymbol *sym = cg_lookup(cg, full_name);
    if (sym) {
      return LLVMBuildLoad2(cg->builder, sym->type, sym->alloca, full_name);
    }
    /* Not an enum — fall through to struct field access below */
  }

  /* General struct field access: obj.field → GEP into struct */
  LLVMValueRef obj = emit_expr(cg, node->as.member.object);
  if (cg->had_error)
    return LLVMConstInt(cg->i64_type, 0, false);

  const char *field = node->as.member.member;
  LLVMTypeRef obj_type = LLVMTypeOf(obj);

  /* obj should be a pointer to a struct type */
  if (LLVMGetTypeKind(obj_type) == LLVMPointerTypeKind) {
    /* Find which struct type this is */
    for (int i = 0; i < cg->struct_count; i++) {
      CgStructType *st = &cg->structs[i];
      LLVMTypeRef st_ptr = LLVMPointerType(st->llvm_type, 0);
      if (obj_type == st_ptr) {
        /* Found the struct — look up field index */
        for (int j = 0; j < st->field_count; j++) {
          if (strcmp(st->field_names[j], field) == 0) {
            LLVMValueRef gep =
                LLVMBuildStructGEP2(cg->builder, st->llvm_type, obj, j, field);
            return LLVMBuildLoad2(cg->builder, st->field_types[j], gep, field);
          }
        }
        cg_error(cg, "Struct '%s' has no field '%s'", st->name, field);
        return LLVMConstInt(cg->i64_type, 0, false);
      }
    }
  }

  /* Fallback for non-struct pointers — return as-is */
  return LLVMConstInt(cg->i64_type, 0, false);
}

static LLVMValueRef emit_expr(Codegen *cg, AstNode *node) {
  if (!node || cg->had_error)
    return LLVMConstInt(cg->i64_type, 0, false);

  switch (node->type) {
  case AST_INT_LITERAL:
    return emit_int_literal(cg, node);
  case AST_FLOAT_LITERAL:
    return emit_float_literal(cg, node);
  case AST_BOOL_LITERAL:
    return emit_bool_literal(cg, node);
  case AST_STRING_LITERAL:
    return emit_string_literal(cg, node);
  case AST_NONE_LITERAL:
    return emit_none_literal(cg, node);
  case AST_IDENTIFIER:
    return emit_identifier(cg, node);
  case AST_BINARY_OP:
    return emit_binary(cg, node);
  case AST_UNARY_OP:
    return emit_unary(cg, node);
  case AST_CALL:
    return emit_call(cg, node);
  case AST_ARRAY_LITERAL:
    return emit_array_literal(cg, node);
  case AST_INDEX:
    return emit_index(cg, node);
  case AST_MEMBER:
    return emit_member(cg, node);
  case AST_LAMBDA: {
    /* Lambda: create an anonymous function and return its pointer */
    static int lambda_id = 0;
    char lname[64];
    snprintf(lname, sizeof(lname), "__lambda_%d", lambda_id++);
    int pc = node->as.lambda.params.count;
    LLVMTypeRef lambda_ret_type =
        cg->lambda_hint_active && cg->lambda_hint_return_type
            ? cg->lambda_hint_return_type
            : cg->i64_type;
    LLVMTypeRef lambda_param_type =
        cg->lambda_hint_active && cg->lambda_hint_param_type
            ? cg->lambda_hint_param_type
            : cg->i64_type;

    /* Stream-lowered lambdas receive contextual param/return types. */
    LLVMTypeRef *pts = NULL;
    if (pc > 0) {
      pts = calloc(pc, sizeof(LLVMTypeRef));
      for (int i = 0; i < pc; i++)
        pts[i] = lambda_param_type;
    }
    LLVMTypeRef fn_type = LLVMFunctionType(lambda_ret_type, pts, pc, false);
    free(pts);
    LLVMValueRef fn = LLVMAddFunction(cg->mod, lname, fn_type);
    LLVMSetLinkage(fn, LLVMPrivateLinkage);

    /* Save current state */
    LLVMValueRef saved_fn = cg->current_fn;
    LLVMBasicBlockRef saved_bb = LLVMGetInsertBlock(cg->builder);

    /* Create entry block */
    LLVMBasicBlockRef entry =
        LLVMAppendBasicBlockInContext(cg->ctx, fn, "entry");
    LLVMPositionBuilderAtEnd(cg->builder, entry);
    cg->current_fn = fn;
    cg_push_scope(cg);

    /* Alloca params */
    for (int i = 0; i < pc; i++) {
      AstNode *p = node->as.lambda.params.items[i];
      const char *pname =
          (p->type == AST_VAR_DECL)
              ? p->as.var_decl.name
              : (p->type == AST_IDENTIFIER ? p->as.identifier.name : "arg");
      LLVMValueRef alloca =
          LLVMBuildAlloca(cg->builder, lambda_param_type, pname);
      LLVMBuildStore(cg->builder, LLVMGetParam(fn, i), alloca);
      cg_define(cg, pname, alloca, lambda_param_type);
    }

    /* Emit body */
    if (node->as.lambda.body) {
      if (node->as.lambda.body->type == AST_BLOCK)
        emit_block(cg, node->as.lambda.body);
      else {
        /* Single expression body */
        LLVMValueRef result = emit_expr(cg, node->as.lambda.body);
        result = cg_coerce_value(cg, result, lambda_ret_type);
        LLVMBuildRet(cg->builder, result);
      }
    }

    /* Implicit return if needed */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
      LLVMValueRef fallback =
          lambda_ret_type == cg->i8ptr_type
              ? LLVMConstNull(cg->i8ptr_type)
              : LLVMConstInt(lambda_ret_type, 0, false);
      LLVMBuildRet(cg->builder, fallback);
    }

    cg_pop_scope(cg);

    /* Restore state */
    cg->current_fn = saved_fn;
    LLVMPositionBuilderAtEnd(cg->builder, saved_bb);

    return fn;
  }
  case AST_BLOCK:
    /* Block as expression (do-block): emit all statements, return 0 */
    emit_block(cg, node);
    return LLVMConstInt(cg->i64_type, 0, false);
  case AST_EXPR_STMT:
    /* Expression statement used in expression context — the node IS the expr */
    /* The actual expression data is in the same union (binary, call, etc.) */
    return LLVMConstInt(cg->i64_type, 0, false);
  case AST_PIPELINE:
    /* Pipeline: a |> b — for now, just evaluate left side */
    if (node->as.binary.left)
      return emit_expr(cg, node->as.binary.left);
    return LLVMConstInt(cg->i64_type, 0, false);
  case AST_STRUCT_LITERAL: {
    /* Struct literal: Person { name: "Alice", age: 30 } */
    const char *sname = node->as.struct_decl.name;
    CgStructType *st = cg_find_struct(cg, sname);
    if (!st) {
      cg_error(cg, "Unknown struct type: %s", sname);
      return LLVMConstInt(cg->i64_type, 0, false);
    }
    /* Malloc the struct */
    LLVMValueRef struct_size = LLVMSizeOf(st->llvm_type);
    LLVMValueRef fn_malloc = LLVMGetNamedFunction(cg->mod, "malloc");
    if (!fn_malloc) {
      LLVMTypeRef mt = LLVMFunctionType(
          cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
      fn_malloc = LLVMAddFunction(cg->mod, "malloc", mt);
    }
    LLVMTypeRef mt = LLVMFunctionType(cg->i8ptr_type,
                                      (LLVMTypeRef[]){cg->i64_type}, 1, false);
    LLVMValueRef raw = LLVMBuildCall2(cg->builder, mt, fn_malloc, &struct_size,
                                      1, "struct_raw");
    LLVMTypeRef st_ptr_type = LLVMPointerType(st->llvm_type, 0);
    LLVMValueRef ptr = LLVMBuildBitCast(cg->builder, raw, st_ptr_type, sname);

    /* Store each field */
    for (int i = 0; i < node->as.struct_decl.fields.count; i++) {
      AstNode *fa = node->as.struct_decl.fields.items[i];
      if (fa->type != AST_ASSIGN || !fa->as.assign.target)
        continue;
      const char *fname = fa->as.assign.target->as.identifier.name;
      /* Find field index */
      for (int j = 0; j < st->field_count; j++) {
        if (strcmp(st->field_names[j], fname) == 0) {
          LLVMValueRef fval = emit_expr(cg, fa->as.assign.value);
          LLVMValueRef gep =
              LLVMBuildStructGEP2(cg->builder, st->llvm_type, ptr, j, fname);
          LLVMBuildStore(cg->builder, fval, gep);
          break;
        }
      }
    }
    return ptr;
  }
  default:
    cg_error(cg, "Cannot emit expression for node type: %d", node->type);
    return LLVMConstInt(cg->i64_type, 0, false);
  }
}

/* ======== Statement Emission ======== */

static void emit_var_decl(Codegen *cg, AstNode *node) {
  const char *name = node->as.var_decl.name;
  AstNode *initializer = node->as.var_decl.initializer;
  bool is_closure_init =
      initializer && initializer->type == AST_LAMBDA;
  bool is_closure_value_init =
      initializer && cg_expr_is_closure_like(cg, initializer);

  /* Determine type */
  LLVMTypeRef var_type = cg->i64_type; /* default */
  if (node->as.var_decl.type_info) {
    var_type = cg_type_from_name(cg, node->as.var_decl.type_info->name);
  } else if (node->as.var_decl.initializer) {
    /* Auto: infer from initializer */
    LLVMValueRef init_val;
    if (is_closure_init) {
      AstNode *lambda = node->as.var_decl.initializer;
      int param_count = lambda->as.lambda.params.count;
      LLVMTypeRef lambda_return_type =
          cg_lambda_general_return_type(cg, lambda);
      LLVMTypeRef *param_types = calloc(param_count ? param_count : 1,
                                        sizeof(LLVMTypeRef));
      for (int i = 0; i < param_count; i++)
        param_types[i] = cg->i64_type;
      init_val = cg_emit_general_closure_value(cg, lambda, param_types,
                                               param_count, lambda_return_type);
      free(param_types);
    } else {
      init_val = emit_expr(cg, node->as.var_decl.initializer);
    }
    if (cg->had_error)
      return;
    var_type = LLVMTypeOf(init_val);

    /* Create alloca and store */
    LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, var_type, name);
    LLVMBuildStore(cg->builder, init_val, alloca);
    cg_define(cg, name, alloca, var_type);
    cg_attach_callable_metadata_from_initializer(cg, name,
                                                 node->as.var_decl.initializer);

    /* Track array length for auto-inferred arrays */
    if (node->as.var_decl.initializer->type == AST_ARRAY_LITERAL) {
      int alen = node->as.var_decl.initializer->as.array_literal.elements.count;
      char len_name[300];
      snprintf(len_name, sizeof(len_name), "__%s__len", name);
      LLVMValueRef len_alloca =
          LLVMBuildAlloca(cg->builder, cg->i64_type, len_name);
      LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, alen, false),
                     len_alloca);
      cg_define(cg, len_name, len_alloca, cg->i64_type);
    }
    /* Track array length for array-like runtime values */
    else if (var_type == cg->i8ptr_type) {
      AstNode *init = node->as.var_decl.initializer;
      if (cg_expr_is_array_like(cg, init)) {
        char len_name[300];
        snprintf(len_name, sizeof(len_name), "__%s__len", name);
        LLVMValueRef len_alloca =
            LLVMBuildAlloca(cg->builder, cg->i64_type, len_name);
        
        /* Call __qisc_array_len(init_val) */
        LLVMValueRef fn_len = LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
        if (fn_len) {
          LLVMTypeRef len_fn_type = LLVMFunctionType(cg->i64_type,
              (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          LLVMValueRef runtime_len = LLVMBuildCall2(cg->builder, len_fn_type, fn_len,
              (LLVMValueRef[]){init_val}, 1, "arr_len");
          LLVMBuildStore(cg->builder, runtime_len, len_alloca);
        } else {
          LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false), len_alloca);
        }
        cg_define(cg, len_name, len_alloca, cg->i64_type);
      }
    }

    {
      CgStreamKind stream_kind = cg_expr_stream_kind(cg, initializer);
      if (stream_kind != CG_STREAM_NONE) {
        char marker_name[300];
        snprintf(marker_name, sizeof(marker_name), "__%s__stream_%s", name,
                 stream_kind == CG_STREAM_STRING ? "string" : "int");
        LLVMValueRef marker =
            LLVMBuildAlloca(cg->builder, cg->i1_type, marker_name);
        LLVMBuildStore(cg->builder, LLVMConstInt(cg->i1_type, 1, false),
                       marker);
        cg_define(cg, marker_name, marker, cg->i1_type);
      }
    }
    if (is_closure_value_init) {
      char marker_name[300];
      snprintf(marker_name, sizeof(marker_name), "__%s__closure", name);
      LLVMValueRef marker =
          LLVMBuildAlloca(cg->builder, cg->i1_type, marker_name);
      LLVMBuildStore(cg->builder, LLVMConstInt(cg->i1_type, 1, false), marker);
      cg_define(cg, marker_name, marker, cg->i1_type);
    }
    return;
  }

  /* Create alloca */
  LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, var_type, name);

  /* Initialize */
  if (node->as.var_decl.initializer) {
    LLVMValueRef init_val;
    if (is_closure_init) {
      AstNode *lambda = initializer;
      int param_count = lambda->as.lambda.params.count;
      LLVMTypeRef lambda_return_type =
          cg_lambda_general_return_type(cg, lambda);
      LLVMTypeRef *param_types = calloc(param_count ? param_count : 1,
                                        sizeof(LLVMTypeRef));
      for (int i = 0; i < param_count; i++)
        param_types[i] = cg->i64_type;
      init_val = cg_emit_general_closure_value(cg, lambda, param_types,
                                               param_count, lambda_return_type);
      free(param_types);
    } else {
      init_val = emit_expr(cg, initializer);
    }
    if (cg->had_error)
      return;
    /* Cast if needed (e.g., int to float) */
    LLVMTypeRef init_type = LLVMTypeOf(init_val);
    if (init_type != var_type) {
      if (var_type == cg->f64_type && init_type == cg->i64_type)
        init_val = LLVMBuildSIToFP(cg->builder, init_val, cg->f64_type, "cast");
      else if (var_type == cg->i64_type && init_type == cg->f64_type)
        init_val = LLVMBuildFPToSI(cg->builder, init_val, cg->i64_type, "cast");
      else if (var_type == cg->i64_type && init_type == cg->i1_type)
        init_val = LLVMBuildZExt(cg->builder, init_val, cg->i64_type, "cast");
    }
    LLVMBuildStore(cg->builder, init_val, alloca);
  } else {
    /* Zero-initialize */
    LLVMBuildStore(cg->builder, LLVMConstNull(var_type), alloca);
  }

  cg_define(cg, name, alloca, var_type);
  if (initializer) {
    cg_attach_callable_metadata_from_initializer(cg, name, initializer);
  }

  /* Track array length: store __name__len if initializer is an array literal */
  if (node->as.var_decl.initializer &&
      node->as.var_decl.initializer->type == AST_ARRAY_LITERAL) {
    int alen = node->as.var_decl.initializer->as.array_literal.elements.count;
    char len_name[300];
    snprintf(len_name, sizeof(len_name), "__%s__len", name);
    LLVMValueRef len_alloca =
        LLVMBuildAlloca(cg->builder, cg->i64_type, len_name);
    LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, alen, false),
                   len_alloca);
    cg_define(cg, len_name, len_alloca, cg->i64_type);
  }
  else if (initializer && var_type == cg->i8ptr_type &&
           cg_expr_is_array_like(cg, initializer)) {
    char len_name[300];
    LLVMValueRef len_alloca =
        LLVMBuildAlloca(cg->builder, cg->i64_type, "__array_len");
    LLVMValueRef fn_len = LLVMGetNamedFunction(cg->mod, "__qisc_array_len");
    LLVMValueRef stored_array =
        LLVMBuildLoad2(cg->builder, var_type, alloca, "arr_for_len");
    snprintf(len_name, sizeof(len_name), "__%s__len", name);
    if (fn_len) {
      LLVMTypeRef len_fn_type = LLVMFunctionType(
          cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
      LLVMValueRef runtime_len = LLVMBuildCall2(
          cg->builder, len_fn_type, fn_len, (LLVMValueRef[]){stored_array}, 1,
          "arr_len");
      LLVMBuildStore(cg->builder, runtime_len, len_alloca);
    } else {
      LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false),
                     len_alloca);
    }
    cg_define(cg, len_name, len_alloca, cg->i64_type);
  }

  if (initializer) {
    CgStreamKind stream_kind = cg_expr_stream_kind(cg, initializer);
    if (stream_kind != CG_STREAM_NONE) {
      char marker_name[300];
      snprintf(marker_name, sizeof(marker_name), "__%s__stream_%s", name,
               stream_kind == CG_STREAM_STRING ? "string" : "int");
      LLVMValueRef marker =
          LLVMBuildAlloca(cg->builder, cg->i1_type, marker_name);
      LLVMBuildStore(cg->builder, LLVMConstInt(cg->i1_type, 1, false), marker);
      cg_define(cg, marker_name, marker, cg->i1_type);
    }
    if (is_closure_value_init) {
      char marker_name[300];
      snprintf(marker_name, sizeof(marker_name), "__%s__closure", name);
      LLVMValueRef marker =
          LLVMBuildAlloca(cg->builder, cg->i1_type, marker_name);
      LLVMBuildStore(cg->builder, LLVMConstInt(cg->i1_type, 1, false), marker);
      cg_define(cg, marker_name, marker, cg->i1_type);
    }
  }
}

static void emit_assign(Codegen *cg, AstNode *node) {
  if (!node->as.assign.target ||
      node->as.assign.target->type != AST_IDENTIFIER) {
    cg_error(cg, "Only simple variable assignment supported");
    return;
  }

  const char *name = node->as.assign.target->as.identifier.name;
  CgSymbol *sym = cg_lookup(cg, name);
  if (!sym) {
    cg_error(cg, "Undefined variable: %s", name);
    return;
  }

  LLVMValueRef val = emit_expr(cg, node->as.assign.value);
  if (cg->had_error)
    return;

  /* Type cast if needed */
  LLVMTypeRef val_type = LLVMTypeOf(val);
  if (val_type != sym->type) {
    if (sym->type == cg->f64_type && val_type == cg->i64_type)
      val = LLVMBuildSIToFP(cg->builder, val, cg->f64_type, "cast");
    else if (sym->type == cg->i64_type && val_type == cg->f64_type)
      val = LLVMBuildFPToSI(cg->builder, val, cg->i64_type, "cast");
    else if (sym->type == cg->i64_type && val_type == cg->i1_type)
      val = LLVMBuildZExt(cg->builder, val, cg->i64_type, "cast");
  }

  LLVMBuildStore(cg->builder, val, sym->alloca);
}

static void emit_give(Codegen *cg, AstNode *node) {
  LLVMValueRef val = NULL;
  if (node->as.give_stmt.value) {
    val = emit_expr(cg, node->as.give_stmt.value);
    if (cg->had_error)
      return;

    /* Cast to match function return type */
    LLVMTypeRef ret_type =
        LLVMGetReturnType(LLVMGlobalGetValueType(cg->current_fn));
    LLVMTypeRef val_type = LLVMTypeOf(val);
    if (val_type != ret_type) {
      if (ret_type == cg->i64_type && val_type == cg->i1_type)
        val = LLVMBuildZExt(cg->builder, val, cg->i64_type, "cast");
      else if (ret_type == cg->f64_type && val_type == cg->i64_type)
        val = LLVMBuildSIToFP(cg->builder, val, cg->f64_type, "cast");
    }
  }

  /* Profile instrumentation: inject exit call before return */
  if (cg->profile_enabled && cg->fn_profile_exit && cg->current_fn_name) {
    emit_profile_call(cg, cg->fn_profile_exit, cg->current_fn_name);
  }

  if (val)
    LLVMBuildRet(cg->builder, val);
  else
    LLVMBuildRetVoid(cg->builder);
}

static void emit_if(Codegen *cg, AstNode *node) {
  char branch_location[256];
  const char *fn_name = cg->current_fn_name ? cg->current_fn_name : "module";
  LLVMValueRef cond = emit_expr(cg, node->as.if_stmt.condition);
  if (cg->had_error)
    return;

  /* Ensure condition is i1 */
  if (LLVMTypeOf(cond) != cg->i1_type) {
    cond = LLVMBuildICmp(cg->builder, LLVMIntNE, cond,
                         LLVMConstNull(LLVMTypeOf(cond)), "tobool");
  }

  LLVMBasicBlockRef then_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "then");
  LLVMBasicBlockRef else_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "else");
  LLVMBasicBlockRef merge_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "merge");

  snprintf(branch_location, sizeof(branch_location), "%s:%d", fn_name,
           node->line);
  LLVMBuildCondBr(cg->builder, cond, then_bb, else_bb);

  /* Then */
  LLVMPositionBuilderAtEnd(cg->builder, then_bb);
  if (cg->profile_enabled) {
    emit_profile_branch_call(cg, branch_location, true);
  }
  cg_push_scope(cg);
  emit_stmt(cg, node->as.if_stmt.then_branch);
  cg_pop_scope(cg);
  /* Only add branch if block isn't already terminated */
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
    LLVMBuildBr(cg->builder, merge_bb);

  /* Else */
  LLVMPositionBuilderAtEnd(cg->builder, else_bb);
  if (cg->profile_enabled) {
    emit_profile_branch_call(cg, branch_location, false);
  }
  if (node->as.if_stmt.else_branch) {
    cg_push_scope(cg);
    emit_stmt(cg, node->as.if_stmt.else_branch);
    cg_pop_scope(cg);
  }
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
    LLVMBuildBr(cg->builder, merge_bb);

  /* Merge */
  LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
}

static void emit_while(Codegen *cg, AstNode *node) {
  char loop_location[256];
  const char *fn_name = cg->current_fn_name ? cg->current_fn_name : "module";
  LLVMBasicBlockRef cond_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "while.cond");
  LLVMBasicBlockRef body_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "while.body");
  LLVMBasicBlockRef end_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "while.end");

  /* Save break/continue targets */
  LLVMBasicBlockRef prev_break = cg->break_bb;
  LLVMBasicBlockRef prev_continue = cg->continue_bb;
  LLVMValueRef loop_counter = NULL;
  cg->break_bb = end_bb;
  cg->continue_bb = cond_bb;

  snprintf(loop_location, sizeof(loop_location), "%s:%d", fn_name, node->line);
  if (cg->profile_enabled) {
    loop_counter =
        LLVMBuildAlloca(cg->builder, cg->i64_type, "__qisc_loop_count");
    LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false),
                   loop_counter);
  }

  LLVMBuildBr(cg->builder, cond_bb);

  /* Condition */
  LLVMPositionBuilderAtEnd(cg->builder, cond_bb);
  LLVMValueRef cond = emit_expr(cg, node->as.while_stmt.condition);
  if (LLVMTypeOf(cond) != cg->i1_type) {
    cond = LLVMBuildICmp(cg->builder, LLVMIntNE, cond,
                         LLVMConstNull(LLVMTypeOf(cond)), "tobool");
  }
  LLVMValueRef cond_br = LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
  
  /* Apply loop pragma metadata (vectorization, etc.) */
  cg_apply_loop_pragmas(cg, cond_br);

  /* Body */
  LLVMPositionBuilderAtEnd(cg->builder, body_bb);
  if (loop_counter) {
    LLVMValueRef old_count =
        LLVMBuildLoad2(cg->builder, cg->i64_type, loop_counter, "loop_count");
    LLVMValueRef new_count = LLVMBuildAdd(
        cg->builder, old_count, LLVMConstInt(cg->i64_type, 1, false),
        "loop_count_next");
    LLVMBuildStore(cg->builder, new_count, loop_counter);
  }
  cg_push_scope(cg);
  emit_stmt(cg, node->as.while_stmt.body);
  cg_pop_scope(cg);
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
    LLVMValueRef back_edge = LLVMBuildBr(cg->builder, cond_bb);
    /* Apply loop metadata to back-edge as well */
    cg_apply_loop_pragmas(cg, back_edge);
  }

  /* End */
  LLVMPositionBuilderAtEnd(cg->builder, end_bb);
  if (loop_counter) {
    LLVMValueRef iterations =
        LLVMBuildLoad2(cg->builder, cg->i64_type, loop_counter, "loop_iters");
    emit_profile_loop_call(cg, loop_location, iterations);
  }
  cg->break_bb = prev_break;
  cg->continue_bb = prev_continue;
}

static void emit_for(Codegen *cg, AstNode *node) {
  char loop_location[256];
  const char *fn_name = cg->current_fn_name ? cg->current_fn_name : "module";
  LLVMBasicBlockRef cond_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "for.cond");
  LLVMBasicBlockRef body_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "for.body");
  LLVMBasicBlockRef step_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "for.step");
  LLVMBasicBlockRef end_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "for.end");

  LLVMBasicBlockRef prev_break = cg->break_bb;
  LLVMBasicBlockRef prev_continue = cg->continue_bb;
  LLVMValueRef loop_counter = NULL;
  cg->break_bb = end_bb;
  cg->continue_bb = step_bb;

  cg_push_scope(cg);
  snprintf(loop_location, sizeof(loop_location), "%s:%d", fn_name, node->line);
  if (cg->profile_enabled) {
    loop_counter =
        LLVMBuildAlloca(cg->builder, cg->i64_type, "__qisc_loop_count");
    LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false),
                   loop_counter);
  }

  /* Detect for-in vs C-style for */
  if (node->as.for_stmt.var_name && node->as.for_stmt.iterable) {
    /* === For-in loop: for var in iterable { body } ===
     * Create hidden __idx counter, emit iterable, load arr[__idx] each iter */
    LLVMValueRef arr = emit_expr(cg, node->as.for_stmt.iterable);
    if (cg->had_error) {
      cg_pop_scope(cg);
      return;
    }

    /* Determine array length */
    int arr_len = 0;
    if (node->as.for_stmt.iterable->type == AST_ARRAY_LITERAL) {
      arr_len = node->as.for_stmt.iterable->as.array_literal.elements.count;
    } else if (node->as.for_stmt.iterable->type == AST_IDENTIFIER) {
      /* Look up hidden __name__len variable */
      const char *iname = node->as.for_stmt.iterable->as.identifier.name;
      char len_key[300];
      snprintf(len_key, sizeof(len_key), "__%s__len", iname);
      CgSymbol *len_sym = cg_lookup(cg, len_key);
      if (len_sym) {
        /* We'll use a runtime load instead of compile-time constant */
        LLVMValueRef runtime_len = LLVMBuildLoad2(cg->builder, cg->i64_type,
                                                  len_sym->alloca, "arr_len");
        /* Store it locally for the condition to use */
        LLVMValueRef len_alloca =
            LLVMBuildAlloca(cg->builder, cg->i64_type, "__forin_len");
        LLVMBuildStore(cg->builder, runtime_len, len_alloca);

        /* Store array pointer */
        LLVMValueRef arr_alloca =
            LLVMBuildAlloca(cg->builder, cg->i8ptr_type, "__arr");
        LLVMBuildStore(cg->builder, arr, arr_alloca);

        /* Create hidden index counter */
        LLVMValueRef idx_alloca =
            LLVMBuildAlloca(cg->builder, cg->i64_type, "__idx");
        LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false),
                       idx_alloca);

        /* Create the loop variable */
        LLVMValueRef var_alloca = LLVMBuildAlloca(cg->builder, cg->i64_type,
                                                  node->as.for_stmt.var_name);
        LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false),
                       var_alloca);
        cg_define(cg, node->as.for_stmt.var_name, var_alloca, cg->i64_type);

        LLVMBuildBr(cg->builder, cond_bb);

        /* Condition: __idx < len */
        LLVMPositionBuilderAtEnd(cg->builder, cond_bb);
        LLVMValueRef idx_val =
            LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "idx");
        LLVMValueRef len_val =
            LLVMBuildLoad2(cg->builder, cg->i64_type, len_alloca, "len");
        LLVMValueRef cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, idx_val,
                                          len_val, "forin_cond");
        LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);

        /* Body */
        LLVMPositionBuilderAtEnd(cg->builder, body_bb);
        if (loop_counter) {
          LLVMValueRef old_count = LLVMBuildLoad2(cg->builder, cg->i64_type,
                                                  loop_counter, "loop_count");
          LLVMValueRef new_count = LLVMBuildAdd(
              cg->builder, old_count, LLVMConstInt(cg->i64_type, 1, false),
              "loop_count_next");
          LLVMBuildStore(cg->builder, new_count, loop_counter);
        }
        LLVMValueRef cur_arr =
            LLVMBuildLoad2(cg->builder, cg->i8ptr_type, arr_alloca, "arr_ptr");
        LLVMValueRef cur_idx =
            LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "cur_idx");
        LLVMValueRef elem_ptr = LLVMBuildGEP2(cg->builder, cg->i64_type,
                                              cur_arr, &cur_idx, 1, "elem");
        LLVMValueRef elem_val =
            LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "elem_val");
        LLVMBuildStore(cg->builder, elem_val, var_alloca);

        emit_stmt(cg, node->as.for_stmt.body);
        if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
          LLVMBuildBr(cg->builder, step_bb);

        /* Step: __idx++ */
        LLVMPositionBuilderAtEnd(cg->builder, step_bb);
        LLVMValueRef old_idx =
            LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "old_idx");
        LLVMValueRef new_idx =
            LLVMBuildAdd(cg->builder, old_idx,
                         LLVMConstInt(cg->i64_type, 1, false), "next_idx");
        LLVMBuildStore(cg->builder, new_idx, idx_alloca);
        LLVMBuildBr(cg->builder, cond_bb);

        goto for_end;
      }
    }

    /* Store array pointer for later GEP */
    LLVMValueRef arr_alloca =
        LLVMBuildAlloca(cg->builder, cg->i8ptr_type, "__arr");
    LLVMBuildStore(cg->builder, arr, arr_alloca);

    /* Create hidden index counter: __idx = 0 */
    LLVMValueRef idx_alloca =
        LLVMBuildAlloca(cg->builder, cg->i64_type, "__idx");
    LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false),
                   idx_alloca);

    /* Create the loop variable */
    LLVMValueRef var_alloca =
        LLVMBuildAlloca(cg->builder, cg->i64_type, node->as.for_stmt.var_name);
    LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, 0, false),
                   var_alloca);
    cg_define(cg, node->as.for_stmt.var_name, var_alloca, cg->i64_type);

    LLVMBuildBr(cg->builder, cond_bb);

    /* Condition: __idx < arr_len */
    LLVMPositionBuilderAtEnd(cg->builder, cond_bb);
    LLVMValueRef idx_val =
        LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "idx");
    LLVMValueRef len_val = LLVMConstInt(cg->i64_type, arr_len, false);
    LLVMValueRef cond =
        LLVMBuildICmp(cg->builder, LLVMIntSLT, idx_val, len_val, "forin_cond");
    LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);

    /* Body: load var = arr[__idx] */
    LLVMPositionBuilderAtEnd(cg->builder, body_bb);
    if (loop_counter) {
      LLVMValueRef old_count = LLVMBuildLoad2(cg->builder, cg->i64_type,
                                              loop_counter, "loop_count");
      LLVMValueRef new_count = LLVMBuildAdd(
          cg->builder, old_count, LLVMConstInt(cg->i64_type, 1, false),
          "loop_count_next");
      LLVMBuildStore(cg->builder, new_count, loop_counter);
    }
    LLVMValueRef cur_arr =
        LLVMBuildLoad2(cg->builder, cg->i8ptr_type, arr_alloca, "arr_ptr");
    LLVMValueRef cur_idx =
        LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "cur_idx");
    LLVMValueRef elem_ptr =
        LLVMBuildGEP2(cg->builder, cg->i64_type, cur_arr, &cur_idx, 1, "elem");
    LLVMValueRef elem_val =
        LLVMBuildLoad2(cg->builder, cg->i64_type, elem_ptr, "elem_val");
    LLVMBuildStore(cg->builder, elem_val, var_alloca);

    emit_stmt(cg, node->as.for_stmt.body);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
      LLVMBuildBr(cg->builder, step_bb);

    /* Step: __idx++ */
    LLVMPositionBuilderAtEnd(cg->builder, step_bb);
    LLVMValueRef old_idx =
        LLVMBuildLoad2(cg->builder, cg->i64_type, idx_alloca, "old_idx");
    LLVMValueRef new_idx = LLVMBuildAdd(
        cg->builder, old_idx, LLVMConstInt(cg->i64_type, 1, false), "next_idx");
    LLVMBuildStore(cg->builder, new_idx, idx_alloca);
    LLVMBuildBr(cg->builder, cond_bb);

  } else {
    /* === C-style for: for init; cond; update { body } === */
    if (node->as.for_stmt.init) {
      emit_stmt(cg, node->as.for_stmt.init);
    }

    LLVMBuildBr(cg->builder, cond_bb);

    /* Condition */
    LLVMPositionBuilderAtEnd(cg->builder, cond_bb);
    if (node->as.for_stmt.condition) {
      LLVMValueRef cond = emit_expr(cg, node->as.for_stmt.condition);
      if (LLVMTypeOf(cond) != cg->i1_type) {
        cond = LLVMBuildICmp(cg->builder, LLVMIntNE, cond,
                             LLVMConstNull(LLVMTypeOf(cond)), "tobool");
      }
      LLVMValueRef cond_br = LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
      /* Apply loop pragma metadata (vectorization, etc.) */
      cg_apply_loop_pragmas(cg, cond_br);
    } else {
      LLVMBuildBr(cg->builder, body_bb);
    }

    /* Body */
    LLVMPositionBuilderAtEnd(cg->builder, body_bb);
    if (loop_counter) {
      LLVMValueRef old_count = LLVMBuildLoad2(cg->builder, cg->i64_type,
                                              loop_counter, "loop_count");
      LLVMValueRef new_count = LLVMBuildAdd(
          cg->builder, old_count, LLVMConstInt(cg->i64_type, 1, false),
          "loop_count_next");
      LLVMBuildStore(cg->builder, new_count, loop_counter);
    }
    emit_stmt(cg, node->as.for_stmt.body);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
      LLVMBuildBr(cg->builder, step_bb);

    /* Step */
    LLVMPositionBuilderAtEnd(cg->builder, step_bb);
    if (node->as.for_stmt.update) {
      emit_stmt(cg, node->as.for_stmt.update);
    }
    LLVMValueRef back_edge = LLVMBuildBr(cg->builder, cond_bb);
    /* Apply loop pragma metadata to back-edge */
    cg_apply_loop_pragmas(cg, back_edge);
  }

for_end:
  /* End */
  LLVMPositionBuilderAtEnd(cg->builder, end_bb);
  if (loop_counter) {
    LLVMValueRef iterations =
        LLVMBuildLoad2(cg->builder, cg->i64_type, loop_counter, "loop_iters");
    emit_profile_loop_call(cg, loop_location, iterations);
  }
  cg_pop_scope(cg);
  cg->break_bb = prev_break;
  cg->continue_bb = prev_continue;
}

static void emit_block(Codegen *cg, AstNode *node) {
  if (!node || node->type != AST_BLOCK)
    return;
  cg_push_scope(cg);
  for (int i = 0; i < node->as.block.statements.count; i++) {
    emit_stmt(cg, node->as.block.statements.items[i]);
    if (cg->had_error)
      break;
    /* Stop if block is already terminated (e.g., after return) */
    if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
      break;
  }
  cg_pop_scope(cg);
}

static void emit_stmt(Codegen *cg, AstNode *node) {
  if (!node || cg->had_error)
    return;

  /* Skip if current block is already terminated */
  if (LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
    return;

  /* Check for personality-aware line notes */
  if (cg->debug_personality_enabled && node->line > 0) {
    add_line_personality(cg, node->line);
  }

  switch (node->type) {
  case AST_VAR_DECL:
    emit_var_decl(cg, node);
    break;
  case AST_ASSIGN:
    emit_assign(cg, node);
    break;
  case AST_GIVE:
    emit_give(cg, node);
    break;
  case AST_BLOCK:
    emit_block(cg, node);
    break;
  case AST_IF:
    emit_if(cg, node);
    break;
  case AST_WHILE:
    emit_while(cg, node);
    break;
  case AST_FOR:
    emit_for(cg, node);
    break;
  case AST_BREAK:
    if (cg->break_bb)
      LLVMBuildBr(cg->builder, cg->break_bb);
    break;
  case AST_CONTINUE:
    if (cg->continue_bb)
      LLVMBuildBr(cg->builder, cg->continue_bb);
    break;

  /* Expression statements — evaluate and discard */
  case AST_EXPR_STMT:
  case AST_CALL:
  case AST_BINARY_OP:
  case AST_UNARY_OP:
  case AST_INT_LITERAL:
  case AST_FLOAT_LITERAL:
  case AST_STRING_LITERAL:
  case AST_BOOL_LITERAL:
  case AST_IDENTIFIER:
  case AST_ARRAY_LITERAL:
  case AST_INDEX:
  case AST_MEMBER:
  case AST_LAMBDA:
  case AST_PIPELINE:
  case AST_STRUCT_LITERAL:
    emit_expr(cg, node);
    break;

  /* Declarations that are no-ops */
  case AST_STRUCT:
  case AST_EXTEND:
  case AST_MODULE:
  case AST_IMPORT:
  case AST_PRAGMA:
  case AST_NONE_LITERAL:
    break;

  case AST_ENUM: {
    /* Register enum variants as named constants: EnumName.Variant = i */
    const char *ename = node->as.enum_decl.name;
    for (int i = 0; i < node->as.enum_decl.variants.count; i++) {
      AstNode *v = node->as.enum_decl.variants.items[i];
      if (v->type == AST_IDENTIFIER) {
        char full[256];
        snprintf(full, sizeof(full), "%s.%s", ename, v->as.identifier.name);
        /* Create a global alloca for the constant */
        LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, full);
        LLVMBuildStore(cg->builder, LLVMConstInt(cg->i64_type, i, false),
                       alloca);
        cg_define(cg, full, alloca, cg->i64_type);
      }
    }
    break;
  }

  case AST_WHEN: {
    /* when val { is X { ... } is Y { ... } else { ... } }
     * → cascading if-else with val == pattern comparisons */
    LLVMValueRef val = emit_expr(cg, node->as.when_stmt.value);
    if (cg->had_error)
      break;
    LLVMTypeRef val_type = LLVMTypeOf(val);
    int case_count = node->as.when_stmt.cases.count;
    LLVMBasicBlockRef merge_bb =
        LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "when.end");

    for (int i = 0; i < case_count; i++) {
      AstNode *wc = node->as.when_stmt.cases.items[i];
      if (!wc)
        continue;
      if (wc->type != AST_IF)
        continue;

      AstNode *pat = wc->as.if_stmt.condition;
      LLVMBasicBlockRef then_bb =
          LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "when.case");
      LLVMBasicBlockRef next_bb =
          LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "when.next");

      LLVMValueRef cond;

      /* Generate condition based on pattern type */
      if (pat->type == AST_IDENTIFIER &&
          strcmp(pat->as.identifier.name, "_") == 0) {
        /* Wildcard: always matches */
        cond = LLVMConstInt(cg->i1_type, 1, false);

      } else if (pat->type == AST_BINARY_OP && pat->as.binary.left == NULL) {
        /* Range pattern: is > 40 → val > 40 */
        LLVMValueRef rhs = emit_expr(cg, pat->as.binary.right);
        if (val_type == cg->f64_type) {
          switch (pat->as.binary.op) {
          case OP_GT:
            cond = LLVMBuildFCmp(cg->builder, LLVMRealOGT, val, rhs, "cmp");
            break;
          case OP_GE:
            cond = LLVMBuildFCmp(cg->builder, LLVMRealOGE, val, rhs, "cmp");
            break;
          case OP_LT:
            cond = LLVMBuildFCmp(cg->builder, LLVMRealOLT, val, rhs, "cmp");
            break;
          case OP_LE:
            cond = LLVMBuildFCmp(cg->builder, LLVMRealOLE, val, rhs, "cmp");
            break;
          default:
            cond = LLVMConstInt(cg->i1_type, 0, false);
            break;
          }
        } else {
          switch (pat->as.binary.op) {
          case OP_GT:
            cond = LLVMBuildICmp(cg->builder, LLVMIntSGT, val, rhs, "cmp");
            break;
          case OP_GE:
            cond = LLVMBuildICmp(cg->builder, LLVMIntSGE, val, rhs, "cmp");
            break;
          case OP_LT:
            cond = LLVMBuildICmp(cg->builder, LLVMIntSLT, val, rhs, "cmp");
            break;
          case OP_LE:
            cond = LLVMBuildICmp(cg->builder, LLVMIntSLE, val, rhs, "cmp");
            break;
          default:
            cond = LLVMConstInt(cg->i1_type, 0, false);
            break;
          }
        }

      } else if (pat->type == AST_BLOCK) {
        /* Multi-pattern: is 1, 2, 3 → val==1 || val==2 || val==3 */
        cond = LLVMConstInt(cg->i1_type, 0, false);
        for (int j = 0; j < pat->as.block.statements.count; j++) {
          LLVMValueRef pv = emit_expr(cg, pat->as.block.statements.items[j]);
          LLVMValueRef eq;
          if (val_type == cg->i8ptr_type) {
            /* String comparison via strcmp */
            LLVMValueRef fn_strcmp = LLVMGetNamedFunction(cg->mod, "strcmp");
            if (!fn_strcmp) {
              LLVMTypeRef st = LLVMFunctionType(
                  LLVMInt32TypeInContext(cg->ctx),
                  (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
              fn_strcmp = LLVMAddFunction(cg->mod, "strcmp", st);
            }
            LLVMTypeRef st = LLVMFunctionType(
                LLVMInt32TypeInContext(cg->ctx),
                (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
            LLVMValueRef r =
                LLVMBuildCall2(cg->builder, st, fn_strcmp,
                               (LLVMValueRef[]){val, pv}, 2, "scmp");
            eq = LLVMBuildICmp(
                cg->builder, LLVMIntEQ, r,
                LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, false), "seq");
          } else if (val_type == cg->f64_type) {
            eq = LLVMBuildFCmp(cg->builder, LLVMRealOEQ, val, pv, "feq");
          } else {
            eq = LLVMBuildICmp(cg->builder, LLVMIntEQ, val, pv, "ieq");
          }
          cond = LLVMBuildOr(cg->builder, cond, eq, "multi");
        }

      } else {
        /* Simple literal/expr: val == pattern */
        LLVMValueRef pv = emit_expr(cg, pat);
        if (val_type == cg->i8ptr_type && LLVMTypeOf(pv) == cg->i8ptr_type) {
          /* String equality via strcmp */
          LLVMValueRef fn_strcmp = LLVMGetNamedFunction(cg->mod, "strcmp");
          if (!fn_strcmp) {
            LLVMTypeRef st = LLVMFunctionType(
                LLVMInt32TypeInContext(cg->ctx),
                (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
            fn_strcmp = LLVMAddFunction(cg->mod, "strcmp", st);
          }
          LLVMTypeRef st = LLVMFunctionType(
              LLVMInt32TypeInContext(cg->ctx),
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
          LLVMValueRef r = LLVMBuildCall2(cg->builder, st, fn_strcmp,
                                          (LLVMValueRef[]){val, pv}, 2, "scmp");
          cond = LLVMBuildICmp(
              cg->builder, LLVMIntEQ, r,
              LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, false), "seq");
        } else if (val_type == cg->f64_type) {
          cond = LLVMBuildFCmp(cg->builder, LLVMRealOEQ, val, pv, "feq");
        } else {
          /* Promote pattern to match val type if needed */
          if (LLVMTypeOf(pv) != val_type && val_type == cg->i64_type &&
              LLVMTypeOf(pv) == cg->i1_type) {
            pv = LLVMBuildZExt(cg->builder, pv, cg->i64_type, "promo");
          }
          cond = LLVMBuildICmp(cg->builder, LLVMIntEQ, val, pv, "ieq");
        }
      }

      LLVMBuildCondBr(cg->builder, cond, then_bb, next_bb);
      LLVMPositionBuilderAtEnd(cg->builder, then_bb);
      cg_push_scope(cg);
      emit_stmt(cg, wc->as.if_stmt.then_branch);
      cg_pop_scope(cg);
      if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
        LLVMBuildBr(cg->builder, merge_bb);
      LLVMPositionBuilderAtEnd(cg->builder, next_bb);
    }
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
      LLVMBuildBr(cg->builder, merge_bb);
    LLVMPositionBuilderAtEnd(cg->builder, merge_bb);
    break;
  }

  case AST_TRY: {
    /* Try/catch using setjmp/longjmp runtime support.
     * 1. Call __qisc_try_push() to get jump buffer pointer
     * 2. Call setjmp() on the buffer
     * 3. If setjmp returns 0: execute try block, then pop and jump to end
     * 4. If setjmp returns non-zero: execute catch block, then pop and jump to end */
    
    LLVMBasicBlockRef try_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "try.body");
    LLVMBasicBlockRef catch_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "try.catch");
    LLVMBasicBlockRef end_bb = LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "try.end");
    
    /* Get or declare __qisc_try_push (returns jmp_buf*) */
    LLVMValueRef fn_try_push = LLVMGetNamedFunction(cg->mod, "__qisc_try_push");
    if (!fn_try_push) {
      LLVMTypeRef ret_type = LLVMPointerType(cg->i8_type, 0);
      LLVMTypeRef fn_type = LLVMFunctionType(ret_type, NULL, 0, false);
      fn_try_push = LLVMAddFunction(cg->mod, "__qisc_try_push", fn_type);
    }
    
    /* Get or declare __qisc_try_pop */
    LLVMValueRef fn_try_pop = LLVMGetNamedFunction(cg->mod, "__qisc_try_pop");
    if (!fn_try_pop) {
      LLVMTypeRef fn_type = LLVMFunctionType(cg->void_type, NULL, 0, false);
      fn_try_pop = LLVMAddFunction(cg->mod, "__qisc_try_pop", fn_type);
    }
    
    /* Get or declare setjmp (returns int, takes jmp_buf*) */
    LLVMValueRef fn_setjmp = LLVMGetNamedFunction(cg->mod, "setjmp");
    if (!fn_setjmp) {
      LLVMTypeRef param_types[] = { LLVMPointerType(cg->i8_type, 0) };
      LLVMTypeRef fn_type = LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx), param_types, 1, false);
      fn_setjmp = LLVMAddFunction(cg->mod, "setjmp", fn_type);
      LLVMSetFunctionCallConv(fn_setjmp, LLVMCCallConv);
      LLVMAddAttributeAtIndex(fn_setjmp, LLVMAttributeFunctionIndex,
        LLVMCreateEnumAttribute(cg->ctx, LLVMGetEnumAttributeKindForName("returns_twice", 13), 0));
    }
    
    /* Call __qisc_try_push() to get jump buffer */
    LLVMTypeRef push_fn_type = LLVMFunctionType(LLVMPointerType(cg->i8_type, 0), NULL, 0, false);
    LLVMValueRef jmp_buf_ptr = LLVMBuildCall2(cg->builder, push_fn_type, fn_try_push, NULL, 0, "jmpbuf");
    
    /* Call setjmp(jmp_buf_ptr) */
    LLVMTypeRef setjmp_param_types[] = { LLVMPointerType(cg->i8_type, 0) };
    LLVMTypeRef setjmp_fn_type = LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx), setjmp_param_types, 1, false);
    LLVMValueRef setjmp_args[] = { jmp_buf_ptr };
    LLVMValueRef setjmp_ret = LLVMBuildCall2(cg->builder, setjmp_fn_type, fn_setjmp, setjmp_args, 1, "setjmp.ret");
    
    /* If setjmp returns 0, go to try block; otherwise go to catch */
    LLVMValueRef zero = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 0, false);
    LLVMValueRef is_try = LLVMBuildICmp(cg->builder, LLVMIntEQ, setjmp_ret, zero, "is.try");
    LLVMBuildCondBr(cg->builder, is_try, try_bb, catch_bb);
    
    /* Try block */
    LLVMPositionBuilderAtEnd(cg->builder, try_bb);
    if (node->as.try_stmt.try_block) {
      cg_push_scope(cg);
      emit_stmt(cg, node->as.try_stmt.try_block);
      cg_pop_scope(cg);
    }
    /* Pop context and jump to end on normal completion */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
      LLVMTypeRef pop_fn_type = LLVMFunctionType(cg->void_type, NULL, 0, false);
      LLVMBuildCall2(cg->builder, pop_fn_type, fn_try_pop, NULL, 0, "");
      LLVMBuildBr(cg->builder, end_bb);
    }
    
    /* Catch block */
    LLVMPositionBuilderAtEnd(cg->builder, catch_bb);
    
    /* Get or declare __qisc_get_error() to retrieve error message */
    LLVMValueRef fn_get_error = LLVMGetNamedFunction(cg->mod, "__qisc_get_error");
    if (!fn_get_error) {
      LLVMTypeRef ret_type = LLVMPointerType(cg->i8_type, 0);
      LLVMTypeRef fn_type = LLVMFunctionType(ret_type, NULL, 0, false);
      fn_get_error = LLVMAddFunction(cg->mod, "__qisc_get_error", fn_type);
    }
    
    for (int i = 0; i < node->as.try_stmt.catches.count; i++) {
      AstNode *catch_node = node->as.try_stmt.catches.items[i];
      if (!catch_node) continue;
      
      cg_push_scope(cg);
      
      /* Extract error variable name from the catch node (stored as condition identifier) */
      if (catch_node->type == AST_IF && catch_node->as.if_stmt.condition &&
          catch_node->as.if_stmt.condition->type == AST_IDENTIFIER) {
        const char *err_var_name = catch_node->as.if_stmt.condition->as.identifier.name;
        
        /* Call __qisc_get_error() to get error info pointer (for now just use message) */
        LLVMTypeRef get_err_fn_type = LLVMFunctionType(LLVMPointerType(cg->i8_type, 0), NULL, 0, false);
        LLVMValueRef err_ptr = LLVMBuildCall2(cg->builder, get_err_fn_type, fn_get_error, NULL, 0, "err.ptr");
        
        /* Define the error variable as a string (pointing to error message in QiscError struct) */
        LLVMValueRef err_alloca = LLVMBuildAlloca(cg->builder, cg->i8ptr_type, err_var_name);
        LLVMBuildStore(cg->builder, err_ptr, err_alloca);
        cg_define(cg, err_var_name, err_alloca, cg->i8ptr_type);
      }
      
      /* Emit the catch body (stored in then_branch) */
      if (catch_node->type == AST_IF && catch_node->as.if_stmt.then_branch) {
        emit_stmt(cg, catch_node->as.if_stmt.then_branch);
      } else {
        emit_stmt(cg, catch_node);
      }
      
      cg_pop_scope(cg);
    }
    /* Pop context and jump to end after catch */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
      LLVMTypeRef pop_fn_type = LLVMFunctionType(cg->void_type, NULL, 0, false);
      LLVMBuildCall2(cg->builder, pop_fn_type, fn_try_pop, NULL, 0, "");
      LLVMBuildBr(cg->builder, end_bb);
    }
    
    LLVMPositionBuilderAtEnd(cg->builder, end_bb);
    break;
  }

  case AST_FAIL: {
    /* fail "message" or fail ErrorType(data) — calls __qisc_fail */
    
    /* Get or declare __qisc_fail(const char *message, int code) */
    LLVMValueRef fn_fail = LLVMGetNamedFunction(cg->mod, "__qisc_fail");
    if (!fn_fail) {
      LLVMTypeRef param_types[] = { 
        LLVMPointerType(cg->i8_type, 0),  /* message */
        LLVMInt32TypeInContext(cg->ctx)    /* code */
      };
      LLVMTypeRef fn_type = LLVMFunctionType(cg->void_type, param_types, 2, false);
      fn_fail = LLVMAddFunction(cg->mod, "__qisc_fail", fn_type);
      LLVMAddAttributeAtIndex(fn_fail, LLVMAttributeFunctionIndex,
        LLVMCreateEnumAttribute(cg->ctx, LLVMGetEnumAttributeKindForName("noreturn", 8), 0));
    }
    
    /* Get the message - handle error type calls like FileNotFound(path) */
    LLVMValueRef msg;
    AstNode *err = node->as.fail_stmt.error;
    if (err && err->type == AST_CALL && err->as.call.callee && 
        err->as.call.callee->type == AST_IDENTIFIER) {
      /* fail ErrorType(data) - convert to string message */
      const char *err_name = err->as.call.callee->as.identifier.name;
      
      /* If there's an argument, use it as the message detail */
      if (err->as.call.args.count > 0) {
        LLVMValueRef detail = emit_expr(cg, err->as.call.args.items[0]);
        /* Concatenate error name with detail */
        char fmt_buf[256];
        snprintf(fmt_buf, sizeof(fmt_buf), "%s: %%s", err_name);
        LLVMValueRef fmt = LLVMBuildGlobalStringPtr(cg->builder, fmt_buf, "err.fmt");
        
        /* Allocate buffer for full message */
        LLVMValueRef buf = LLVMBuildArrayAlloca(
            cg->builder, cg->i8_type,
            LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 256, false), "err.buf");
        
        /* Call sprintf to format message */
        LLVMValueRef fn_sprintf = LLVMGetNamedFunction(cg->mod, "sprintf");
        if (!fn_sprintf) {
          LLVMTypeRef sprintf_type = LLVMFunctionType(
              LLVMInt32TypeInContext(cg->ctx),
              (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, true);
          fn_sprintf = LLVMAddFunction(cg->mod, "sprintf", sprintf_type);
        }
        LLVMTypeRef sprintf_type = LLVMFunctionType(
            LLVMInt32TypeInContext(cg->ctx),
            (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, true);
        LLVMValueRef sprintf_args[] = {buf, fmt, detail};
        LLVMBuildCall2(cg->builder, sprintf_type, fn_sprintf, sprintf_args, 3, "");
        msg = buf;
      } else {
        msg = LLVMBuildGlobalStringPtr(cg->builder, err_name, "err.name");
      }
    } else if (err) {
      msg = emit_expr(cg, err);
    } else {
      msg = LLVMBuildGlobalStringPtr(cg->builder, "fail", "fail.msg");
    }
    
    /* Ensure msg is a pointer (strings) */
    if (LLVMTypeOf(msg) != cg->i8ptr_type) {
      /* Convert non-string to string representation */
      msg = emit_str_call(cg, msg);
    }
    
    /* Call __qisc_fail(message, 1) */
    LLVMTypeRef fail_param_types[] = { 
      LLVMPointerType(cg->i8_type, 0),
      LLVMInt32TypeInContext(cg->ctx)
    };
    LLVMTypeRef fail_fn_type = LLVMFunctionType(cg->void_type, fail_param_types, 2, false);
    LLVMValueRef one = LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 1, false);
    LLVMValueRef args[] = { msg, one };
    LLVMBuildCall2(cg->builder, fail_fn_type, fn_fail, args, 2, "");
    LLVMBuildUnreachable(cg->builder);
    break;
  }

  case AST_PROC:
    /* Nested proc — handled at top level in emit_program */
    break;

  default:
    cg_error(cg, "Unsupported statement type: %d", node->type);
    break;
  }
}

/* ======== Profile Instrumentation Helpers ======== */

/* Emit a call to __qisc_profile_fn_enter or __qisc_profile_fn_exit */
static void emit_profile_call(Codegen *cg, LLVMValueRef fn_profile, 
                               const char *func_name) {
  if (!fn_profile) return;
  
  /* Create global string constant for function name */
  LLVMValueRef name_str = LLVMBuildGlobalStringPtr(cg->builder, func_name, "");
  LLVMTypeRef fn_type = LLVMFunctionType(cg->void_type,
      (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
  LLVMBuildCall2(cg->builder, fn_type, fn_profile, 
                 (LLVMValueRef[]){name_str}, 1, "");
}

static void emit_profile_branch_call(Codegen *cg, const char *location,
                                     bool taken) {
  if (!cg || !cg->fn_profile_branch || !location)
    return;

  LLVMValueRef location_str =
      LLVMBuildGlobalStringPtr(cg->builder, location, "");
  LLVMTypeRef fn_type =
      LLVMFunctionType(cg->void_type,
                       (LLVMTypeRef[]){cg->i8ptr_type, cg->i1_type}, 2, false);
  LLVMValueRef args[] = {
      location_str,
      LLVMConstInt(cg->i1_type, taken ? 1 : 0, false),
  };
  LLVMBuildCall2(cg->builder, fn_type, cg->fn_profile_branch, args, 2, "");
}

static void emit_profile_loop_call(Codegen *cg, const char *location,
                                   LLVMValueRef iterations) {
  if (!cg || !cg->fn_profile_loop || !location || !iterations)
    return;

  LLVMValueRef location_str =
      LLVMBuildGlobalStringPtr(cg->builder, location, "");
  LLVMTypeRef fn_type =
      LLVMFunctionType(cg->void_type,
                       (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2, false);
  LLVMValueRef args[] = {location_str, iterations};
  LLVMBuildCall2(cg->builder, fn_type, cg->fn_profile_loop, args, 2, "");
}

/* ======== Procedure Emission ======== */

static void emit_proc(Codegen *cg, AstNode *node) {
  const char *name = node->as.proc.name;
  int param_count = node->as.proc.params.count;

  /* Return type */
  LLVMTypeRef ret_type = cg_return_type(cg, node);

  /* Parameter types */
  LLVMTypeRef *param_types = NULL;
  if (param_count > 0) {
    param_types = calloc(param_count, sizeof(LLVMTypeRef));
    for (int i = 0; i < param_count; i++) {
      AstNode *p = node->as.proc.params.items[i];
      if (p->type == AST_VAR_DECL && p->as.var_decl.type_info) {
        param_types[i] = cg_type_from_name(cg, p->as.var_decl.type_info->name);
      } else {
        param_types[i] = cg->i64_type;
      }
    }
  }

  /* Create function type and function */
  LLVMTypeRef fn_type =
      LLVMFunctionType(ret_type, param_types, param_count, false);
  free(param_types);

  /* Reuse the forward-declared function from the first pass */
  LLVMValueRef fn = LLVMGetNamedFunction(cg->mod, name);
  if (!fn) {
    fn = LLVMAddFunction(cg->mod, name, fn_type);
  }
  LLVMSetLinkage(fn, LLVMExternalLinkage);

  /* Apply pragma-controlled LLVM attributes to function */
  cg_apply_pragma_attrs(cg, fn);

  /* Add personality-aware debug comments for this function */
  if (cg->debug_personality_enabled) {
    add_function_debug_comments(cg, fn, name);
    add_debug_personality_comment(cg, name, 4);  /* Function prologue */
  }

  /* Create entry block */
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(cg->ctx, fn, "entry");
  LLVMPositionBuilderAtEnd(cg->builder, entry);

  cg->current_fn = fn;
  cg->current_fn_name = name;  /* Track for profile exit calls in give statements */
  cg_push_scope(cg);

  /* Profile instrumentation: inject entry call */
  if (cg->profile_enabled && cg->fn_profile_enter) {
    emit_profile_call(cg, cg->fn_profile_enter, name);
  }

  /* Alloca params */
  for (int i = 0; i < param_count; i++) {
    AstNode *p = node->as.proc.params.items[i];
    if (p->type == AST_VAR_DECL) {
      LLVMTypeRef ptype = LLVMTypeOf(LLVMGetParam(fn, i));
      LLVMValueRef alloca =
          LLVMBuildAlloca(cg->builder, ptype, p->as.var_decl.name);
      LLVMBuildStore(cg->builder, LLVMGetParam(fn, i), alloca);
      cg_define(cg, p->as.var_decl.name, alloca, ptype);
    }
  }

  /* Emit body */
  if (node->as.proc.body) {
    emit_block(cg, node->as.proc.body);
  }

  /* Add implicit return if block not terminated */
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder))) {
    /* Profile instrumentation: inject exit call before implicit return */
    if (cg->profile_enabled && cg->fn_profile_exit) {
      emit_profile_call(cg, cg->fn_profile_exit, name);
    }
    if (ret_type == cg->void_type)
      LLVMBuildRetVoid(cg->builder);
    else
      LLVMBuildRet(cg->builder, LLVMConstNull(ret_type));
  }

  cg_pop_scope(cg);

  /* Verify function */
  if (LLVMVerifyFunction(fn, LLVMPrintMessageAction)) {
    fprintf(stderr, "[codegen] Warning: Function '%s' failed verification\n",
            name);
  }
}

/* ======== Pragma Processing ======== */

static void process_pragma(Codegen *cg, AstNode *pragma) {
  if (!pragma || pragma->type != AST_PRAGMA)
    return;
  
  const char *name = pragma->as.pragma.name;
  const char *value = pragma->as.pragma.value;
  
  if (!name)
    return;
  
  /* Context pragmas */
  if (strcmp(name, "context") == 0) {
    if (value) {
      if (strcmp(value, "cli") == 0)
        cg->pragma_opts.context = CG_CONTEXT_CLI;
      else if (strcmp(value, "server") == 0)
        cg->pragma_opts.context = CG_CONTEXT_SERVER;
      else if (strcmp(value, "web") == 0)
        cg->pragma_opts.context = CG_CONTEXT_WEB;
      else if (strcmp(value, "embedded") == 0)
        cg->pragma_opts.context = CG_CONTEXT_EMBEDDED;
      else if (strcmp(value, "notebook") == 0)
        cg->pragma_opts.context = CG_CONTEXT_NOTEBOOK;
    }
  }
  /* Optimization pragmas */
  else if (strcmp(name, "optimize") == 0) {
    if (value) {
      if (strcmp(value, "latency") == 0)
        cg->pragma_opts.opt_focus = CG_OPT_LATENCY;
      else if (strcmp(value, "throughput") == 0)
        cg->pragma_opts.opt_focus = CG_OPT_THROUGHPUT;
      else if (strcmp(value, "memory") == 0)
        cg->pragma_opts.opt_focus = CG_OPT_MEMORY;
      else if (strcmp(value, "size") == 0)
        cg->pragma_opts.opt_focus = CG_OPT_SIZE;
      else if (strcmp(value, "balanced") == 0)
        cg->pragma_opts.opt_focus = CG_OPT_BALANCED;
    }
  }
  /* Inline control */
  else if (strcmp(name, "inline") == 0) {
    if (value) {
      if (strcmp(value, "always") == 0 || strcmp(value, "yes") == 0)
        cg->pragma_opts.enable_inline = true;
      else if (strcmp(value, "never") == 0 || strcmp(value, "no") == 0)
        cg->pragma_opts.enable_inline = false;
    }
  }
  /* Vectorization */
  else if (strcmp(name, "vectorize") == 0) {
    if (value) {
      if (strcmp(value, "auto") == 0 || strcmp(value, "yes") == 0)
        cg->pragma_opts.enable_vectorize = true;
      else if (strcmp(value, "no") == 0)
        cg->pragma_opts.enable_vectorize = false;
    }
  }
  /* Compiler personality is handled at CLI level, but we note it */
  else if (strcmp(name, "compiler_personality") == 0) {
    /* Handled in CLI */
  }
  /* Hot path - aggressive optimization */
  else if (strcmp(name, "hot_path") == 0) {
    cg->pragma_opts.mark_hot_path = true;
    cg->pragma_opts.enable_inline = true;  /* Force aggressive inlining */
  }
  /* Cold path - skip expensive optimizations */
  else if (strcmp(name, "cold_path") == 0) {
    cg->pragma_opts.mark_cold_path = true;
  }
  /* Profile directives */
  else if (strcmp(name, "profile") == 0) {
    if (value) {
      if (strcmp(value, "this") == 0)
        cg->pragma_opts.profile_this = true;
      else if (strcmp(value, "ignore") == 0)
        cg->pragma_opts.profile_ignore = true;
    }
  }
  /* Parallel auto-parallelization hint */
  else if (strcmp(name, "parallel") == 0) {
    if (value && strcmp(value, "auto") == 0)
      cg->pragma_opts.enable_parallel = true;
  }
  /* Bounds checking control */
  else if (strcmp(name, "bounds_check") == 0) {
    if (value && strcmp(value, "off") == 0) {
      fprintf(stderr, "Warning: bounds_check:off is unsafe, use with caution\n");
      cg->pragma_opts.disable_bounds = true;
    }
  }
  /* Memoization hint */
  else if (strcmp(name, "memoize") == 0) {
    if (value && strcmp(value, "auto") == 0)
      cg->pragma_opts.enable_memoize = true;
  }
}

/* ======== Program Emission ======== */

static void emit_program(Codegen *cg, AstNode *program) {
  if (!program || program->type != AST_PROGRAM)
    return;

  /* Pass 0: Process all pragmas first */
  for (int i = 0; i < program->as.program.pragmas.count; i++) {
    AstNode *pragma = program->as.program.pragmas.items[i];
    process_pragma(cg, pragma);
  }

  /* Pass 1: register struct types (must come before function declarations
   * so that return types like `gives Point` can resolve to struct pointers) */
  for (int i = 0; i < program->as.program.declarations.count; i++) {
    AstNode *decl = program->as.program.declarations.items[i];
    if (decl->type == AST_STRUCT)
      cg_register_struct(cg, decl);
  }

  /* Pass 2: declare all functions (forward declarations) */
  for (int i = 0; i < program->as.program.declarations.count; i++) {
    AstNode *decl = program->as.program.declarations.items[i];
    if (decl->type == AST_PROC) {
      const char *name = decl->as.proc.name;
      int pc = decl->as.proc.params.count;
      LLVMTypeRef ret = cg_return_type(cg, decl);
      LLVMTypeRef *pts = NULL;
      if (pc > 0) {
        pts = calloc(pc, sizeof(LLVMTypeRef));
        for (int j = 0; j < pc; j++) {
          AstNode *p = decl->as.proc.params.items[j];
          if (p->type == AST_VAR_DECL && p->as.var_decl.type_info)
            pts[j] = cg_type_from_name(cg, p->as.var_decl.type_info->name);
          else
            pts[j] = cg->i64_type;
        }
      }
      LLVMTypeRef ft = LLVMFunctionType(ret, pts, pc, false);
      LLVMAddFunction(cg->mod, name, ft);
      free(pts);
    }
  }

  /* 1.7 pass: forward-declare extend block methods */
  for (int i = 0; i < program->as.program.declarations.count; i++) {
    AstNode *decl = program->as.program.declarations.items[i];
    if (decl->type == AST_EXTEND) {
      const char *type_name = decl->as.extend_decl.type_name;
      for (int m = 0; m < decl->as.extend_decl.methods.count; m++) {
        AstNode *method = decl->as.extend_decl.methods.items[m];
        if (method->type != AST_PROC)
          continue;
        /* Mangle name: TypeName__method_name */
        char mangled[256];
        snprintf(mangled, sizeof(mangled), "%s__%s", type_name,
                 method->as.proc.name);
        int pc = method->as.proc.params.count;
        LLVMTypeRef ret = cg_return_type(cg, method);
        LLVMTypeRef *pts = calloc(pc, sizeof(LLVMTypeRef));
        for (int j = 0; j < pc; j++) {
          AstNode *p = method->as.proc.params.items[j];
          if (p->type == AST_VAR_DECL && p->as.var_decl.name &&
              strcmp(p->as.var_decl.name, "self") == 0) {
            /* self parameter: pointer to struct type */
            CgStructType *st = cg_find_struct(cg, type_name);
            pts[j] = st ? LLVMPointerType(st->llvm_type, 0) : cg->i64_type;
          } else if (p->type == AST_VAR_DECL && p->as.var_decl.type_info) {
            pts[j] = cg_type_from_name(cg, p->as.var_decl.type_info->name);
          } else {
            pts[j] = cg->i64_type;
          }
        }
        LLVMTypeRef ft = LLVMFunctionType(ret, pts, pc, false);
        LLVMAddFunction(cg->mod, mangled, ft);
        free(pts);
      }
    }
  }

  /* Second pass: register enums as global constants */
  for (int i = 0; i < program->as.program.declarations.count; i++) {
    AstNode *decl = program->as.program.declarations.items[i];
    if (decl->type == AST_ENUM) {
      const char *ename = decl->as.enum_decl.name;
      for (int j = 0; j < decl->as.enum_decl.variants.count; j++) {
        AstNode *v = decl->as.enum_decl.variants.items[j];
        if (v->type == AST_IDENTIFIER) {
          char full[256];
          snprintf(full, sizeof(full), "%s.%s", ename, v->as.identifier.name);
          /* Create a global variable initialized to the variant index */
          LLVMValueRef gv = LLVMAddGlobal(cg->mod, cg->i64_type, full);
          LLVMSetInitializer(gv, LLVMConstInt(cg->i64_type, j, false));
          LLVMSetGlobalConstant(gv, true);
          LLVMSetLinkage(gv, LLVMPrivateLinkage);
          /* Register in global (scope 0) for lookup */
          cg_define(cg, full, gv, cg->i64_type);
        }
      }
    }
  }

  /* Third pass: emit all procedure bodies (including extend methods) */
  for (int i = 0; i < program->as.program.declarations.count; i++) {
    AstNode *decl = program->as.program.declarations.items[i];
    if (decl->type == AST_PROC) {
      emit_proc(cg, decl);
    } else if (decl->type == AST_EXTEND) {
      /* Emit extend method bodies with mangled names */
      const char *type_name = decl->as.extend_decl.type_name;
      for (int m = 0; m < decl->as.extend_decl.methods.count; m++) {
        AstNode *method = decl->as.extend_decl.methods.items[m];
        if (method->type != AST_PROC)
          continue;
        /* Temporarily rename proc for emission with mangled name */
        char *orig_name = method->as.proc.name;
        char mangled[256];
        snprintf(mangled, sizeof(mangled), "%s__%s", type_name, orig_name);
        method->as.proc.name = mangled;
        emit_proc(cg, method);
        method->as.proc.name = orig_name; /* Restore original */
      }
    }
    if (cg->had_error)
      return;
  }

  /* Add personality-aware compilation metadata */
  if (cg->debug_personality_enabled) {
    add_compilation_metadata(cg);
    add_debug_easter_egg_string(cg);
  }
}

/* ======== Public API ======== */

void codegen_init(Codegen *cg, const char *module_name) {
  memset(cg, 0, sizeof(Codegen));

  cg->ctx = LLVMContextCreate();
  cg->mod = LLVMModuleCreateWithNameInContext(module_name, cg->ctx);
  cg->builder = LLVMCreateBuilderInContext(cg->ctx);

  /* Cache common types */
  cg->i64_type = LLVMInt64TypeInContext(cg->ctx);
  cg->f64_type = LLVMDoubleTypeInContext(cg->ctx);
  cg->i1_type = LLVMInt1TypeInContext(cg->ctx);
  cg->i8_type = LLVMInt8TypeInContext(cg->ctx);
  cg->i8ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);
  cg->void_type = LLVMVoidTypeInContext(cg->ctx);

  /* Declare extern printf */
  LLVMTypeRef printf_type =
      LLVMFunctionType(LLVMInt32TypeInContext(cg->ctx),
                       (LLVMTypeRef[]){cg->i8ptr_type}, 1, true);
  cg->fn_printf = LLVMAddFunction(cg->mod, "printf", printf_type);

  /* Declare array runtime functions */
  /* __qisc_array_from(void *elements, size_t elem_size, size_t count) -> void* */
  LLVMTypeRef arr_from_type = LLVMFunctionType(cg->i8ptr_type,
      (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type, cg->i64_type}, 3, false);
  LLVMAddFunction(cg->mod, "__qisc_array_from", arr_from_type);

  /* __qisc_array_len(void *array) -> size_t */
  LLVMTypeRef arr_len_type = LLVMFunctionType(cg->i64_type,
      (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
  LLVMAddFunction(cg->mod, "__qisc_array_len", arr_len_type);

  /* __qisc_array_push(void *array, void *element) -> void* */
  LLVMTypeRef arr_push_type = LLVMFunctionType(cg->i8ptr_type,
      (LLVMTypeRef[]){cg->i8ptr_type, cg->i8ptr_type}, 2, false);
  LLVMAddFunction(cg->mod, "__qisc_array_push", arr_push_type);

  /* __qisc_array_pop(void *array) -> void* */
  LLVMTypeRef arr_pop_type = LLVMFunctionType(cg->i8ptr_type,
      (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
  LLVMAddFunction(cg->mod, "__qisc_array_pop", arr_pop_type);

  /* __qisc_array_new(size_t elem_size, size_t initial_capacity) -> void* */
  LLVMTypeRef arr_new_type = LLVMFunctionType(cg->i8ptr_type,
      (LLVMTypeRef[]){cg->i64_type, cg->i64_type}, 2, false);
  LLVMAddFunction(cg->mod, "__qisc_array_new", arr_new_type);

  /* __qisc_array_get(void *array, size_t index) -> void* (element pointer) */
  LLVMTypeRef arr_get_type = LLVMFunctionType(cg->i8ptr_type,
      (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2, false);
  LLVMAddFunction(cg->mod, "__qisc_array_get", arr_get_type);

  /* Initialize scope */
  cg->scope_depth = 0;
  cg->scopes[0].count = 0;
  cg->program_ast = NULL;
  cg->lambda_hint_active = false;
  cg->lambda_hint_param_type = NULL;
  cg->lambda_hint_return_type = NULL;

  /* Initialize pragma options with defaults */
  cg->pragma_opts.context = CG_CONTEXT_CLI;
  cg->pragma_opts.opt_focus = CG_OPT_BALANCED;
  cg->pragma_opts.enable_inline = true;
  cg->pragma_opts.enable_vectorize = false;
  cg->pragma_opts.strict_math = false;
#ifdef DEBUG
  cg->pragma_opts.debug_info = true;
#else
  cg->pragma_opts.debug_info = false;
#endif

  /* Profile instrumentation disabled by default */
  cg->profile_enabled = false;
  cg->fn_profile_enter = NULL;
  cg->fn_profile_exit = NULL;
  cg->fn_profile_branch = NULL;
  cg->fn_profile_loop = NULL;

  /* Personality-aware debug info disabled by default */
  cg->personality = QISC_PERSONALITY_OFF;
  cg->debug_personality_enabled = false;
  cg->optimization_count = 0;
}

void codegen_set_personality(Codegen *cg, QiscPersonality personality) {
  cg->personality = personality;
  
  /* Enable debug personality for cryptic and snarky modes */
  if (personality == QISC_PERSONALITY_CRYPTIC ||
      personality == QISC_PERSONALITY_SNARKY) {
    cg->debug_personality_enabled = true;
    
    /* Seed random for easter eggs */
    srand((unsigned int)time(NULL));
  }
}

void codegen_enable_profiling(Codegen *cg) {
  cg->profile_enabled = true;
  
  /* Declare __qisc_profile_fn_enter(const char* name) -> void */
  LLVMTypeRef enter_type = LLVMFunctionType(cg->void_type,
      (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
  cg->fn_profile_enter = LLVMAddFunction(cg->mod, "__qisc_profile_fn_enter", enter_type);
  
  /* Declare __qisc_profile_fn_exit(const char* name) -> void */
  LLVMTypeRef exit_type = LLVMFunctionType(cg->void_type,
      (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
  cg->fn_profile_exit = LLVMAddFunction(cg->mod, "__qisc_profile_fn_exit", exit_type);

  /* Declare __qisc_profile_branch(const char* location, bool taken) -> void */
  LLVMTypeRef branch_type = LLVMFunctionType(
      cg->void_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i1_type}, 2, false);
  cg->fn_profile_branch =
      LLVMAddFunction(cg->mod, "__qisc_profile_branch", branch_type);

  /* Declare __qisc_profile_loop(const char* location, i64 iterations) -> void */
  LLVMTypeRef loop_type = LLVMFunctionType(
      cg->void_type, (LLVMTypeRef[]){cg->i8ptr_type, cg->i64_type}, 2, false);
  cg->fn_profile_loop =
      LLVMAddFunction(cg->mod, "__qisc_profile_loop", loop_type);
}

void codegen_set_context(Codegen *cg, int context) {
  if (!cg) return;
  
  switch (context) {
    case CG_CONTEXT_CLI:
    case CG_CONTEXT_SERVER:
    case CG_CONTEXT_WEB:
    case CG_CONTEXT_EMBEDDED:
    case CG_CONTEXT_NOTEBOOK:
      cg->pragma_opts.context = context;
      break;
    default:
      cg->pragma_opts.context = CG_CONTEXT_CLI;
      break;
  }
  
  /* Apply context-specific default options */
  switch (cg->pragma_opts.context) {
    case CG_CONTEXT_CLI:
      cg->pragma_opts.opt_focus = CG_OPT_SIZE;
      cg->pragma_opts.enable_inline = false;
      break;
    case CG_CONTEXT_SERVER:
      cg->pragma_opts.opt_focus = CG_OPT_THROUGHPUT;
      cg->pragma_opts.enable_inline = true;
      cg->pragma_opts.enable_vectorize = true;
      break;
    case CG_CONTEXT_EMBEDDED:
      cg->pragma_opts.opt_focus = CG_OPT_SIZE;
      cg->pragma_opts.enable_inline = false;
      cg->pragma_opts.enable_vectorize = false;
      break;
    case CG_CONTEXT_WEB:
      cg->pragma_opts.opt_focus = CG_OPT_SIZE;
      cg->pragma_opts.enable_inline = false;
      break;
    case CG_CONTEXT_NOTEBOOK:
      cg->pragma_opts.opt_focus = CG_OPT_BALANCED;
      cg->pragma_opts.debug_info = true;
      break;
    default:
      break;
  }
}

const char *codegen_get_context_description(Codegen *cg) {
  if (!cg) return "unknown";
  
  switch (cg->pragma_opts.context) {
    case CG_CONTEXT_CLI:
      return "cli (optimize startup, small binary)";
    case CG_CONTEXT_SERVER:
      return "server (optimize throughput, aggressive inlining)";
    case CG_CONTEXT_EMBEDDED:
      return "embedded (optimize size, energy, minimal features)";
    case CG_CONTEXT_WEB:
      return "web (optimize for WASM, small binary, fast startup)";
    case CG_CONTEXT_NOTEBOOK:
      return "notebook (optimize for interactive, keep debug info)";
    default:
      return "unknown";
  }
}

int codegen_emit(Codegen *cg, AstNode *program) {
  cg->program_ast = program;
  emit_program(cg, program);
  return cg->had_error ? 1 : 0;
}

void codegen_dump_ir(Codegen *cg) {
  char *ir = LLVMPrintModuleToString(cg->mod);
  printf("%s", ir);
  LLVMDisposeMessage(ir);
}

int codegen_write_ir(Codegen *cg, const char *path) {
  if (LLVMPrintModuleToFile(cg->mod, path, NULL)) {
    cg_error(cg, "Failed to write IR to %s", path);
    return 1;
  }
  return 0;
}

int codegen_write_object(Codegen *cg, const char *path) {
  /* Initialize target */
  LLVMInitializeAllTargetInfos();
  LLVMInitializeAllTargets();
  LLVMInitializeAllTargetMCs();
  LLVMInitializeAllAsmParsers();
  LLVMInitializeAllAsmPrinters();

  char *triple = LLVMGetDefaultTargetTriple();
  LLVMSetTarget(cg->mod, triple);

  LLVMTargetRef target;
  char *error = NULL;
  if (LLVMGetTargetFromTriple(triple, &target, &error)) {
    cg_error(cg, "Failed to get target: %s", error);
    LLVMDisposeMessage(error);
    LLVMDisposeMessage(triple);
    return 1;
  }

  /* Get context-specific optimization level */
  LLVMCodeGenOptLevel opt_level = cg_get_context_opt_level(cg);
  
  /* Get context-specific CPU features */
  const char *cpu_features = "";
  switch (cg->pragma_opts.context) {
    case CG_CONTEXT_SERVER:
      /* Server: enable all available features for throughput */
      cpu_features = "+sse4.2,+avx";
      break;
    case CG_CONTEXT_EMBEDDED:
      /* Embedded: minimal features for portability */
      cpu_features = "";
      break;
    case CG_CONTEXT_WEB:
      /* Web: WASM-compatible features */
      cpu_features = "";
      break;
    default:
      cpu_features = "";
      break;
  }

  LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
      target, triple, "generic", cpu_features, opt_level, LLVMRelocPIC,
      LLVMCodeModelDefault);

  LLVMTargetDataRef data_layout = LLVMCreateTargetDataLayout(machine);
  char *layout_str = LLVMCopyStringRepOfTargetData(data_layout);
  LLVMSetDataLayout(cg->mod, layout_str);
  LLVMDisposeMessage(layout_str);
  LLVMDisposeTargetData(data_layout);

  if (LLVMTargetMachineEmitToFile(machine, cg->mod, (char *)path,
                                  LLVMObjectFile, &error)) {
    cg_error(cg, "Failed to emit object file: %s", error);
    LLVMDisposeMessage(error);
    LLVMDisposeTargetMachine(machine);
    LLVMDisposeMessage(triple);
    return 1;
  }

  LLVMDisposeTargetMachine(machine);
  LLVMDisposeMessage(triple);
  return 0;
}

void codegen_free(Codegen *cg) {
  /* Free all scope symbols */
  for (int d = 0; d <= cg->scope_depth; d++) {
    for (int i = 0; i < cg->scopes[d].count; i++) {
      free(cg->scopes[d].symbols[i].name);
    }
  }
  
  /* Free syntax-aware IR generation resources */
  if (cg->syntax_profile) {
    syntax_profile_free(cg->syntax_profile);
    cg->syntax_profile = NULL;
  }
  if (cg->ir_hints) {
    ir_hints_free(cg->ir_hints);
    cg->ir_hints = NULL;
  }
  
  LLVMDisposeBuilder(cg->builder);
  LLVMDisposeModule(cg->mod);
  LLVMContextDispose(cg->ctx);
}
