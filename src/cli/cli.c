/*
 * QISC CLI Implementation
 */

#include "cli.h"
#include "../achievements/achievements.h"
#include "../codegen/codegen.h"
#include "../interpreter/interpreter.h"
#include "../ir/ir_hash.h"
#include "../ir/living_ir.h"
#include "../lexer/lexer.h"
#include "../optimization/fusion.h"
#include "../optimization/memoize.h"
#include "../optimization/tail_call.h"
#include "../parser/parser.h"
#include "../personality/personality.h"
#include "../personality/tiny_llm.h"
#include "../profile/profile.h"
#include "../typechecker/typechecker.h"
#include "../utils/utils.h"
#include <ctype.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <io.h>
#include <process.h>
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#else
#include <unistd.h>
#include <sys/wait.h>
#endif

/* Forward declarations */
static QiscResult qisc_compile_file_with_hash(const char *path, QiscOptions *options, uint64_t *ir_hash);
static AstNode *qisc_parse_program_with_imports(const char *path, char **error_path,
                                                char *error_message,
                                                size_t error_message_size);
static bool qisc_profile_has_samples(const QiscProfile *profile);
static void qisc_binary_path_from_source(const char *path, char *out, size_t out_size);
static void qisc_profile_paths_from_source(const char *path, char *profile_path,
                                           size_t profile_path_size,
                                           char *runtime_path,
                                           size_t runtime_path_size);
static void qisc_llvm_profile_paths_from_source(const char *path,
                                                char *profraw_path,
                                                size_t profraw_path_size,
                                                char *profdata_path,
                                                size_t profdata_path_size,
                                                char *ir_path,
                                                size_t ir_path_size);
static void qisc_tiny_llm_path_from_source(const char *path, char *out,
                                           size_t out_size);
static int qisc_living_ir_total_mutations(const LivingIRMetrics *metrics);
static void qisc_tiny_llm_report_living_ir(const char *path,
                                           QiscOptions *options,
                                           const LivingIRMetrics *metrics,
                                           const QiscProfile *profile,
                                           double compile_time_ms);
static int qisc_execute_binary(const char *bin_path, const char *profile_out,
                               const char *llvm_profile_out, bool quiet);
static int qisc_merge_runtime_profile(QiscProfile *profile,
                                      const char *runtime_path);
static int qisc_merge_llvm_profile(const char *profraw_path,
                                   const char *profdata_path, bool quiet);
static bool qisc_should_use_clang_ir_backend(const QiscOptions *options);
static int qisc_emit_and_link_binary(Codegen *cg, const char *path,
                                     QiscOptions *options, char *bin_path,
                                     size_t bin_path_size, bool quiet,
                                     bool *link_succeeded);
static int qisc_run_repl(QiscOptions *options);
static int qisc_run_notebook(const char *path, QiscOptions *options);
static char *qisc_repl_trim_copy(const char *text);

typedef struct {
  char **paths;
  int count;
  int capacity;
} ImportSet;

typedef struct {
  AstNode **items;
  int count;
  int capacity;
} ReplAstStore;

typedef struct {
  char *kind;
  char *source;
  int start_line;
} NotebookCell;

typedef struct {
  NotebookCell *items;
  int count;
  int capacity;
} NotebookDoc;

/* Global achievement registry */
static AchievementRegistry g_achievements;
static bool g_achievements_initialized = false;

static void ensure_achievements_initialized(void) {
  if (!g_achievements_initialized) {
    achievements_init(&g_achievements);
    g_achievements_initialized = true;
  }
}

static bool qisc_profile_has_samples(const QiscProfile *profile) {
  return profile &&
         (profile->function_count > 0 || profile->branch_count > 0 ||
          profile->loop_count > 0);
}

static void qisc_binary_path_from_source(const char *path, char *out,
                                         size_t out_size) {
  if (!out || out_size == 0)
    return;

  strncpy(out, path, out_size - 1);
  out[out_size - 1] = '\0';

  char *dot = strrchr(out, '.');
  if (dot)
    *dot = '\0';
}

static void qisc_profile_paths_from_source(const char *path, char *profile_path,
                                           size_t profile_path_size,
                                           char *runtime_path,
                                           size_t runtime_path_size) {
  if (profile_path && profile_path_size > 0) {
    snprintf(profile_path, profile_path_size, "%s.profile", path);
  }
  if (runtime_path && runtime_path_size > 0) {
    snprintf(runtime_path, runtime_path_size, "%s.profile.runtime", path);
  }
}

static void qisc_llvm_profile_paths_from_source(const char *path,
                                                char *profraw_path,
                                                size_t profraw_path_size,
                                                char *profdata_path,
                                                size_t profdata_path_size,
                                                char *ir_path,
                                                size_t ir_path_size) {
  if (profraw_path && profraw_path_size > 0) {
    snprintf(profraw_path, profraw_path_size, "%s.llvm.profraw", path);
  }
  if (profdata_path && profdata_path_size > 0) {
    snprintf(profdata_path, profdata_path_size, "%s.llvm.profdata", path);
  }
  if (ir_path && ir_path_size > 0) {
    snprintf(ir_path, ir_path_size, "%s.opt.ll", path);
  }
}

static void qisc_tiny_llm_path_from_source(const char *path, char *out,
                                           size_t out_size) {
  if (!out || out_size == 0)
    return;

  if (!path) {
    out[0] = '\0';
    return;
  }

  snprintf(out, out_size, "%s.tinyllm.json", path);
}

static int qisc_living_ir_total_mutations(const LivingIRMetrics *metrics) {
  if (!metrics)
    return 0;

  return metrics->functions_inlined + metrics->cold_blocks_outlined +
         metrics->hot_functions_specialized +
         metrics->cold_functions_specialized +
         metrics->argument_specializations +
         metrics->loops_unrolled + metrics->loops_prefetched +
         metrics->branch_weights_applied + metrics->blocks_reordered;
}

static void qisc_living_ir_guidance(const LivingIRMetrics *metrics,
                                    const QiscProfile *profile, char *out,
                                    size_t out_size) {
  const char *phase;

  if (!out || out_size == 0) {
    return;
  }
  out[0] = '\0';

  if (!metrics) {
    return;
  }

  if (!profile) {
    phase = "no-profile";
  } else if (profile->has_converged) {
    phase = "stable";
  } else if (profile->run_count >= 2) {
    phase = "warming";
  } else {
    phase = "first-run";
  }

  if (metrics->hot_functions_specialized > 0 &&
      metrics->argument_specializations > 0 &&
      metrics->cold_blocks_outlined == 0) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: hot clones are now specializing stable "
             "constant arguments; the next leverage point is dispatcher-level "
             "specialization for richer runtime value classes.",
             phase);
  } else if (metrics->hot_functions_specialized > 0 &&
      (metrics->cold_functions_specialized > 0 ||
       metrics->cold_blocks_outlined > 0) &&
      metrics->loops_restructured > 0 &&
      metrics->blocks_reordered > 0) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: the core hot-path, cold-path, loop, and "
             "layout mutation layers are all active; further changes are now "
             "workload-specific refinement rather than missing core behavior.",
             phase);
  } else if (metrics->hot_functions_specialized >= 2 &&
      metrics->cold_blocks_outlined == 0) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: multi-caller hot specialization is "
             "active; the next step is real dispatcher-level cloning and "
             "argument-pattern specialization.",
             phase);
  } else if (metrics->cold_blocks_outlined >= 2 &&
      metrics->cold_functions_specialized > 0) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: cold descendants are being outlined and "
             "their linear tails are now folded back into hot predecessors; the "
             "next step is stricter region cloning and multi-caller hot dispatch.",
             phase);
  } else if (metrics->cold_blocks_outlined >= 2 &&
             metrics->cold_blocks_found > metrics->cold_blocks_outlined) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: multi-block cold regions are being "
             "decomposed into safe cold descendants; the next step is strict "
             "region cloning for single-entry/single-exit chains.",
             phase);
  } else if (metrics->cold_blocks_outlined >= 2) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: multiple cold descendants were extracted; "
             "the next step is merging them into stricter region-level clones.",
             phase);
  } else if (metrics->mutations_rejected > 0 && profile && !profile->has_converged) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: more profile runs should unlock stronger "
             "mutations; %d candidates were rejected on confidence.",
             phase, metrics->mutations_rejected);
  } else if (metrics->cold_blocks_outlined > 0 &&
             metrics->blocks_reordered > 0) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: cold extraction and block layout are both "
             "active; the next meaningful step is multi-block cold regions and "
             "hot-path cloning.",
             phase);
  } else if (metrics->cold_blocks_outlined > 0) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: cold outlining is active; block layout and "
             "region-level extraction are the next leverage points.",
             phase);
  } else if (metrics->branch_weights_applied > 0) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: profile steering is active; cold outlining "
             "or region splitting is the next useful mutation.",
             phase);
  } else if (metrics->loops_unrolled > 0 || metrics->loops_prefetched > 0) {
    snprintf(out, out_size,
             "Living IR guidance [%s]: loop shaping is active; more branch and "
             "cold-path evidence would expand specialization.",
             phase);
  } else {
    snprintf(out, out_size,
             "Living IR guidance [%s]: gather more runtime evidence before "
             "aggressive specialization.", phase);
  }
}

