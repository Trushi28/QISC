/*
 * QISC Achievement System — Implementation
 *
 * Tracks compiler milestones and unlocks achievements with personality flair.
 */

#include "achievements.h"
#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>

/* ANSI color codes */
#define COLOR_RESET "\033[0m"
#define COLOR_GOLD "\033[33m"
#define COLOR_GREEN "\033[32m"
#define COLOR_CYAN "\033[36m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_BLUE "\033[34m"
#define COLOR_BOLD "\033[1m"

/* Default achievements - 50+ achievements across categories */
static const struct {
  const char *id;
  const char *name;
  const char *description;
  AchievementCategory category;
} DEFAULT_ACHIEVEMENTS[] = {
    /* ═══════════════════════════════════════════════════════════════
     * COMPILATION MILESTONES
     * ═══════════════════════════════════════════════════════════════ */
    {"first_compile", "Hello, World!", 
     "Compile your first QISC program", ACH_CAT_COMPILATION},
    {"ten_compiles", "Getting Warmed Up",
     "Successfully compile 10 programs", ACH_CAT_COMPILATION},
    {"hundred_compiles", "Centurion",
     "Successfully compile 100 programs", ACH_CAT_COMPILATION},
    {"speed_demon", "Speed Demon",
     "Compile a program in under 100ms", ACH_CAT_COMPILATION},
    {"no_errors", "First Try!",
     "Compile without any errors on first attempt", ACH_CAT_COMPILATION},

    /* ═══════════════════════════════════════════════════════════════
     * OPTIMIZATION ACHIEVEMENTS
     * ═══════════════════════════════════════════════════════════════ */
    {"first_converge", "The Journey Begins",
     "Complete your first convergence compilation", ACH_CAT_OPTIMIZATION},
    {"speedup_2x", "Double Trouble",
     "Achieve a 2x speedup through convergence", ACH_CAT_OPTIMIZATION},
    {"speedup_5x", "Quintessence",
     "Achieve a 5x speedup through convergence", ACH_CAT_OPTIMIZATION},
    {"speedup_10x", "Order of Magnitude",
     "Achieve a 10x speedup through convergence", ACH_CAT_OPTIMIZATION},
    {"profile_master", "Data Driven",
     "Use profile-guided optimization", ACH_CAT_OPTIMIZATION},
    /* New optimization achievements */
    {"easter_streak10", "Streak Master",
     "10 successful compilations in a row", ACH_CAT_OPTIMIZATION},
    {"quality_small", "Minimalist",
     "Binary under 10KB", ACH_CAT_OPTIMIZATION},
    {"quality_fast", "Lightning Fast",
     "Compilation under 0.1 seconds", ACH_CAT_OPTIMIZATION},
    {"pattern_recursive", "Recursion Master",
     "Tail-call optimized 10+ functions", ACH_CAT_OPTIMIZATION},
    {"comp_speedup10", "10x Speedup",
     "Achieved 10x optimization speedup", ACH_CAT_OPTIMIZATION},
    {"comp_convergence3", "Quick Converger",
     "Converged in 3 or fewer iterations", ACH_CAT_OPTIMIZATION},
    {"streak_20", "Unstoppable",
     "20 successful compilations in a row", ACH_CAT_OPTIMIZATION},

    /* ═══════════════════════════════════════════════════════════════
     * FEATURE DISCOVERY / EXPLORATION
     * ═══════════════════════════════════════════════════════════════ */
    {"use_lambda", "Anonymous Hero",
     "Use a lambda expression", ACH_CAT_EXPLORATION},
    {"use_pipeline", "Go With the Flow",
     "Use the pipeline operator", ACH_CAT_EXPLORATION},
    {"use_when", "Pattern Seeker",
     "Use pattern matching with 'when'", ACH_CAT_EXPLORATION},
    {"use_pragma", "Compiler Whisperer",
     "Use a pragma directive", ACH_CAT_EXPLORATION},
    {"all_contexts", "Context Collector",
     "Use all context pragma types", ACH_CAT_EXPLORATION},
    /* New exploration achievements */
    {"quality_clean", "Clean Coder",
     "Zero warnings compilation", ACH_CAT_EXPLORATION},
    {"quality_documented", "Documentation Master",
     "All functions have docstrings", ACH_CAT_EXPLORATION},
    {"pattern_pipeline", "Pipeline Pro",
     "50%+ pipeline syntax in a file", ACH_CAT_EXPLORATION},
    {"pattern_functional", "Functional Fanatic",
     "Used map/filter/reduce 50+ times", ACH_CAT_EXPLORATION},
    {"meta_all_basic", "Getting Started",
     "Unlocked 5 achievements", ACH_CAT_EXPLORATION},
    {"use_async", "Async Adventurer",
     "Use async/await syntax", ACH_CAT_EXPLORATION},
    {"use_generics", "Generic Genius",
     "Use generic type parameters", ACH_CAT_EXPLORATION},
    {"use_macro", "Macro Magician",
     "Define and use a macro", ACH_CAT_EXPLORATION},

    /* ═══════════════════════════════════════════════════════════════
     * MASTERY ACHIEVEMENTS
     * ═══════════════════════════════════════════════════════════════ */
    {"all_features", "Feature Complete",
     "Use all major language features", ACH_CAT_MASTERY},
    {"zero_warnings", "Perfectionist",
     "Compile 50 programs with zero warnings", ACH_CAT_MASTERY},
    {"large_project", "Architect",
     "Compile a project with 50+ functions", ACH_CAT_MASTERY},
    /* New mastery achievements */
    {"easter_100", "Century Club",
     "100 successful compilations", ACH_CAT_MASTERY},
    {"easter_1000", "Millennial Coder",
     "1000 successful compilations", ACH_CAT_MASTERY},
    {"comp_speedup100", "100x Speedup",
     "Achieved 100x optimization speedup", ACH_CAT_MASTERY},
    {"comp_opts100", "Optimization Addict",
     "100+ optimizations in one compile", ACH_CAT_MASTERY},
    {"meta_half", "Halfway There",
     "Unlocked 25 achievements", ACH_CAT_MASTERY},
    {"meta_all", "Completionist",
     "Unlocked all achievements", ACH_CAT_MASTERY},
    {"thousand_funcs", "Code Factory",
     "Compiled 1000+ functions total", ACH_CAT_MASTERY},
    {"million_lines", "Line Lord",
     "Compiled 1 million lines total", ACH_CAT_MASTERY},
    {"hundred_project", "Senior Architect",
     "Compile a project with 100+ functions", ACH_CAT_MASTERY},

    /* ═══════════════════════════════════════════════════════════════
     * FUN / EASTER EGGS
     * ═══════════════════════════════════════════════════════════════ */
    {"midnight_coder", "Night Owl",
     "Compile between midnight and 4am", ACH_CAT_FUN},
    {"friday_deploy", "Living Dangerously",
     "Compile on a Friday afternoon", ACH_CAT_FUN},
    {"quantum_state", "Quantum Observer",
     "Compile while the compiler observes you", ACH_CAT_FUN},
    /* Easter egg achievements */
    {"easter_dragon", "Dragon Slayer",
     "Fixed O(n²) to O(n log n)", ACH_CAT_FUN},
    {"easter_midnight", "Midnight Oil",
     "Compiled between 1-5 AM", ACH_CAT_FUN},
    {"easter_friday13", "Lucky Compiler",
     "Successful compile on Friday the 13th", ACH_CAT_FUN},
    {"easter_crash99", "So Close!",
     "Compilation crashed at 99%+", ACH_CAT_FUN},
    {"easter_5hour", "Compilation Marathon",
     "Single compilation took 5+ hours", ACH_CAT_FUN},
    /* Seasonal achievements */
    {"seasonal_newyear", "New Year, New Code",
     "First compile of the year", ACH_CAT_FUN},
    {"seasonal_summer", "Summer Coder",
     "Compiled 100+ times in summer months", ACH_CAT_FUN},
    {"seasonal_holiday", "Holiday Spirit",
     "Compiled on a major holiday", ACH_CAT_FUN},
    {"seasonal_halloween", "Spooky Coder",
     "Compiled on Halloween", ACH_CAT_FUN},
    {"seasonal_valentine", "Code is Love",
     "Compiled on Valentine's Day", ACH_CAT_FUN},
    /* Survival achievements */
    {"survival_crash_recover", "Phoenix",
     "Succeeded after 5+ crash attempts", ACH_CAT_FUN},
    {"survival_memory99", "RAM Dancer",
     "Completed with 99%+ memory usage", ACH_CAT_FUN},
    {"survival_thermal", "Thermal Warrior",
     "Compiled through thermal throttling", ACH_CAT_FUN},
    /* Meta achievements */
    {"meta_view", "Achievement Hunter",
     "Viewed achievements 10 times", ACH_CAT_FUN},
    {"weekend_warrior", "Weekend Warrior",
     "Compiled on both Saturday and Sunday", ACH_CAT_FUN},
    {"early_bird", "Early Bird",
     "Compiled before 6 AM", ACH_CAT_FUN},
    {"pi_day", "Pi Enthusiast",
     "Compiled on March 14th (Pi Day)", ACH_CAT_FUN},
    {"leap_year", "Leap of Faith",
     "Compiled on February 29th", ACH_CAT_FUN},
    {"debug_hero", "Debug Hero",
     "Fixed a bug after 10+ attempts", ACH_CAT_FUN},
    {"binary_day", "Binary Day",
     "Compiled on 11/11", ACH_CAT_FUN},
    {"palindrome", "Palindrome Programmer",
     "Compiled at a palindrome time (e.g., 12:21)", ACH_CAT_FUN},
    {"lucky_seven", "Lucky Seven",
     "7 successful compiles on the 7th", ACH_CAT_FUN},
};

