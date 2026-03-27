/*
 * QISC Profile Engine — Implementation
 *
 * Collects and manages runtime profile data for profile-guided optimization.
 * This is the foundation of QISC's "Living IR" concept.
 */

#include "profile.h"
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define PROFILE_VERSION 1

/* ======== Initialization ======== */

void profile_init(QiscProfile *profile) {
  memset(profile, 0, sizeof(QiscProfile));
  profile->version = PROFILE_VERSION;
  profile->hot_threshold = 0.10;   /* 10% of total time = hot */
  profile->cold_threshold = 0.001; /* 0.1% of total time = cold */
  profile->timestamp = (uint64_t)time(NULL);
  profile->run_count = 1;
}

void profile_free(QiscProfile *profile) {
  if (profile->source_file) {
    free(profile->source_file);
    profile->source_file = NULL;
  }
  
  for (int i = 0; i < profile->function_count; i++) {
    if (profile->functions[i].name) {
      free(profile->functions[i].name);
    }
  }
  
  for (int i = 0; i < profile->branch_count; i++) {
    if (profile->branches[i].location) {
      free(profile->branches[i].location);
    }
  }
  
  for (int i = 0; i < profile->loop_count; i++) {
    if (profile->loops[i].location) {
      free(profile->loops[i].location);
    }
  }
  
  memset(profile, 0, sizeof(QiscProfile));
}

/* ======== Recording ======== */

static ProfileFunction *find_or_create_function(QiscProfile *profile,
                                                 const char *name) {
  /* Search existing */
  for (int i = 0; i < profile->function_count; i++) {
    if (profile->functions[i].name &&
        strcmp(profile->functions[i].name, name) == 0) {
      return &profile->functions[i];
    }
  }
  
  /* Create new */
  if (profile->function_count >= PROFILE_MAX_FUNCTIONS) {
    return NULL; /* Full */
  }
  
  ProfileFunction *fn = &profile->functions[profile->function_count++];
  fn->name = strdup(name);
  fn->call_count = 0;
  fn->total_cycles = 0;
  fn->avg_time_us = 0.0;
  fn->is_hot = false;
  fn->is_cold = false;
  fn->should_inline = false;
  return fn;
}

void profile_record_call(QiscProfile *profile, const char *func_name,
                         uint64_t cycles) {
  ProfileFunction *fn = find_or_create_function(profile, func_name);
  if (!fn) return;
  
  fn->call_count++;
  fn->total_cycles += cycles;
}

static ProfileBranch *find_or_create_branch(QiscProfile *profile,
                                             const char *location) {
  /* Search existing */
  for (int i = 0; i < profile->branch_count; i++) {
    if (profile->branches[i].location &&
        strcmp(profile->branches[i].location, location) == 0) {
      return &profile->branches[i];
    }
  }
  
  /* Create new */
  if (profile->branch_count >= PROFILE_MAX_BRANCHES) {
    return NULL;
  }
  
  ProfileBranch *br = &profile->branches[profile->branch_count++];
  br->location = strdup(location);
  br->taken_count = 0;
  br->not_taken_count = 0;
  br->taken_ratio = 0.0;
  br->is_predictable = false;
  return br;
}

void profile_record_branch(QiscProfile *profile, const char *location,
                           bool taken) {
  ProfileBranch *br = find_or_create_branch(profile, location);
  if (!br) return;
  
  if (taken) {
    br->taken_count++;
  } else {
    br->not_taken_count++;
  }
}

static ProfileLoop *find_or_create_loop(QiscProfile *profile,
                                         const char *location) {
  /* Search existing */
  for (int i = 0; i < profile->loop_count; i++) {
    if (profile->loops[i].location &&
        strcmp(profile->loops[i].location, location) == 0) {
      return &profile->loops[i];
    }
  }
  
  /* Create new */
  if (profile->loop_count >= PROFILE_MAX_LOOPS) {
    return NULL;
  }
  
  ProfileLoop *lp = &profile->loops[profile->loop_count++];
  lp->location = strdup(location);
  lp->invocation_count = 0;
  lp->total_iterations = 0;
  lp->avg_iterations = 0.0;
  lp->typical_iteration_count = 0;
  lp->should_unroll = false;
  lp->suggested_unroll_factor = 1;
  return lp;
}

void profile_record_loop(QiscProfile *profile, const char *location,
                         int iterations) {
  ProfileLoop *lp = find_or_create_loop(profile, location);
  if (!lp) return;
  
  lp->invocation_count++;
  lp->total_iterations += iterations;
}