static void qisc_tiny_llm_report_living_ir(const char *path,
                                           QiscOptions *options,
                                           const LivingIRMetrics *metrics,
                                           const QiscProfile *profile,
                                           double compile_time_ms) {
  TinyLLM *llm;
  char llm_path[PATH_MAX];
  char context[1024];
  char summary[256];
  char guidance[256];
  char *comment;
  char *structured_comment;
  TinyLLMCodePattern patterns[3];
  int pattern_count = 0;
  int total_mutations;
  const char *phase;

  if (!path || !options || !metrics)
    return;
  if (options->personality == QISC_PERSONALITY_OFF ||
      options->personality == QISC_PERSONALITY_MINIMAL ||
      options->converge) {
    return;
  }

  qisc_tiny_llm_path_from_source(path, llm_path, sizeof(llm_path));
  llm = tiny_llm_load(llm_path);
  if (!llm) {
    llm = tiny_llm_create(3);
  }
  if (!llm)
    return;

  total_mutations = qisc_living_ir_total_mutations(metrics);
  if (!profile) {
    phase = "no-profile";
  } else if (profile->has_converged) {
    phase = "stable";
  } else if (profile->run_count >= 2) {
    phase = "warming";
  } else {
    phase = "first-run";
  }

  snprintf(
      context, sizeof(context),
      "Living IR analyzed %d functions and %d loops. Inlined %d hot paths. "
      "Outlined %d cold blocks. Specialized %d constant arguments. Unrolled "
      "%d loops. Added %d prefetch hints. Applied %d branch weights. "
      "Estimated speedup %.2fx. Profile samples: %d functions, %d branches, "
      "%d loops.",
      metrics->functions_analyzed, metrics->loops_analyzed,
      metrics->functions_inlined, metrics->cold_blocks_outlined,
      metrics->argument_specializations,
      metrics->loops_unrolled, metrics->loops_prefetched,
      metrics->branch_weights_applied, metrics->estimated_speedup,
      profile ? profile->function_count : 0, profile ? profile->branch_count : 0,
      profile ? profile->loop_count : 0);

  if (total_mutations > 0) {
    patterns[pattern_count++] = (TinyLLMCodePattern){
        .type = QISC_PATTERN_BRILLIANT,
        .context = context,
        .severity = total_mutations > 3 ? 8 : 5,
        .line_number = 0,
    };
  } else {
    patterns[pattern_count++] = (TinyLLMCodePattern){
        .type = QISC_PATTERN_PREMATURE_OPT,
        .context = context,
        .severity = 4,
        .line_number = 0,
    };
  }

  if (metrics->branch_weights_applied > 0) {
    patterns[pattern_count++] = (TinyLLMCodePattern){
        .type = QISC_PATTERN_BRILLIANT,
        .context = "Predictable branches were converted into profile-guided IR metadata.",
        .severity = 6,
        .line_number = 0,
    };
  }

  if (metrics->loops_unrolled > 0 || metrics->loops_prefetched > 0) {
    patterns[pattern_count++] = (TinyLLMCodePattern){
        .type = QISC_PATTERN_NESTED_LOOPS,
        .context = "Loop profile data drove unrolling and loop-shaping decisions.",
        .severity = 3,
        .line_number = 0,
    };
  }

  tiny_llm_train_on_patterns(llm, patterns, pattern_count);

  switch (options->personality) {
  case QISC_PERSONALITY_FRIENDLY:
    comment = tiny_llm_encourage(llm, total_mutations);
    break;
  case QISC_PERSONALITY_SNARKY:
    comment = total_mutations > 0 ? tiny_llm_encourage(llm, total_mutations)
                                  : tiny_llm_roast(llm, context);
    break;
  case QISC_PERSONALITY_SAGE:
  case QISC_PERSONALITY_CRYPTIC:
    comment = tiny_llm_existential(llm);
    break;
  case QISC_PERSONALITY_OFF:
  case QISC_PERSONALITY_MINIMAL:
    comment = NULL;
    break;
  }

  structured_comment = tiny_llm_summarize_optimization(
      llm, phase, total_mutations, metrics->hot_functions_specialized,
      metrics->cold_functions_specialized, metrics->cold_blocks_outlined,
      metrics->loops_restructured, metrics->blocks_reordered,
      metrics->branch_weights_applied, metrics->estimated_speedup);

  snprintf(summary, sizeof(summary), "%d mutations, %.2fx estimate",
           total_mutations, metrics->estimated_speedup);
  qisc_living_ir_guidance(metrics, profile, guidance, sizeof(guidance));
  if (guidance[0]) {
    qisc_personality_print(options->personality, "%s\n", guidance);
  }
  if (structured_comment && structured_comment[0]) {
    qisc_personality_print(options->personality, "Tiny LLM analysis [%s]: %s\n",
                           summary, structured_comment);
  }
  if (comment && comment[0]) {
    qisc_personality_print(options->personality, "Tiny LLM aside [%s]: %s\n",
                           summary, comment);
  }

  tiny_llm_learn_outcome(llm, path, true, compile_time_ms, total_mutations);
  tiny_llm_save(llm, llm_path);
  free(structured_comment);
  free(comment);
  tiny_llm_destroy(llm);
}

static int qisc_execute_binary(const char *bin_path, const char *profile_out,
                               const char *llvm_profile_out, bool quiet) {
#ifdef _WIN32
  intptr_t status;
  char *old_profile = NULL;
  char *old_llvm_profile = NULL;
  const char *argv[] = {bin_path, NULL};

  (void)quiet;

  if (profile_out && *profile_out) {
    const char *existing = getenv("QISC_PROFILE_OUT");
    if (existing) {
      old_profile = strdup(existing);
    }
    _putenv_s("QISC_PROFILE_OUT", profile_out);
  }

  if (llvm_profile_out && *llvm_profile_out) {
    const char *existing = getenv("LLVM_PROFILE_FILE");
    if (existing) {
      old_llvm_profile = strdup(existing);
    }
    _putenv_s("LLVM_PROFILE_FILE", llvm_profile_out);
  }

  status = _spawnv(_P_WAIT, bin_path, argv);

  if (profile_out && *profile_out) {
    if (old_profile) {
      _putenv_s("QISC_PROFILE_OUT", old_profile);
      free(old_profile);
    } else {
      _putenv_s("QISC_PROFILE_OUT", "");
    }
  }

  if (llvm_profile_out && *llvm_profile_out) {
    if (old_llvm_profile) {
      _putenv_s("LLVM_PROFILE_FILE", old_llvm_profile);
      free(old_llvm_profile);
    } else {
      _putenv_s("LLVM_PROFILE_FILE", "");
    }
  }

  return status == -1 ? 1 : 0;
#else
  char cmd[2048];
  int status;

  cmd[0] = '\0';
  if (profile_out && *profile_out) {
    strncat(cmd, "QISC_PROFILE_OUT='", sizeof(cmd) - strlen(cmd) - 1);
    strncat(cmd, profile_out, sizeof(cmd) - strlen(cmd) - 1);
    strncat(cmd, "' ", sizeof(cmd) - strlen(cmd) - 1);
  }
  if (llvm_profile_out && *llvm_profile_out) {
    strncat(cmd, "LLVM_PROFILE_FILE='", sizeof(cmd) - strlen(cmd) - 1);
    strncat(cmd, llvm_profile_out, sizeof(cmd) - strlen(cmd) - 1);
    strncat(cmd, "' ", sizeof(cmd) - strlen(cmd) - 1);
  }
  strncat(cmd, "'", sizeof(cmd) - strlen(cmd) - 1);
  strncat(cmd, bin_path, sizeof(cmd) - strlen(cmd) - 1);
  strncat(cmd, "'", sizeof(cmd) - strlen(cmd) - 1);
  if (quiet) {
    strncat(cmd, " >/dev/null 2>/dev/null", sizeof(cmd) - strlen(cmd) - 1);
  }

  status = system(cmd);
  if (status == -1) {
    return 1;
  }

  if (WIFEXITED(status) || WIFSIGNALED(status)) {
    return 0;
  }

  return 1;
#endif
}

static int qisc_merge_runtime_profile(QiscProfile *profile,
                                      const char *runtime_path) {
  QiscProfile runtime_profile;
  profile_init(&runtime_profile);

  if (profile_load(&runtime_profile, runtime_path) != 0 ||
      !qisc_profile_has_samples(&runtime_profile)) {
    profile_free(&runtime_profile);
    return 1;
  }

  profile_finalize(&runtime_profile);

  if (qisc_profile_has_samples(profile)) {
    profile_merge(profile, &runtime_profile);
    profile_free(&runtime_profile);
  } else {
    profile_free(profile);
    *profile = runtime_profile;
  }

  return 0;
}

static int qisc_merge_llvm_profile(const char *profraw_path,
                                   const char *profdata_path, bool quiet) {
#ifdef _WIN32
  (void)profraw_path;
  (void)profdata_path;
  (void)quiet;
  fprintf(stderr, "LLVM PGO merge is not supported on Windows in this build\n");
  return 1;
#else
  char cmd[4096];
  char tmp_path[PATH_MAX];
  int ret;
  FILE *existing;

  if (!profraw_path || !*profraw_path || !profdata_path || !*profdata_path) {
    return 1;
  }

  snprintf(tmp_path, sizeof(tmp_path), "%s.tmp", profdata_path);
  existing = fopen(profdata_path, "r");
  if (existing) {
    fclose(existing);
    snprintf(cmd, sizeof(cmd),
             "llvm-profdata merge -output='%s' '%s' '%s'%s",
             tmp_path, profdata_path, profraw_path,
             quiet ? " >/dev/null 2>/dev/null" : "");
    ret = system(cmd);
    if (ret != 0) {
      remove(tmp_path);
      return 1;
    }
    if (rename(tmp_path, profdata_path) != 0) {
      remove(tmp_path);
      return 1;
    }
    return 0;
  }

  snprintf(cmd, sizeof(cmd),
           "llvm-profdata merge -output='%s' '%s'%s",
           profdata_path, profraw_path, quiet ? " >/dev/null 2>/dev/null" : "");
  ret = system(cmd);
  return ret == 0 ? 0 : 1;
#endif
}

static bool qisc_should_use_clang_ir_backend(const QiscOptions *options) {
  return options && (options->llvm_pgo_generate || options->llvm_pgo_use ||
                     options->lto_mode != QISC_LTO_NONE);
}

static int qisc_append_runtime_lib(char *out, size_t out_size,
                                   const char *path) {
  int written;
  size_t used;

  if (!out || !path) return 1;
  used = strlen(out);
  written = snprintf(out + used, out_size - used, "%s'%s'",
                     out[0] ? " " : "", path);
  return (written < 0 || (size_t)written >= out_size - used) ? 1 : 0;
}

static int qisc_build_runtime_libs(bool include_profile_runtime, char *out,
                                   size_t out_size) {
  const char *paths[5] = {
      "lib/qisc_error.o",
      "lib/qisc_array.o",
      "lib/qisc_io.o",
      "lib/qisc_stream.o",
      "lib/qisc_runtime.o",
  };
  const char *fallbacks[5] = {
      "/home/Trushi/ai/QISC/lib/qisc_error.o",
      "/home/Trushi/ai/QISC/lib/qisc_array.o",
      "/home/Trushi/ai/QISC/lib/qisc_io.o",
      "/home/Trushi/ai/QISC/lib/qisc_stream.o",
      "/home/Trushi/ai/QISC/lib/qisc_runtime.o",
  };
  int count = include_profile_runtime ? 5 : 4;

  if (!out || out_size == 0) return 1;
  out[0] = '\0';

  for (int i = 0; i < count; i++) {
    const char *selected = paths[i];
    FILE *f = fopen(paths[i], "r");
    if (f) {
      fclose(f);
    } else {
      selected = fallbacks[i];
    }
    if (qisc_append_runtime_lib(out, out_size, selected) != 0) {
      return 1;
    }
  }

  return 0;
}

static int qisc_link_object_binary(const char *obj_path, const char *bin_path,
                                   const char *runtime_libs,
                                   QiscOptions *options, bool quiet) {
  char link_cmd[4096];
  int ret;
  (void)options;

  snprintf(link_cmd, sizeof(link_cmd), "cc '%s' %s -o '%s' -lm%s",
           obj_path, runtime_libs, bin_path,
           quiet ? " >/dev/null 2>/dev/null" : "");
  if (!quiet) {
    printf("Linking: %s\n", link_cmd);
  }
  ret = system(link_cmd);
  return ret == 0 ? 0 : 1;
}