#define NUM_DEFAULT_ACHIEVEMENTS \
  (sizeof(DEFAULT_ACHIEVEMENTS) / sizeof(DEFAULT_ACHIEVEMENTS[0]))

/* Get achievements file path */
static const char *get_achievements_path(void) {
  static char path[512];
  const char *home = getenv("HOME");
  if (!home)
    home = ".";
  snprintf(path, sizeof(path), "%s/.qisc/achievements.json", home);
  return path;
}

/* Ensure ~/.qisc directory exists */
static void ensure_qisc_dir(void) {
  char dir[512];
  const char *home = getenv("HOME");
  if (!home)
    home = ".";
  snprintf(dir, sizeof(dir), "%s/.qisc", home);
  mkdir(dir, 0755);
}

void achievements_init(AchievementRegistry *reg) {
  memset(reg, 0, sizeof(*reg));

  /* Register default achievements */
  for (int i = 0; i < (int)NUM_DEFAULT_ACHIEVEMENTS && i < MAX_ACHIEVEMENTS; i++) {
    reg->achievements[i].id = DEFAULT_ACHIEVEMENTS[i].id;
    reg->achievements[i].name = DEFAULT_ACHIEVEMENTS[i].name;
    reg->achievements[i].description = DEFAULT_ACHIEVEMENTS[i].description;
    reg->achievements[i].category = DEFAULT_ACHIEVEMENTS[i].category;
    reg->achievements[i].unlocked = false;
    reg->achievements[i].unlock_time = 0;
  }
  reg->count = (int)NUM_DEFAULT_ACHIEVEMENTS;
  
  /* Initialize extended stats */
  reg->extended.current_success_streak = 0;
  reg->extended.best_success_streak = 0;
  reg->extended.zero_warning_compiles = 0;
  reg->extended.achievement_views = 0;
  reg->fastest_convergence_iters = INT_MAX;

  /* Load any previously unlocked achievements */
  achievements_load(reg);
}

