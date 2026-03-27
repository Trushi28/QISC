/*
 * QISC Competitive Statistics & Leaderboard Implementation
 */

#include "competitive.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

/* Forward declarations */
static time_t time_t_now(void);

/* ANSI color codes */
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_BOLD "\033[1m"
#define COLOR_DIM "\033[2m"

/* Box drawing characters */
#define BOX_TL "┌"
#define BOX_TR "┐"
#define BOX_BL "└"
#define BOX_BR "┘"
#define BOX_H "─"
#define BOX_V "│"
#define BOX_SEP "│"

/* Default paths */
static const char *DEFAULT_STATS_PATH = "/.qisc/stats.json";
static const char *DEFAULT_LEADERBOARD_PATH = "/.qisc/leaderboard.json";
static const char *QISC_DIR = "/.qisc";

/* Title thresholds */
typedef struct {
  int min_compilations;
  const char *title;
} TitleThreshold;

static const TitleThreshold TITLES[] = {
    {1000, "Transcendent Developer"},
    {500, "Code Sage"},
    {100, "Compiler Whisperer"},
    {50, "Optimization Enthusiast"},
    {10, "Determined Coder"},
    {0, "Curious Novice"},
};
#define TITLE_COUNT 6

/* Encouragement messages */
static const char *ENCOURAGEMENTS[] = {
    "Every compilation brings you closer to mastery!",
    "The compiler sees your dedication. Keep going!",
    "Your code grows stronger with each attempt.",
    "Persistence is the key to optimization.",
    "The journey of a thousand optimizations begins with a single compile.",
    "You're doing great! The bits are aligning in your favor.",
    "Stay determined! Even the greatest compilers started small.",
    "Your CPU believes in you!",
};
#define ENCOURAGEMENT_COUNT 8

/* Streak messages */
static const char *STREAK_MESSAGES[] = {
    "You're on fire! 🔥",
    "Unstoppable! ⚡",
    "The compiler fears your perfection!",
    "Flawless execution!",
    "Is this even legal?",
    "Achievement: Serial Success!",
};
#define STREAK_MESSAGE_COUNT 6

/* Comparison messages */
static const char *COMPARISON_MESSAGES[] = {
    "You're in the top 10%! Elite status achieved!",
    "Above average! You're climbing the ranks!",
    "Solid performance! Keep pushing!",
    "Room for improvement, but you're on track!",
    "The only way is up! Let's go!",
};
#define COMPARISON_COUNT 5

/* ============================================================================
 * Utility Functions
 * ============================================================================ */

const char *stats_get_default_path(void) {
  static char path[512];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(path, sizeof(path), "%s%s", home, DEFAULT_STATS_PATH);
  } else {
    snprintf(path, sizeof(path), ".%s", DEFAULT_STATS_PATH);
  }
  return path;
}

const char *leaderboard_get_default_path(void) {
  static char path[512];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(path, sizeof(path), "%s%s", home, DEFAULT_LEADERBOARD_PATH);
  } else {
    snprintf(path, sizeof(path), ".%s", DEFAULT_LEADERBOARD_PATH);
  }
  return path;
}

void stats_ensure_directory(void) {
  char dir[512];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(dir, sizeof(dir), "%s%s", home, QISC_DIR);
  } else {
    snprintf(dir, sizeof(dir), ".%s", QISC_DIR);
  }
  mkdir(dir, 0755);
}

int stats_is_3am(void) {
  time_t now = time(NULL);
  struct tm *local = localtime(&now);
  return (local->tm_hour >= 2 && local->tm_hour < 4);
}

void stats_format_time(int seconds, char *buffer, size_t bufsize) {
  int hours = seconds / 3600;
  int mins = (seconds % 3600) / 60;
  int secs = seconds % 60;

  if (hours > 0) {
    snprintf(buffer, bufsize, "%dh %dm", hours, mins);
  } else if (mins > 0) {
    snprintf(buffer, bufsize, "%dm %ds", mins, secs);
  } else {
    snprintf(buffer, bufsize, "%ds", secs);
  }
}

/* Simple JSON string escaping */
static void json_escape_string(const char *src, char *dst, size_t dstsize) {
  size_t j = 0;
  for (size_t i = 0; src[i] && j < dstsize - 2; i++) {
    if (src[i] == '"' || src[i] == '\\') {
      if (j < dstsize - 3) {
        dst[j++] = '\\';
      }
    }
    dst[j++] = src[i];
  }
  dst[j] = '\0';
}

/* Simple JSON string parsing (finds value for key) */
static int json_get_int(const char *json, const char *key) {
  char search[128];
  snprintf(search, sizeof(search), "\"%s\":", key);
  const char *pos = strstr(json, search);
  if (!pos)
    return 0;
  pos += strlen(search);
  while (*pos == ' ')
    pos++;
  return atoi(pos);
}

static double json_get_double(const char *json, const char *key) {
  char search[128];
  snprintf(search, sizeof(search), "\"%s\":", key);
  const char *pos = strstr(json, search);
  if (!pos)
    return 0.0;
  pos += strlen(search);
  while (*pos == ' ')
    pos++;
  return atof(pos);
}

static long json_get_long(const char *json, const char *key) {
  char search[128];
  snprintf(search, sizeof(search), "\"%s\":", key);
  const char *pos = strstr(json, search);
  if (!pos)
    return 0;
  pos += strlen(search);
  while (*pos == ' ')
    pos++;
  return atol(pos);
}

static void json_get_string(const char *json, const char *key, char *dst,
                            size_t dstsize) {
  char search[128];
  snprintf(search, sizeof(search), "\"%s\":", key);
  const char *pos = strstr(json, search);
  if (!pos) {
    dst[0] = '\0';
    return;
  }
  pos += strlen(search);
  /* Skip whitespace and find opening quote */
  while (*pos == ' ' || *pos == '\t' || *pos == '\n' || *pos == '\r')
    pos++;
  if (*pos != '"') {
    dst[0] = '\0';
    return;
  }
  pos++; /* Skip opening quote */
  size_t i = 0;
  while (*pos && *pos != '"' && i < dstsize - 1) {
    if (*pos == '\\' && *(pos + 1)) {
      pos++;
    }
    dst[i++] = *pos++;
  }
  dst[i] = '\0';
}

/* ============================================================================
 * Stats Functions
 * ============================================================================ */

void stats_init(CompileStats *stats) {
  memset(stats, 0, sizeof(CompileStats));
  stats->fastest_compile_seconds = -1.0; /* Indicates no record yet */
  stats->slowest_compile_seconds = 0.0;
  stats->first_compilation = 0;
  stats->last_compilation = 0;
}