static int qisc_link_ir_binary(const char *ir_path, const char *bin_path,
                               const char *runtime_libs, QiscOptions *options,
                               const char *llvm_profraw_path, bool quiet) {
#ifdef _WIN32
  (void)ir_path;
  (void)bin_path;
  (void)runtime_libs;
  (void)options;
  (void)llvm_profraw_path;
  (void)quiet;
  fprintf(stderr, "LLVM PGO/LTO link path is not supported on Windows in this build\n");
  return 1;
#else
  char compile_cmd[8192];
  char link_cmd[8192];
  char ir_obj_path[PATH_MAX];
  const char *lto_compile_flag = "";
  const char *lto_link_flag = "";
  const char *arch_flags = "";
  const char *context_flags = "";
  char pgo_gen_flag[PATH_MAX + 64] = "";
  char pgo_use_flag[PATH_MAX + 64] = "";
  int ret;

  if (!ir_path || !bin_path || !runtime_libs || !options) return 1;

  switch (options->lto_mode) {
  case QISC_LTO_FULL:
    lto_compile_flag = "-flto=full";
    lto_link_flag = "-flto=full -fuse-ld=lld";
    break;
  case QISC_LTO_THIN:
    lto_compile_flag = "-flto=thin";
    lto_link_flag = "-flto=thin -fuse-ld=lld";
    break;
  case QISC_LTO_NONE:
  default:
    lto_compile_flag = "";
    lto_link_flag = "";
    break;
  }

  switch (options->context) {
  case QISC_CONTEXT_SERVER:
  case QISC_CONTEXT_CLI:
  case QISC_CONTEXT_NOTEBOOK:
    arch_flags = "-march=native -mtune=native";
    break;
  case QISC_CONTEXT_WEB:
    context_flags = "-Os";
    break;
  case QISC_CONTEXT_EMBEDDED:
    context_flags = "-Oz";
    break;
  default:
    break;
  }

  if (options->llvm_pgo_generate && llvm_profraw_path) {
    snprintf(pgo_gen_flag, sizeof(pgo_gen_flag),
             "-fprofile-instr-generate");
  }
  if (options->llvm_pgo_use && options->llvm_profile_path) {
    snprintf(pgo_use_flag, sizeof(pgo_use_flag),
             "-fprofile-instr-use='%s'", options->llvm_profile_path);
  }

  snprintf(ir_obj_path, sizeof(ir_obj_path), "%s.clang.o", ir_path);
  snprintf(compile_cmd, sizeof(compile_cmd),
           "clang -x ir -c -O%d %s %s %s %s %s '%s' -o '%s'%s",
           options->optimization_level,
           arch_flags,
           context_flags,
           lto_compile_flag,
           pgo_gen_flag,
           pgo_use_flag,
           ir_path,
           ir_obj_path,
           quiet ? " >/dev/null 2>/dev/null" : "");
  snprintf(link_cmd, sizeof(link_cmd), "clang %s %s %s %s %s -o '%s' -lm%s",
           lto_link_flag, pgo_gen_flag, pgo_use_flag, ir_obj_path, runtime_libs,
           bin_path,
           quiet ? " >/dev/null 2>/dev/null" : "");

  if (!quiet) {
    printf("LLVM compile: %s\n", compile_cmd);
    printf("LLVM link: %s\n", link_cmd);
  }
  ret = system(compile_cmd);
  if (ret != 0) {
    remove(ir_obj_path);
    return 1;
  }
  ret = system(link_cmd);
  remove(ir_obj_path);
  return ret == 0 ? 0 : 1;
#endif
}

static int qisc_emit_and_link_binary(Codegen *cg, const char *path,
                                     QiscOptions *options, char *bin_path,
                                     size_t bin_path_size, bool quiet,
                                     bool *link_succeeded) {
  char obj_path[PATH_MAX];
  char ir_path[PATH_MAX];
  char llvm_profraw_path[PATH_MAX];
  char llvm_profdata_path[PATH_MAX];
  char runtime_libs[2048];
  int ret;

  if (link_succeeded) *link_succeeded = false;
  if (!cg || !path || !options || !bin_path || bin_path_size == 0) return 1;

  qisc_binary_path_from_source(path, bin_path, bin_path_size);
  qisc_llvm_profile_paths_from_source(path, llvm_profraw_path,
                                      sizeof(llvm_profraw_path),
                                      llvm_profdata_path,
                                      sizeof(llvm_profdata_path), ir_path,
                                      sizeof(ir_path));
  snprintf(obj_path, sizeof(obj_path), "%s.o", path);

  if (codegen_write_object(cg, obj_path) != 0) {
    return 1;
  }

  if (!options->converge) {
    printf("=== Optimized LLVM IR ===\n");
    codegen_dump_ir(cg);
  }

  if (qisc_build_runtime_libs(options->collect_profile, runtime_libs,
                              sizeof(runtime_libs)) != 0) {
    remove(obj_path);
    return 1;
  }

  if (qisc_should_use_clang_ir_backend(options)) {
    if (codegen_write_ir(cg, ir_path) != 0) {
      remove(obj_path);
      return 1;
    }
    ret = qisc_link_ir_binary(ir_path, bin_path, runtime_libs, options,
                              options->llvm_pgo_generate ? llvm_profraw_path
                                                         : NULL,
                              quiet);
    remove(ir_path);
  } else {
    ret = qisc_link_object_binary(obj_path, bin_path, runtime_libs, options,
                                  quiet);
  }

  remove(obj_path);
  if (link_succeeded) *link_succeeded = (ret == 0);
  return ret;
}

static void repl_ast_store_init(ReplAstStore *store) {
  if (!store)
    return;
  memset(store, 0, sizeof(*store));
}

static void repl_ast_store_free(ReplAstStore *store) {
  if (!store)
    return;
  for (int i = 0; i < store->count; i++) {
    ast_free(store->items[i]);
  }
  free(store->items);
  store->items = NULL;
  store->count = 0;
  store->capacity = 0;
}

static bool repl_ast_store_add(ReplAstStore *store, AstNode *node) {
  if (!store || !node)
    return false;
  if (store->count >= store->capacity) {
    int new_capacity = store->capacity == 0 ? 16 : store->capacity * 2;
    AstNode **new_items =
        realloc(store->items, (size_t)new_capacity * sizeof(AstNode *));
    if (!new_items)
      return false;
    store->items = new_items;
    store->capacity = new_capacity;
  }
  store->items[store->count++] = node;
  return true;
}

static void notebook_doc_init(NotebookDoc *doc) {
  if (!doc)
    return;
  memset(doc, 0, sizeof(*doc));
}

static void notebook_doc_free(NotebookDoc *doc) {
  if (!doc)
    return;
  for (int i = 0; i < doc->count; i++) {
    free(doc->items[i].kind);
    free(doc->items[i].source);
  }
  free(doc->items);
  doc->items = NULL;
  doc->count = 0;
  doc->capacity = 0;
}

static bool notebook_doc_add(NotebookDoc *doc, const char *kind,
                             const char *source, int start_line) {
  NotebookCell *cell;
  if (!doc || !kind || !source)
    return false;
  if (doc->count >= doc->capacity) {
    int new_capacity = doc->capacity == 0 ? 8 : doc->capacity * 2;
    NotebookCell *new_items =
        realloc(doc->items, (size_t)new_capacity * sizeof(NotebookCell));
    if (!new_items)
      return false;
    doc->items = new_items;
    doc->capacity = new_capacity;
  }
  cell = &doc->items[doc->count++];
  cell->kind = strdup(kind);
  cell->source = strdup(source);
  cell->start_line = start_line;
  return cell->kind != NULL && cell->source != NULL;
}

static bool qisc_strieq(const char *left, const char *right) {
  if (!left || !right)
    return false;
  while (*left && *right) {
    if (tolower((unsigned char)*left) != tolower((unsigned char)*right))
      return false;
    left++;
    right++;
  }
  return *left == '\0' && *right == '\0';
}

static char *qisc_read_text_file(const char *path) {
  FILE *fp;
  long size;
  size_t nread;
  char *buffer;

  if (!path)
    return NULL;

  fp = fopen(path, "rb");
  if (!fp)
    return NULL;

  if (fseek(fp, 0, SEEK_END) != 0) {
    fclose(fp);
    return NULL;
  }
  size = ftell(fp);
  if (size < 0) {
    fclose(fp);
    return NULL;
  }
  rewind(fp);

  buffer = malloc((size_t)size + 1);
  if (!buffer) {
    fclose(fp);
    return NULL;
  }

  nread = fread(buffer, 1, (size_t)size, fp);
  buffer[nread] = '\0';
  fclose(fp);
  return buffer;
}

static bool qisc_notebook_kind_is_ignored(const char *kind) {
  return qisc_strieq(kind, "markdown") || qisc_strieq(kind, "md") ||
         qisc_strieq(kind, "text") || qisc_strieq(kind, "raw");
}

static bool qisc_notebook_kind_is_code(const char *kind) {
  return kind == NULL || kind[0] == '\0' || qisc_strieq(kind, "qisc") ||
         qisc_strieq(kind, "code");
}

static int qisc_parse_notebook_file(const char *path, NotebookDoc *doc) {
  char *text = qisc_read_text_file(path);
  char *cursor;
  char *line_start;
  char *cell_buf = NULL;
  size_t cell_len = 0;
  char *current_kind = strdup("qisc");
  int current_start_line = 1;
  int line_no = 1;
  bool saw_marker = false;

  if (!text || !current_kind)
    return 1;

  cursor = text;
  line_start = text;

  while (1) {
    if (*cursor == '\n' || *cursor == '\0') {
      size_t line_len = (size_t)(cursor - line_start);
      const char *trim = line_start;
      while (*trim == ' ' || *trim == '\t')
        trim++;

      if (trim[0] == '%' && trim[1] == '%') {
        char *kind_buf;
        char *kind_trimmed;
        saw_marker = true;
        if (cell_len > 0) {
          cell_buf[cell_len] = '\0';
          if (!notebook_doc_add(doc, current_kind, cell_buf, current_start_line)) {
            free(cell_buf);
            free(current_kind);
            free(text);
            return 1;
          }
          free(cell_buf);
          cell_buf = NULL;
          cell_len = 0;
        }

        trim += 2;
        while (*trim == ' ' || *trim == '\t')
          trim++;
        kind_buf = malloc(line_len - (size_t)(trim - line_start) + 1);
        if (!kind_buf) {
          free(current_kind);
          free(text);
          return 1;
        }
        memcpy(kind_buf, trim, line_len - (size_t)(trim - line_start));
        kind_buf[line_len - (size_t)(trim - line_start)] = '\0';
        kind_trimmed = qisc_repl_trim_copy(kind_buf);
        free(kind_buf);
        if (!kind_trimmed) {
          free(current_kind);
          free(text);
          return 1;
        }
        free(current_kind);
        if (kind_trimmed[0] == '\0') {
          free(kind_trimmed);
          current_kind = strdup("qisc");
        } else {
          current_kind = kind_trimmed;
        }
        if (!current_kind) {
          free(text);
          return 1;
        }
        current_start_line = line_no + 1;
      } else {
        char *grown = realloc(cell_buf, cell_len + line_len + 2);
        if (!grown) {
          free(cell_buf);
          free(current_kind);
          free(text);
          return 1;
        }
        cell_buf = grown;
        memcpy(cell_buf + cell_len, line_start, line_len);
        cell_len += line_len;
        if (*cursor == '\n')
          cell_buf[cell_len++] = '\n';
        cell_buf[cell_len] = '\0';
      }

      if (*cursor == '\0')
        break;

      cursor++;
      line_start = cursor;
      line_no++;
      continue;
    }
    cursor++;
  }

  if (cell_len > 0 || !saw_marker) {
    if (!cell_buf) {
      cell_buf = strdup("");
      if (!cell_buf) {
        free(current_kind);
        free(text);
        return 1;
      }
    }
    if (!notebook_doc_add(doc, current_kind, cell_buf, current_start_line)) {
      free(cell_buf);
      free(current_kind);
      free(text);
      return 1;
    }
    free(cell_buf);
  }

  free(current_kind);
  free(text);
  return 0;
}