void achievements_free(AchievementRegistry *reg) {
  /* Nothing to free - all strings are static */
  (void)reg;
}

int achievements_load(AchievementRegistry *reg) {
  const char *path = get_achievements_path();
  FILE *f = fopen(path, "r");
  if (!f)
    return -1;

  char line[1024];
  while (fgets(line, sizeof(line), f)) {
    /* Simple parsing: look for "id": "xxx", "unlocked": true */
    char *id_start = strstr(line, "\"id\":\"");
    if (!id_start)
      continue;
    id_start += 6;
    char *id_end = strchr(id_start, '"');
    if (!id_end)
      continue;
    *id_end = '\0';

    /* Find matching achievement and mark unlocked */
    for (int i = 0; i < reg->count; i++) {
      if (strcmp(reg->achievements[i].id, id_start) == 0) {
        reg->achievements[i].unlocked = true;
        break;
      }
    }
  }

  fclose(f);
  return 0;
}

int achievements_save(AchievementRegistry *reg) {
  ensure_qisc_dir();
  const char *path = get_achievements_path();
  FILE *f = fopen(path, "w");
  if (!f) {
    fprintf(stderr, "Warning: Could not save achievements to %s\n", path);
    return -1;
  }

  fprintf(f, "{\n  \"achievements\": [\n");

  bool first = true;
  for (int i = 0; i < reg->count; i++) {
    if (!reg->achievements[i].unlocked)
      continue;

    if (!first)
      fprintf(f, ",\n");
    first = false;

    fprintf(f, "    {\"id\":\"%s\", \"unlock_time\":%lu}",
            reg->achievements[i].id,
            (unsigned long)reg->achievements[i].unlock_time);
  }

  fprintf(f, "\n  ],\n");
  fprintf(f, "  \"stats\": {\n");
  fprintf(f, "    \"total_compilations\": %d,\n", reg->total_compilations);
  fprintf(f, "    \"successful_compilations\": %d,\n", reg->successful_compilations);
  fprintf(f, "    \"convergence_runs\": %d,\n", reg->convergence_runs);
  fprintf(f, "    \"best_speedup\": %.2f\n", reg->best_speedup);
  fprintf(f, "  }\n");
  fprintf(f, "}\n");

  fclose(f);
  return 0;
}