void stats_load(CompileStats *stats, const char *path) {
  stats_init(stats);

  FILE *f = fopen(path, "r");
  if (!f)
    return;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0) {
    fclose(f);
    return;
  }

  char *json = malloc(size + 1);
  if (!json) {
    fclose(f);
    return;
  }

  size_t read = fread(json, 1, size, f);
  json[read] = '\0';
  fclose(f);

  /* Parse JSON fields */
  stats->total_compilations = json_get_int(json, "total_compilations");
  stats->successful_compilations = json_get_int(json, "successful_compilations");
  stats->failed_compilations = json_get_int(json, "failed_compilations");
  stats->convergence_achieved = json_get_int(json, "convergence_achieved");

  stats->fastest_compile_seconds = json_get_double(json, "fastest_compile_seconds");
  stats->slowest_compile_seconds = json_get_double(json, "slowest_compile_seconds");
  stats->longest_convergence_minutes = json_get_double(json, "longest_convergence_minutes");
  stats->most_optimizations_applied = json_get_int(json, "most_optimizations_applied");
  stats->highest_speedup_x10 = json_get_int(json, "highest_speedup_x10");
  stats->closest_to_crash_percent = json_get_int(json, "closest_to_crash_percent");

  stats->current_success_streak = json_get_int(json, "current_success_streak");
  stats->best_success_streak = json_get_int(json, "best_success_streak");
  stats->rage_quit_count = json_get_int(json, "rage_quit_count");

  stats->total_lines_compiled = json_get_int(json, "total_lines_compiled");
  stats->total_optimizations_ever = json_get_int(json, "total_optimizations_ever");
  stats->total_time_compiling_seconds = json_get_int(json, "total_time_compiling_seconds");
  stats->compilations_at_3am = json_get_int(json, "compilations_at_3am");
  stats->thermal_throttle_count = json_get_int(json, "thermal_throttle_count");

  stats->first_compilation = (time_t)json_get_long(json, "first_compilation");
  stats->last_compilation = (time_t)json_get_long(json, "last_compilation");

  json_get_string(json, "fastest_compile_file", stats->fastest_compile_file,
                  sizeof(stats->fastest_compile_file));
  json_get_string(json, "best_speedup_file", stats->best_speedup_file,
                  sizeof(stats->best_speedup_file));
  json_get_string(json, "most_opts_file", stats->most_opts_file,
                  sizeof(stats->most_opts_file));

  free(json);
}

void stats_save(CompileStats *stats, const char *path) {
  stats_ensure_directory();

  FILE *f = fopen(path, "w");
  if (!f) {
    fprintf(stderr, "Warning: Could not save stats to %s: %s\n", path,
            strerror(errno));
    return;
  }

  char fastest_esc[256], speedup_esc[256], opts_esc[256];
  json_escape_string(stats->fastest_compile_file, fastest_esc,
                     sizeof(fastest_esc));
  json_escape_string(stats->best_speedup_file, speedup_esc, sizeof(speedup_esc));
  json_escape_string(stats->most_opts_file, opts_esc, sizeof(opts_esc));

  fprintf(f, "{\n");
  fprintf(f, "  \"total_compilations\": %d,\n", stats->total_compilations);
  fprintf(f, "  \"successful_compilations\": %d,\n",
          stats->successful_compilations);
  fprintf(f, "  \"failed_compilations\": %d,\n", stats->failed_compilations);
  fprintf(f, "  \"convergence_achieved\": %d,\n", stats->convergence_achieved);
  fprintf(f, "  \"fastest_compile_seconds\": %.4f,\n",
          stats->fastest_compile_seconds);
  fprintf(f, "  \"slowest_compile_seconds\": %.4f,\n",
          stats->slowest_compile_seconds);
  fprintf(f, "  \"longest_convergence_minutes\": %.2f,\n",
          stats->longest_convergence_minutes);
  fprintf(f, "  \"most_optimizations_applied\": %d,\n",
          stats->most_optimizations_applied);
  fprintf(f, "  \"highest_speedup_x10\": %d,\n", stats->highest_speedup_x10);
  fprintf(f, "  \"closest_to_crash_percent\": %d,\n",
          stats->closest_to_crash_percent);
  fprintf(f, "  \"current_success_streak\": %d,\n",
          stats->current_success_streak);
  fprintf(f, "  \"best_success_streak\": %d,\n", stats->best_success_streak);
  fprintf(f, "  \"rage_quit_count\": %d,\n", stats->rage_quit_count);
  fprintf(f, "  \"total_lines_compiled\": %d,\n", stats->total_lines_compiled);
  fprintf(f, "  \"total_optimizations_ever\": %d,\n",
          stats->total_optimizations_ever);
  fprintf(f, "  \"total_time_compiling_seconds\": %d,\n",
          stats->total_time_compiling_seconds);
  fprintf(f, "  \"compilations_at_3am\": %d,\n", stats->compilations_at_3am);
  fprintf(f, "  \"thermal_throttle_count\": %d,\n",
          stats->thermal_throttle_count);
  fprintf(f, "  \"first_compilation\": %ld,\n", (long)stats->first_compilation);
  fprintf(f, "  \"last_compilation\": %ld,\n", (long)stats->last_compilation);
  fprintf(f, "  \"fastest_compile_file\": \"%s\",\n", fastest_esc);
  fprintf(f, "  \"best_speedup_file\": \"%s\",\n", speedup_esc);
  fprintf(f, "  \"most_opts_file\": \"%s\"\n", opts_esc);
  fprintf(f, "}\n");

  fclose(f);
}

void stats_record_compilation(CompileStats *stats, double seconds, int success,
                              int optimizations, double speedup) {
  stats_record_compilation_file(stats, seconds, success, optimizations, speedup,
                                NULL, 0);
}

void stats_record_compilation_file(CompileStats *stats, double seconds,
                                   int success, int optimizations,
                                   double speedup, const char *filename,
                                   int lines) {
  time_t now = time(NULL);

  stats->total_compilations++;
  stats->total_time_compiling_seconds += (int)seconds;
  stats->total_optimizations_ever += optimizations;
  stats->total_lines_compiled += lines;
  stats->last_compilation = now;

  if (stats->first_compilation == 0) {
    stats->first_compilation = now;
  }

  if (stats_is_3am()) {
    stats->compilations_at_3am++;
  }

  if (success) {
    stats->successful_compilations++;
    stats->current_success_streak++;

    if (stats->current_success_streak > stats->best_success_streak) {
      stats->best_success_streak = stats->current_success_streak;
    }

    /* Check for records */
    if (stats->fastest_compile_seconds < 0 ||
        seconds < stats->fastest_compile_seconds) {
      stats->fastest_compile_seconds = seconds;
      if (filename) {
        strncpy(stats->fastest_compile_file, filename,
                sizeof(stats->fastest_compile_file) - 1);
      }
    }

    if (seconds > stats->slowest_compile_seconds) {
      stats->slowest_compile_seconds = seconds;
    }

    if (optimizations > stats->most_optimizations_applied) {
      stats->most_optimizations_applied = optimizations;
      if (filename) {
        strncpy(stats->most_opts_file, filename,
                sizeof(stats->most_opts_file) - 1);
      }
    }

    int speedup_x10 = (int)(speedup * 10);
    if (speedup_x10 > stats->highest_speedup_x10) {
      stats->highest_speedup_x10 = speedup_x10;
      if (filename) {
        strncpy(stats->best_speedup_file, filename,
                sizeof(stats->best_speedup_file) - 1);
      }
    }
  } else {
    stats->failed_compilations++;
    stats->current_success_streak = 0;
  }
}