static char *qisc_repl_trim_copy(const char *text) {
  const char *start;
  const char *end;
  char *out;
  size_t len;

  if (!text)
    return strdup("");

  start = text;
  while (*start && isspace((unsigned char)*start))
    start++;

  end = text + strlen(text);
  while (end > start && isspace((unsigned char)end[-1]))
    end--;

  len = (size_t)(end - start);
  out = malloc(len + 1);
  if (!out)
    return NULL;
  memcpy(out, start, len);
  out[len] = '\0';
  return out;
}

static bool qisc_repl_is_declaration_token(TokenType type) {
  switch (type) {
  case TOK_PROC:
  case TOK_STRUCT:
  case TOK_ENUM:
  case TOK_EXTEND:
  case TOK_MODULE:
  case TOK_IMPORT:
  case TOK_EXPORT:
  case TOK_CONST:
  case TOK_AUTO:
  case TOK_INT:
  case TOK_UINT:
  case TOK_I8:
  case TOK_I16:
  case TOK_I32:
  case TOK_I64:
  case TOK_U8:
  case TOK_U16:
  case TOK_U32:
  case TOK_U64:
  case TOK_F32:
  case TOK_F64:
  case TOK_FLOAT:
  case TOK_DOUBLE:
  case TOK_BOOL:
  case TOK_CHAR:
  case TOK_STRING:
  case TOK_VOID:
  case TOK_MAYBE:
    return true;
  default:
    return false;
  }
}

static bool qisc_repl_is_statement_token(TokenType type) {
  switch (type) {
  case TOK_IF:
  case TOK_WHEN:
  case TOK_WHILE:
  case TOK_FOR:
  case TOK_GIVE:
  case TOK_BREAK:
  case TOK_CONTINUE:
  case TOK_TRY:
  case TOK_FAIL:
    return true;
  default:
    return false;
  }
}

static bool qisc_repl_needs_semicolon(TokenType type) {
  switch (type) {
  case TOK_MODULE:
  case TOK_IMPORT:
  case TOK_CONST:
  case TOK_AUTO:
  case TOK_INT:
  case TOK_UINT:
  case TOK_I8:
  case TOK_I16:
  case TOK_I32:
  case TOK_I64:
  case TOK_U8:
  case TOK_U16:
  case TOK_U32:
  case TOK_U64:
  case TOK_F32:
  case TOK_F64:
  case TOK_FLOAT:
  case TOK_DOUBLE:
  case TOK_BOOL:
  case TOK_CHAR:
  case TOK_STRING:
  case TOK_VOID:
  case TOK_MAYBE:
  case TOK_GIVE:
  case TOK_BREAK:
  case TOK_CONTINUE:
  case TOK_FAIL:
    return true;
  default:
    return false;
  }
}

static char qisc_repl_last_non_space(const char *text) {
  size_t len;
  if (!text)
    return '\0';
  len = strlen(text);
  while (len > 0 && isspace((unsigned char)text[len - 1]))
    len--;
  return len > 0 ? text[len - 1] : '\0';
}

static char *qisc_repl_prepare_source(const char *source, TokenType first_type,
                                      bool expression_mode) {
  char *trimmed = qisc_repl_trim_copy(source);
  char *out;
  size_t len;
  char last;

  if (!trimmed)
    return NULL;

  len = strlen(trimmed);
  if (len == 0)
    return trimmed;

  last = qisc_repl_last_non_space(trimmed);
  if (expression_mode) {
    while (len > 0 && isspace((unsigned char)trimmed[len - 1]))
      len--;
    if (len > 0 && trimmed[len - 1] == ';') {
      trimmed[len - 1] = '\0';
    }
    return trimmed;
  }

  if (!qisc_repl_needs_semicolon(first_type) || last == ';' || last == '}')
    return trimmed;

  out = malloc(len + 2);
  if (!out) {
    free(trimmed);
    return NULL;
  }
  memcpy(out, trimmed, len);
  out[len] = ';';
  out[len + 1] = '\0';
  free(trimmed);
  return out;
}

static bool qisc_repl_source_complete(const char *source) {
  int paren = 0;
  int brace = 0;
  int bracket = 0;
  bool in_string = false;
  bool escape = false;
  bool in_line_comment = false;
  bool in_block_comment = false;
  char quote = '\0';

  if (!source)
    return false;

  for (size_t i = 0; source[i] != '\0'; i++) {
    char c = source[i];
    char next = source[i + 1];

    if (in_line_comment) {
      if (c == '\n')
        in_line_comment = false;
      continue;
    }
    if (in_block_comment) {
      if (c == '*' && next == '/') {
        in_block_comment = false;
        i++;
      }
      continue;
    }
    if (in_string) {
      if (escape) {
        escape = false;
      } else if (c == '\\') {
        escape = true;
      } else if (c == quote) {
        in_string = false;
      }
      continue;
    }
    if (c == '/' && next == '/') {
      in_line_comment = true;
      i++;
      continue;
    }
    if (c == '/' && next == '*') {
      in_block_comment = true;
      i++;
      continue;
    }
    if (c == '"' || c == '\'') {
      in_string = true;
      quote = c;
      continue;
    }

    switch (c) {
    case '(':
      paren++;
      break;
    case ')':
      paren--;
      break;
    case '{':
      brace++;
      break;
    case '}':
      brace--;
      break;
    case '[':
      bracket++;
      break;
    case ']':
      bracket--;
      break;
    default:
      break;
    }
  }

  return !in_string && !in_block_comment && !in_line_comment && paren == 0 &&
         brace == 0 && bracket == 0 &&
         qisc_repl_last_non_space(source) != '\0';
}

static AstNode *qisc_parse_repl_input(const char *source, bool *is_expression) {
  char *prepared = NULL;
  Lexer first_lexer;
  Token first;
  Lexer lexer;
  Parser parser;
  AstNode *node = NULL;
  bool expression_mode = false;

  lexer_init(&first_lexer, source);
  first = lexer_scan_token(&first_lexer);
  if (first.type == TOK_ERROR || first.type == TOK_EOF)
    return NULL;

  expression_mode = !qisc_repl_is_declaration_token(first.type) &&
                    !qisc_repl_is_statement_token(first.type) &&
                    first.type != TOK_PRAGMA;
  prepared = qisc_repl_prepare_source(source, first.type, expression_mode);
  if (!prepared)
    return NULL;

  lexer_init(&lexer, prepared);
  parser_init(&parser, &lexer);

  if (first.type == TOK_PRAGMA) {
    node = parser_parse(&parser);
  } else if (qisc_repl_is_declaration_token(first.type)) {
    node = parser_parse_declaration(&parser);
  } else if (qisc_repl_is_statement_token(first.type)) {
    node = parser_parse_statement(&parser);
  } else {
    node = parser_parse_expression(&parser);
  }

  if (!parser.had_error && parser.current.type != TOK_EOF) {
    parser_error_at_current(&parser, "Unexpected trailing input");
  }

  free(prepared);

  if (parser.had_error) {
    if (node)
      ast_free(node);
    return NULL;
  }

  if (is_expression)
    *is_expression = expression_mode;
  return node;
}

static void qisc_repl_clear_runtime_state(Interpreter *interp) {
  if (!interp)
    return;
  interp->had_error = false;
  interp->error_message[0] = '\0';
  interp->returning = false;
  value_free(&interp->return_value);
  interp->return_value = value_none();
  interp->breaking = false;
  interp->continuing = false;
}

static int qisc_session_run_node(Interpreter *interp, ReplAstStore *store,
                                 AstNode *node, bool is_expression,
                                 QiscOptions *options, const char *error_prefix) {
  if (!interp || !store || !node)
    return 1;

  if (!repl_ast_store_add(store, node)) {
    ast_free(node);
    fprintf(stderr, "Error: out of memory while storing session state\n");
    return 1;
  }

  if (is_expression) {
    Value result = interpreter_eval(interp, node);
    if (interp->had_error) {
      qisc_personality_print(options->personality,
                             "%sRuntime error: %s\n",
                             error_prefix ? error_prefix : "Error: ",
                             interp->error_message);
      qisc_repl_clear_runtime_state(interp);
      return 1;
    }
    if (result.type != VAL_NONE) {
      printf("=> ");
      value_print(&result);
      printf("\n");
    }
    return 0;
  }

  interpreter_exec(interp, node);
  if (interp->had_error) {
    qisc_personality_print(options->personality,
                           "%sRuntime error: %s\n",
                           error_prefix ? error_prefix : "Error: ",
                           interp->error_message);
    qisc_repl_clear_runtime_state(interp);
    return 1;
  }
  if (interp->returning && interp->return_value.type != VAL_NONE) {
    printf("=> ");
    value_print(&interp->return_value);
    printf("\n");
  }
  qisc_repl_clear_runtime_state(interp);
  return 0;
}

static void qisc_repl_reset_session(Interpreter *interp, ReplAstStore *store) {
  if (!interp || !store)
    return;
  interpreter_free(interp);
  repl_ast_store_free(store);
  repl_ast_store_init(store);
  interpreter_init(interp);
}

static int qisc_repl_load_file(Interpreter *interp, ReplAstStore *store,
                               const char *path, QiscOptions *options) {
  char *parse_error_path = NULL;
  char parse_error[512] = {0};
  AstNode *program = qisc_parse_program_with_imports(path, &parse_error_path,
                                                     parse_error,
                                                     sizeof(parse_error));
  if (!program) {
    fprintf(stderr, "%s: %s\n", parse_error_path ? parse_error_path : path,
            parse_error[0] ? parse_error : "parse failed");
    free(parse_error_path);
    return 1;
  }

  if (!repl_ast_store_add(store, program)) {
    ast_free(program);
    fprintf(stderr, "Error: out of memory while storing REPL state\n");
    return 1;
  }

  interpreter_exec(interp, program);
  if (interp->had_error) {
    qisc_personality_print(options->personality,
                           "Error: Runtime error: %s\n",
                           interp->error_message);
    qisc_repl_clear_runtime_state(interp);
    return 1;
  }

  qisc_personality_print(options->personality, "Loaded %s\n", path);
  return 0;
}