void achievements_print_unlock(Achievement *ach, QiscPersonality personality) {
  const char *trophy = "🏆";
  const char *color;

  switch (ach->category) {
  case ACH_CAT_OPTIMIZATION:
    color = COLOR_GREEN;
    break;
  case ACH_CAT_EXPLORATION:
    color = COLOR_CYAN;
    break;
  case ACH_CAT_MASTERY:
    color = COLOR_MAGENTA;
    break;
  case ACH_CAT_FUN:
    color = COLOR_BLUE;
    break;
  default:
    color = COLOR_GOLD;
    break;
  }

  printf("\n");

  switch (personality) {
  case QISC_PERSONALITY_OFF:
  case QISC_PERSONALITY_MINIMAL:
    printf("Achievement Unlocked: %s\n", ach->name);
    break;

  case QISC_PERSONALITY_FRIENDLY:
    printf("%s%s╔══════════════════════════════════════════╗%s\n", 
           COLOR_BOLD, color, COLOR_RESET);
    printf("%s%s║  %s ACHIEVEMENT UNLOCKED!                  ║%s\n", 
           COLOR_BOLD, color, trophy, COLOR_RESET);
    printf("%s%s╠══════════════════════════════════════════╣%s\n", 
           COLOR_BOLD, color, COLOR_RESET);
    printf("%s%s║  %-40s║%s\n", COLOR_BOLD, color, ach->name, COLOR_RESET);
    printf("%s%s║  %-40s║%s\n", color, "", ach->description, COLOR_RESET);
    printf("%s%s╚══════════════════════════════════════════╝%s\n", 
           COLOR_BOLD, color, COLOR_RESET);
    break;

  case QISC_PERSONALITY_SNARKY:
    printf("%s%s Achievement Unlocked: \"%s\"%s\n", 
           color, trophy, ach->name, COLOR_RESET);
    printf("   %s\n", ach->description);
    printf("   %s(I suppose that's... impressive?)%s\n", 
           COLOR_GOLD, COLOR_RESET);
    break;

  case QISC_PERSONALITY_SAGE:
    printf("%s\"%s\"%s\n", COLOR_MAGENTA, ach->name, COLOR_RESET);
    printf("%s   — %s%s\n", COLOR_MAGENTA, ach->description, COLOR_RESET);
    printf("   The path of the compiler reveals another truth.\n");
    break;

  case QISC_PERSONALITY_CRYPTIC:
    printf("%s[PATTERN RECOGNIZED]%s\n", COLOR_BLUE, COLOR_RESET);
    printf("   %s%s%s\n", COLOR_BLUE, ach->name, COLOR_RESET);
    printf("   %s\n", ach->description);
    break;
  }
}

bool achievements_unlock(AchievementRegistry *reg, const char *id,
                         QiscPersonality personality) {
  for (int i = 0; i < reg->count; i++) {
    if (strcmp(reg->achievements[i].id, id) == 0) {
      if (reg->achievements[i].unlocked)
        return false; /* Already unlocked */

      reg->achievements[i].unlocked = true;
      reg->achievements[i].unlock_time = (uint64_t)time(NULL);
      achievements_print_unlock(&reg->achievements[i], personality);
      achievements_save(reg);
      return true;
    }
  }
  return false;
}