void stats_record_convergence(CompileStats *stats, int iterations,
                              double total_time) {
  (void)iterations;
  stats->convergence_achieved++;

  double minutes = total_time / 60.0;
  if (minutes > stats->longest_convergence_minutes) {
    stats->longest_convergence_minutes = minutes;
  }
}

void stats_record_crash(CompileStats *stats, int progress_percent) {
  int progress_x10 = progress_percent * 10;
  if (progress_x10 > stats->closest_to_crash_percent) {
    stats->closest_to_crash_percent = progress_x10;
  }
}

void stats_record_rage_quit(CompileStats *stats) { stats->rage_quit_count++; }

void stats_record_thermal_throttle(CompileStats *stats) {
  stats->thermal_throttle_count++;
}

/* Print helper for box drawing */
static void print_box_line(int width) {
  printf(BOX_H);
  for (int i = 0; i < width - 2; i++) {
    printf(BOX_H);
  }
  printf(BOX_H);
}

void stats_print_summary(CompileStats *stats) {
  const int WIDTH = 45;
  double success_rate = 0.0;
  if (stats->total_compilations > 0) {
    success_rate =
        (double)stats->successful_compilations / stats->total_compilations * 100.0;
  }

  const char *title = stats_get_title(stats);
  char time_buf[64];
  stats_format_time(stats->total_time_compiling_seconds, time_buf,
                    sizeof(time_buf));

  printf("\n");
  printf(COLOR_CYAN BOX_TL);
  print_box_line(WIDTH);
  printf(BOX_TR COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V COLOR_RESET COLOR_BOLD
         "          COMPILATION STATISTICS            " COLOR_RESET COLOR_CYAN
             BOX_V COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V);
  print_box_line(WIDTH);
  printf(BOX_V COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V COLOR_RESET " Title: " COLOR_YELLOW "%s" COLOR_RESET
                    " (%d compilations)" COLOR_CYAN,
         title, stats->total_compilations);
  int pad = WIDTH - 9 - (int)strlen(title) - 15 -
            snprintf(NULL, 0, "%d", stats->total_compilations);
  for (int i = 0; i < pad; i++)
    printf(" ");
  printf(BOX_V COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V COLOR_RESET " Success Rate: " COLOR_GREEN
                    "%.1f%%" COLOR_RESET,
         success_rate);
  pad = WIDTH - 16 - snprintf(NULL, 0, "%.1f%%", success_rate);
  for (int i = 0; i < pad; i++)
    printf(" ");
  printf(COLOR_CYAN BOX_V COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V COLOR_RESET " Current Streak: %d ⚡",
         stats->current_success_streak);
  pad = WIDTH - 19 - snprintf(NULL, 0, "%d", stats->current_success_streak);
  for (int i = 0; i < pad; i++)
    printf(" ");
  printf(COLOR_CYAN BOX_V COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V COLOR_RESET "                                             " COLOR_CYAN BOX_V COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V COLOR_RESET COLOR_BOLD
         " PERSONAL BESTS:                            " COLOR_RESET COLOR_CYAN
             BOX_V COLOR_RESET "\n");

  if (stats->fastest_compile_seconds >= 0) {
    printf(COLOR_CYAN BOX_V COLOR_RESET " Fastest compile: " COLOR_GREEN
                      "%.2fs" COLOR_RESET,
           stats->fastest_compile_seconds);
    if (stats->fastest_compile_file[0]) {
      printf(" (%s)", stats->fastest_compile_file);
    }
    pad = WIDTH - 20 -
          snprintf(NULL, 0, "%.2fs", stats->fastest_compile_seconds);
    if (stats->fastest_compile_file[0]) {
      pad -= (int)strlen(stats->fastest_compile_file) + 3;
    }
    for (int i = 0; i < pad && i < WIDTH; i++)
      printf(" ");
    printf(COLOR_CYAN BOX_V COLOR_RESET "\n");
  }

  if (stats->highest_speedup_x10 > 0) {
    double speedup = stats->highest_speedup_x10 / 10.0;
    printf(COLOR_CYAN BOX_V COLOR_RESET " Best speedup: " COLOR_GREEN
                      "%.1fx" COLOR_RESET,
           speedup);
    if (stats->best_speedup_file[0]) {
      printf(" (%s)", stats->best_speedup_file);
    }
    pad = WIDTH - 17 - snprintf(NULL, 0, "%.1fx", speedup);
    if (stats->best_speedup_file[0]) {
      pad -= (int)strlen(stats->best_speedup_file) + 3;
    }
    for (int i = 0; i < pad && i < WIDTH; i++)
      printf(" ");
    printf(COLOR_CYAN BOX_V COLOR_RESET "\n");
  }

  if (stats->most_optimizations_applied > 0) {
    printf(COLOR_CYAN BOX_V COLOR_RESET " Most optimizations: " COLOR_GREEN
                      "%d" COLOR_RESET,
           stats->most_optimizations_applied);
    if (stats->most_opts_file[0]) {
      printf(" (%s)", stats->most_opts_file);
    }
    pad = WIDTH - 22 -
          snprintf(NULL, 0, "%d", stats->most_optimizations_applied);
    if (stats->most_opts_file[0]) {
      pad -= (int)strlen(stats->most_opts_file) + 3;
    }
    for (int i = 0; i < pad && i < WIDTH; i++)
      printf(" ");
    printf(COLOR_CYAN BOX_V COLOR_RESET "\n");
  }

  printf(COLOR_CYAN BOX_V COLOR_RESET "                                             " COLOR_CYAN BOX_V COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V COLOR_RESET COLOR_BOLD
         " FUN FACTS:                                 " COLOR_RESET COLOR_CYAN
             BOX_V COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V COLOR_RESET " Lines compiled: " COLOR_MAGENTA
                    "%d" COLOR_RESET,
         stats->total_lines_compiled);
  pad = WIDTH - 18 - snprintf(NULL, 0, "%d", stats->total_lines_compiled);
  for (int i = 0; i < pad; i++)
    printf(" ");
  printf(COLOR_CYAN BOX_V COLOR_RESET "\n");

  printf(COLOR_CYAN BOX_V COLOR_RESET " Time compiling: " COLOR_MAGENTA
                    "%s" COLOR_RESET,
         time_buf);
  pad = WIDTH - 18 - (int)strlen(time_buf);
  for (int i = 0; i < pad; i++)
    printf(" ");
  printf(COLOR_CYAN BOX_V COLOR_RESET "\n");

  if (stats->compilations_at_3am > 0) {
    printf(COLOR_CYAN BOX_V COLOR_RESET " 3AM compilations: " COLOR_YELLOW
                      "%d" COLOR_RESET " 🌙",
           stats->compilations_at_3am);
    pad = WIDTH - 23 - snprintf(NULL, 0, "%d", stats->compilations_at_3am);
    for (int i = 0; i < pad; i++)
      printf(" ");
    printf(COLOR_CYAN BOX_V COLOR_RESET "\n");
  }

  printf(COLOR_CYAN BOX_BL);
  print_box_line(WIDTH);
  printf(BOX_BR COLOR_RESET "\n\n");
}

