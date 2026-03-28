/*
 * QISC Runtime Library — Profiling Support
 *
 * Provides runtime functions for profile instrumentation.
 * This is linked into programs compiled with --profile.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_FUNCTIONS 256
#define MAX_BRANCHES 1024
#define MAX_LOOPS 512
#define PROFILE_VERSION 1

typedef struct {
  const char *name;
  long call_count;
  clock_t start_time;
  clock_t total_time;
} FunctionProfile;

typedef struct {
  const char *location;
  long taken_count;
  long not_taken_count;
} BranchProfile;

typedef struct {
  const char *location;
  long invocation_count;
  long total_iterations;
} LoopProfile;

static FunctionProfile profiles[MAX_FUNCTIONS];
static BranchProfile branch_profiles[MAX_BRANCHES];
static LoopProfile loop_profiles[MAX_LOOPS];
static int profile_count = 0;
static int branch_profile_count = 0;
static int loop_profile_count = 0;
static int profiling_active = 1;

static void write_escaped_string(FILE *fp, const char *str) {
  fputc('"', fp);
  for (const char *p = str; *p; p++) {
    switch (*p) {
    case '"':
      fputs("\\\"", fp);
      break;
    case '\\':
      fputs("\\\\", fp);
      break;
    case '\n':
      fputs("\\n", fp);
      break;
    case '\t':
      fputs("\\t", fp);
      break;
    default:
      fputc(*p, fp);
      break;
    }
  }
  fputc('"', fp);
}

static FunctionProfile *find_or_create_profile(const char *name) {
  for (int i = 0; i < profile_count; i++) {
    if (strcmp(profiles[i].name, name) == 0) {
      return &profiles[i];
    }
  }

  if (profile_count < MAX_FUNCTIONS) {
    profiles[profile_count].name = name;
    profiles[profile_count].call_count = 0;
    profiles[profile_count].start_time = 0;
    profiles[profile_count].total_time = 0;
    return &profiles[profile_count++];
  }

  return NULL;
}

static BranchProfile *find_or_create_branch(const char *location) {
  for (int i = 0; i < branch_profile_count; i++) {
    if (strcmp(branch_profiles[i].location, location) == 0) {
      return &branch_profiles[i];
    }
  }

  if (branch_profile_count < MAX_BRANCHES) {
    branch_profiles[branch_profile_count].location = location;
    branch_profiles[branch_profile_count].taken_count = 0;
    branch_profiles[branch_profile_count].not_taken_count = 0;
    return &branch_profiles[branch_profile_count++];
  }

  return NULL;
}

static LoopProfile *find_or_create_loop(const char *location) {
  for (int i = 0; i < loop_profile_count; i++) {
    if (strcmp(loop_profiles[i].location, location) == 0) {
      return &loop_profiles[i];
    }
  }

  if (loop_profile_count < MAX_LOOPS) {
    loop_profiles[loop_profile_count].location = location;
    loop_profiles[loop_profile_count].invocation_count = 0;
    loop_profiles[loop_profile_count].total_iterations = 0;
    return &loop_profiles[loop_profile_count++];
  }

  return NULL;
}

static void write_profile_json(const char *path) {
  uint64_t total_time = 0;
  FILE *fp = fopen(path, "w");
  if (!fp) {
    return;
  }

  for (int i = 0; i < profile_count; i++) {
    total_time += (uint64_t)profiles[i].total_time;
  }

  fprintf(fp, "{\n");
  fprintf(fp, "  \"version\": %d,\n", PROFILE_VERSION);
  fprintf(fp, "  \"timestamp\": %llu,\n", (unsigned long long)time(NULL));
  fprintf(fp, "  \"run_count\": 1,\n");
  fprintf(fp, "  \"ir_hash\": 0,\n");
  fprintf(fp, "  \"has_converged\": false,\n");
  fprintf(fp, "  \"functions\": [\n");

  for (int i = 0; i < profile_count; i++) {
    FunctionProfile *p = &profiles[i];
    bool is_hot = false;
    bool is_cold = false;
    bool should_inline = false;

    if (total_time > 0) {
      double ratio = (double)p->total_time / (double)total_time;
      is_hot = ratio >= 0.10;
      is_cold = ratio <= 0.001;
    }
    should_inline = p->call_count > 100 &&
                    ((double)p->total_time / CLOCKS_PER_SEC) < 0.010;

    fprintf(fp, "    {\"name\": ");
    write_escaped_string(fp, p->name ? p->name : "");
    fprintf(fp, ", \"call_count\": %ld", p->call_count);
    fprintf(fp, ", \"total_cycles\": %llu",
            (unsigned long long)p->total_time);
    fprintf(fp, ", \"is_hot\": %s", is_hot ? "true" : "false");
    fprintf(fp, ", \"is_cold\": %s", is_cold ? "true" : "false");
    fprintf(fp, ", \"should_inline\": %s}", should_inline ? "true" : "false");
    if (i < profile_count - 1) {
      fputc(',', fp);
    }
    fputc('\n', fp);
  }

  fprintf(fp, "  ],\n");
  fprintf(fp, "  \"branches\": [\n");
  for (int i = 0; i < branch_profile_count; i++) {
    BranchProfile *p = &branch_profiles[i];
    long total = p->taken_count + p->not_taken_count;
    double ratio = total > 0 ? (double)p->taken_count / (double)total : 0.0;
    bool predictable = ratio > 0.95 || ratio < 0.05;

    fprintf(fp, "    {\"location\": ");
    write_escaped_string(fp, p->location ? p->location : "");
    fprintf(fp, ", \"taken_count\": %ld", p->taken_count);
    fprintf(fp, ", \"not_taken_count\": %ld", p->not_taken_count);
    fprintf(fp, ", \"taken_ratio\": %.4f", ratio);
    fprintf(fp, ", \"is_predictable\": %s}", predictable ? "true" : "false");
    if (i < branch_profile_count - 1) {
      fputc(',', fp);
    }
    fputc('\n', fp);
  }

  fprintf(fp, "  ],\n");
  fprintf(fp, "  \"loops\": [\n");
  for (int i = 0; i < loop_profile_count; i++) {
    LoopProfile *p = &loop_profiles[i];
    double avg_iterations = p->invocation_count > 0
                                ? (double)p->total_iterations /
                                      (double)p->invocation_count
                                : 0.0;
    int suggested_unroll_factor = 1;
    bool should_unroll = false;

    if (avg_iterations >= 2.0 && avg_iterations <= 8.0) {
      should_unroll = true;
      suggested_unroll_factor = avg_iterations <= 4.0 ? (int)avg_iterations : 4;
    }

    fprintf(fp, "    {\"location\": ");
    write_escaped_string(fp, p->location ? p->location : "");
    fprintf(fp, ", \"invocation_count\": %ld", p->invocation_count);
    fprintf(fp, ", \"total_iterations\": %ld", p->total_iterations);
    fprintf(fp, ", \"avg_iterations\": %.2f", avg_iterations);
    fprintf(fp, ", \"should_unroll\": %s", should_unroll ? "true" : "false");
    fprintf(fp, ", \"suggested_unroll_factor\": %d}",
            suggested_unroll_factor);
    if (i < loop_profile_count - 1) {
      fputc(',', fp);
    }
    fputc('\n', fp);
  }

  fprintf(fp, "  ]\n");
  fprintf(fp, "}\n");
  fclose(fp);
}

void __qisc_profile_fn_enter(const char *name) {
  if (!profiling_active)
    return;

  FunctionProfile *p = find_or_create_profile(name);
  if (p) {
    p->call_count++;
    p->start_time = clock();
  }
}

void __qisc_profile_fn_exit(const char *name) {
  if (!profiling_active)
    return;

  FunctionProfile *p = find_or_create_profile(name);
  if (p && p->start_time != 0) {
    p->total_time += clock() - p->start_time;
    p->start_time = 0;
  }
}

void __qisc_profile_branch(const char *location, bool taken) {
  if (!profiling_active)
    return;

  BranchProfile *p = find_or_create_branch(location);
  if (!p)
    return;

  if (taken) {
    p->taken_count++;
  } else {
    p->not_taken_count++;
  }
}

void __qisc_profile_loop(const char *location, long iterations) {
  if (!profiling_active)
    return;

  LoopProfile *p = find_or_create_loop(location);
  if (!p)
    return;

  p->invocation_count++;
  p->total_iterations += iterations;
}

/* Called at program exit to dump profile data */
__attribute__((destructor)) static void __qisc_profile_dump(void) {
  if (profile_count == 0 && branch_profile_count == 0 && loop_profile_count == 0)
    return;

  fprintf(stderr, "\n[QISC Profile Summary]\n");
  fprintf(stderr, "%-20s %10s %12s\n", "Function", "Calls", "Time (ms)");
  fprintf(stderr, "--------------------------------------------\n");

  for (int i = 0; i < profile_count; i++) {
    double time_ms =
        (double)profiles[i].total_time / CLOCKS_PER_SEC * 1000.0;
    fprintf(stderr, "%-20s %10ld %12.3f\n", profiles[i].name,
            profiles[i].call_count, time_ms);
  }

  if (branch_profile_count > 0 || loop_profile_count > 0) {
    fprintf(stderr, "\nProfiled branches: %d | Profiled loops: %d\n",
            branch_profile_count, loop_profile_count);
  }
  fprintf(stderr, "\n");
  fflush(stderr);

  const char *profile_out = getenv("QISC_PROFILE_OUT");
  if (profile_out && *profile_out) {
    write_profile_json(profile_out);
  }
}