bool achievements_is_unlocked(AchievementRegistry *reg, const char *id) {
  for (int i = 0; i < reg->count; i++) {
    if (strcmp(reg->achievements[i].id, id) == 0) {
      return reg->achievements[i].unlocked;
    }
  }
  return false;
}

Achievement *achievements_get(AchievementRegistry *reg, const char *id) {
  for (int i = 0; i < reg->count; i++) {
    if (strcmp(reg->achievements[i].id, id) == 0) {
      return &reg->achievements[i];
    }
  }
  return NULL;
}

void achievements_check(AchievementRegistry *reg, QiscPersonality personality) {
  /* First compile */
  if (reg->successful_compilations >= 1) {
    achievements_unlock(reg, "first_compile", personality);
  }

  /* 10 compiles */
  if (reg->successful_compilations >= 10) {
    achievements_unlock(reg, "ten_compiles", personality);
  }

  /* 100 compiles */
  if (reg->successful_compilations >= 100) {
    achievements_unlock(reg, "hundred_compiles", personality);
    achievements_unlock(reg, "easter_100", personality);
  }

  /* 1000 compiles */
  if (reg->successful_compilations >= 1000) {
    achievements_unlock(reg, "easter_1000", personality);
  }

  /* Speed demon */
  if (reg->fastest_compile_ms > 0 && reg->fastest_compile_ms < 100) {
    achievements_unlock(reg, "speed_demon", personality);
  }
  
  /* Lightning fast - under 0.1 seconds */
  if (reg->fastest_compile_ms > 0 && reg->fastest_compile_ms < 100) {
    achievements_unlock(reg, "quality_fast", personality);
  }

  /* Convergence achievements */
  if (reg->convergence_runs >= 1) {
    achievements_unlock(reg, "first_converge", personality);
  }

  if (reg->best_speedup >= 2.0) {
    achievements_unlock(reg, "speedup_2x", personality);
  }

  if (reg->best_speedup >= 5.0) {
    achievements_unlock(reg, "speedup_5x", personality);
  }

  if (reg->best_speedup >= 10.0) {
    achievements_unlock(reg, "speedup_10x", personality);
    achievements_unlock(reg, "comp_speedup10", personality);
  }
  
  if (reg->best_speedup >= 100.0) {
    achievements_unlock(reg, "comp_speedup100", personality);
  }

  /* Profile usage */
  if (reg->used_profile) {
    achievements_unlock(reg, "profile_master", personality);
  }
  
  /* Streak achievements */
  if (reg->extended.current_success_streak >= 10) {
    achievements_unlock(reg, "easter_streak10", personality);
  }
  if (reg->extended.current_success_streak >= 20) {
    achievements_unlock(reg, "streak_20", personality);
  }
  
  /* Zero warnings achievement */
  if (reg->extended.zero_warning_compiles >= 1) {
    achievements_unlock(reg, "quality_clean", personality);
  }
  if (reg->extended.zero_warning_compiles >= 50) {
    achievements_unlock(reg, "zero_warnings", personality);
  }
  
  /* Large project achievement */
  if (reg->total_functions_compiled >= 50) {
    achievements_unlock(reg, "large_project", personality);
  }
  if (reg->total_functions_compiled >= 100) {
    achievements_unlock(reg, "hundred_project", personality);
  }
  if (reg->total_functions_compiled >= 1000) {
    achievements_unlock(reg, "thousand_funcs", personality);
  }
  
  /* Lines compiled */
  if (reg->total_lines_compiled >= 1000000) {
    achievements_unlock(reg, "million_lines", personality);
  }
  
  /* Quick convergence */
  if (reg->fastest_convergence_iters <= 3 && reg->fastest_convergence_iters > 0) {
    achievements_unlock(reg, "comp_convergence3", personality);
  }
  
  /* Optimization count */
  if (reg->max_optimizations_in_compile >= 100) {
    achievements_unlock(reg, "comp_opts100", personality);
  }
  
  /* Pattern usage */
  if (reg->extended.map_filter_reduce_uses >= 50) {
    achievements_unlock(reg, "pattern_functional", personality);
  }
  if (reg->extended.tail_call_optimized_funcs >= 10) {
    achievements_unlock(reg, "pattern_recursive", personality);
  }
  
  /* Compilation marathon - 5+ hours */
  if (reg->extended.slowest_compile_ms >= 5 * 60 * 60 * 1000) {
    achievements_unlock(reg, "easter_5hour", personality);
  }
  
  /* Crash at 99% */
  if (reg->extended.had_crash_at_99_percent) {
    achievements_unlock(reg, "easter_crash99", personality);
  }
  
  /* Survival achievements */
  if (reg->extended.current_fail_streak >= 5 && reg->successful_compilations > 0) {
    achievements_unlock(reg, "survival_crash_recover", personality);
  }
  if (reg->extended.thermal_throttled) {
    achievements_unlock(reg, "survival_thermal", personality);
  }

  /* Check time-based, seasonal, and meta achievements */
  achievements_check_easter_eggs(reg, personality);
  achievements_check_seasonal(reg, personality);
  achievements_check_meta(reg, personality);
}