void stats_print_records(CompileStats *stats) {
  printf(COLOR_BOLD "\n📊 PERSONAL RECORDS:\n" COLOR_RESET);
  printf("─────────────────────────────────────────\n");

  if (stats->fastest_compile_seconds >= 0) {
    printf("⚡ Fastest compile: %.3fs", stats->fastest_compile_seconds);
    if (stats->fastest_compile_file[0]) {
      printf(" (%s)", stats->fastest_compile_file);
    }
    printf("\n");
  }

  if (stats->slowest_compile_seconds > 0) {
    printf("🐌 Slowest compile: %.3fs\n", stats->slowest_compile_seconds);
  }

  if (stats->highest_speedup_x10 > 0) {
    printf("🚀 Best speedup: %.1fx", stats->highest_speedup_x10 / 10.0);
    if (stats->best_speedup_file[0]) {
      printf(" (%s)", stats->best_speedup_file);
    }
    printf("\n");
  }

  if (stats->most_optimizations_applied > 0) {
    printf("🔧 Most optimizations: %d", stats->most_optimizations_applied);
    if (stats->most_opts_file[0]) {
      printf(" (%s)", stats->most_opts_file);
    }
    printf("\n");
  }

  if (stats->best_success_streak > 0) {
    printf("🔥 Best streak: %d successful compilations\n",
           stats->best_success_streak);
  }

  if (stats->longest_convergence_minutes > 0) {
    printf("⏱️  Longest convergence: %.1f minutes\n",
           stats->longest_convergence_minutes);
  }

  if (stats->closest_to_crash_percent > 0) {
    printf("💀 Closest to crash: %.1f%% (and survived!)\n",
           stats->closest_to_crash_percent / 10.0);
  }

  printf("\n");
}

/* ============================================================================
 * Title and Message Functions
 * ============================================================================ */

const char *stats_get_title(CompileStats *stats) {
  for (int i = 0; i < TITLE_COUNT; i++) {
    if (stats->total_compilations >= TITLES[i].min_compilations) {
      return TITLES[i].title;
    }
  }
  return "Curious Novice";
}

const char *stats_get_encouragement(CompileStats *stats) {
  unsigned int seed = (unsigned int)(stats->total_compilations +
                                     stats->successful_compilations);
  return ENCOURAGEMENTS[seed % ENCOURAGEMENT_COUNT];
}

const char *stats_compare_to_average(CompileStats *stats) {
  double success_rate = 0.0;
  if (stats->total_compilations > 0) {
    success_rate =
        (double)stats->successful_compilations / stats->total_compilations * 100.0;
  }

  if (success_rate >= 95.0)
    return COMPARISON_MESSAGES[0];
  if (success_rate >= 85.0)
    return COMPARISON_MESSAGES[1];
  if (success_rate >= 70.0)
    return COMPARISON_MESSAGES[2];
  if (success_rate >= 50.0)
    return COMPARISON_MESSAGES[3];
  return COMPARISON_MESSAGES[4];
}

const char *stats_get_streak_message(CompileStats *stats) {
  if (stats->current_success_streak >= 20) {
    return STREAK_MESSAGES[4];
  } else if (stats->current_success_streak >= 15) {
    return STREAK_MESSAGES[3];
  } else if (stats->current_success_streak >= 10) {
    return STREAK_MESSAGES[2];
  } else if (stats->current_success_streak >= 7) {
    return STREAK_MESSAGES[1];
  } else if (stats->current_success_streak >= 5) {
    return STREAK_MESSAGES[0];
  }
  return NULL;
}

const char *stats_get_time_fact(CompileStats *stats) {
  static char buffer[256];
  int seconds = stats->total_time_compiling_seconds;

  if (seconds >= 86400) {
    snprintf(buffer, sizeof(buffer),
             "You've spent over %d days compiling. Dedication!", seconds / 86400);
  } else if (seconds >= 3600) {
    snprintf(buffer, sizeof(buffer),
             "That's %d hours of compilation. Time well spent!", seconds / 3600);
  } else if (seconds >= 60) {
    snprintf(buffer, sizeof(buffer),
             "%d minutes of your life, optimized!", seconds / 60);
  } else {
    snprintf(buffer, sizeof(buffer), "Just getting started! Keep compiling!");
  }
  return buffer;
}

/* ============================================================================
 * Leaderboard Functions
 * ============================================================================ */

void leaderboard_init(Leaderboard *lb) {
  memset(lb, 0, sizeof(Leaderboard));
  lb->count = 0;
}

static int leaderboard_compare(const void *a, const void *b) {
  const LeaderboardEntry *ea = (const LeaderboardEntry *)a;
  const LeaderboardEntry *eb = (const LeaderboardEntry *)b;
  return eb->score - ea->score; /* Descending order */
}

void leaderboard_add(Leaderboard *lb, const char *name, int score,
                     const char *file) {
  if (lb->count >= 100) {
    /* Replace lowest score if new score is higher */
    if (score <= lb->entries[99].score) {
      return;
    }
    lb->count = 99;
  }

  LeaderboardEntry *entry = &lb->entries[lb->count];
  strncpy(entry->name, name, sizeof(entry->name) - 1);
  entry->name[sizeof(entry->name) - 1] = '\0';
  entry->score = score;
  strncpy(entry->file, file, sizeof(entry->file) - 1);
  entry->file[sizeof(entry->file) - 1] = '\0';
  entry->achieved = time(NULL);
  lb->count++;

  /* Sort by score descending */
  qsort(lb->entries, lb->count, sizeof(LeaderboardEntry), leaderboard_compare);
}

int leaderboard_get_rank(Leaderboard *lb, int score) {
  for (int i = 0; i < lb->count; i++) {
    if (score >= lb->entries[i].score) {
      return i + 1;
    }
  }
  return lb->count + 1;
}

void leaderboard_print(Leaderboard *lb, int top_n) {
  if (lb->count == 0) {
    printf("No leaderboard entries yet. Start compiling!\n");
    return;
  }

  int count = (top_n > lb->count) ? lb->count : top_n;
  if (count <= 0)
    count = lb->count;

  printf("\n");
  printf(COLOR_YELLOW "🏆 LEADERBOARD 🏆\n" COLOR_RESET);
  printf("═══════════════════════════════════════════════\n");
  printf(COLOR_BOLD "  #  %-20s %10s  %-15s\n" COLOR_RESET, "Name", "Score",
         "File");
  printf("───────────────────────────────────────────────\n");

  for (int i = 0; i < count; i++) {
    const char *medal = "";
    const char *color = COLOR_RESET;

    if (i == 0) {
      medal = "🥇";
      color = COLOR_YELLOW;
    } else if (i == 1) {
      medal = "🥈";
      color = COLOR_RESET;
    } else if (i == 2) {
      medal = "🥉";
      color = COLOR_RED;
    } else {
      medal = "  ";
    }

    struct tm *tm_info = localtime(&lb->entries[i].achieved);
    char date_buf[16];
    strftime(date_buf, sizeof(date_buf), "%Y-%m-%d", tm_info);

    printf("%s %s%2d. %-20s %10d  %-15s" COLOR_RESET " %s\n", medal, color,
           i + 1, lb->entries[i].name, lb->entries[i].score,
           lb->entries[i].file, date_buf);
  }

  printf("═══════════════════════════════════════════════\n\n");
}

