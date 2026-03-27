/*
 * QISC Competitive Statistics & Leaderboard System
 *
 * Tracks compilation stats, personal records, and leaderboards
 * to make the compilation experience more engaging.
 *
 * Features:
 * - Global leaderboard rankings
 * - Personal best tracking per file
 * - Compilation records (fastest, most optimizations)
 * - Titles/ranks based on compilation history
 * - Streak tracking (consecutive successful builds)
 */

#ifndef QISC_COMPETITIVE_H
#define QISC_COMPETITIVE_H

#include <stdbool.h>
#include <stddef.h>
#include <time.h>

/* ============================================================================
 * Core Types
 * ============================================================================ */

/* Compilation statistics tracking */
typedef struct {
  /* Compilation stats */
  int total_compilations;
  int successful_compilations;
  int failed_compilations;
  int convergence_achieved;

  /* Records */
  double fastest_compile_seconds;
  double slowest_compile_seconds;
  double longest_convergence_minutes;
  int most_optimizations_applied;
  int highest_speedup_x10; /* 4.3x = 43 */
  int closest_to_crash_percent; /* 99.7% = 997 */

  /* Streaks */
  int current_success_streak;
  int best_success_streak;
  int rage_quit_count;

  /* Fun stats */
  int total_lines_compiled;
  int total_optimizations_ever;
  int total_time_compiling_seconds;
  int compilations_at_3am;
  int thermal_throttle_count;

  /* Dates */
  time_t first_compilation;
  time_t last_compilation;

  /* Record file names */
  char fastest_compile_file[128];
  char best_speedup_file[128];
  char most_opts_file[128];
} CompileStats;

/* Leaderboard entry */
typedef struct {
  char name[64];
  int score;
  char file[128];
  time_t achieved;
} LeaderboardEntry;

/* Leaderboard container */
typedef struct {
  LeaderboardEntry entries[100];
  int count;
} Leaderboard;

/* ============================================================================
 * Competitive Mode Types
 * ============================================================================ */

/* Personal best record for a specific file */
typedef struct {
  char filename[256];
  double fastest_time;
  int most_optimizations;
  double best_speedup;
  int best_streak_at_compile;
  time_t achieved;
} PersonalBest;

/* Personal bests container */
typedef struct {
  PersonalBest records[256];
  int count;
} PersonalBests;

/* Streak tracking data */
typedef struct {
  int current_streak;
  int best_streak;
  time_t streak_start;
  time_t last_success;
  int daily_streak;          /* Consecutive days with builds */
  int best_daily_streak;
  char last_build_date[16];  /* YYYY-MM-DD */
} StreakData;

/* Competitive mode compilation result */
typedef struct {
  /* Current compilation metrics */
  double build_time;
  int optimizations;
  double speedup;
  int lines;
  bool success;
  const char *filename;

  /* Comparison to history */
  int global_rank;
  int total_players;  /* Simulated for fun */
  bool is_personal_best;
  int optimization_delta; /* Change from last compile */
  double time_percentile; /* Top X% */
  const char *title;
  int streak;
  bool new_streak_record;

  /* Records broken */
  bool fastest_ever;
  bool most_opts_ever;
  bool best_speedup_ever;
} CompetitiveResult;

/* Competitive mode configuration */
typedef struct {
  bool enabled;
  bool show_leaderboard;
  bool show_personal_bests;
  bool show_titles;
  bool show_streaks;
  bool show_encouragement;
  bool verbose;
} CompetitiveConfig;

/* Stats API */
void stats_init(CompileStats *stats);
void stats_load(CompileStats *stats, const char *path);
void stats_save(CompileStats *stats, const char *path);
void stats_record_compilation(CompileStats *stats, double seconds, int success,
                              int optimizations, double speedup);
void stats_record_compilation_file(CompileStats *stats, double seconds,
                                   int success, int optimizations,
                                   double speedup, const char *filename,
                                   int lines);
void stats_record_convergence(CompileStats *stats, int iterations,
                              double total_time);
void stats_record_crash(CompileStats *stats, int progress_percent);
void stats_record_rage_quit(CompileStats *stats);
void stats_record_thermal_throttle(CompileStats *stats);
void stats_print_summary(CompileStats *stats);
void stats_print_records(CompileStats *stats);

/* Leaderboard API */
void leaderboard_init(Leaderboard *lb);
void leaderboard_add(Leaderboard *lb, const char *name, int score,
                     const char *file);
void leaderboard_print(Leaderboard *lb, int top_n);
void leaderboard_save(Leaderboard *lb, const char *path);
void leaderboard_load(Leaderboard *lb, const char *path);
int leaderboard_get_rank(Leaderboard *lb, int score);

/* Fun messages based on stats */
const char *stats_get_title(CompileStats *stats);
const char *stats_get_encouragement(CompileStats *stats);
const char *stats_compare_to_average(CompileStats *stats);
const char *stats_get_streak_message(CompileStats *stats);
const char *stats_get_time_fact(CompileStats *stats);

/* Utility functions */
const char *stats_get_default_path(void);
const char *leaderboard_get_default_path(void);
void stats_ensure_directory(void);
int stats_is_3am(void);
void stats_format_time(int seconds, char *buffer, size_t bufsize);

/* ============================================================================
 * Competitive Mode API
 * ============================================================================ */

/* Initialize competitive mode */
void competitive_init(CompetitiveConfig *config);
CompetitiveConfig competitive_default_config(void);

/* Main competitive mode display after compilation */
void competitive_mode_display(CompetitiveResult *result);

/* Update records and get result for display */
CompetitiveResult competitive_update_records(
    CompileStats *stats,
    PersonalBests *personal_bests,
    StreakData *streaks,
    Leaderboard *leaderboard,
    const char *filename,
    double build_time,
    int optimizations,
    double speedup,
    int lines,
    bool success
);

/* Calculate global ranking (simulated with fun messages) */
int competitive_calculate_rank(CompileStats *stats, Leaderboard *lb, int score);

/* Award title based on stats */
const char *competitive_award_title(CompileStats *stats);

/* Track and update streaks */
void competitive_track_streak(StreakData *streaks, bool success);

/* Personal bests management */
void personal_bests_init(PersonalBests *pb);
void personal_bests_load(PersonalBests *pb, const char *path);
void personal_bests_save(PersonalBests *pb, const char *path);
PersonalBest *personal_bests_get(PersonalBests *pb, const char *filename);
bool personal_bests_update(PersonalBests *pb, const char *filename, 
                           double time, int opts, double speedup);
const char *personal_bests_get_default_path(void);

/* Streaks management */
void streaks_init(StreakData *streaks);
void streaks_load(StreakData *streaks, const char *path);
void streaks_save(StreakData *streaks, const char *path);
const char *streaks_get_default_path(void);

/* Full competitive mode flow */
void competitive_on_compile_complete(
    const char *filename,
    double build_time,
    int optimizations,
    double speedup,
    int lines,
    bool success,
    CompetitiveConfig *config
);

/* Check if competitive mode is enabled via pragma */
bool competitive_check_pragma(const char *source);

#endif /* QISC_COMPETITIVE_H */