void achievements_record_compilation(AchievementRegistry *reg,
                                     bool success,
                                     double compile_time_ms,
                                     int functions,
                                     int lines) {
  reg->total_compilations++;
  if (success) {
    reg->successful_compilations++;
    reg->extended.current_success_streak++;
    reg->extended.current_fail_streak = 0;
    if (reg->extended.current_success_streak > reg->extended.best_success_streak) {
      reg->extended.best_success_streak = reg->extended.current_success_streak;
    }
  } else {
    reg->failed_compilations++;
    reg->extended.current_fail_streak++;
    reg->extended.current_success_streak = 0;
  }

  reg->total_functions_compiled += functions;
  reg->total_lines_compiled += lines;

  if (compile_time_ms > 0) {
    if (reg->fastest_compile_ms == 0 || compile_time_ms < reg->fastest_compile_ms) {
      reg->fastest_compile_ms = compile_time_ms;
    }
    if (compile_time_ms > reg->extended.slowest_compile_ms) {
      reg->extended.slowest_compile_ms = compile_time_ms;
    }
  }
}

void achievements_record_convergence(AchievementRegistry *reg,
                                     int iterations,
                                     double speedup) {
  reg->convergence_runs++;
  if (speedup > reg->best_speedup) {
    reg->best_speedup = speedup;
  }
  if (iterations > 0 && iterations < reg->fastest_convergence_iters) {
    reg->fastest_convergence_iters = iterations;
  }
}

void achievements_check_easter_eggs(AchievementRegistry *reg, 
                                    QiscPersonality personality) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  
  /* Night owl: midnight to 4am */
  if (t->tm_hour >= 0 && t->tm_hour < 4) {
    achievements_unlock(reg, "midnight_coder", personality);
  }
  
  /* Midnight oil: 1-5 AM */
  if (t->tm_hour >= 1 && t->tm_hour < 5) {
    achievements_unlock(reg, "easter_midnight", personality);
  }
  
  /* Early bird: before 6 AM */
  if (t->tm_hour < 6) {
    achievements_unlock(reg, "early_bird", personality);
  }
  
  /* Friday afternoon (Friday = 5, after 2pm) */
  if (t->tm_wday == 5 && t->tm_hour >= 14) {
    achievements_unlock(reg, "friday_deploy", personality);
  }
  
  /* Friday the 13th */
  if (t->tm_wday == 5 && t->tm_mday == 13) {
    achievements_unlock(reg, "easter_friday13", personality);
  }
}