void leaderboard_save(Leaderboard *lb, const char *path) {
  stats_ensure_directory();

  FILE *f = fopen(path, "w");
  if (!f) {
    fprintf(stderr, "Warning: Could not save leaderboard to %s: %s\n", path,
            strerror(errno));
    return;
  }

  fprintf(f, "{\n");
  fprintf(f, "  \"entries\": [\n");

  for (int i = 0; i < lb->count; i++) {
    char name_esc[128], file_esc[256];
    json_escape_string(lb->entries[i].name, name_esc, sizeof(name_esc));
    json_escape_string(lb->entries[i].file, file_esc, sizeof(file_esc));

    fprintf(f, "    {\n");
    fprintf(f, "      \"name\": \"%s\",\n", name_esc);
    fprintf(f, "      \"score\": %d,\n", lb->entries[i].score);
    fprintf(f, "      \"file\": \"%s\",\n", file_esc);
    fprintf(f, "      \"achieved\": %ld\n", (long)lb->entries[i].achieved);
    fprintf(f, "    }%s\n", (i < lb->count - 1) ? "," : "");
  }

  fprintf(f, "  ],\n");
  fprintf(f, "  \"count\": %d\n", lb->count);
  fprintf(f, "}\n");

  fclose(f);
}

void leaderboard_load(Leaderboard *lb, const char *path) {
  leaderboard_init(lb);

  FILE *f = fopen(path, "r");
  if (!f)
    return;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0) {
    fclose(f);
    return;
  }

  char *json = malloc(size + 1);
  if (!json) {
    fclose(f);
    return;
  }

  size_t read = fread(json, 1, size, f);
  json[read] = '\0';
  fclose(f);

  /* Parse entries array */
  const char *entries_start = strstr(json, "\"entries\"");
  if (!entries_start) {
    free(json);
    return;
  }

  const char *array_start = strchr(entries_start, '[');
  if (!array_start) {
    free(json);
    return;
  }

  /* Parse each entry object */
  const char *pos = array_start;
  while (lb->count < 100) {
    const char *obj_start = strchr(pos, '{');
    if (!obj_start)
      break;

    const char *obj_end = strchr(obj_start, '}');
    if (!obj_end)
      break;

    /* Extract entry data within this object */
    size_t obj_len = obj_end - obj_start + 1;
    char *obj = malloc(obj_len + 1);
    if (!obj)
      break;
    strncpy(obj, obj_start, obj_len);
    obj[obj_len] = '\0';

    LeaderboardEntry *entry = &lb->entries[lb->count];
    json_get_string(obj, "name", entry->name, sizeof(entry->name));
    entry->score = json_get_int(obj, "score");
    json_get_string(obj, "file", entry->file, sizeof(entry->file));
    entry->achieved = (time_t)json_get_long(obj, "achieved");

    free(obj);

    if (entry->name[0] != '\0') {
      lb->count++;
    }

    pos = obj_end + 1;
  }

  free(json);

  /* Ensure sorted */
  qsort(lb->entries, lb->count, sizeof(LeaderboardEntry), leaderboard_compare);
}

/* ============================================================================
 * Competitive Mode Implementation
 * ============================================================================ */

/* Additional file paths */
static const char *DEFAULT_PERSONAL_BESTS_PATH = "/.qisc/competitive/personal_bests.json";
static const char *DEFAULT_STREAKS_PATH = "/.qisc/competitive/streaks.json";
static const char *DEFAULT_COMPETITIVE_LEADERBOARD_PATH = "/.qisc/competitive/leaderboard.json";
static const char *COMPETITIVE_DIR = "/.qisc/competitive";

/* Extended titles for competitive mode */
static const TitleThreshold COMPETITIVE_TITLES[] = {
    {5000, "🏆 Legendary Compiler"},
    {2500, "⚡ Transcendent Developer"},
    {1000, "🔥 Code Sage"},
    {500, "✨ Compiler Whisperer"},
    {250, "💎 Optimization Master"},
    {100, "🚀 Build Champion"},
    {50, "⭐ Code Warrior"},
    {25, "🎯 Determined Coder"},
    {10, "🌱 Rising Star"},
    {5, "🔰 Apprentice"},
    {0, "👋 Curious Novice"},
};
#define COMPETITIVE_TITLE_COUNT 11

/* Rank messages based on percentile */
static const char *RANK_MESSAGES[] = {
    "Top 1%! 🏆 Elite compiler!",
    "Top 5%! 🥇 Outstanding!",
    "Top 10%! ⭐ Excellent work!",
    "Top 25%! 💪 Above average!",
    "Top 50%! 📈 Keep climbing!",
    "Warming up! 🔥 You've got this!",
};

/* Streak fire emojis based on streak length */
static const char *STREAK_FIRES[] = {
    "",
    "🔥",
    "🔥🔥",
    "🔥🔥🔥",
    "🔥🔥🔥🔥",
    "🔥🔥🔥🔥🔥",
    "💥🔥🔥🔥🔥🔥💥",
};
#define STREAK_FIRE_COUNT 7

/* Ensure competitive directory exists */
static void competitive_ensure_directory(void) {
  char dir[512];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(dir, sizeof(dir), "%s%s", home, COMPETITIVE_DIR);
  } else {
    snprintf(dir, sizeof(dir), ".%s", COMPETITIVE_DIR);
  }

  /* Create ~/.qisc first */
  char parent_dir[512];
  snprintf(parent_dir, sizeof(parent_dir), "%s/.qisc", home ? home : ".");
  mkdir(parent_dir, 0755);

  /* Create competitive subdirectory */
  mkdir(dir, 0755);
}

/* Get default paths for competitive mode files */
const char *personal_bests_get_default_path(void) {
  static char path[512];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(path, sizeof(path), "%s%s", home, DEFAULT_PERSONAL_BESTS_PATH);
  } else {
    snprintf(path, sizeof(path), ".%s", DEFAULT_PERSONAL_BESTS_PATH);
  }
  return path;
}

const char *streaks_get_default_path(void) {
  static char path[512];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(path, sizeof(path), "%s%s", home, DEFAULT_STREAKS_PATH);
  } else {
    snprintf(path, sizeof(path), ".%s", DEFAULT_STREAKS_PATH);
  }
  return path;
}

static const char *competitive_leaderboard_get_default_path(void) {
  static char path[512];
  const char *home = getenv("HOME");
  if (home) {
    snprintf(path, sizeof(path), "%s%s", home, DEFAULT_COMPETITIVE_LEADERBOARD_PATH);
  } else {
    snprintf(path, sizeof(path), ".%s", DEFAULT_COMPETITIVE_LEADERBOARD_PATH);
  }
  return path;
}

/* ============================================================================
 * Personal Bests Implementation
 * ============================================================================ */

void personal_bests_init(PersonalBests *pb) {
  memset(pb, 0, sizeof(PersonalBests));
}

