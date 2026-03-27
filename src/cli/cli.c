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
#include "../profile/profile.h"
#include "../typechecker/typechecker.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* Forward declarations */
static QiscResult qisc_compile_file_with_hash(const char *path, QiscOptions *options, uint64_t *ir_hash);

/* Global achievement registry */
static AchievementRegistry g_achievements;
static bool g_achievements_initialized = false;

static void ensure_achievements_initialized(void) {
  if (!g_achievements_initialized) {
    achievements_init(&g_achievements);
    g_achievements_initialized = true;
  }
}

/* Default options */
QiscOptions qisc_default_options(void) {
  QiscOptions opts = {
      .context = QISC_CONTEXT_CLI,
      .personality = QISC_PERSONALITY_FRIENDLY,
      .collect_profile = false,
      .use_profile = false,
      .converge = false,
      .profile_path = NULL,
      .optimization_level = 2,
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
  printf("  achievements Show unlocked achievements\n");
  printf("  version      Show version information\n");
  printf("  help         Show this help message\n\n");
  printf("Build Options:\n");
  printf("  --profile         Collect profile data during execution\n");
  printf("  --use-profile <f> Use profile file for optimization\n");
  printf("  --converge        Compile until convergence\n");
  printf(
      "  --context <ctx>   Set context (cli|server|web|notebook|embedded)\n");
  printf("  --personality <p> Set personality "
         "(off|minimal|friendly|snarky|sage|cryptic)\n");
  printf("  -o <file>         Output file name\n");
  printf("  -O<n>             Optimization level (0-3)\n\n");
  printf("Examples:\n");
  printf("  qisc build hello.qisc\n");
  printf("  qisc run hello.qisc\n");
  printf("  qisc build --profile app.qisc\n");
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
        
        /* Update convergence metrics with new IR hash */
        bool converged = convergence_update(&metrics, ir_hash);
        
        qisc_personality_print(args.options.personality,
                               "  Hash: 0x%016llx | Stability: %.1f%%\n",
                               (unsigned long long)ir_hash, metrics.stability * 100.0);
        
        if (converged) {
          qisc_personality_print(args.options.personality,
                                 "\n┌─────────────────────────────────────────────────┐\n"
                                 "│     🎯 CONVERGENCE ACHIEVED! 🎯                 │\n"
                                 "│                                                 │\n"
                                 "│ IR hash stabilized - optimal form reached       │\n"
                                 "│ Iterations: %-36d│\n"
                                 "│ Final Hash: 0x%016llx             │\n"
                                 "│ Status: OPTIMAL                                 │\n"
                                 "└─────────────────────────────────────────────────┘\n",
                                 metrics.iterations,
                                 (unsigned long long)metrics.current_hash);
          break;
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
  char *source = qisc_read_file(path);
  if (!source) {
    fprintf(stderr, "Could not read file: %s\n", path);
    return QISC_ERROR_FILE_NOT_FOUND;
  }

  /* Load existing profile if --use-profile */
  QiscProfile profile;
  profile_init(&profile);
  char profile_path[512];
  snprintf(profile_path, sizeof(profile_path), "%s.profile", path);
  
  if (options->use_profile) {
    const char *ppath = options->profile_path ? options->profile_path : profile_path;
    if (profile_load(&profile, ppath) == 0) {
      qisc_personality_print(options->personality,
                             "Loaded profile data from %s\n", ppath);
      profile_print_summary(&profile);
    }
  }

  /* Initialize lexer */
  Lexer lexer;
  lexer_init(&lexer, source);

  /* Initialize parser */
  Parser parser;
  parser_init(&parser, &lexer);

  /* Parse program */
  AstNode *program = parser_parse(&parser);

  if (parser.had_error) {
    free(source);
    if (program)
      ast_free(program);
    profile_free(&profile);
    return QISC_ERROR_SYNTAX;
  }

  /* Type check */
  TypeChecker tc;
  typechecker_init(&tc);
  typecheck(&tc, program);
  typechecker_report(&tc);

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
    free(source);
    profile_free(&profile);
    return QISC_ERROR_SYNTAX;
  }

  /* ========== LIVING IR OPTIMIZATION (Post-Codegen) ========== */
  /* Apply profile-driven IR mutations if we have profile data */
  if (profile.function_count > 0) {
    LivingIR *living = living_ir_create(cg.mod, &profile);
    if (living) {
      printf("\n🧬 Living IR Evolution:\n");
      
      /* Analyze the IR based on profile data */
      living_ir_analyze(living);
      
      /* Apply profile-driven mutations */
      living_ir_evolve(living);
      
      /* Print summary of what was done */
      living_ir_print_summary(living);
      
      living_ir_destroy(living);
    }
  }
  /* ========== END LIVING IR ========== */

  /* Dump IR to stdout (only if not converging) */
  if (!options->converge) {
    printf("=== LLVM IR ===\n");
    codegen_dump_ir(&cg);
  }

  /* Write object file */
  char obj_path[512];
  snprintf(obj_path, sizeof(obj_path), "%s.o", path);
  if (codegen_write_object(&cg, obj_path) == 0) {
    /* Link to binary */
    char bin_path[512];
    /* Strip .qisc extension */
    strncpy(bin_path, path, sizeof(bin_path) - 1);
    char *dot = strrchr(bin_path, '.');
    if (dot)
      *dot = '\0';

    char link_cmd[2048];
    char runtime_libs[1024] = "";
    
    /* Always link error handling runtime for try/catch/fail support */
    char error_path[512];
    snprintf(error_path, sizeof(error_path), "lib/qisc_error.o");
    FILE *ef = fopen(error_path, "r");
    if (!ef) {
      snprintf(error_path, sizeof(error_path), 
               "%s/../lib/qisc_error.o", 
               "/home/Trushi/ai/QISC");
    } else {
      fclose(ef);
    }
    snprintf(runtime_libs, sizeof(runtime_libs), "%s", error_path);
    
    /* Always link array runtime for length tracking */
    char array_path[512];
    snprintf(array_path, sizeof(array_path), "lib/qisc_array.o");
    FILE *af = fopen(array_path, "r");
    if (!af) {
      snprintf(array_path, sizeof(array_path), 
               "%s/../lib/qisc_array.o", 
               "/home/Trushi/ai/QISC");
    } else {
      fclose(af);
    }
    {
      char temp[1024];
      snprintf(temp, sizeof(temp), "%s %s", runtime_libs, array_path);
      strncpy(runtime_libs, temp, sizeof(runtime_libs) - 1);
    }
    
    /* If profiling is enabled, also link with the profiling runtime library */
    if (options->collect_profile) {
      /* Get path to runtime library relative to executable */
      char runtime_path[512];
      snprintf(runtime_path, sizeof(runtime_path), "lib/qisc_runtime.o");
      
      /* Check if runtime exists in ./lib/, if not try the source tree */
      FILE *f = fopen(runtime_path, "r");
      if (!f) {
        /* Fall back to source tree path */
        snprintf(runtime_path, sizeof(runtime_path), 
                 "%s/../lib/qisc_runtime.o", 
                 "/home/Trushi/ai/QISC");
      } else {
        fclose(f);
      }
      
      char temp[1024];
      snprintf(temp, sizeof(temp), "%s %s", runtime_libs, runtime_path);
      strncpy(runtime_libs, temp, sizeof(runtime_libs) - 1);
    }
    
    snprintf(link_cmd, sizeof(link_cmd), "cc %s %s -o %s -lm", 
             obj_path, runtime_libs, bin_path);
    
    if (!options->converge) {
      printf("Linking: %s\n", link_cmd);
    }
    
    int ret = system(link_cmd);
    if (ret == 0) {
      clock_t end_time = clock();
      double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;
      double elapsed_ms = elapsed * 1000.0;
      
      qisc_personality_print(options->personality,
                             "Binary written to: %s (%.2fs)\n", bin_path, elapsed);
      
      /* Track achievements */
      ensure_achievements_initialized();
      achievements_record_compilation(&g_achievements, true, elapsed_ms, 0, 0);
      achievements_check(&g_achievements, options->personality);
    } else {
      fprintf(stderr, "Linking failed\n");
    }
    /* Remove object file */
    remove(obj_path);
  }

  /* Save profile if --profile */
  if (options->collect_profile) {
    profile.source_file = strdup(path);
    profile_finalize(&profile);
    if (profile_save(&profile, profile_path) == 0) {
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
  free(source);
  profile_free(&profile);

  return QISC_OK;
}

/* Compile a file and return IR hash for convergence detection */
static QiscResult qisc_compile_file_with_hash(const char *path, QiscOptions *options, uint64_t *ir_hash) {
  clock_t start_time = clock();
  
  /* Initialize hash to 0 */
  if (ir_hash) *ir_hash = 0;

  /* Read source file */
  char *source = qisc_read_file(path);
  if (!source) {
    fprintf(stderr, "Could not read file: %s\n", path);
    return QISC_ERROR_FILE_NOT_FOUND;
  }

  /* Load existing profile if --use-profile */
  QiscProfile profile;
  profile_init(&profile);
  char profile_path[512];
  snprintf(profile_path, sizeof(profile_path), "%s.profile", path);
  
  if (options->use_profile) {
    const char *ppath = options->profile_path ? options->profile_path : profile_path;
    if (profile_load(&profile, ppath) == 0) {
      qisc_personality_print(options->personality,
                             "Loaded profile data from %s\n", ppath);
    }
  }

  /* Initialize lexer */
  Lexer lexer;
  lexer_init(&lexer, source);

  /* Initialize parser */
  Parser parser;
  parser_init(&parser, &lexer);

  /* Parse program */
  AstNode *program = parser_parse(&parser);

  if (parser.had_error) {
    free(source);
    if (program)
      ast_free(program);
    profile_free(&profile);
    return QISC_ERROR_SYNTAX;
  }

  /* Type check */
  TypeChecker tc;
  typechecker_init(&tc);
  typecheck(&tc, program);

  /* LLVM Codegen */
  Codegen cg;
  codegen_init(&cg, path);

  /* Enable profile instrumentation if --profile flag is set */
  if (options->collect_profile) {
    codegen_enable_profiling(&cg);
  }

  if (codegen_emit(&cg, program)) {
    fprintf(stderr, "Codegen failed: %s\n", cg.error_msg);
    codegen_free(&cg);
    ast_free(program);
    free(source);
    profile_free(&profile);
    return QISC_ERROR_SYNTAX;
  }

  /* Compute IR hash for convergence detection */
  if (ir_hash) {
    *ir_hash = ir_hash_module(cg.mod);
  }

  /* Write object file */
  char obj_path[512];
  snprintf(obj_path, sizeof(obj_path), "%s.o", path);
  if (codegen_write_object(&cg, obj_path) == 0) {
    /* Link to binary */
    char bin_path[512];
    /* Strip .qisc extension */
    strncpy(bin_path, path, sizeof(bin_path) - 1);
    char *dot = strrchr(bin_path, '.');
    if (dot)
      *dot = '\0';

    char link_cmd[2048];
    char runtime_libs[1024] = "";
    
    /* Always link error handling runtime for try/catch/fail support */
    char error_path[512];
    snprintf(error_path, sizeof(error_path), "lib/qisc_error.o");
    FILE *ef = fopen(error_path, "r");
    if (!ef) {
      snprintf(error_path, sizeof(error_path), 
               "%s/../lib/qisc_error.o", 
               "/home/Trushi/ai/QISC");
    } else {
      fclose(ef);
    }
    snprintf(runtime_libs, sizeof(runtime_libs), "%s", error_path);
    
    /* Always link array runtime for length tracking */
    char array_path[512];
    snprintf(array_path, sizeof(array_path), "lib/qisc_array.o");
    FILE *af = fopen(array_path, "r");
    if (!af) {
      snprintf(array_path, sizeof(array_path), 
               "%s/../lib/qisc_array.o", 
               "/home/Trushi/ai/QISC");
    } else {
      fclose(af);
    }
    {
      char temp[1024];
      snprintf(temp, sizeof(temp), "%s %s", runtime_libs, array_path);
      strncpy(runtime_libs, temp, sizeof(runtime_libs) - 1);
    }
    
    /* If profiling is enabled, also link with the profiling runtime library */
    if (options->collect_profile) {
      /* Get path to runtime library relative to executable */
      char runtime_path[512];
      snprintf(runtime_path, sizeof(runtime_path), "lib/qisc_runtime.o");
      
      /* Check if runtime exists in ./lib/, if not try the source tree */
      FILE *f = fopen(runtime_path, "r");
      if (!f) {
        /* Fall back to source tree path */
        snprintf(runtime_path, sizeof(runtime_path), 
                 "%s/../lib/qisc_runtime.o", 
                 "/home/Trushi/ai/QISC");
      } else {
        fclose(f);
      }
      
      char temp[1024];
      snprintf(temp, sizeof(temp), "%s %s", runtime_libs, runtime_path);
      strncpy(runtime_libs, temp, sizeof(runtime_libs) - 1);
    }
    
    snprintf(link_cmd, sizeof(link_cmd), "cc %s %s -o %s -lm 2>/dev/null", 
             obj_path, runtime_libs, bin_path);
    
    int ret = system(link_cmd);
    if (ret == 0) {
      clock_t end_time = clock();
      double elapsed = (double)(end_time - start_time) / CLOCKS_PER_SEC;
      
      qisc_personality_print(options->personality,
                             "  → Binary: %s (%.2fs)\n", bin_path, elapsed);
    }
    /* Remove object file */
    remove(obj_path);
  }

  /* Save profile if --profile */
  if (options->collect_profile) {
    profile.source_file = strdup(path);
    profile_finalize(&profile);
    profile_save(&profile, profile_path);
  }

  /* Cleanup */
  codegen_free(&cg);
  ast_free(program);
  free(source);
  profile_free(&profile);

  return QISC_OK;
}

/* Run a file - compile and execute */
int qisc_run_file(const char *path, QiscOptions *options) {
  qisc_personality_print(options->personality, "Compiling %s...\n", path);

  /* Read source file */
  char *source = qisc_read_file(path);
  if (!source) {
    qisc_personality_print(options->personality,
                           "Error: Could not read file: %s\n", path);
    return 1;
  }

  /* Initialize lexer */
  Lexer lexer;
  lexer_init(&lexer, source);

  /* Initialize parser */
  Parser parser;
  parser_init(&parser, &lexer);

  /* Parse program */
  AstNode *program = parser_parse(&parser);

  if (parser.had_error) {
    qisc_personality_print(options->personality, "Error: Parse error: %s\n",
                           parser.error_message);
    free(source);
    if (program)
      ast_free(program);
    return 1;
  }

  /* Type checking pass (warnings only, doesn't block execution) */
  TypeChecker tc;
  typechecker_init(&tc);
  typecheck(&tc, program);
  typechecker_report(&tc);

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
    free(source);
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
  free(source);

  return 0;
}

QiscResult qisc_compile_string(const char *source, QiscOptions *options) {
  (void)source;
  (void)options;
  /* TODO: Implement string compilation */
  return QISC_OK;
}