static int qisc_run_repl(QiscOptions *options) {
  Interpreter interp;
  ReplAstStore store;
  bool interactive = false;
  char line[4096];
  char *buffer = NULL;
  size_t buffer_len = 0;

  interpreter_init(&interp);
  repl_ast_store_init(&store);

#ifdef _WIN32
  interactive = _isatty(_fileno(stdin)) != 0;
#else
  interactive = isatty(fileno(stdin)) != 0;
#endif

  qisc_personality_print(options->personality,
                         "QISC REPL\n"
                         "Type :help for commands, :quit to exit.\n");

  for (;;) {
    char *trimmed = NULL;
    size_t line_len;
    bool is_expression = false;
    AstNode *node = NULL;

    if (interactive) {
      fputs(buffer_len == 0 ? "qisc> " : "....> ", stdout);
      fflush(stdout);
    }

    if (!fgets(line, sizeof(line), stdin)) {
      break;
    }

    line_len = strlen(line);
    if (line_len == 0)
      continue;

    {
      char *grown = realloc(buffer, buffer_len + line_len + 1);
      if (!grown) {
        fprintf(stderr, "Error: out of memory\n");
        break;
      }
      buffer = grown;
      memcpy(buffer + buffer_len, line, line_len + 1);
      buffer_len += line_len;
    }

    if (!qisc_repl_source_complete(buffer))
      continue;

    trimmed = qisc_repl_trim_copy(buffer);
    free(buffer);
    buffer = NULL;
    buffer_len = 0;

    if (!trimmed)
      continue;
    if (trimmed[0] == '\0') {
      free(trimmed);
      continue;
    }

    if (trimmed[0] == ':') {
      if (strcmp(trimmed, ":quit") == 0 || strcmp(trimmed, ":exit") == 0) {
        free(trimmed);
        break;
      }
      if (strcmp(trimmed, ":help") == 0) {
        printf("REPL commands:\n");
        printf("  :help         Show this help\n");
        printf("  :load <file>  Load declarations from a .qisc file\n");
        printf("  :reset        Clear interpreter state\n");
        printf("  :quit         Exit the REPL\n");
        free(trimmed);
        continue;
      }
      if (strcmp(trimmed, ":reset") == 0) {
        qisc_repl_reset_session(&interp, &store);
        qisc_personality_print(options->personality, "Session reset\n");
        free(trimmed);
        continue;
      }
      if (strncmp(trimmed, ":load ", 6) == 0) {
        qisc_repl_load_file(&interp, &store, trimmed + 6, options);
        free(trimmed);
        continue;
      }
      fprintf(stderr, "Unknown REPL command: %s\n", trimmed);
      free(trimmed);
      continue;
    }

    node = qisc_parse_repl_input(trimmed, &is_expression);
    free(trimmed);
    if (!node)
      continue;

    qisc_session_run_node(&interp, &store, node, is_expression, options,
                          "Error: ");
  }

  free(buffer);
  interpreter_free(&interp);
  repl_ast_store_free(&store);
  return 0;
}

static int qisc_run_notebook(const char *path, QiscOptions *options) {
  NotebookDoc doc;
  ReplAstStore store;
  Interpreter interp;

  notebook_doc_init(&doc);
  repl_ast_store_init(&store);
  interpreter_init(&interp);

  if (qisc_parse_notebook_file(path, &doc) != 0) {
    fprintf(stderr, "Error: could not read notebook file %s\n", path);
    interpreter_free(&interp);
    repl_ast_store_free(&store);
    notebook_doc_free(&doc);
    return 1;
  }

  for (int i = 0; i < doc.count; i++) {
    NotebookCell *cell = &doc.items[i];
    char *trimmed;
    AstNode *node;
    bool is_expression = false;
    char prefix[128];

    if (!qisc_notebook_kind_is_code(cell->kind)) {
      if (!qisc_notebook_kind_is_ignored(cell->kind)) {
        fprintf(stderr, "Error: notebook cell %d has unknown kind '%s'\n",
                i + 1, cell->kind);
        interpreter_free(&interp);
        repl_ast_store_free(&store);
        notebook_doc_free(&doc);
        return 1;
      }
      continue;
    }

    trimmed = qisc_repl_trim_copy(cell->source);
    if (!trimmed) {
      fprintf(stderr, "Error: out of memory\n");
      interpreter_free(&interp);
      repl_ast_store_free(&store);
      notebook_doc_free(&doc);
      return 1;
    }
    if (trimmed[0] == '\0') {
      free(trimmed);
      continue;
    }
    free(trimmed);

    node = qisc_parse_repl_input(cell->source, &is_expression);
    if (!node) {
      fprintf(stderr,
              "Notebook parse failed in cell %d (starts at line %d)\n",
              i + 1, cell->start_line);
      interpreter_free(&interp);
      repl_ast_store_free(&store);
      notebook_doc_free(&doc);
      return 1;
    }

    snprintf(prefix, sizeof(prefix), "Notebook cell %d (line %d): ",
             i + 1, cell->start_line);
    if (qisc_session_run_node(&interp, &store, node, is_expression, options,
                              prefix) != 0) {
      interpreter_free(&interp);
      repl_ast_store_free(&store);
      notebook_doc_free(&doc);
      return 1;
    }
  }

  interpreter_free(&interp);
  repl_ast_store_free(&store);
  notebook_doc_free(&doc);
  return 0;
}

static void import_set_init(ImportSet *set) {
  memset(set, 0, sizeof(*set));
}

static void import_set_free(ImportSet *set) {
  if (!set)
    return;

  for (int i = 0; i < set->count; i++) {
    free(set->paths[i]);
  }
  free(set->paths);
}

static bool import_set_contains(ImportSet *set, const char *path) {
  if (!set || !path)
    return false;

  for (int i = 0; i < set->count; i++) {
    if (strcmp(set->paths[i], path) == 0) {
      return true;
    }
  }

  return false;
}

static bool import_set_add(ImportSet *set, const char *path) {
  if (!set || !path)
    return false;

  if (import_set_contains(set, path)) {
    return true;
  }

  if (set->count >= set->capacity) {
    int new_capacity = set->capacity == 0 ? 8 : set->capacity * 2;
    char **new_paths = realloc(set->paths, (size_t)new_capacity * sizeof(char *));
    if (!new_paths)
      return false;
    set->paths = new_paths;
    set->capacity = new_capacity;
  }

  set->paths[set->count++] = strdup(path);
  return true;
}

static void qisc_normalize_path(const char *path, char *out, size_t out_size) {
  if (!out || out_size == 0)
    return;

#ifdef _WIN32
  if (_fullpath(out, path, out_size)) {
    out[out_size - 1] = '\0';
  } else {
    strncpy(out, path, out_size - 1);
    out[out_size - 1] = '\0';
  }
#else
  char *resolved;
  resolved = realpath(path, NULL);
  if (resolved) {
    strncpy(out, resolved, out_size - 1);
    out[out_size - 1] = '\0';
    free(resolved);
  } else {
    strncpy(out, path, out_size - 1);
    out[out_size - 1] = '\0';
  }
#endif
}

static const char *qisc_basename_ptr(const char *path) {
  const char *slash = strrchr(path, '/');
  const char *backslash = strrchr(path, '\\');
  const char *sep = slash;

  if (backslash && (!sep || backslash > sep)) {
    sep = backslash;
  }

  return sep ? sep + 1 : path;
}

static void qisc_dirname_from_path(const char *path, char *out, size_t out_size) {
  const char *slash = strrchr(path, '/');
  const char *backslash = strrchr(path, '\\');
  const char *sep;
  size_t len;

  if (!out || out_size == 0)
    return;

  sep = slash;
  if (backslash && (!sep || backslash > sep)) {
    sep = backslash;
  }

  if (!sep) {
    snprintf(out, out_size, ".");
    return;
  }

  len = (size_t)(sep - path);
  if (len == 0) {
    len = 1;
  }
  if (len >= out_size) {
    len = out_size - 1;
  }

  memcpy(out, path, len);
  out[len] = '\0';
}

static bool qisc_resolve_import_path(const char *current_path,
                                     const char *import_name,
                                     char *resolved,
                                     size_t resolved_size) {
  char base_dir[PATH_MAX];
  char import_rel[PATH_MAX];
  char import_rel_no_ext[PATH_MAX];
  char candidate[PATH_MAX];
  const char *stdlib_root;
  size_t i, len;

  if (!current_path || !import_name || !resolved || resolved_size == 0)
    return false;

  qisc_dirname_from_path(current_path, base_dir, sizeof(base_dir));
  stdlib_root = getenv("QISC_STDLIB_PATH");
  if (!stdlib_root || !*stdlib_root) {
    stdlib_root = "stdlib";
  }

  len = strlen(import_name);
  if (len >= sizeof(import_rel) - 6)
    return false;

  for (i = 0; i < len; i++) {
    import_rel[i] = (import_name[i] == '.') ? '/' : import_name[i];
    import_rel_no_ext[i] = import_rel[i];
  }
  memcpy(import_rel + len, ".qisc", 6);
  import_rel_no_ext[len] = '\0';

  snprintf(candidate, sizeof(candidate), "%s/%s", base_dir, import_rel);
  if (qisc_file_exists(candidate)) {
    qisc_normalize_path(candidate, resolved, resolved_size);
    return true;
  }

  snprintf(candidate, sizeof(candidate), "%s/%s/main.qisc", base_dir,
           import_rel_no_ext);
  if (qisc_file_exists(candidate)) {
    qisc_normalize_path(candidate, resolved, resolved_size);
    return true;
  }

  snprintf(candidate, sizeof(candidate), "%s/%s", stdlib_root, import_rel);
  if (qisc_file_exists(candidate)) {
    qisc_normalize_path(candidate, resolved, resolved_size);
    return true;
  }

  snprintf(candidate, sizeof(candidate), "%s/%s/main.qisc", stdlib_root,
           import_rel_no_ext);
  if (qisc_file_exists(candidate)) {
    qisc_normalize_path(candidate, resolved, resolved_size);
    return true;
  }

  if (strncmp(import_name, "std.", 4) == 0 || strcmp(import_name, "std") == 0) {
    snprintf(candidate, sizeof(candidate), "%s/%s.qisc", stdlib_root, import_name);
    for (i = 0; candidate[i] != '\0'; i++) {
      if (candidate[i] == '.') {
        candidate[i] = '/';
      }
    }
    if (qisc_file_exists(candidate)) {
      qisc_normalize_path(candidate, resolved, resolved_size);
      return true;
    }
  }

  snprintf(candidate, sizeof(candidate), "%s/%s.qisc", base_dir, import_name);
  if (qisc_file_exists(candidate)) {
    qisc_normalize_path(candidate, resolved, resolved_size);
    return true;
  }

  return false;
}