/* ======== Finalization ======== */

void profile_finalize(QiscProfile *profile) {
  /* Calculate total cycles */
  uint64_t total_cycles = 0;
  for (int i = 0; i < profile->function_count; i++) {
    total_cycles += profile->functions[i].total_cycles;
  }
  
  /* Finalize function metrics */
  for (int i = 0; i < profile->function_count; i++) {
    ProfileFunction *fn = &profile->functions[i];
    
    /* Average time (assuming ~3GHz CPU for rough estimate) */
    if (fn->call_count > 0) {
      fn->avg_time_us = (double)fn->total_cycles / fn->call_count / 3000.0;
    }
    
    /* Mark hot/cold based on % of total */
    double ratio = (total_cycles > 0)
                       ? (double)fn->total_cycles / total_cycles
                       : 0.0;
    fn->is_hot = (ratio >= profile->hot_threshold);
    fn->is_cold = (ratio <= profile->cold_threshold);
    
    /* Suggest inlining for small, frequently called functions */
    fn->should_inline = (fn->call_count > 100 && fn->avg_time_us < 1.0);
  }
  
  /* Finalize branch metrics */
  for (int i = 0; i < profile->branch_count; i++) {
    ProfileBranch *br = &profile->branches[i];
    uint64_t total = br->taken_count + br->not_taken_count;
    
    if (total > 0) {
      br->taken_ratio = (double)br->taken_count / total;
      /* Predictable if >95% one way */
      br->is_predictable = (br->taken_ratio > 0.95 || br->taken_ratio < 0.05);
    }
  }
  
  /* Finalize loop metrics */
  for (int i = 0; i < profile->loop_count; i++) {
    ProfileLoop *lp = &profile->loops[i];
    
    if (lp->invocation_count > 0) {
      lp->avg_iterations = (double)lp->total_iterations / lp->invocation_count;
      lp->typical_iteration_count = (int)lp->avg_iterations;
      
      /* Suggest unrolling for small, predictable loops */
      if (lp->avg_iterations <= 8 && lp->avg_iterations >= 2) {
        lp->should_unroll = true;
        if (lp->avg_iterations <= 4) {
          lp->suggested_unroll_factor = (int)lp->avg_iterations;
        } else {
          lp->suggested_unroll_factor = 4;
        }
      }
    }
  }
  
  /* Estimate total execution time */
  profile->total_execution_time_us = total_cycles / 3000; /* ~3GHz estimate */
}

/* ======== Persistence (JSON format) ======== */

static void write_escaped_string(FILE *fp, const char *str) {
  fputc('"', fp);
  for (const char *p = str; *p; p++) {
    switch (*p) {
      case '"': fputs("\\\"", fp); break;
      case '\\': fputs("\\\\", fp); break;
      case '\n': fputs("\\n", fp); break;
      case '\t': fputs("\\t", fp); break;
      default: fputc(*p, fp); break;
    }
  }
  fputc('"', fp);
}