void achievements_check_seasonal(AchievementRegistry *reg,
                                 QiscPersonality personality) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);
  
  /* New Year's Day (Jan 1) */
  if (t->tm_mon == 0 && t->tm_mday == 1) {
    achievements_unlock(reg, "seasonal_newyear", personality);
  }
  
  /* Summer months (June, July, August) - northern hemisphere */
  if (t->tm_mon >= 5 && t->tm_mon <= 7) {
    reg->extended.summer_compilations++;
    if (reg->extended.summer_compilations >= 100) {
      achievements_unlock(reg, "seasonal_summer", personality);
    }
  }
  
  /* Halloween (Oct 31) */
  if (t->tm_mon == 9 && t->tm_mday == 31) {
    achievements_unlock(reg, "seasonal_halloween", personality);
    achievements_unlock(reg, "seasonal_holiday", personality);
  }
  
  /* Valentine's Day (Feb 14) */
  if (t->tm_mon == 1 && t->tm_mday == 14) {
    achievements_unlock(reg, "seasonal_valentine", personality);
    achievements_unlock(reg, "seasonal_holiday", personality);
  }
  
  /* Christmas (Dec 25) */
  if (t->tm_mon == 11 && t->tm_mday == 25) {
    achievements_unlock(reg, "seasonal_holiday", personality);
  }
  
  /* Pi Day (Mar 14) */
  if (t->tm_mon == 2 && t->tm_mday == 14) {
    achievements_unlock(reg, "pi_day", personality);
  }
  
  /* Leap year Feb 29 */
  if (t->tm_mon == 1 && t->tm_mday == 29) {
    achievements_unlock(reg, "leap_year", personality);
  }
  
  /* Binary Day (Nov 11 - 11/11) */
  if (t->tm_mon == 10 && t->tm_mday == 11) {
    achievements_unlock(reg, "binary_day", personality);
  }
  
  /* Palindrome time (e.g., 12:21, 11:11, 21:12) */
  int hour_tens = t->tm_hour / 10;
  int hour_ones = t->tm_hour % 10;
  int min_tens = t->tm_min / 10;
  int min_ones = t->tm_min % 10;
  if (hour_tens == min_ones && hour_ones == min_tens) {
    achievements_unlock(reg, "palindrome", personality);
  }
  
  /* Lucky seven - 7th of the month */
  if (t->tm_mday == 7) {
    achievements_unlock(reg, "lucky_seven", personality);
  }
  
  /* Weekend warrior check - store in extended stats */
  if (t->tm_wday == 0 || t->tm_wday == 6) {
    /* This would need persistent tracking for "both days" */
    achievements_unlock(reg, "weekend_warrior", personality);
  }
}

void achievements_check_meta(AchievementRegistry *reg,
                             QiscPersonality personality) {
  int unlocked = achievements_count_unlocked(reg);
  
  if (unlocked >= 5) {
    achievements_unlock(reg, "meta_all_basic", personality);
  }
  
  if (unlocked >= 25) {
    achievements_unlock(reg, "meta_half", personality);
  }
  
  /* Check if all achievements except meta_all are unlocked */
  int total_except_meta_all = reg->count - 1;
  if (unlocked >= total_except_meta_all) {
    achievements_unlock(reg, "meta_all", personality);
  }
  
  /* Achievement views */
  if (reg->extended.achievement_views >= 10) {
    achievements_unlock(reg, "meta_view", personality);
  }
}

void achievements_record_view(AchievementRegistry *reg) {
  reg->extended.achievement_views++;
}

void achievements_record_pattern_usage(AchievementRegistry *reg,
                                       const char *pattern_type,
                                       int count) {
  if (strcmp(pattern_type, "pipeline") == 0) {
    reg->extended.pipeline_uses += count;
  } else if (strcmp(pattern_type, "lambda") == 0) {
    reg->extended.lambda_uses += count;
  } else if (strcmp(pattern_type, "map_filter_reduce") == 0) {
    reg->extended.map_filter_reduce_uses += count;
  } else if (strcmp(pattern_type, "tail_call") == 0) {
    reg->extended.tail_call_optimized_funcs += count;
  }
}

int achievements_count_unlocked(AchievementRegistry *reg) {
  int count = 0;
  for (int i = 0; i < reg->count; i++) {
    if (reg->achievements[i].unlocked) {
      count++;
    }
  }
  return count;
}

/* Helper function to get category emoji and name */
static const char *get_category_info(AchievementCategory cat, const char **emoji) {
  switch (cat) {
  case ACH_CAT_COMPILATION:
    *emoji = "📝";
    return "Compilation";
  case ACH_CAT_OPTIMIZATION:
    *emoji = "⚡";
    return "Optimization";
  case ACH_CAT_EXPLORATION:
    *emoji = "🔍";
    return "Exploration";
  case ACH_CAT_MASTERY:
    *emoji = "🎯";
    return "Mastery";
  case ACH_CAT_FUN:
    *emoji = "🎪";
    return "Fun & Easter Eggs";
  default:
    *emoji = "❓";
    return "Unknown";
  }
}

