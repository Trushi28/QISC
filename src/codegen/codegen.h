/*
 * QISC LLVM Code Generator — Header
 *
 * Compiles QISC AST → LLVM IR → native binary.
 * Supports syntax-aware IR generation strategies.
 */

#ifndef QISC_CODEGEN_H
#define QISC_CODEGEN_H

#include "../parser/ast.h"
#include "../syntax/syntax_profile.h"
#include "qisc.h"
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>

/* Max symbols per scope */
#define CG_MAX_SYMBOLS 512
#define CG_MAX_SCOPES 64

/* Symbol entry (variable name → alloca) */
typedef struct {
  char *name;
  LLVMValueRef alloca;
  LLVMTypeRef type;
} CgSymbol;

/* Scope for block-level variable lookup */
typedef struct {
  CgSymbol symbols[CG_MAX_SYMBOLS];
  int count;
} CgScope;

/* Struct type registry */
#define CG_MAX_STRUCTS 32
#define CG_MAX_FIELDS 32

typedef struct {
  char *name;
  LLVMTypeRef llvm_type;
  char *field_names[CG_MAX_FIELDS];
  LLVMTypeRef field_types[CG_MAX_FIELDS];
  int field_count;
} CgStructType;

/* Pragma-controlled compilation options */
typedef struct {
  /* Context: affects optimization strategy */
  enum {
    CG_CONTEXT_CLI,      /* Optimize for startup time */
    CG_CONTEXT_SERVER,   /* Optimize for throughput */
    CG_CONTEXT_WEB,      /* Optimize for size */
    CG_CONTEXT_EMBEDDED, /* Optimize for size + energy */
    CG_CONTEXT_NOTEBOOK  /* Balanced */
  } context;
  
  /* Optimization focus */
  enum {
    CG_OPT_BALANCED,     /* Default balanced optimization */
    CG_OPT_LATENCY,      /* Optimize for response time */
    CG_OPT_THROUGHPUT,   /* Optimize for batch processing */
    CG_OPT_MEMORY,       /* Minimize memory usage */
    CG_OPT_SIZE          /* Minimize binary size */
  } opt_focus;
  
  /* Specific toggles */
  bool enable_inline;       /* Default: true */
  bool enable_vectorize;    /* Default: false */
  bool strict_math;         /* Default: false */
  bool debug_info;          /* Default: true in debug builds */

  /* Profile directives */
  bool mark_hot_path;       /* #pragma hot_path */
  bool mark_cold_path;      /* #pragma cold_path */
  bool profile_this;        /* #pragma profile:this */
  bool profile_ignore;      /* #pragma profile:ignore */

  /* Behavior directives */
  bool enable_parallel;     /* #pragma parallel:auto */
  bool disable_bounds;      /* #pragma bounds_check:off */
  bool enable_memoize;      /* #pragma memoize:auto */
} CgPragmaOpts;

/* Syntax-aware IR generation mode */
typedef enum {
  CG_SYNTAX_MODE_DEFAULT,     /* Standard balanced code generation */
  CG_SYNTAX_MODE_PIPELINE,    /* Stream-oriented: lazy iterators, fusion */
  CG_SYNTAX_MODE_FUNCTIONAL,  /* Data-flow: SSA-heavy, tail call opt */
  CG_SYNTAX_MODE_IMPERATIVE,  /* Control-flow: mutable-friendly */
} CgSyntaxMode;