int profile_save(QiscProfile *profile, const char *path) {
  FILE *fp = fopen(path, "w");
  if (!fp) {
    fprintf(stderr, "[profile] Error: Cannot open %s for writing\n", path);
    return 1;
  }
  
  fprintf(fp, "{\n");
  fprintf(fp, "  \"version\": %u,\n", profile->version);
  fprintf(fp, "  \"timestamp\": %llu,\n", (unsigned long long)profile->timestamp);
  fprintf(fp, "  \"run_count\": %u,\n", profile->run_count);
  fprintf(fp, "  \"ir_hash\": %llu,\n", (unsigned long long)profile->ir_hash);
  fprintf(fp, "  \"has_converged\": %s,\n", profile->has_converged ? "true" : "false");
  
  /* Functions */
  fprintf(fp, "  \"functions\": [\n");
  for (int i = 0; i < profile->function_count; i++) {
    ProfileFunction *fn = &profile->functions[i];
    fprintf(fp, "    {\"name\": ");
    write_escaped_string(fp, fn->name ? fn->name : "");
    fprintf(fp, ", \"call_count\": %llu", (unsigned long long)fn->call_count);
    fprintf(fp, ", \"total_cycles\": %llu", (unsigned long long)fn->total_cycles);
    fprintf(fp, ", \"is_hot\": %s", fn->is_hot ? "true" : "false");
    fprintf(fp, ", \"is_cold\": %s", fn->is_cold ? "true" : "false");
    fprintf(fp, ", \"should_inline\": %s}", fn->should_inline ? "true" : "false");
    if (i < profile->function_count - 1) fprintf(fp, ",");
    fprintf(fp, "\n");
  }
  fprintf(fp, "  ],\n");
  
  /* Branches */
  fprintf(fp, "  \"branches\": [\n");
  for (int i = 0; i < profile->branch_count; i++) {
    ProfileBranch *br = &profile->branches[i];
    fprintf(fp, "    {\"location\": ");
    write_escaped_string(fp, br->location ? br->location : "");
    fprintf(fp, ", \"taken_count\": %llu", (unsigned long long)br->taken_count);
    fprintf(fp, ", \"not_taken_count\": %llu", (unsigned long long)br->not_taken_count);
    fprintf(fp, ", \"taken_ratio\": %.4f", br->taken_ratio);
    fprintf(fp, ", \"is_predictable\": %s}", br->is_predictable ? "true" : "false");
    if (i < profile->branch_count - 1) fprintf(fp, ",");
    fprintf(fp, "\n");
  }
  fprintf(fp, "  ],\n");
  
  /* Loops */
  fprintf(fp, "  \"loops\": [\n");
  for (int i = 0; i < profile->loop_count; i++) {
    ProfileLoop *lp = &profile->loops[i];
    fprintf(fp, "    {\"location\": ");
    write_escaped_string(fp, lp->location ? lp->location : "");
    fprintf(fp, ", \"invocation_count\": %llu", (unsigned long long)lp->invocation_count);
    fprintf(fp, ", \"total_iterations\": %llu", (unsigned long long)lp->total_iterations);
    fprintf(fp, ", \"avg_iterations\": %.2f", lp->avg_iterations);
    fprintf(fp, ", \"should_unroll\": %s", lp->should_unroll ? "true" : "false");
    fprintf(fp, ", \"suggested_unroll_factor\": %d}", lp->suggested_unroll_factor);
    if (i < profile->loop_count - 1) fprintf(fp, ",");
    fprintf(fp, "\n");
  }
  fprintf(fp, "  ]\n");
  
  fprintf(fp, "}\n");
  fclose(fp);
  return 0;
}

/* Simple JSON parsing helpers */
static char *skip_ws(char *p) {
  while (*p && isspace(*p)) p++;
  return p;
}

static char *parse_string(char *p, char **out) {
  if (*p != '"') return p;
  p++;
  char *start = p;
  while (*p && *p != '"') {
    if (*p == '\\') p++; /* Skip escaped char */
    if (*p) p++;
  }
  int len = p - start;
  *out = malloc(len + 1);
  memcpy(*out, start, len);
  (*out)[len] = '\0';
  if (*p == '"') p++;
  return p;
}

static char *parse_uint64(char *p, uint64_t *out) {
  *out = 0;
  while (*p && isdigit(*p)) {
    *out = *out * 10 + (*p - '0');
    p++;
  }
  return p;
}

static char *parse_bool(char *p, bool *out) {
  if (strncmp(p, "true", 4) == 0) {
    *out = true;
    return p + 4;
  } else if (strncmp(p, "false", 5) == 0) {
    *out = false;
    return p + 5;
  }
  return p;
}

static char *parse_double(char *p, double *out) {
  char *end;
  *out = strtod(p, &end);
  return end;
}

