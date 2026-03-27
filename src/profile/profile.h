/*
 * QISC Profile Engine — Header
 *
 * The heart of QISC's "Living IR" concept:
 * - Collects runtime profile data (function frequencies, branch patterns, etc.)
 * - Guides optimization decisions in subsequent compilations
 * - Enables convergence detection for optimal binaries
 *
 * Philosophy: Optimize for observed reality, not theoretical possibilities.
 */

#ifndef QISC_PROFILE_H
#define QISC_PROFILE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Maximum tracked items */
#define PROFILE_MAX_FUNCTIONS 1024
#define PROFILE_MAX_BRANCHES 4096
#define PROFILE_MAX_LOOPS 1024

/* Function profile entry */
typedef struct {
  char *name;                    /* Function name */
  uint64_t call_count;           /* Total invocations */
  uint64_t total_cycles;         /* CPU cycles spent (approximation) */
  double avg_time_us;            /* Average execution time in microseconds */
  bool is_hot;                   /* Marked as hot path (>10% of total time) */
  bool is_cold;                  /* Marked as cold path (<0.1% of total time) */
  bool should_inline;            /* Profile suggests inlining */
} ProfileFunction;

/* Branch profile entry */
typedef struct {
  char *location;                /* Source location (file:line) */
  uint64_t taken_count;          /* Times branch was taken */
  uint64_t not_taken_count;      /* Times branch was not taken */
  double taken_ratio;            /* Ratio of taken (0.0 to 1.0) */
  bool is_predictable;           /* >95% one direction */
} ProfileBranch;

/* Loop profile entry */
typedef struct {
  char *location;                /* Source location */
  uint64_t invocation_count;     /* Times loop was entered */
  uint64_t total_iterations;     /* Total iterations across all invocations */
  double avg_iterations;         /* Average iterations per invocation */
  int typical_iteration_count;   /* Most common iteration count */
  bool should_unroll;            /* Profile suggests unrolling */
  int suggested_unroll_factor;   /* Suggested unroll factor (2, 4, 8, etc.) */
} ProfileLoop;

/* Main profile structure */
struct QiscProfile {
  /* Version and metadata */
  uint32_t version;
  char *source_file;
  uint64_t timestamp;
  uint32_t run_count;            /* Number of profiling runs merged */
  
  /* IR fingerprint for convergence detection */
  uint64_t ir_hash;
  bool has_converged;
  
  /* Function profiles */
  ProfileFunction functions[PROFILE_MAX_FUNCTIONS];
  int function_count;
  
  /* Branch profiles */
  ProfileBranch branches[PROFILE_MAX_BRANCHES];
  int branch_count;
  
  /* Loop profiles */
  ProfileLoop loops[PROFILE_MAX_LOOPS];
  int loop_count;
  
  /* Aggregate metrics */
  uint64_t total_execution_time_us;
  uint64_t total_instructions;
  double cache_hit_ratio;        /* Estimated from access patterns */
  
  /* Hotness threshold (% of total time to be considered hot) */
  double hot_threshold;
  double cold_threshold;
};

typedef struct QiscProfile QiscProfile;

/* Initialize empty profile */
void profile_init(QiscProfile *profile);

/* Free profile resources */
void profile_free(QiscProfile *profile);

/* Record function call */
void profile_record_call(QiscProfile *profile, const char *func_name,
                         uint64_t cycles);

/* Record branch taken/not-taken */
void profile_record_branch(QiscProfile *profile, const char *location,
                           bool taken);

/* Record loop iteration */
void profile_record_loop(QiscProfile *profile, const char *location,
                         int iterations);

/* Finalize profile (compute derived metrics, mark hot/cold paths) */
void profile_finalize(QiscProfile *profile);

/* Save profile to file (JSON format) */
int profile_save(QiscProfile *profile, const char *path);

/* Load profile from file */
int profile_load(QiscProfile *profile, const char *path);

/* Merge another profile into this one (for multiple runs) */
void profile_merge(QiscProfile *target, const QiscProfile *source);

/* Query functions */
ProfileFunction *profile_get_function(QiscProfile *profile, const char *name);
ProfileBranch *profile_get_branch(QiscProfile *profile, const char *location);
ProfileLoop *profile_get_loop(QiscProfile *profile, const char *location);

/* Check if function is hot */
bool profile_is_hot(QiscProfile *profile, const char *func_name);

/* Check if branch is predictable */
bool profile_is_branch_predictable(QiscProfile *profile, const char *location);

/* Get suggested optimization level for function */
int profile_get_opt_level(QiscProfile *profile, const char *func_name);

/* Convergence detection */
void profile_set_ir_hash(QiscProfile *profile, uint64_t hash);
bool profile_check_convergence(QiscProfile *profile, uint64_t current_hash);

/* Print profile summary */
void profile_print_summary(QiscProfile *profile);

/* Debug: dump profile to stdout */
void profile_dump(QiscProfile *profile);

#endif /* QISC_PROFILE_H */