/* Helper function to print achievements for a category */
static void print_category_achievements(AchievementRegistry *reg, 
                                        AchievementCategory cat,
                                        const char *emoji,
                                        const char *name) {
  printf("║  %s %s%s %-20s%s\n", emoji, COLOR_BOLD, COLOR_CYAN, name, COLOR_RESET);
  printf("║  ────────────────────────────────────────────────────────\n");
  
  for (int i = 0; i < reg->count; i++) {
    if (reg->achievements[i].category != cat)
      continue;

    const char *status = reg->achievements[i].unlocked ? "✓" : "○";
    const char *color = reg->achievements[i].unlocked ? COLOR_GREEN : COLOR_GOLD;
    const char *reset = COLOR_RESET;

    if (reg->achievements[i].unlocked) {
      printf("║    %s%s%s %-22s %s%s%s\n",
             color, status, reset,
             reg->achievements[i].name,
             COLOR_GOLD, reg->achievements[i].description, reset);
    } else {
      /* Hidden description for locked achievements */
      printf("║    %s%s%s %-22s %s???%s\n",
             color, status, reset,
             reg->achievements[i].name,
             COLOR_GOLD, reset);
    }
  }
  printf("║\n");
}

void achievements_print_fancy(AchievementRegistry *reg) {
  int unlocked = achievements_count_unlocked(reg);
  
  printf("\n");
  printf("%s╔════════════════════════════════════════════════════════════╗%s\n",
         COLOR_GOLD, COLOR_RESET);
  printf("%s║%s         🏆 %sQISC ACHIEVEMENTS%s 🏆                           %s║%s\n",
         COLOR_GOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET, COLOR_GOLD, COLOR_RESET);
  printf("%s╠════════════════════════════════════════════════════════════╣%s\n",
         COLOR_GOLD, COLOR_RESET);
  
  /* Progress bar */
  int bar_width = 30;
  int filled = (unlocked * bar_width) / reg->count;
  printf("%s║%s  Unlocked: %s%d%s/%s%d%s  [", 
         COLOR_GOLD, COLOR_RESET,
         COLOR_GREEN, unlocked, COLOR_RESET,
         COLOR_CYAN, reg->count, COLOR_RESET);
  for (int i = 0; i < bar_width; i++) {
    if (i < filled) {
      printf("%s█%s", COLOR_GREEN, COLOR_RESET);
    } else {
      printf("%s░%s", COLOR_GOLD, COLOR_RESET);
    }
  }
  printf("] %d%%    %s║%s\n", (unlocked * 100) / reg->count, COLOR_GOLD, COLOR_RESET);
  
  printf("%s╟────────────────────────────────────────────────────────────╢%s\n",
         COLOR_GOLD, COLOR_RESET);
  
  /* Print by category */
  const char *emoji;
  for (int cat = 0; cat < 5; cat++) {
    const char *name = get_category_info((AchievementCategory)cat, &emoji);
    print_category_achievements(reg, (AchievementCategory)cat, emoji, name);
  }
  
  printf("%s╠════════════════════════════════════════════════════════════╣%s\n",
         COLOR_GOLD, COLOR_RESET);
  printf("%s║%s  %s📊 STATISTICS%s                                            %s║%s\n",
         COLOR_GOLD, COLOR_RESET, COLOR_BOLD, COLOR_RESET, COLOR_GOLD, COLOR_RESET);
  printf("%s╟────────────────────────────────────────────────────────────╢%s\n",
         COLOR_GOLD, COLOR_RESET);
  printf("║  Total compilations:   %-6d   Successful:  %-6d       ║\n", 
         reg->total_compilations, reg->successful_compilations);
  printf("║  Best streak:          %-6d   Convergence runs: %-4d    ║\n",
         reg->extended.best_success_streak, reg->convergence_runs);
  printf("║  Best speedup:         %-6.1fx  Functions:   %-6d       ║\n",
         reg->best_speedup, reg->total_functions_compiled);
  printf("║  Fastest compile:      %-6.1fms Lines:       %-6d       ║\n",
         reg->fastest_compile_ms, reg->total_lines_compiled);
  printf("%s╚════════════════════════════════════════════════════════════╝%s\n",
         COLOR_GOLD, COLOR_RESET);
  printf("\n");
}

void achievements_print_all(AchievementRegistry *reg) {
  /* Record that achievements were viewed */
  achievements_record_view(reg);
  
  /* Use the fancy printer */
  achievements_print_fancy(reg);
}