int profile_load(QiscProfile *profile, const char *path) {
  FILE *fp = fopen(path, "r");
  if (!fp) {
    return 1; /* File not found is not an error - just no profile */
  }
  
  /* Read entire file */
  fseek(fp, 0, SEEK_END);
  long size = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  
  char *content = malloc(size + 1);
  if (!content) {
    fclose(fp);
    return 1;
  }
  
  fread(content, 1, size, fp);
  content[size] = '\0';
  fclose(fp);
  
  profile_init(profile);
  
  char *p = content;
  
  /* Very simple JSON parsing - expects well-formed output from profile_save */
  while (*p) {
    p = skip_ws(p);
    
    /* Look for "key": value patterns */
    if (strncmp(p, "\"version\":", 10) == 0) {
      p = skip_ws(p + 10);
      uint64_t v;
      p = parse_uint64(p, &v);
      profile->version = (uint32_t)v;
    } else if (strncmp(p, "\"timestamp\":", 12) == 0) {
      p = skip_ws(p + 12);
      p = parse_uint64(p, &profile->timestamp);
    } else if (strncmp(p, "\"run_count\":", 12) == 0) {
      p = skip_ws(p + 12);
      uint64_t v;
      p = parse_uint64(p, &v);
      profile->run_count = (uint32_t)v;
    } else if (strncmp(p, "\"ir_hash\":", 10) == 0) {
      p = skip_ws(p + 10);
      p = parse_uint64(p, &profile->ir_hash);
    } else if (strncmp(p, "\"has_converged\":", 16) == 0) {
      p = skip_ws(p + 16);
      p = parse_bool(p, &profile->has_converged);
    } else if (strncmp(p, "\"functions\":", 12) == 0) {
      /* Parse function array */
      p = skip_ws(p + 12);
      if (*p == '[') p++;
      
      while (*p && *p != ']') {
        p = skip_ws(p);
        if (*p == '{') {
          p++;
          ProfileFunction fn = {0};
          
          while (*p && *p != '}') {
            p = skip_ws(p);
            if (strncmp(p, "\"name\":", 7) == 0) {
              p = skip_ws(p + 7);
              p = parse_string(p, &fn.name);
            } else if (strncmp(p, "\"call_count\":", 13) == 0) {
              p = skip_ws(p + 13);
              p = parse_uint64(p, &fn.call_count);
            } else if (strncmp(p, "\"total_cycles\":", 15) == 0) {
              p = skip_ws(p + 15);
              p = parse_uint64(p, &fn.total_cycles);
            } else if (strncmp(p, "\"is_hot\":", 9) == 0) {
              p = skip_ws(p + 9);
              p = parse_bool(p, &fn.is_hot);
            } else if (strncmp(p, "\"is_cold\":", 10) == 0) {
              p = skip_ws(p + 10);
              p = parse_bool(p, &fn.is_cold);
            } else if (strncmp(p, "\"should_inline\":", 16) == 0) {
              p = skip_ws(p + 16);
              p = parse_bool(p, &fn.should_inline);
            }
            if (*p == ',') p++;
            p = skip_ws(p);
          }
          
          if (profile->function_count < PROFILE_MAX_FUNCTIONS) {
            profile->functions[profile->function_count++] = fn;
          }
          
          if (*p == '}') p++;
        }
        if (*p == ',') p++;
        p = skip_ws(p);
      }
      if (*p == ']') p++;
    } else if (strncmp(p, "\"branches\":", 11) == 0) {
      /* Parse branch array */
      p = skip_ws(p + 11);
      if (*p == '[') p++;
      
      while (*p && *p != ']') {
        p = skip_ws(p);
        if (*p == '{') {
          p++;
          ProfileBranch br = {0};
          
          while (*p && *p != '}') {
            p = skip_ws(p);
            if (strncmp(p, "\"location\":", 11) == 0) {
              p = skip_ws(p + 11);
              p = parse_string(p, &br.location);
            } else if (strncmp(p, "\"taken_count\":", 14) == 0) {
              p = skip_ws(p + 14);
              p = parse_uint64(p, &br.taken_count);
            } else if (strncmp(p, "\"not_taken_count\":", 18) == 0) {
              p = skip_ws(p + 18);
              p = parse_uint64(p, &br.not_taken_count);
            } else if (strncmp(p, "\"taken_ratio\":", 14) == 0) {
              p = skip_ws(p + 14);
              p = parse_double(p, &br.taken_ratio);
            } else if (strncmp(p, "\"is_predictable\":", 17) == 0) {
              p = skip_ws(p + 17);
              p = parse_bool(p, &br.is_predictable);
            }
            if (*p == ',') p++;
            p = skip_ws(p);
          }
          
          if (profile->branch_count < PROFILE_MAX_BRANCHES) {
            profile->branches[profile->branch_count++] = br;
          }
          
          if (*p == '}') p++;
        }
        if (*p == ',') p++;
        p = skip_ws(p);
      }
      if (*p == ']') p++;
    } else if (strncmp(p, "\"loops\":", 8) == 0) {
      /* Parse loop array */
      p = skip_ws(p + 8);
      if (*p == '[') p++;
      
      while (*p && *p != ']') {
        p = skip_ws(p);
        if (*p == '{') {
          p++;
          ProfileLoop lp = {0};
          
          while (*p && *p != '}') {
            p = skip_ws(p);
            if (strncmp(p, "\"location\":", 11) == 0) {
              p = skip_ws(p + 11);
              p = parse_string(p, &lp.location);
            } else if (strncmp(p, "\"invocation_count\":", 19) == 0) {
              p = skip_ws(p + 19);
              p = parse_uint64(p, &lp.invocation_count);
            } else if (strncmp(p, "\"total_iterations\":", 19) == 0) {
              p = skip_ws(p + 19);
              p = parse_uint64(p, &lp.total_iterations);
            } else if (strncmp(p, "\"avg_iterations\":", 17) == 0) {
              p = skip_ws(p + 17);
              p = parse_double(p, &lp.avg_iterations);
            } else if (strncmp(p, "\"should_unroll\":", 16) == 0) {
              p = skip_ws(p + 16);
              p = parse_bool(p, &lp.should_unroll);
            } else if (strncmp(p, "\"suggested_unroll_factor\":", 26) == 0) {
              p = skip_ws(p + 26);
              uint64_t v;
              p = parse_uint64(p, &v);
              lp.suggested_unroll_factor = (int)v;
            }
            if (*p == ',') p++;
            p = skip_ws(p);
          }
          
          if (profile->loop_count < PROFILE_MAX_LOOPS) {
            profile->loops[profile->loop_count++] = lp;
          }
          
          if (*p == '}') p++;
        }
        if (*p == ',') p++;
        p = skip_ws(p);
      }
      if (*p == ']') p++;
    } else {
      p++;
    }
  }
  
  free(content);
  return 0;
}

