/*
 * QISC - Quantum-Inspired Superposition Compiler
 *
 * A self-evolving, profile-driven compiler that learns from
 * actual execution patterns and converges to optimal binaries.
 *
 * Main header file - includes all public API
 */

#ifndef QISC_H
#define QISC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Version info */
#define QISC_VERSION_MAJOR 0
#define QISC_VERSION_MINOR 1
#define QISC_VERSION_PATCH 0
#define QISC_VERSION_STRING "0.1.0"

/* Result type for operations that can fail */
typedef enum {
  QISC_OK = 0,
  QISC_ERROR_FILE_NOT_FOUND,
  QISC_ERROR_SYNTAX,
  QISC_ERROR_TYPE,
  QISC_ERROR_MEMORY,
  QISC_ERROR_INTERNAL,
} QiscResult;

/* Forward declarations */
typedef struct QiscCompiler QiscCompiler;
typedef struct QiscLexer QiscLexer;
typedef struct QiscParser QiscParser;
typedef struct QiscAST QiscAST;
typedef struct QiscIR QiscIR;
typedef struct QiscProfile QiscProfile;

/* Compiler context modes */
typedef enum {
  QISC_CONTEXT_CLI,
  QISC_CONTEXT_SERVER,
  QISC_CONTEXT_WEB,
  QISC_CONTEXT_NOTEBOOK,
  QISC_CONTEXT_EMBEDDED,
} QiscContext;

/* Compiler personality modes */
typedef enum {
  QISC_PERSONALITY_OFF,
  QISC_PERSONALITY_MINIMAL,
  QISC_PERSONALITY_FRIENDLY,
  QISC_PERSONALITY_SNARKY,
  QISC_PERSONALITY_SAGE,
  QISC_PERSONALITY_CRYPTIC,
} QiscPersonality;

/* Compilation options */
typedef struct {
  QiscContext context;
  QiscPersonality personality;
  bool collect_profile;
  bool use_profile;
  bool converge;
  const char *profile_path;
  int optimization_level; /* 0-3 */
} QiscOptions;

/* Create default options */
QiscOptions qisc_default_options(void);

/* Main compilation functions */
QiscResult qisc_compile_file(const char *path, QiscOptions *options);
QiscResult qisc_compile_string(const char *source, QiscOptions *options);

/* Version info */
const char *qisc_version(void);

#endif /* QISC_H */