/* Code generator state */
typedef struct {
  LLVMContextRef ctx;
  LLVMModuleRef mod;
  LLVMBuilderRef builder;
  AstNode *program_ast;
  bool lambda_hint_active;
  LLVMTypeRef lambda_hint_param_type;
  LLVMTypeRef lambda_hint_return_type;

  /* Scope stack */
  CgScope scopes[CG_MAX_SCOPES];
  int scope_depth;

  /* Current function being compiled */
  LLVMValueRef current_fn;
  const char *current_fn_name;  /* Name for profile instrumentation */

  /* Break/continue targets for loops */
  LLVMBasicBlockRef break_bb;
  LLVMBasicBlockRef continue_bb;

  /* Cached types */
  LLVMTypeRef i64_type;
  LLVMTypeRef f64_type;
  LLVMTypeRef i1_type;
  LLVMTypeRef i8_type;
  LLVMTypeRef i8ptr_type;
  LLVMTypeRef void_type;

  /* Cached extern functions */
  LLVMValueRef fn_printf;
  LLVMValueRef fn_puts;

  /* Struct type registry */
  CgStructType structs[CG_MAX_STRUCTS];
  int struct_count;

  /* Pragma-controlled options */
  CgPragmaOpts pragma_opts;

  /* Profile instrumentation */
  bool profile_enabled;           /* Inject profiling calls */
  LLVMValueRef fn_profile_enter;  /* __qisc_profile_fn_enter */
  LLVMValueRef fn_profile_exit;   /* __qisc_profile_fn_exit */
  LLVMValueRef fn_profile_branch; /* __qisc_profile_branch */
  LLVMValueRef fn_profile_loop;   /* __qisc_profile_loop */

  /* Personality-aware debug info */
  QiscPersonality personality;    /* Compiler personality mode */
  bool debug_personality_enabled; /* Enable personality-aware debug comments */
  int optimization_count;         /* Count of optimizations applied */

  /* Easter eggs */
  bool easter_eggs_enabled;       /* Inject Easter eggs into assembly */

  /* Syntax-aware IR generation */
  CgSyntaxMode syntax_mode;       /* Current syntax-driven IR mode */
  SyntaxProfile *syntax_profile;  /* Analyzed syntax profile (owned) */
  IRGenerationHints *ir_hints;    /* IR generation hints (owned) */

  /* Stream IR runtime functions (for pipeline mode) */
  LLVMValueRef fn_stream_create;  /* __qisc_stream_create */
  LLVMValueRef fn_stream_next;    /* __qisc_stream_next */
  LLVMValueRef fn_stream_map;     /* __qisc_stream_map */
  LLVMValueRef fn_stream_filter;  /* __qisc_stream_filter */
  LLVMValueRef fn_stream_reduce;  /* __qisc_stream_reduce */
  LLVMValueRef fn_stream_collect; /* __qisc_stream_collect */
  LLVMValueRef fn_stream_free;    /* __qisc_stream_free */

  /* Functional IR support (for functional mode) */
  LLVMValueRef fn_memo_lookup;    /* __qisc_memo_lookup */
  LLVMValueRef fn_memo_store;     /* __qisc_memo_store */

  /* Reference counting (for python mode) */
  LLVMValueRef fn_refcount_inc;   /* __qisc_rc_inc */
  LLVMValueRef fn_refcount_dec;   /* __qisc_rc_dec */

  /* Optimization contexts (set by CLI before codegen_emit) */
  void *tco_context;              /* TcoContext* - tail call optimization */
  void *fusion_optimizer;         /* FusionOptimizer* - pipeline fusion */
  void *memo_context;             /* MemoContext* - memoization */

  /* Error handling */
  bool had_error;
  char error_msg[512];
} Codegen;

/* Initialize code generator */
void codegen_init(Codegen *cg, const char *module_name);

/* Set compiler personality for debug symbols */
void codegen_set_personality(Codegen *cg, QiscPersonality personality);

/* Enable profile instrumentation (call before codegen_emit) */
void codegen_enable_profiling(Codegen *cg);

/* Enable Easter eggs injection (call before codegen_emit) */
void codegen_enable_easter_eggs(Codegen *cg);

/* Set compilation context for context-specific optimizations */
void codegen_set_context(Codegen *cg, int context);

/* Get context-specific optimization description */
const char *codegen_get_context_description(Codegen *cg);

/* ======== Syntax-Aware IR Generation ======== */

/*
 * Configure codegen based on syntax profile.
 * Analyzes the AST and sets up appropriate IR generation strategy.
 * Call this before codegen_emit() for syntax-aware compilation.
 */
void codegen_set_syntax_mode(Codegen *cg, SyntaxProfile *profile);

/*
 * Configure codegen from pragma directive.
 * Supported pragmas:
 *   #pragma style:pipeline   → Stream-oriented IR (lazy, fusion)
 *   #pragma style:brace      → Standard control flow IR
 *   #pragma style:python     → Reference counting focus
 *   #pragma style:functional → Immutable, SSA-heavy IR
 */
void codegen_set_syntax_mode_from_pragma(Codegen *cg, const char *style);

/*
 * Emit pipeline-oriented IR for stream operations.
 * Generates lazy iterators with fusion opportunities.
 * Used when syntax analysis detects pipeline-heavy code.
 *
 * Features:
 * - Lazy stream evaluation (pull-based)
 * - Automatic fusion of map/filter/reduce chains
 * - Arena allocation for intermediate values
 */
LLVMValueRef codegen_emit_pipeline_ir(Codegen *cg, AstNode *pipeline);

/*
 * Emit functional-style IR with SSA emphasis.
 * Optimized for pure functions and immutable data.
 *
 * Features:
 * - Aggressive function inlining
 * - Tail call optimization
 * - Memoization support
 * - Immutable values by default
 */
LLVMValueRef codegen_emit_functional_ir(Codegen *cg, AstNode *node);

/*
 * Emit imperative-style IR with mutation support.
 * Traditional control flow with efficient loop handling.
 *
 * Features:
 * - In-place mutation allowed
 * - Traditional loop constructs
 * - Minimal PHI nodes
 * - Efficient memory updates
 */
LLVMValueRef codegen_emit_imperative_ir(Codegen *cg, AstNode *node);

/*
 * Get current syntax mode name for debugging/reporting.
 */
const char *codegen_syntax_mode_name(CgSyntaxMode mode);

/*
 * Print syntax-aware IR generation configuration.
 */
void codegen_print_syntax_config(Codegen *cg);

/* Compile AST to LLVM IR */
int codegen_emit(Codegen *cg, AstNode *program);

/* Dump LLVM IR to stdout */
void codegen_dump_ir(Codegen *cg);

/* Write LLVM IR to file */
int codegen_write_ir(Codegen *cg, const char *path);

/* Write object file */
int codegen_write_object(Codegen *cg, const char *path);

/* Free code generator */
void codegen_free(Codegen *cg);

#endif /* QISC_CODEGEN_H */