/* ======== Merging ======== */

void profile_merge(QiscProfile *target, const QiscProfile *source) {
  target->run_count += source->run_count;
  
  /* Merge functions */
  for (int i = 0; i < source->function_count; i++) {
    const ProfileFunction *sfn = &source->functions[i];
    ProfileFunction *tfn = find_or_create_function(target, sfn->name);
    if (tfn) {
      tfn->call_count += sfn->call_count;
      tfn->total_cycles += sfn->total_cycles;
    }
  }
  
  /* Merge branches */
  for (int i = 0; i < source->branch_count; i++) {
    const ProfileBranch *sbr = &source->branches[i];
    ProfileBranch *tbr = find_or_create_branch(target, sbr->location);
    if (tbr) {
      tbr->taken_count += sbr->taken_count;
      tbr->not_taken_count += sbr->not_taken_count;
    }
  }
  
  /* Merge loops */
  for (int i = 0; i < source->loop_count; i++) {
    const ProfileLoop *slp = &source->loops[i];
    ProfileLoop *tlp = find_or_create_loop(target, slp->location);
    if (tlp) {
      tlp->invocation_count += slp->invocation_count;
      tlp->total_iterations += slp->total_iterations;
    }
  }
  
  /* Re-finalize after merge */
  profile_finalize(target);
}

/* ======== Queries ======== */

ProfileFunction *profile_get_function(QiscProfile *profile, const char *name) {
  for (int i = 0; i < profile->function_count; i++) {
    if (profile->functions[i].name &&
        strcmp(profile->functions[i].name, name) == 0) {
      return &profile->functions[i];
    }
  }
  return NULL;
}

ProfileBranch *profile_get_branch(QiscProfile *profile, const char *location) {
  for (int i = 0; i < profile->branch_count; i++) {
    if (profile->branches[i].location &&
        strcmp(profile->branches[i].location, location) == 0) {
      return &profile->branches[i];
    }
  }
  return NULL;
}

ProfileLoop *profile_get_loop(QiscProfile *profile, const char *location) {
  for (int i = 0; i < profile->loop_count; i++) {
    if (profile->loops[i].location &&
        strcmp(profile->loops[i].location, location) == 0) {
      return &profile->loops[i];
    }
  }
  return NULL;
}

bool profile_is_hot(QiscProfile *profile, const char *func_name) {
  ProfileFunction *fn = profile_get_function(profile, func_name);
  return fn ? fn->is_hot : false;
}

bool profile_is_branch_predictable(QiscProfile *profile, const char *location) {
  ProfileBranch *br = profile_get_branch(profile, location);
  return br ? br->is_predictable : false;
}

int profile_get_opt_level(QiscProfile *profile, const char *func_name) {
  ProfileFunction *fn = profile_get_function(profile, func_name);
  if (!fn) return 2; /* Default: normal optimization */
  
  if (fn->is_hot) return 3;  /* Hot: aggressive optimization */
  if (fn->is_cold) return 1; /* Cold: minimal optimization */
  return 2;
}

/* ======== Convergence ======== */

void profile_set_ir_hash(QiscProfile *profile, uint64_t hash) {
  if (profile->ir_hash != 0 && profile->ir_hash == hash) {
    profile->has_converged = true;
  }
  profile->ir_hash = hash;
}