static void qisc_merge_program_ast(AstNode *target, AstNode *source) {
  if (!target || !source || target->type != AST_PROGRAM || source->type != AST_PROGRAM)
    return;

  for (int i = 0; i < source->as.program.pragmas.count; i++) {
    ast_array_push(&target->as.program.pragmas, source->as.program.pragmas.items[i]);
    source->as.program.pragmas.items[i] = NULL;
  }

  for (int i = 0; i < source->as.program.declarations.count; i++) {
    ast_array_push(&target->as.program.declarations,
                   source->as.program.declarations.items[i]);
    source->as.program.declarations.items[i] = NULL;
  }
}

static AstNode *qisc_parse_program_recursive(const char *path, ImportSet *visited,
                                             char **error_path,
                                             char *error_message,
                                             size_t error_message_size) {
  char *source = NULL;
  AstNode *program = NULL;

  char normalized_path[PATH_MAX];
  qisc_normalize_path(path, normalized_path, sizeof(normalized_path));

  if (import_set_contains(visited, normalized_path)) {
    return ast_new_program();
  }

  if (!import_set_add(visited, normalized_path)) {
    snprintf(error_message, error_message_size, "Out of memory while tracking imports");
    if (error_path)
      *error_path = strdup(normalized_path);
    return NULL;
  }

  source = qisc_read_file(normalized_path);
  if (!source) {
    snprintf(error_message, error_message_size, "Could not read file");
    if (error_path)
      *error_path = strdup(normalized_path);
    return NULL;
  }

  Lexer lexer;
  lexer_init(&lexer, source);

  Parser parser;
  parser_init(&parser, &lexer);

  program = parser_parse(&parser);
  free(source);

  if (parser.had_error) {
    if (error_path)
      *error_path = strdup(normalized_path);
    strncpy(error_message, parser.error_message, error_message_size - 1);
    error_message[error_message_size - 1] = '\0';
    ast_free(program);
    return NULL;
  }

  AstNode *merged = ast_new_program();
  const char *self_base = qisc_basename_ptr(normalized_path);
  char self_module[PATH_MAX];
  strncpy(self_module, self_base, sizeof(self_module) - 1);
  self_module[sizeof(self_module) - 1] = '\0';
  char *dot = strrchr(self_module, '.');
  if (dot)
    *dot = '\0';

  for (int i = 0; i < program->as.program.pragmas.count; i++) {
    ast_array_push(&merged->as.program.pragmas, program->as.program.pragmas.items[i]);
    program->as.program.pragmas.items[i] = NULL;
  }

  for (int i = 0; i < program->as.program.declarations.count; i++) {
    AstNode *decl = program->as.program.declarations.items[i];
    program->as.program.declarations.items[i] = NULL;

    if (!decl)
      continue;

    if (decl->type == AST_IMPORT) {
      char import_path[PATH_MAX];
      bool same_module = strcmp(decl->as.import_decl.path, self_module) == 0;

      if (!same_module &&
          qisc_resolve_import_path(normalized_path, decl->as.import_decl.path, import_path,
                                   sizeof(import_path))) {
        AstNode *imported = qisc_parse_program_recursive(import_path, visited,
                                                         error_path,
                                                         error_message,
                                                         error_message_size);
        if (!imported) {
          ast_free(decl);
          ast_free(program);
          ast_free(merged);
          return NULL;
        }
        qisc_merge_program_ast(merged, imported);
        ast_free(imported);
      }

      ast_free(decl);
      continue;
    }

    if (decl->type == AST_MODULE) {
      ast_free(decl);
      continue;
    }

    ast_array_push(&merged->as.program.declarations, decl);
  }

  ast_free(program);
  return merged;
}

static AstNode *qisc_parse_program_with_imports(const char *path, char **error_path,
                                                char *error_message,
                                                size_t error_message_size) {
  ImportSet visited;
  AstNode *program;

  import_set_init(&visited);
  program = qisc_parse_program_recursive(path, &visited, error_path,
                                         error_message, error_message_size);
  import_set_free(&visited);
  return program;
}

/* Default options */
QiscOptions qisc_default_options(void) {
  QiscOptions opts = {
      .context = QISC_CONTEXT_CLI,
      .personality = QISC_PERSONALITY_FRIENDLY,
      .collect_profile = false,
      .use_profile = false,
      .converge = false,
      .llvm_pgo_generate = false,
      .llvm_pgo_use = false,
      .profile_path = NULL,
      .llvm_profile_path = NULL,
      .optimization_level = 2,
      .lto_mode = QISC_LTO_NONE,
  };
  return opts;
}

/* Version string */
const char *qisc_version(void) { return QISC_VERSION_STRING; }

/* Print help message */
void qisc_cli_help(void) {
  printf("QISC - Quantum-Inspired Superposition Compiler v%s\n\n",
         QISC_VERSION_STRING);
  printf("Usage: qisc <command> [options] <file>\n\n");
  printf("Commands:\n");
  printf("  build        Compile a .qisc file\n");
  printf("  run          Compile and immediately run\n");
  printf("  repl         Start an interactive session\n");
  printf("  notebook     Run a notebook-style cell file\n");
  printf("  achievements Show unlocked achievements\n");
  printf("  version      Show version information\n");
  printf("  help         Show this help message\n\n");
  printf("Build Options:\n");
  printf("  --profile         Collect profile data during execution\n");
  printf("  --use-profile <f> Use profile file for optimization\n");
  printf("  --converge        Compile until convergence\n");
  printf("  --llvm-pgo-gen    Build an instrumented binary for LLVM PGO\n");
  printf("  --llvm-pgo-use <f> Use merged LLVM .profdata for final optimization\n");
  printf("  --lto             Enable full LTO in the Clang/LLD link path\n");
  printf("  --thinlto         Enable ThinLTO in the Clang/LLD link path\n");
  printf(
      "  --context <ctx>   Set context (cli|server|web|notebook|embedded)\n");
  printf("  --personality <p> Set personality "
         "(off|minimal|friendly|snarky|sage|cryptic)\n");
  printf("  -o <file>         Output file name\n");
  printf("  -O<n>             Optimization level (0-3)\n\n");
  printf("Examples:\n");
  printf("  qisc build hello.qisc\n");
  printf("  qisc run hello.qisc\n");
  printf("  qisc repl\n");
  printf("  qisc notebook examples/notebook_demo.qnb\n");
  printf("  qisc build --profile app.qisc\n");
  printf("  qisc run --llvm-pgo-gen app.qisc\n");
  printf("  qisc build --llvm-pgo-use app.qisc.llvm.profdata --thinlto app.qisc\n");
  printf("  qisc build --converge app.qisc\n");
  printf("  qisc achievements\n");
}

/* Print version */
void qisc_cli_version(void) {
  qisc_personality_print(QISC_PERSONALITY_FRIENDLY,
                         "QISC Compiler v%s\n"
                         "A self-evolving, profile-driven compiler\n"
                         "\"Getting smarter with every compilation\"\n",
                         QISC_VERSION_STRING);
}

/* Parse CLI arguments */
CliArgs qisc_cli_parse(int argc, char **argv) {
  CliArgs args = {
      .command = CLI_CMD_NONE,
      .input_file = NULL,
      .output_file = NULL,
      .options = qisc_default_options(),
  };

  if (argc < 2) {
    return args;
  }

  /* Parse command */
  if (strcmp(argv[1], "build") == 0) {
    args.command = CLI_CMD_BUILD;
  } else if (strcmp(argv[1], "run") == 0) {
    args.command = CLI_CMD_RUN;
  } else if (strcmp(argv[1], "repl") == 0) {
    args.command = CLI_CMD_REPL;
  } else if (strcmp(argv[1], "notebook") == 0) {
    args.command = CLI_CMD_NOTEBOOK;
    args.options.context = QISC_CONTEXT_NOTEBOOK;
  } else if (strcmp(argv[1], "version") == 0 ||
             strcmp(argv[1], "--version") == 0) {
    args.command = CLI_CMD_VERSION;
  } else if (strcmp(argv[1], "help") == 0 || strcmp(argv[1], "--help") == 0) {
    args.command = CLI_CMD_HELP;
  } else if (strcmp(argv[1], "achievements") == 0) {
    args.command = CLI_CMD_ACHIEVEMENTS;
  }

  /* Parse options */
  for (int i = 2; i < argc; i++) {
    if (strcmp(argv[i], "--profile") == 0) {
      args.options.collect_profile = true;
    } else if (strcmp(argv[i], "--use-profile") == 0 && i + 1 < argc) {
      args.options.use_profile = true;
      args.options.profile_path = argv[++i];
    } else if (strcmp(argv[i], "--llvm-pgo-gen") == 0) {
      args.options.llvm_pgo_generate = true;
    } else if (strcmp(argv[i], "--llvm-pgo-use") == 0 && i + 1 < argc) {
      args.options.llvm_pgo_use = true;
      args.options.llvm_profile_path = argv[++i];
    } else if (strcmp(argv[i], "--lto") == 0) {
      args.options.lto_mode = QISC_LTO_FULL;
    } else if (strcmp(argv[i], "--thinlto") == 0) {
      args.options.lto_mode = QISC_LTO_THIN;
    } else if (strcmp(argv[i], "--converge") == 0) {
      args.options.converge = true;
    } else if (strcmp(argv[i], "--context") == 0 && i + 1 < argc) {
      i++;
      if (strcmp(argv[i], "cli") == 0)
        args.options.context = QISC_CONTEXT_CLI;
      else if (strcmp(argv[i], "server") == 0)
        args.options.context = QISC_CONTEXT_SERVER;
      else if (strcmp(argv[i], "web") == 0)
        args.options.context = QISC_CONTEXT_WEB;
      else if (strcmp(argv[i], "notebook") == 0)
        args.options.context = QISC_CONTEXT_NOTEBOOK;
      else if (strcmp(argv[i], "embedded") == 0)
        args.options.context = QISC_CONTEXT_EMBEDDED;
    } else if (strcmp(argv[i], "--personality") == 0 && i + 1 < argc) {
      i++;
      if (strcmp(argv[i], "off") == 0)
        args.options.personality = QISC_PERSONALITY_OFF;
      else if (strcmp(argv[i], "minimal") == 0)
        args.options.personality = QISC_PERSONALITY_MINIMAL;
      else if (strcmp(argv[i], "friendly") == 0)
        args.options.personality = QISC_PERSONALITY_FRIENDLY;
      else if (strcmp(argv[i], "snarky") == 0)
        args.options.personality = QISC_PERSONALITY_SNARKY;
      else if (strcmp(argv[i], "sage") == 0)
        args.options.personality = QISC_PERSONALITY_SAGE;
      else if (strcmp(argv[i], "cryptic") == 0)
        args.options.personality = QISC_PERSONALITY_CRYPTIC;
    } else if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
      args.output_file = argv[++i];
    } else if (strncmp(argv[i], "-O", 2) == 0) {
      args.options.optimization_level = argv[i][2] - '0';
      if (args.options.optimization_level < 0)
        args.options.optimization_level = 0;
      if (args.options.optimization_level > 3)
        args.options.optimization_level = 3;
    } else if (argv[i][0] != '-') {
      /* Input file */
      args.input_file = argv[i];
    }
  }

  return args;
}