PersonalBest *personal_bests_get(PersonalBests *pb, const char *filename) {
  for (int i = 0; i < pb->count; i++) {
    if (strcmp(pb->records[i].filename, filename) == 0) {
      return &pb->records[i];
    }
  }
  return NULL;
}

bool personal_bests_update(PersonalBests *pb, const char *filename,
                           double time, int opts, double speedup) {
  bool is_new_record = false;
  PersonalBest *existing = personal_bests_get(pb, filename);

  if (existing) {
    if (time < existing->fastest_time || existing->fastest_time <= 0) {
      existing->fastest_time = time;
      is_new_record = true;
    }
    if (opts > existing->most_optimizations) {
      existing->most_optimizations = opts;
      is_new_record = true;
    }
    if (speedup > existing->best_speedup) {
      existing->best_speedup = speedup;
      is_new_record = true;
    }
    if (is_new_record) {
      existing->achieved = time_t_now();
    }
  } else if (pb->count < 256) {
    PersonalBest *new_record = &pb->records[pb->count++];
    strncpy(new_record->filename, filename, sizeof(new_record->filename) - 1);
    new_record->fastest_time = time;
    new_record->most_optimizations = opts;
    new_record->best_speedup = speedup;
    new_record->achieved = time_t_now();
    is_new_record = true;
  }

  return is_new_record;
}

static time_t time_t_now(void) {
  return time(NULL);
}

void personal_bests_save(PersonalBests *pb, const char *path) {
  competitive_ensure_directory();

  FILE *f = fopen(path, "w");
  if (!f) {
    return;
  }

  fprintf(f, "{\n");
  fprintf(f, "  \"records\": [\n");

  for (int i = 0; i < pb->count; i++) {
    char filename_esc[512];
    json_escape_string(pb->records[i].filename, filename_esc, sizeof(filename_esc));

    fprintf(f, "    {\n");
    fprintf(f, "      \"filename\": \"%s\",\n", filename_esc);
    fprintf(f, "      \"fastest_time\": %.4f,\n", pb->records[i].fastest_time);
    fprintf(f, "      \"most_optimizations\": %d,\n", pb->records[i].most_optimizations);
    fprintf(f, "      \"best_speedup\": %.2f,\n", pb->records[i].best_speedup);
    fprintf(f, "      \"achieved\": %ld\n", (long)pb->records[i].achieved);
    fprintf(f, "    }%s\n", (i < pb->count - 1) ? "," : "");
  }

  fprintf(f, "  ],\n");
  fprintf(f, "  \"count\": %d\n", pb->count);
  fprintf(f, "}\n");

  fclose(f);
}

void personal_bests_load(PersonalBests *pb, const char *path) {
  personal_bests_init(pb);

  FILE *f = fopen(path, "r");
  if (!f) return;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0) {
    fclose(f);
    return;
  }

  char *json = malloc(size + 1);
  if (!json) {
    fclose(f);
    return;
  }

  size_t read = fread(json, 1, size, f);
  json[read] = '\0';
  fclose(f);

  /* Parse records array */
  const char *records_start = strstr(json, "\"records\"");
  if (!records_start) {
    free(json);
    return;
  }

  const char *array_start = strchr(records_start, '[');
  if (!array_start) {
    free(json);
    return;
  }

  const char *pos = array_start;
  while (pb->count < 256) {
    const char *obj_start = strchr(pos, '{');
    if (!obj_start) break;

    const char *obj_end = strchr(obj_start, '}');
    if (!obj_end) break;

    size_t obj_len = obj_end - obj_start + 1;
    char *obj = malloc(obj_len + 1);
    if (!obj) break;
    strncpy(obj, obj_start, obj_len);
    obj[obj_len] = '\0';

    PersonalBest *record = &pb->records[pb->count];
    json_get_string(obj, "filename", record->filename, sizeof(record->filename));
    record->fastest_time = json_get_double(obj, "fastest_time");
    record->most_optimizations = json_get_int(obj, "most_optimizations");
    record->best_speedup = json_get_double(obj, "best_speedup");
    record->achieved = (time_t)json_get_long(obj, "achieved");

    free(obj);

    if (record->filename[0] != '\0') {
      pb->count++;
    }

    pos = obj_end + 1;
  }

  free(json);
}

/* ============================================================================
 * Streak Tracking Implementation
 * ============================================================================ */

void streaks_init(StreakData *streaks) {
  memset(streaks, 0, sizeof(StreakData));
}

static void get_current_date(char *buffer, size_t size) {
  time_t now = time(NULL);
  struct tm *tm_info = localtime(&now);
  strftime(buffer, size, "%Y-%m-%d", tm_info);
}

void competitive_track_streak(StreakData *streaks, bool success) {
  time_t now = time(NULL);
  char today[16];
  get_current_date(today, sizeof(today));

  if (success) {
    streaks->current_streak++;
    streaks->last_success = now;

    if (streaks->current_streak > streaks->best_streak) {
      streaks->best_streak = streaks->current_streak;
    }

    if (streaks->streak_start == 0) {
      streaks->streak_start = now;
    }

    /* Track daily streak */
    if (streaks->last_build_date[0] == '\0') {
      streaks->daily_streak = 1;
      strncpy(streaks->last_build_date, today, sizeof(streaks->last_build_date) - 1);
    } else if (strcmp(streaks->last_build_date, today) != 0) {
      /* Check if it's the next day */
      time_t yesterday = now - 86400;
      struct tm *yt = localtime(&yesterday);
      char yesterday_str[16];
      strftime(yesterday_str, sizeof(yesterday_str), "%Y-%m-%d", yt);

      if (strcmp(streaks->last_build_date, yesterday_str) == 0) {
        streaks->daily_streak++;
      } else {
        streaks->daily_streak = 1;
      }
      strncpy(streaks->last_build_date, today, sizeof(streaks->last_build_date) - 1);

      if (streaks->daily_streak > streaks->best_daily_streak) {
        streaks->best_daily_streak = streaks->daily_streak;
      }
    }
  } else {
    streaks->current_streak = 0;
    streaks->streak_start = 0;
  }
}

void streaks_save(StreakData *streaks, const char *path) {
  competitive_ensure_directory();

  FILE *f = fopen(path, "w");
  if (!f) return;

  char date_esc[32];
  json_escape_string(streaks->last_build_date, date_esc, sizeof(date_esc));

  fprintf(f, "{\n");
  fprintf(f, "  \"current_streak\": %d,\n", streaks->current_streak);
  fprintf(f, "  \"best_streak\": %d,\n", streaks->best_streak);
  fprintf(f, "  \"streak_start\": %ld,\n", (long)streaks->streak_start);
  fprintf(f, "  \"last_success\": %ld,\n", (long)streaks->last_success);
  fprintf(f, "  \"daily_streak\": %d,\n", streaks->daily_streak);
  fprintf(f, "  \"best_daily_streak\": %d,\n", streaks->best_daily_streak);
  fprintf(f, "  \"last_build_date\": \"%s\"\n", date_esc);
  fprintf(f, "}\n");

  fclose(f);
}

