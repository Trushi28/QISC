/*
 * QISC Personality System
 *
 * Makes compilation enjoyable with contextual messages,
 * achievements, and Easter eggs.
 */

#ifndef QISC_PERSONALITY_H
#define QISC_PERSONALITY_H

#include "qisc.h"
#include <stdbool.h>
#include <stdarg.h>
#include <stdio.h>
#include <time.h>

/* Code pattern analysis for snarky comments */
typedef enum {
  QISC_PATTERN_NONE,
  QISC_PATTERN_NESTED_LOOPS,  /* O(n²+) complexity */
  QISC_PATTERN_BUBBLE_SORT,   /* Obvious bubble sort pattern */
  QISC_PATTERN_COPY_PASTE,    /* Duplicate code detected */
  QISC_PATTERN_GOD_FUNCTION,  /* Function > 200 lines */
  QISC_PATTERN_EMPTY_CATCH,   /* try/catch that catches nothing */
  QISC_PATTERN_MAGIC_NUMBERS, /* Hardcoded numbers without explanation */
  QISC_PATTERN_PREMATURE_OPT, /* Micro-optimization in cold code */
  QISC_PATTERN_BRILLIANT,     /* Actually good code (for praise) */
} QiscSnarkyPattern;

/* Progress state for advanced progress bars */
typedef struct {
  int current_percent;
  int total_phases;
  int current_phase;
  const char *phase_name;
  time_t start_time;
  double memory_usage_percent;
  double cpu_usage_percent;
  bool is_struggling;
  bool is_desperate;
} ProgressState;

/* Print with personality */
void qisc_personality_print(QiscPersonality personality, const char *format,
                            ...);

/* Specific message types */
void qisc_msg_compiling(QiscPersonality p, const char *filename);
void qisc_msg_success(QiscPersonality p, double elapsed_seconds);
void qisc_msg_error(QiscPersonality p, const char *error);
void qisc_msg_warning(QiscPersonality p, const char *warning);
void qisc_msg_convergence(QiscPersonality p, int iterations, double speedup);
void qisc_msg_progress(QiscPersonality p, int percent, const char *phase);

/* Desperation phase messages (long/failing compilations) */
void qisc_msg_last_leg(QiscPersonality p, int memory_percent, int progress);
void qisc_msg_near_crash(QiscPersonality p, int progress);
void qisc_msg_encourage(QiscPersonality p, int progress, int elapsed_minutes);
void qisc_msg_survival_victory(QiscPersonality p, int total_minutes);
void qisc_msg_99_crash(QiscPersonality p);

/* Snarky comment generation based on code patterns
 * Usage:
 *   qisc_snarky_comment(QISC_PATTERN_NESTED_LOOPS, depth);
 *   qisc_snarky_comment(QISC_PATTERN_BUBBLE_SORT);
 *   qisc_snarky_comment(QISC_PATTERN_COPY_PASTE, count);
 *   qisc_snarky_comment(QISC_PATTERN_GOD_FUNCTION, func_name, lines, complexity);
 *   qisc_snarky_comment(QISC_PATTERN_MAGIC_NUMBERS, number);
 *   qisc_snarky_comment(QISC_PATTERN_BRILLIANT);
 */
void qisc_snarky_comment(int pattern, ...);

/* Achievement system */
void qisc_achievement_unlock(const char *id, const char *name,
                             const char *description);
void qisc_achievement_check(void);

/* Sage personality functions */
void qisc_sage_observe(QiscPersonality p, const char *code_pattern,
                       const char *suggestion);
void qisc_sage_slow_compile(QiscPersonality p);
void qisc_sage_optimization_success(QiscPersonality p);
void qisc_sage_first_compile(QiscPersonality p);

/* ==========================================================================
 * Advanced Progress Bar System
 * ========================================================================== */

/* Initialize progress state */
void qisc_progress_init(ProgressState *state);

/* Get time-of-day aware message */
const char *qisc_get_time_message(void);

/* Progress bar styles */
void progress_bar_standard(int percent);
void progress_bar_gradient(int percent);
void progress_bar_emoji(int percent, bool struggling);
void progress_bar_emotion(int percent, ProgressState *state);
void progress_bar_cryptic(int percent);
void progress_bar_sage(int percent);

/* Stress and mood indicators */
void qisc_print_stress_indicators(ProgressState *state);
void qisc_update_progress_mood(ProgressState *state);
void qisc_print_desperate_message(ProgressState *state);

/* Get phase message */
const char *qisc_get_phase_message(int phase);

/* Main progress update functions */
void qisc_progress_update(ProgressState *state, int percent, const char *phase);
void qisc_progress_update_personality(QiscPersonality p, ProgressState *state,
                                       int percent, const char *phase);

/* Alternative progress displays */
void qisc_spinner_update(int frame, const char *message);
void qisc_progress_multi_line(ProgressState *state, int phase_progress[],
                               int phase_count);
void qisc_progress_with_eta(ProgressState *state, int percent);
void qisc_progress_compact(int percent, const char *phase);

/* Recovery state for crash tracking */
typedef struct {
    time_t last_crash;
    int crash_count;
    int last_crash_progress;
    char last_crash_file[256];
} RecoveryState;

/* Existential and recovery personality functions */
void qisc_msg_existential(QiscPersonality p);
void qisc_msg_recovery_start(RecoveryState *state);
void qisc_msg_recovery_success(RecoveryState *state);
void qisc_msg_template_depth(int depth);
void qisc_msg_solidarity(int minutes_elapsed, int hour);
void recovery_load_state(RecoveryState *state);
void recovery_save_state(RecoveryState *state);

/* ==========================================================================
 * Cryptic Mode Easter Egg System
 * ========================================================================== */

/* ROT13 encoding/decoding helpers (caller must free result) */
char *personality_rot13_encode(const char *input);
char *personality_rot13_decode(const char *input);

/* Check if today is a cryptic trigger day (Friday 13th, full moon) */
bool personality_is_cryptic_day(void);
const char *personality_get_cryptic_day_reason(void);

/* Get cryptic messages */
const char *personality_cryptic_message(void);
const char *personality_cryptic_error(const char *original_error);
const char *personality_cryptic_fortune(void);

/* Assembly comment injection */
const char *personality_get_asm_comment(void);
void personality_emit_asm_comment(FILE *out);
const char *personality_cryptic_prologue(const char *func_name);
const char *personality_cryptic_epilogue(const char *func_name);

/* Hidden debug symbol generation */
const char *personality_get_cryptic_symbol(void);
const char *personality_cryptic_varname(int index);

/* Cryptic mode display functions */
void personality_cryptic_banner(void);
void personality_cryptic_compile_start(const char *filename);
void personality_cryptic_compile_end(double elapsed, bool success);
void personality_cryptic_progress(int percent);

#endif /* QISC_PERSONALITY_H */