/* Main CLI entry point */
int qisc_cli_run(int argc, char **argv) {
  CliArgs args = qisc_cli_parse(argc, argv);

  if (args.options.llvm_pgo_generate && args.options.llvm_pgo_use) {
    fprintf(stderr,
            "Error: --llvm-pgo-gen and --llvm-pgo-use cannot be used together\n");
    return 1;
  }
  if (args.options.llvm_pgo_use && !args.options.llvm_profile_path) {
    fprintf(stderr, "Error: --llvm-pgo-use requires a .profdata path\n");
    return 1;
  }

  switch (args.command) {
  case CLI_CMD_VERSION:
    qisc_cli_version();
    return 0;

  case CLI_CMD_HELP:
    qisc_cli_help();
    return 0;

  case CLI_CMD_BUILD:
    if (!args.input_file) {
      qisc_personality_print(args.options.personality,
                             "Error: No input file specified\n"
                             "Use 'qisc help' for usage information\n");
      return 1;
    }

    if (args.options.converge) {
      /* Convergence mode: compile repeatedly until IR hash stabilizes */
      qisc_personality_print(args.options.personality,
                             "Starting convergence compilation for %s...\n",
                             args.input_file);
      
      int max_iterations = 10;
      QiscResult result;
      ConvergenceMetrics metrics;
      convergence_init(&metrics);
      uint64_t ir_hash;
      char profile_path[512];
      char runtime_profile_path[576];
      qisc_profile_paths_from_source(args.input_file, profile_path,
                                     sizeof(profile_path), runtime_profile_path,
                                     sizeof(runtime_profile_path));
      
      /* First pass: enable profiling */
      args.options.collect_profile = true;
      
      while (convergence_should_continue(&metrics, max_iterations)) {
        qisc_personality_print(args.options.personality,
                               "\n[Iteration %d/%d]\n", metrics.iterations + 1, max_iterations);
        
        /* After first pass, use the profile */
        if (metrics.iterations > 0) {
          args.options.use_profile = true;
        }
        
        result = qisc_compile_file_with_hash(args.input_file, &args.options, &ir_hash);
        
        if (result != QISC_OK) {
          qisc_personality_print(args.options.personality,
                                 "Compilation failed on iteration %d\n", metrics.iterations + 1);
          return 1;
        }
        
        /* Update convergence metrics with both IR and normalized profile state */
        {
          QiscProfile profile;
          uint64_t profile_hash = 0;
          profile_init(&profile);
          if (profile_load(&profile, profile_path) == 0 &&
              qisc_profile_has_samples(&profile)) {
            profile_finalize(&profile);
            profile_hash = profile_fingerprint(&profile);
          }
          bool converged = convergence_update(&metrics, ir_hash, profile_hash);
          qisc_personality_print(args.options.personality,
                                 "  IR: 0x%016llx | Profile: 0x%016llx | Stability: %.1f%%\n",
                                 (unsigned long long)ir_hash,
                                 (unsigned long long)profile_hash,
                                 metrics.stability * 100.0);
          profile_free(&profile);
        
          if (converged) {
            qisc_personality_print(args.options.personality,
                                   "\n┌─────────────────────────────────────────────────┐\n"
                                   "│     🎯 CONVERGENCE ACHIEVED! 🎯                 │\n"
                                   "│                                                 │\n"
                                   "│ IR and profile behavior both stabilized         │\n"
                                   "│ Iterations: %-36d│\n"
                                   "│ Final IR: 0x%016llx                 │\n"
                                   "│ Status: OPTIMAL                                 │\n"
                                   "└─────────────────────────────────────────────────┘\n",
                                   metrics.iterations,
                                   (unsigned long long)metrics.current_hash);
            break;
          }
        }
      }
      
      if (!metrics.converged) {
        qisc_personality_print(args.options.personality,
                               "\n⚠️  Max iterations reached without convergence\n"
                               "   Final: %s\n", convergence_summary(&metrics));
      }
      
      return 0;
    }

    qisc_personality_print(args.options.personality, "Compiling %s...\n",
                           args.input_file);

    QiscResult result = qisc_compile_file(args.input_file, &args.options);

    if (result != QISC_OK) {
      qisc_personality_print(args.options.personality,
                             "Compilation failed with error %d\n", result);
      return 1;
    }

    qisc_personality_print(args.options.personality,
                           "Compilation successful!\n");
    return 0;

  case CLI_CMD_REPL:
    return qisc_run_repl(&args.options);

  case CLI_CMD_NOTEBOOK:
    if (!args.input_file) {
      fprintf(stderr, "Error: No notebook file specified\n");
      return 1;
    }
    return qisc_run_notebook(args.input_file, &args.options);

  case CLI_CMD_RUN:
    if (!args.input_file) {
      fprintf(stderr, "Error: No input file specified\n");
      return 1;
    }
    return qisc_run_file(args.input_file, &args.options);

  case CLI_CMD_ACHIEVEMENTS:
    ensure_achievements_initialized();
    achievements_print_all(&g_achievements);
    return 0;

  default:
    qisc_cli_help();
    return 1;
  }
}

/* Compile a file */
QiscResult qisc_compile_file(const char *path, QiscOptions *options) {
  clock_t start_time = clock();

  /* Read source file */
  char *parse_error_path = NULL;
  char parse_error[512] = {0};
  AstNode *program = qisc_parse_program_with_imports(path, &parse_error_path,
                                                     parse_error,
                                                     sizeof(parse_error));
  if (!program) {
    fprintf(stderr, "%s: %s\n", parse_error_path ? parse_error_path : path,
            parse_error[0] ? parse_error : "parse failed");
    free(parse_error_path);
    return QISC_ERROR_FILE_NOT_FOUND;
  }

  /* Load existing profile if --use-profile */
  QiscProfile profile;
  profile_init(&profile);
  char profile_path[512];
  char runtime_profile_path[576];
  qisc_profile_paths_from_source(path, profile_path, sizeof(profile_path),
                                 runtime_profile_path,
                                 sizeof(runtime_profile_path));
  
  if (options->use_profile) {
    const char *ppath = options->profile_path ? options->profile_path : profile_path;
    if (profile_load(&profile, ppath) == 0) {
      qisc_personality_print(options->personality,
                             "Loaded profile data from %s\n", ppath);
      profile_print_summary(&profile);
    }
  }

  /* Type check */
  TypeChecker tc;
  typechecker_init(&tc);
  typecheck(&tc, program);
  int type_errors = tc.error_count;
  typechecker_report(&tc);
  if (type_errors > 0) {
    ast_free(program);
    profile_free(&profile);
    return QISC_ERROR_TYPE;
  }

  /* ========== OPTIMIZATION PASSES ========== */
  
  int total_optimizations = 0;
  
  /* 1. Tail Call Optimization - detect and mark tail-recursive functions */
  TailCallOptimizer tco;
  tco_init(&tco);
  int tco_candidates = tco_analyze_program(&tco, program);
  if (tco_candidates > 0) {
    printf("🔄 Tail Call Optimization: Found %d candidates\n", tco_candidates);
    total_optimizations += tco_candidates;
  }
  
  /* 2. Stage Fusion - optimize pipeline operations */
  FusionOptimizer fusion;
  fusion_optimizer_init(&fusion);
  int fusion_opps = analyze_ast_for_fusion(&fusion, program);
  if (fusion_opps > 0) {
    printf("⚡ Pipeline Fusion: Found %d opportunities\n", fusion_opps);
    total_optimizations += fusion_opps;
  }
  
  /* 3. Memoization Analysis (requires profile data) */
  if (profile.function_count > 0) {
    MemoContext *memo = memo_create(NULL, &profile);
    if (memo) {
      int memo_candidates = memo_find_candidates(memo, program);
      if (memo_candidates > 0) {
        printf("📝 Memoization: Found %d pure function candidates\n", memo_candidates);
        total_optimizations += memo_candidates;
      }
      memo_destroy(memo);
    }
  }
  
  if (total_optimizations > 0) {
    printf("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━\n");
    printf("📊 Total optimizations identified: %d\n\n", total_optimizations);
  }
  
  /* ========== END OPTIMIZATION PASSES ========== */

  /* LLVM Codegen */
  Codegen cg;
  codegen_init(&cg, path);
  codegen_set_personality(&cg, options->personality);
  codegen_set_context(&cg, options->context);

  SyntaxProfile *syntax_profile = analyze_syntax(program, path);
  codegen_set_syntax_mode(&cg, syntax_profile);
  if (syntax_profile) {
    syntax_profile_free(syntax_profile);
  }
  
  /* Pass optimization info to codegen */
  cg.tco_context = &tco;
  cg.fusion_optimizer = &fusion;

  /* Enable profile instrumentation if --profile flag is set */
  if (options->collect_profile) {
    codegen_enable_profiling(&cg);
  }

  if (codegen_emit(&cg, program)) {
    fprintf(stderr, "Codegen failed: %s\n", cg.error_msg);
    codegen_free(&cg);
    ast_free(program);
    profile_free(&profile);
    return QISC_ERROR_SYNTAX;
  }

  /* ========== LIVING IR OPTIMIZATION (Post-Codegen) ========== */
  /* Apply profile-driven IR mutations if we have profile data */
  if (profile.function_count > 0) {
    LivingIR *living = living_ir_create(cg.mod, &profile);
    if (living) {
      printf("\n🧬 Living IR Evolution:\n");

      /* Apply profile-driven mutations */
      living_ir_evolve(living);

      /* Print summary of what was done */
      living_ir_print_summary(living);
      qisc_tiny_llm_report_living_ir(
          path, options, &living->metrics, &profile,
          ((double)(clock() - start_time) * 1000.0) / CLOCKS_PER_SEC);

      living_ir_destroy(living);
    }
  }
  /* ========== END LIVING IR ========== */

  uint64_t current_ir_hash = ir_hash_module(cg.mod);
  bool link_succeeded = false;
  {
    char bin_path[PATH_MAX];
    char llvm_profraw_path[PATH_MAX];
    char llvm_profdata_path[PATH_MAX];
    int link_ret;

    qisc_llvm_profile_paths_from_source(path, llvm_profraw_path,
                                        sizeof(llvm_profraw_path),
                                        llvm_profdata_path,
                                        sizeof(llvm_profdata_path), NULL, 0);
    link_ret = qisc_emit_and_link_binary(&cg, path, options, bin_path,
                                         sizeof(bin_path), options->converge,
                                         &link_succeeded);
    if (link_ret == 0 && link_succeeded) {
      clock_t end_time = clock();
      double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;
      double elapsed_ms = elapsed * 1000.0;

      qisc_personality_print(options->personality,
                             "Binary written to: %s (%.2fs)\n", bin_path, elapsed);

      ensure_achievements_initialized();
      achievements_record_compilation(&g_achievements, true, elapsed_ms, 0, 0);
      achievements_check(&g_achievements, options->personality);

      if (options->collect_profile && options->converge) {
        if (qisc_execute_binary(bin_path, runtime_profile_path,
                                options->llvm_pgo_generate
                                    ? llvm_profraw_path
                                    : NULL,
                                true) == 0) {
          qisc_merge_runtime_profile(&profile, runtime_profile_path);
          remove(runtime_profile_path);
          if (options->llvm_pgo_generate) {
            qisc_merge_llvm_profile(llvm_profraw_path, llvm_profdata_path, true);
            remove(llvm_profraw_path);
          }
        }
      }
    } else if (!link_succeeded) {
      fprintf(stderr, "Linking failed\n");
    }
  }

  /* Save profile if --profile */
  if (!link_succeeded) {
    codegen_free(&cg);
    ast_free(program);
    profile_free(&profile);
    return QISC_ERROR_INTERNAL;
  }

  if (options->collect_profile) {
    if (!profile.source_file) {
      profile.source_file = strdup(path);
    }
    if (qisc_profile_has_samples(&profile)) {
      profile_finalize(&profile);
      profile_set_ir_hash(&profile, current_ir_hash);
    }
    if (qisc_profile_has_samples(&profile) &&
        profile_save(&profile, profile_path) == 0) {
      qisc_personality_print(options->personality,
                             "Profile data saved to %s\n", profile_path);
    }
    
    /* Track profile usage */
    ensure_achievements_initialized();
    g_achievements.used_profile = true;
    achievements_check(&g_achievements, options->personality);
  }

  /* Cleanup */
  codegen_free(&cg);
  ast_free(program);
  profile_free(&profile);

  return QISC_OK;
}