void streaks_load(StreakData *streaks, const char *path) {
  streaks_init(streaks);

  FILE *f = fopen(path, "r");
  if (!f) return;

  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0) {
    fclose(f);
    return;
  }

  char *json = malloc(size + 1);
  if (!json) {
    fclose(f);
    return;
  }

  size_t read = fread(json, 1, size, f);
  json[read] = '\0';
  fclose(f);

  streaks->current_streak = json_get_int(json, "current_streak");
  streaks->best_streak = json_get_int(json, "best_streak");
  streaks->streak_start = (time_t)json_get_long(json, "streak_start");
  streaks->last_success = (time_t)json_get_long(json, "last_success");
  streaks->daily_streak = json_get_int(json, "daily_streak");
  streaks->best_daily_streak = json_get_int(json, "best_daily_streak");
  json_get_string(json, "last_build_date", streaks->last_build_date,
                  sizeof(streaks->last_build_date));

  free(json);
}

/* ============================================================================
 * Competitive Mode Core
 * ============================================================================ */

void competitive_init(CompetitiveConfig *config) {
  config->enabled = true;
  config->show_leaderboard = true;
  config->show_personal_bests = true;
  config->show_titles = true;
  config->show_streaks = true;
  config->show_encouragement = true;
  config->verbose = false;
}

CompetitiveConfig competitive_default_config(void) {
  CompetitiveConfig config;
  competitive_init(&config);
  return config;
}

const char *competitive_award_title(CompileStats *stats) {
  for (int i = 0; i < COMPETITIVE_TITLE_COUNT; i++) {
    if (stats->total_compilations >= COMPETITIVE_TITLES[i].min_compilations) {
      return COMPETITIVE_TITLES[i].title;
    }
  }
  return "👋 Curious Novice";
}

int competitive_calculate_rank(CompileStats *stats, Leaderboard *lb, int score) {
  /* Get actual rank from leaderboard */
  int rank = leaderboard_get_rank(lb, score);

  /* If no leaderboard entries, simulate a fun rank based on stats */
  if (lb->count == 0) {
    /* Simulate global rank based on success rate and compilations */
    double success_rate = 0.0;
    if (stats->total_compilations > 0) {
      success_rate = (double)stats->successful_compilations / stats->total_compilations;
    }

    /* Fun simulation: more compilations + higher success = better rank */
    int simulated_total = 1000 + (stats->total_compilations * 10);
    int simulated_rank = simulated_total - (int)(stats->total_compilations * success_rate);
    if (simulated_rank < 1) simulated_rank = 1;
    return simulated_rank;
  }

  return rank;
}

static const char *get_streak_fire(int streak) {
  if (streak <= 0) return "";
  if (streak >= STREAK_FIRE_COUNT) return STREAK_FIRES[STREAK_FIRE_COUNT - 1];
  return STREAK_FIRES[streak];
}

static double calculate_percentile(int rank, int total) {
  if (total <= 0) return 50.0;
  return 100.0 - ((double)rank / total * 100.0);
}

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
) {
  CompetitiveResult result;
  memset(&result, 0, sizeof(result));

  result.build_time = build_time;
  result.optimizations = optimizations;
  result.speedup = speedup;
  result.lines = lines;
  result.success = success;
  result.filename = filename;

  /* Get previous optimization count for delta calculation */
  PersonalBest *prev_best = personal_bests_get(personal_bests, filename);
  int prev_opts = prev_best ? prev_best->most_optimizations : 0;

  /* Update personal bests */
  result.is_personal_best = personal_bests_update(personal_bests, filename,
                                                   build_time, optimizations, speedup);

  /* Track streak */
  int prev_streak = streaks->current_streak;
  competitive_track_streak(streaks, success);
  result.streak = streaks->current_streak;
  result.new_streak_record = (streaks->current_streak > prev_streak &&
                              streaks->current_streak == streaks->best_streak &&
                              streaks->current_streak > 1);

  /* Update main stats */
  stats_record_compilation_file(stats, build_time, success, optimizations,
                                speedup, filename, lines);

  /* Check for records */
  result.fastest_ever = (stats->fastest_compile_seconds >= 0 &&
                         build_time <= stats->fastest_compile_seconds + 0.001);
  result.most_opts_ever = (optimizations == stats->most_optimizations_applied &&
                           optimizations > 0);
  result.best_speedup_ever = (stats->highest_speedup_x10 > 0 &&
                              (int)(speedup * 10) >= stats->highest_speedup_x10);

  /* Calculate optimization delta */
  result.optimization_delta = optimizations - prev_opts;

  /* Calculate score for leaderboard */
  int score = (int)(optimizations * 10 + (1.0 / (build_time + 0.1)) * 100 +
                    speedup * 50 + streaks->current_streak * 5);

  /* Update leaderboard */
  char user[64] = "you";
  const char *username = getenv("USER");
  if (username) {
    strncpy(user, username, sizeof(user) - 1);
  }
  leaderboard_add(leaderboard, user, score, filename);

  /* Calculate rank */
  int simulated_players = 500 + stats->total_compilations * 2;
  result.total_players = simulated_players;
  result.global_rank = competitive_calculate_rank(stats, leaderboard, score);

  /* Calculate percentile */
  result.time_percentile = calculate_percentile(result.global_rank, result.total_players);

  /* Get title */
  result.title = competitive_award_title(stats);

  return result;
}

/* ============================================================================
 * Competitive Mode Display
 * ============================================================================ */

/* Double-width box drawing for competitive display */
#define COMP_BOX_TL "╔"
#define COMP_BOX_TR "╗"
#define COMP_BOX_BL "╚"
#define COMP_BOX_BR "╝"
#define COMP_BOX_H  "═"
#define COMP_BOX_V  "║"
#define COMP_BOX_ML "╠"
#define COMP_BOX_MR "╣"

static void print_comp_line(int width, const char *left, const char *right) {
  printf("%s", left);
  for (int i = 0; i < width; i++) {
    printf(COMP_BOX_H);
  }
  printf("%s\n", right);
}

static void print_comp_content(int width, const char *content) {
  int len = (int)strlen(content);
  /* Account for ANSI codes - rough estimate, strip for calculation */
  int visible_len = 0;
  bool in_escape = false;
  for (int i = 0; content[i]; i++) {
    if (content[i] == '\033') in_escape = true;
    else if (in_escape && content[i] == 'm') in_escape = false;
    else if (!in_escape) visible_len++;
  }

  int padding = width - visible_len;
  if (padding < 0) padding = 0;

  printf(COMP_BOX_V " %s", content);
  for (int i = 0; i < padding; i++) printf(" ");
  printf(" " COMP_BOX_V "\n");
}