bool profile_check_convergence(QiscProfile *profile, uint64_t current_hash) {
  return profile->ir_hash != 0 && profile->ir_hash == current_hash;
}

/* ======== Printing ======== */

void profile_print_summary(QiscProfile *profile) {
  printf("\n");
  printf("┌─────────────────────────────────────────────────┐\n");
  printf("│            QISC Profile Summary                 │\n");
  printf("├─────────────────────────────────────────────────┤\n");
  printf("│ Runs:      %-37u│\n", profile->run_count);
  printf("│ Functions: %-37d│\n", profile->function_count);
  printf("│ Branches:  %-37d│\n", profile->branch_count);
  printf("│ Loops:     %-37d│\n", profile->loop_count);
  printf("│ Converged: %-37s│\n", profile->has_converged ? "YES ✓" : "No");
  printf("└─────────────────────────────────────────────────┘\n");
  
  /* Hot functions */
  int hot_count = 0;
  for (int i = 0; i < profile->function_count; i++) {
    if (profile->functions[i].is_hot) hot_count++;
  }
  
  if (hot_count > 0) {
    printf("\n🔥 Hot Functions:\n");
    for (int i = 0; i < profile->function_count; i++) {
      ProfileFunction *fn = &profile->functions[i];
      if (fn->is_hot) {
        printf("   %-30s %llu calls, %.2f µs avg\n",
               fn->name, (unsigned long long)fn->call_count, fn->avg_time_us);
      }
    }
  }
  
  /* Predictable branches */
  int pred_count = 0;
  for (int i = 0; i < profile->branch_count; i++) {
    if (profile->branches[i].is_predictable) pred_count++;
  }
  
  if (pred_count > 0 && pred_count <= 5) {
    printf("\n🎯 Predictable Branches: %d\n", pred_count);
  }
  
  /* Unrollable loops */
  int unroll_count = 0;
  for (int i = 0; i < profile->loop_count; i++) {
    if (profile->loops[i].should_unroll) unroll_count++;
  }
  
  if (unroll_count > 0) {
    printf("\n🔄 Unrollable Loops:\n");
    for (int i = 0; i < profile->loop_count; i++) {
      ProfileLoop *lp = &profile->loops[i];
      if (lp->should_unroll) {
        printf("   %s (factor: %d)\n", lp->location, lp->suggested_unroll_factor);
      }
    }
  }
  
  printf("\n");
}

void profile_dump(QiscProfile *profile) {
  printf("=== QISC Profile Dump ===\n");
  printf("Version: %u\n", profile->version);
  printf("Timestamp: %llu\n", (unsigned long long)profile->timestamp);
  printf("Run count: %u\n", profile->run_count);
  printf("IR hash: %llx\n", (unsigned long long)profile->ir_hash);
  printf("Converged: %s\n", profile->has_converged ? "true" : "false");
  
  printf("\nFunctions (%d):\n", profile->function_count);
  for (int i = 0; i < profile->function_count; i++) {
    ProfileFunction *fn = &profile->functions[i];
    printf("  [%d] %s: calls=%llu, cycles=%llu, hot=%d, cold=%d, inline=%d\n",
           i, fn->name ? fn->name : "(null)",
           (unsigned long long)fn->call_count,
           (unsigned long long)fn->total_cycles,
           fn->is_hot, fn->is_cold, fn->should_inline);
  }
  
  printf("\nBranches (%d):\n", profile->branch_count);
  for (int i = 0; i < profile->branch_count; i++) {
    ProfileBranch *br = &profile->branches[i];
    printf("  [%d] %s: taken=%llu, not_taken=%llu, ratio=%.2f, pred=%d\n",
           i, br->location ? br->location : "(null)",
           (unsigned long long)br->taken_count,
           (unsigned long long)br->not_taken_count,
           br->taken_ratio, br->is_predictable);
  }
  
  printf("\nLoops (%d):\n", profile->loop_count);
  for (int i = 0; i < profile->loop_count; i++) {
    ProfileLoop *lp = &profile->loops[i];
    printf("  [%d] %s: invocations=%llu, iterations=%llu, avg=%.2f, unroll=%d (factor=%d)\n",
           i, lp->location ? lp->location : "(null)",
           (unsigned long long)lp->invocation_count,
           (unsigned long long)lp->total_iterations,
           lp->avg_iterations, lp->should_unroll, lp->suggested_unroll_factor);
  }
}