/* Compile a file and return IR hash for convergence detection */
static QiscResult qisc_compile_file_with_hash(const char *path, QiscOptions *options, uint64_t *ir_hash) {
  clock_t start_time = clock();
  
  /* Initialize hash to 0 */
  if (ir_hash) *ir_hash = 0;

  char *parse_error_path = NULL;
  char parse_error[512] = {0};
  AstNode *program = qisc_parse_program_with_imports(path, &parse_error_path,
                                                     parse_error,
                                                     sizeof(parse_error));
  if (!program) {
    fprintf(stderr, "%s: %s\n", parse_error_path ? parse_error_path : path,
            parse_error[0] ? parse_error : "parse failed");
    free(parse_error_path);
    return QISC_ERROR_FILE_NOT_FOUND;
  }

  /* Load existing profile if --use-profile */
  QiscProfile profile;
  profile_init(&profile);
  char profile_path[512];
  char runtime_profile_path[576];
  qisc_profile_paths_from_source(path, profile_path, sizeof(profile_path),
                                 runtime_profile_path,
                                 sizeof(runtime_profile_path));
  
  if (options->use_profile) {
    const char *ppath = options->profile_path ? options->profile_path : profile_path;
    if (profile_load(&profile, ppath) == 0) {
      qisc_personality_print(options->personality,
                             "Loaded profile data from %s\n", ppath);
    }
  }

  /* Type check */
  TypeChecker tc;
  typechecker_init(&tc);
  typecheck(&tc, program);
  int type_errors = tc.error_count;
  typechecker_report(&tc);
  if (type_errors > 0) {
    ast_free(program);
    profile_free(&profile);
    return QISC_ERROR_TYPE;
  }

  /* LLVM Codegen */
  Codegen cg;
  codegen_init(&cg, path);
  codegen_set_personality(&cg, options->personality);
  codegen_set_context(&cg, options->context);

  SyntaxProfile *syntax_profile = analyze_syntax(program, path);
  codegen_set_syntax_mode(&cg, syntax_profile);
  if (syntax_profile) {
    syntax_profile_free(syntax_profile);
  }

  /* Enable profile instrumentation if --profile flag is set */
  if (options->collect_profile) {
    codegen_enable_profiling(&cg);
  }

  if (codegen_emit(&cg, program)) {
    fprintf(stderr, "Codegen failed: %s\n", cg.error_msg);
    codegen_free(&cg);
    ast_free(program);
    profile_free(&profile);
    return QISC_ERROR_SYNTAX;
  }

  if (profile.function_count > 0) {
    LivingIR *living = living_ir_create(cg.mod, &profile);
    if (living) {
      living_ir_evolve(living);
      living_ir_destroy(living);
    }
  }

  /* Compute IR hash for convergence detection */
  if (ir_hash) {
    *ir_hash = ir_hash_module(cg.mod);
  }

  bool link_succeeded = false;
  {
    char bin_path[PATH_MAX];
    char llvm_profraw_path[PATH_MAX];
    char llvm_profdata_path[PATH_MAX];
    int link_ret;

    qisc_llvm_profile_paths_from_source(path, llvm_profraw_path,
                                        sizeof(llvm_profraw_path),
                                        llvm_profdata_path,
                                        sizeof(llvm_profdata_path), NULL, 0);
    link_ret = qisc_emit_and_link_binary(&cg, path, options, bin_path,
                                         sizeof(bin_path), true,
                                         &link_succeeded);
    if (link_ret == 0 && link_succeeded) {
      clock_t end_time = clock();
      double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;

      qisc_personality_print(options->personality,
                             "  → Binary: %s (%.2fs)\n", bin_path, elapsed);

      if (options->collect_profile && options->converge) {
        if (qisc_execute_binary(bin_path, runtime_profile_path,
                                options->llvm_pgo_generate
                                    ? llvm_profraw_path
                                    : NULL,
                                true) == 0) {
          qisc_merge_runtime_profile(&profile, runtime_profile_path);
          remove(runtime_profile_path);
          if (options->llvm_pgo_generate) {
            qisc_merge_llvm_profile(llvm_profraw_path, llvm_profdata_path, true);
            remove(llvm_profraw_path);
          }
        }
      }
    }
  }

  /* Save profile if --profile */
  if (!link_succeeded) {
    codegen_free(&cg);
    ast_free(program);
    profile_free(&profile);
    return QISC_ERROR_INTERNAL;
  }

  if (options->collect_profile) {
    if (!profile.source_file) {
      profile.source_file = strdup(path);
    }
    if (qisc_profile_has_samples(&profile)) {
      profile_finalize(&profile);
      if (ir_hash) {
        profile_set_ir_hash(&profile, *ir_hash);
      }
      profile_save(&profile, profile_path);
    }
  }

  /* Cleanup */
  codegen_free(&cg);
  ast_free(program);
  profile_free(&profile);

  return QISC_OK;
}

/* Run a file - compile and execute */
int qisc_run_file(const char *path, QiscOptions *options) {
  if (options->collect_profile || options->use_profile || options->converge ||
      qisc_should_use_clang_ir_backend(options)) {
    QiscResult result = qisc_compile_file(path, options);
    if (result != QISC_OK) {
      return 1;
    }

    char bin_path[512];
    char profile_path[512];
    char runtime_profile_path[576];
    char llvm_profraw_path[PATH_MAX];
    char llvm_profdata_path[PATH_MAX];
    qisc_binary_path_from_source(path, bin_path, sizeof(bin_path));
    qisc_profile_paths_from_source(path, profile_path, sizeof(profile_path),
                                   runtime_profile_path,
                                   sizeof(runtime_profile_path));
    qisc_llvm_profile_paths_from_source(path, llvm_profraw_path,
                                        sizeof(llvm_profraw_path),
                                        llvm_profdata_path,
                                        sizeof(llvm_profdata_path), NULL, 0);

    int ret = qisc_execute_binary(bin_path,
                                  options->collect_profile
                                      ? runtime_profile_path
                                      : NULL,
                                  options->llvm_pgo_generate
                                      ? llvm_profraw_path
                                      : NULL,
                                  false);
    if (ret != 0) {
      return 1;
    }

    if (options->collect_profile) {
      QiscProfile profile;
      profile_init(&profile);

      if (options->use_profile) {
        profile_load(&profile, profile_path);
      }

      if (qisc_merge_runtime_profile(&profile, runtime_profile_path) == 0) {
        profile_set_ir_hash(&profile, profile.ir_hash);
        if (profile.source_file == NULL) {
          profile.source_file = strdup(path);
        }
        profile_save(&profile, profile_path);
      }

      profile_free(&profile);
      remove(runtime_profile_path);
    }

    if (options->llvm_pgo_generate) {
      const char *profdata_path = options->llvm_profile_path
                                      ? options->llvm_profile_path
                                      : llvm_profdata_path;
      if (qisc_merge_llvm_profile(llvm_profraw_path, profdata_path, false) == 0) {
        qisc_personality_print(options->personality,
                               "LLVM profdata saved to %s\n", profdata_path);
      }
      remove(llvm_profraw_path);
    }

    return 0;
  }

  qisc_personality_print(options->personality, "Compiling %s...\n", path);

  char *parse_error_path = NULL;
  char parse_error[512] = {0};
  AstNode *program = qisc_parse_program_with_imports(path, &parse_error_path,
                                                     parse_error,
                                                     sizeof(parse_error));
  if (!program) {
    qisc_personality_print(options->personality,
                           "Error: %s: %s\n",
                           parse_error_path ? parse_error_path : path,
                           parse_error[0] ? parse_error : "parse failed");
    free(parse_error_path);
    return 1;
  }

  /* Type checking pass */
  TypeChecker tc;
  typechecker_init(&tc);
  typecheck(&tc, program);
  int type_errors = tc.error_count;
  typechecker_report(&tc);
  if (type_errors > 0) {
    ast_free(program);
    return 1;
  }

  /* Initialize interpreter */
  Interpreter interp;
  interpreter_init(&interp);

  /* Run program */
  Value result = interpreter_run(&interp, program);

  if (interp.had_error) {
    qisc_personality_print(options->personality, "Error: Runtime error: %s\n",
                           interp.error_message);
    interpreter_free(&interp);
    ast_free(program);
    return 1;
  }

  /* Print result if not none */
  if (result.type != VAL_NONE) {
    printf("=> ");
    value_print(&result);
    printf("\n");
  }

  qisc_personality_print(options->personality, "Successfully ran %s\n", path);

  /* Cleanup */
  interpreter_free(&interp);
  ast_free(program);

  return 0;
}

QiscResult qisc_compile_string(const char *source, QiscOptions *options) {
  (void)source;
  (void)options;
  /* TODO: Implement string compilation */
  return QISC_OK;
}