void competitive_mode_display(CompetitiveResult *result) {
  const int WIDTH = 44;

  printf("\n");

  /* Top border */
  printf(COLOR_YELLOW COMP_BOX_TL);
  print_comp_line(WIDTH, "", COMP_BOX_TR COLOR_RESET);

  /* Header */
  if (result->success) {
    printf(COLOR_YELLOW COMP_BOX_V COLOR_RESET COLOR_BOLD
           "         🏆 COMPILATION COMPLETE 🏆         " COLOR_RESET
           COLOR_YELLOW COMP_BOX_V COLOR_RESET "\n");
  } else {
    printf(COLOR_YELLOW COMP_BOX_V COLOR_RESET COLOR_RED COLOR_BOLD
           "         💥 COMPILATION FAILED 💥          " COLOR_RESET
           COLOR_YELLOW COMP_BOX_V COLOR_RESET "\n");
  }

  /* Separator */
  printf(COLOR_YELLOW COMP_BOX_ML);
  print_comp_line(WIDTH, "", COMP_BOX_MR COLOR_RESET);

  /* Rank */
  char rank_buf[128];
  if (result->time_percentile >= 99.0) {
    snprintf(rank_buf, sizeof(rank_buf), 
             COLOR_GREEN "Rank: #%d globally (Top 1%%! 🏆)" COLOR_RESET, result->global_rank);
  } else if (result->time_percentile >= 90.0) {
    snprintf(rank_buf, sizeof(rank_buf),
             COLOR_GREEN "Rank: #%d globally (Top 10%%)" COLOR_RESET, result->global_rank);
  } else {
    snprintf(rank_buf, sizeof(rank_buf),
             "Rank: #%d globally", result->global_rank);
  }
  print_comp_content(WIDTH, rank_buf);

  /* Personal Best */
  char pb_buf[128];
  if (result->is_personal_best) {
    snprintf(pb_buf, sizeof(pb_buf), 
             COLOR_GREEN "Personal Best: ✓ NEW RECORD!" COLOR_RESET);
  } else {
    snprintf(pb_buf, sizeof(pb_buf), "Personal Best: -");
  }
  print_comp_content(WIDTH, pb_buf);

  /* Optimizations */
  char opts_buf[128];
  if (result->optimization_delta > 0) {
    snprintf(opts_buf, sizeof(opts_buf),
             COLOR_GREEN "Optimizations: %d (↑%d from last)" COLOR_RESET,
             result->optimizations, result->optimization_delta);
  } else if (result->optimization_delta < 0) {
    snprintf(opts_buf, sizeof(opts_buf),
             COLOR_RED "Optimizations: %d (↓%d from last)" COLOR_RESET,
             result->optimizations, -result->optimization_delta);
  } else {
    snprintf(opts_buf, sizeof(opts_buf), "Optimizations: %d", result->optimizations);
  }
  print_comp_content(WIDTH, opts_buf);

  /* Build Time */
  char time_buf[128];
  const char *percentile_label = "";
  if (result->time_percentile >= 90.0) percentile_label = " (Top 10%)";
  else if (result->time_percentile >= 75.0) percentile_label = " (Top 25%)";
  else if (result->time_percentile >= 50.0) percentile_label = " (Top 50%)";

  if (result->fastest_ever) {
    snprintf(time_buf, sizeof(time_buf),
             COLOR_GREEN "Build Time: %.2fs 🚀 FASTEST EVER!" COLOR_RESET, result->build_time);
  } else {
    snprintf(time_buf, sizeof(time_buf), "Build Time: %.2fs%s", result->build_time, percentile_label);
  }
  print_comp_content(WIDTH, time_buf);

  /* Title */
  char title_buf[128];
  snprintf(title_buf, sizeof(title_buf), "Title: %s", result->title);
  print_comp_content(WIDTH, title_buf);

  /* Streak */
  char streak_buf[128];
  const char *fire = get_streak_fire(result->streak);
  if (result->new_streak_record && result->streak > 1) {
    snprintf(streak_buf, sizeof(streak_buf),
             COLOR_YELLOW "Streak: %s %d builds 🎉 NEW BEST!" COLOR_RESET, fire, result->streak);
  } else if (result->streak >= 5) {
    snprintf(streak_buf, sizeof(streak_buf),
             COLOR_YELLOW "Streak: %s %d builds" COLOR_RESET, fire, result->streak);
  } else if (result->streak > 0) {
    snprintf(streak_buf, sizeof(streak_buf), "Streak: %s %d builds", fire, result->streak);
  } else {
    snprintf(streak_buf, sizeof(streak_buf), "Streak: 0 builds (start fresh!)");
  }
  print_comp_content(WIDTH, streak_buf);

  /* Special achievements */
  if (result->most_opts_ever && result->optimizations > 0) {
    print_comp_content(WIDTH, COLOR_MAGENTA "🔧 MOST OPTIMIZATIONS EVER!" COLOR_RESET);
  }
  if (result->best_speedup_ever && result->speedup > 1.0) {
    char speedup_buf[128];
    snprintf(speedup_buf, sizeof(speedup_buf),
             COLOR_MAGENTA "⚡ BEST SPEEDUP: %.1fx" COLOR_RESET, result->speedup);
    print_comp_content(WIDTH, speedup_buf);
  }

  /* Bottom border */
  printf(COLOR_YELLOW COMP_BOX_BL);
  print_comp_line(WIDTH, "", COMP_BOX_BR COLOR_RESET);

  printf("\n");
}

/* ============================================================================
 * Full Competitive Mode Flow
 * ============================================================================ */

void competitive_on_compile_complete(
    const char *filename,
    double build_time,
    int optimizations,
    double speedup,
    int lines,
    bool success,
    CompetitiveConfig *config
) {
  if (!config || !config->enabled) {
    return;
  }

  /* Load all state */
  CompileStats stats;
  PersonalBests personal_bests;
  StreakData streaks;
  Leaderboard leaderboard;

  stats_load(&stats, stats_get_default_path());
  personal_bests_load(&personal_bests, personal_bests_get_default_path());
  streaks_load(&streaks, streaks_get_default_path());
  leaderboard_load(&leaderboard, competitive_leaderboard_get_default_path());

  /* Update records and get result */
  CompetitiveResult result = competitive_update_records(
      &stats, &personal_bests, &streaks, &leaderboard,
      filename, build_time, optimizations, speedup, lines, success
  );

  /* Display competitive UI */
  competitive_mode_display(&result);

  /* Show encouragement if enabled */
  if (config->show_encouragement && success) {
    const char *encouragement = stats_get_encouragement(&stats);
    printf(COLOR_DIM "💡 %s" COLOR_RESET "\n\n", encouragement);
  }

  /* Save all state */
  stats_save(&stats, stats_get_default_path());
  personal_bests_save(&personal_bests, personal_bests_get_default_path());
  streaks_save(&streaks, streaks_get_default_path());
  leaderboard_save(&leaderboard, competitive_leaderboard_get_default_path());
}

/* ============================================================================
 * Pragma Detection
 * ============================================================================ */

bool competitive_check_pragma(const char *source) {
  if (!source) return false;

  /* Look for #pragma competitive */
  const char *pos = source;
  while ((pos = strstr(pos, "#pragma")) != NULL) {
    pos += 7; /* Skip "#pragma" */

    /* Skip whitespace */
    while (*pos == ' ' || *pos == '\t') pos++;

    /* Check for "competitive" */
    if (strncmp(pos, "competitive", 11) == 0) {
      char next = pos[11];
      /* Must be end of word (space, newline, or end of string) */
      if (next == '\0' || next == ' ' || next == '\t' || 
          next == '\n' || next == '\r') {
        return true;
      }
    }
  }

  return false;
}
