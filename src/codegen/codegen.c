/*
 * QISC LLVM Code Generator — Implementation
 *
 * Phase 1: int/float/bool literals, binary/unary ops, variables,
 *          functions, if/else, while, for, give, print/str builtins.
 */

#include "codegen.h"
#include "qisc.h"
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/Target.h>
#include <llvm-c/TargetMachine.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

/* ======== Type Helpers ======== */

static LLVMTypeRef cg_type_from_name(Codegen *cg, const char *name) {
  if (!name)
    return cg->i64_type;
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
  /* Allocate buffer on stack (64 bytes) */
  LLVMValueRef buf = LLVMBuildArrayAlloca(
      cg->builder, cg->i8_type,
      LLVMConstInt(LLVMInt32TypeInContext(cg->ctx), 64, false), "strbuf");

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

  /* Handle builtin: sizeof() — return a constant */
  if (strcmp(fname, "sizeof") == 0) {
    if (argc >= 1) {
      LLVMValueRef val = emit_expr(cg, node->as.call.args.items[0]);
      LLVMTypeRef vt = LLVMTypeOf(val);
      if (vt == cg->i8ptr_type) {
        /* For strings: call strlen */
        LLVMValueRef fn_strlen = LLVMGetNamedFunction(cg->mod, "strlen");
        if (!fn_strlen) {
          LLVMTypeRef st = LLVMFunctionType(
              cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
          fn_strlen = LLVMAddFunction(cg->mod, "strlen", st);
        }
        LLVMTypeRef st = LLVMFunctionType(
            cg->i64_type, (LLVMTypeRef[]){cg->i8ptr_type}, 1, false);
        return LLVMBuildCall2(cg->builder, st, fn_strlen, (LLVMValueRef[]){val},
                              1, "slen");
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

  /* Regular function call */
  LLVMValueRef fn = LLVMGetNamedFunction(cg->mod, fname);
  if (!fn) {
    /* Maybe it's a lambda variable (function pointer) */
    CgSymbol *sym = cg_lookup(cg, fname);
    if (sym) {
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

/* Emit array literal: [a, b, c] → malloc + store elements */
static LLVMValueRef emit_array_literal(Codegen *cg, AstNode *node) {
  int count = node->as.array_literal.elements.count;

  /* Allocate array: malloc(count * sizeof(i64)) */
  LLVMValueRef fn_malloc = LLVMGetNamedFunction(cg->mod, "malloc");
  if (!fn_malloc) {
    LLVMTypeRef mt = LLVMFunctionType(cg->i8ptr_type,
                                      (LLVMTypeRef[]){cg->i64_type}, 1, false);
    fn_malloc = LLVMAddFunction(cg->mod, "malloc", mt);
  }
  LLVMValueRef size = LLVMConstInt(cg->i64_type, count * 8, false);
  LLVMTypeRef mt =
      LLVMFunctionType(cg->i8ptr_type, (LLVMTypeRef[]){cg->i64_type}, 1, false);
  LLVMValueRef raw = LLVMBuildCall2(cg->builder, mt, fn_malloc,
                                    (LLVMValueRef[]){size}, 1, "arr_raw");

  /* Cast to i64* for indexing */
  LLVMTypeRef arr_ptr_type = LLVMPointerTypeInContext(cg->ctx, 0);
  (void)arr_ptr_type;

  /* Store elements */
  for (int i = 0; i < count; i++) {
    LLVMValueRef val = emit_expr(cg, node->as.array_literal.elements.items[i]);
    LLVMValueRef idx = LLVMConstInt(cg->i64_type, i, false);
    LLVMValueRef ptr =
        LLVMBuildGEP2(cg->builder, cg->i64_type, raw, &idx, 1, "elem_ptr");
    LLVMBuildStore(cg->builder, val, ptr);
  }

  /* Also store the count in the scope for sizeof */
  return raw;
}

/* Emit array index: arr[idx] → GEP + load */
static LLVMValueRef emit_index(Codegen *cg, AstNode *node) {
  LLVMValueRef obj = emit_expr(cg, node->as.index.object);
  LLVMValueRef idx = emit_expr(cg, node->as.index.index);
  if (cg->had_error)
    return obj;

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

    /* All params default to i64 */
    LLVMTypeRef *pts = NULL;
    if (pc > 0) {
      pts = calloc(pc, sizeof(LLVMTypeRef));
      for (int i = 0; i < pc; i++)
        pts[i] = cg->i64_type;
    }
    LLVMTypeRef fn_type = LLVMFunctionType(cg->i64_type, pts, pc, false);
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
      LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, cg->i64_type, pname);
      LLVMBuildStore(cg->builder, LLVMGetParam(fn, i), alloca);
      cg_define(cg, pname, alloca, cg->i64_type);
    }

    /* Emit body */
    if (node->as.lambda.body) {
      if (node->as.lambda.body->type == AST_BLOCK)
        emit_block(cg, node->as.lambda.body);
      else {
        /* Single expression body */
        LLVMValueRef result = emit_expr(cg, node->as.lambda.body);
        LLVMBuildRet(cg->builder, result);
      }
    }

    /* Implicit return if needed */
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
      LLVMBuildRet(cg->builder, LLVMConstInt(cg->i64_type, 0, false));

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

  /* Determine type */
  LLVMTypeRef var_type = cg->i64_type; /* default */
  if (node->as.var_decl.type_info) {
    var_type = cg_type_from_name(cg, node->as.var_decl.type_info->name);
  } else if (node->as.var_decl.initializer) {
    /* Auto: infer from initializer */
    LLVMValueRef init_val = emit_expr(cg, node->as.var_decl.initializer);
    if (cg->had_error)
      return;
    var_type = LLVMTypeOf(init_val);

    /* Create alloca and store */
    LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, var_type, name);
    LLVMBuildStore(cg->builder, init_val, alloca);
    cg_define(cg, name, alloca, var_type);

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
    return;
  }

  /* Create alloca */
  LLVMValueRef alloca = LLVMBuildAlloca(cg->builder, var_type, name);

  /* Initialize */
  if (node->as.var_decl.initializer) {
    LLVMValueRef init_val = emit_expr(cg, node->as.var_decl.initializer);
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

  if (val)
    LLVMBuildRet(cg->builder, val);
  else
    LLVMBuildRetVoid(cg->builder);
}

static void emit_if(Codegen *cg, AstNode *node) {
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

  LLVMBuildCondBr(cg->builder, cond, then_bb, else_bb);

  /* Then */
  LLVMPositionBuilderAtEnd(cg->builder, then_bb);
  cg_push_scope(cg);
  emit_stmt(cg, node->as.if_stmt.then_branch);
  cg_pop_scope(cg);
  /* Only add branch if block isn't already terminated */
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
    LLVMBuildBr(cg->builder, merge_bb);

  /* Else */
  LLVMPositionBuilderAtEnd(cg->builder, else_bb);
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
  LLVMBasicBlockRef cond_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "while.cond");
  LLVMBasicBlockRef body_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "while.body");
  LLVMBasicBlockRef end_bb =
      LLVMAppendBasicBlockInContext(cg->ctx, cg->current_fn, "while.end");

  /* Save break/continue targets */
  LLVMBasicBlockRef prev_break = cg->break_bb;
  LLVMBasicBlockRef prev_continue = cg->continue_bb;
  cg->break_bb = end_bb;
  cg->continue_bb = cond_bb;

  LLVMBuildBr(cg->builder, cond_bb);

  /* Condition */
  LLVMPositionBuilderAtEnd(cg->builder, cond_bb);
  LLVMValueRef cond = emit_expr(cg, node->as.while_stmt.condition);
  if (LLVMTypeOf(cond) != cg->i1_type) {
    cond = LLVMBuildICmp(cg->builder, LLVMIntNE, cond,
                         LLVMConstNull(LLVMTypeOf(cond)), "tobool");
  }
  LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);

  /* Body */
  LLVMPositionBuilderAtEnd(cg->builder, body_bb);
  cg_push_scope(cg);
  emit_stmt(cg, node->as.while_stmt.body);
  cg_pop_scope(cg);
  if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
    LLVMBuildBr(cg->builder, cond_bb);

  /* End */
  LLVMPositionBuilderAtEnd(cg->builder, end_bb);
  cg->break_bb = prev_break;
  cg->continue_bb = prev_continue;
}

static void emit_for(Codegen *cg, AstNode *node) {
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
  cg->break_bb = end_bb;
  cg->continue_bb = step_bb;

  cg_push_scope(cg);

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
      LLVMBuildCondBr(cg->builder, cond, body_bb, end_bb);
    } else {
      LLVMBuildBr(cg->builder, body_bb);
    }

    /* Body */
    LLVMPositionBuilderAtEnd(cg->builder, body_bb);
    emit_stmt(cg, node->as.for_stmt.body);
    if (!LLVMGetBasicBlockTerminator(LLVMGetInsertBlock(cg->builder)))
      LLVMBuildBr(cg->builder, step_bb);

    /* Step */
    LLVMPositionBuilderAtEnd(cg->builder, step_bb);
    if (node->as.for_stmt.update) {
      emit_stmt(cg, node->as.for_stmt.update);
    }
    LLVMBuildBr(cg->builder, cond_bb);
  }

for_end:
  /* End */
  LLVMPositionBuilderAtEnd(cg->builder, end_bb);
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

  case AST_TRY:
  case AST_FAIL:
    /* Try/catch: for now, just execute the try block */
    if (node->type == AST_TRY && node->as.try_stmt.try_block) {
      cg_push_scope(cg);
      emit_stmt(cg, node->as.try_stmt.try_block);
      cg_pop_scope(cg);
    }
    break;

  case AST_PROC:
    /* Nested proc — handled at top level in emit_program */
    break;

  default:
    cg_error(cg, "Unsupported statement type: %d", node->type);
    break;
  }
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

  /* Create entry block */
  LLVMBasicBlockRef entry = LLVMAppendBasicBlockInContext(cg->ctx, fn, "entry");
  LLVMPositionBuilderAtEnd(cg->builder, entry);

  cg->current_fn = fn;
  cg_push_scope(cg);

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

/* ======== Program Emission ======== */

static void emit_program(Codegen *cg, AstNode *program) {
  if (!program || program->type != AST_PROGRAM)
    return;

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

  /* Initialize scope */
  cg->scope_depth = 0;
  cg->scopes[0].count = 0;
}

int codegen_emit(Codegen *cg, AstNode *program) {
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

  LLVMTargetMachineRef machine = LLVMCreateTargetMachine(
      target, triple, "generic", "", LLVMCodeGenLevelDefault, LLVMRelocPIC,
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
  LLVMDisposeBuilder(cg->builder);
  LLVMDisposeModule(cg->mod);
  LLVMContextDispose(cg->ctx);
}
