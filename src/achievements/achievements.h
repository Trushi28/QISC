/*
 * QISC Achievement System — Header
 *
 * Tracks and rewards compiler milestones with personality-aware messages.
 * Achievements are persistent across sessions via ~/.qisc/achievements.json
 */

#ifndef QISC_ACHIEVEMENTS_H
#define QISC_ACHIEVEMENTS_H

#include "../personality/personality.h"
#include <stdbool.h>
#include <stdint.h>

/* Maximum achievements */
#define MAX_ACHIEVEMENTS 128

/* Achievement categories */
typedef enum {
  ACH_CAT_COMPILATION,    /* Compilation milestones */
  ACH_CAT_OPTIMIZATION,   /* Optimization achievements */
  ACH_CAT_EXPLORATION,    /* Feature discovery */
  ACH_CAT_MASTERY,        /* Expert-level accomplishments */
  ACH_CAT_FUN             /* Easter eggs and fun achievements */
} AchievementCategory;

/* Achievement definition */
typedef struct {
  const char *id;         /* Unique identifier */
  const char *name;       /* Display name */
  const char *description;
  AchievementCategory category;
  bool unlocked;
  uint64_t unlock_time;   /* Unix timestamp */
} Achievement;

/* Compilation statistics for achievement tracking */
typedef struct {
  int current_success_streak;
  int best_success_streak;
  int current_fail_streak;
  int zero_warning_compiles;
  int pipeline_uses;
  int lambda_uses;
  int map_filter_reduce_uses;
  int tail_call_optimized_funcs;
  int achievement_views;
  double slowest_compile_ms;
  bool had_crash_at_99_percent;
  bool thermal_throttled;
  int compilations_this_year;
  int summer_compilations;
} ExtendedStats;

/* Achievement registry */
typedef struct {
  Achievement achievements[MAX_ACHIEVEMENTS];
  int count;
  
  /* Statistics for triggering achievements */
  int total_compilations;
  int successful_compilations;
  int failed_compilations;
  int convergence_runs;
  int total_functions_compiled;
  int total_lines_compiled;
  double best_speedup;
  double fastest_compile_ms;
  bool used_profile;
  bool used_pragma_optimize;
  bool used_pragma_context;
  
  /* Extended statistics for new achievements */
  ExtendedStats extended;
  
  /* Optimization tracking */
  int optimizations_in_compile;
  int max_optimizations_in_compile;
  int fastest_convergence_iters;
} AchievementRegistry;

/* Initialize achievement registry with default achievements */
void achievements_init(AchievementRegistry *reg);

/* Free achievement registry resources */
void achievements_free(AchievementRegistry *reg);

/* Load achievements from persistent storage */
int achievements_load(AchievementRegistry *reg);

/* Save achievements to persistent storage */
int achievements_save(AchievementRegistry *reg);

/* Check and possibly unlock achievements based on current stats */
void achievements_check(AchievementRegistry *reg, QiscPersonality personality);

/* Manually unlock an achievement */
bool achievements_unlock(AchievementRegistry *reg, const char *id, 
                         QiscPersonality personality);

/* Check if an achievement is unlocked */
bool achievements_is_unlocked(AchievementRegistry *reg, const char *id);

/* Get achievement by ID */
Achievement *achievements_get(AchievementRegistry *reg, const char *id);

/* Print all achievements (for `qisc achievements` command) */
void achievements_print_all(AchievementRegistry *reg);

/* Print unlock message with personality */
void achievements_print_unlock(Achievement *ach, QiscPersonality personality);

/* Update statistics after a compilation */
void achievements_record_compilation(AchievementRegistry *reg, 
                                     bool success, 
                                     double compile_time_ms,
                                     int functions,
                                     int lines);

/* Update statistics after convergence */
void achievements_record_convergence(AchievementRegistry *reg,
                                     int iterations,
                                     double speedup);

/* Check easter egg achievements (time-based, streaks, etc.) */
void achievements_check_easter_eggs(AchievementRegistry *reg, 
                                    QiscPersonality personality);

/* Check seasonal achievements */
void achievements_check_seasonal(AchievementRegistry *reg,
                                 QiscPersonality personality);

/* Check meta achievements (achievement-based achievements) */
void achievements_check_meta(AchievementRegistry *reg,
                             QiscPersonality personality);

/* Record achievement view (for meta achievement) */
void achievements_record_view(AchievementRegistry *reg);

/* Record pattern usage */
void achievements_record_pattern_usage(AchievementRegistry *reg,
                                       const char *pattern_type,
                                       int count);

/* Print achievements with fancy formatting */
void achievements_print_fancy(AchievementRegistry *reg);

/* Get count of unlocked achievements */
int achievements_count_unlocked(AchievementRegistry *reg);

#endif /* QISC_ACHIEVEMENTS_H */
