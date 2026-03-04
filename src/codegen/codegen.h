/*
 * QISC LLVM Code Generator — Header
 *
 * Compiles QISC AST → LLVM IR → native binary.
 */

#ifndef QISC_CODEGEN_H
#define QISC_CODEGEN_H

#include "../parser/ast.h"
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

/* Code generator state */
typedef struct {
  LLVMContextRef ctx;
  LLVMModuleRef mod;
  LLVMBuilderRef builder;

  /* Scope stack */
  CgScope scopes[CG_MAX_SCOPES];
  int scope_depth;

  /* Current function being compiled */
  LLVMValueRef current_fn;

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

  /* Error handling */
  bool had_error;
  char error_msg[512];
} Codegen;

/* Initialize code generator */
void codegen_init(Codegen *cg, const char *module_name);

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
