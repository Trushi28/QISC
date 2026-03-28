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
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#ifdef _WIN32
#include <direct.h>
#include <process.h>
#ifndef PATH_MAX
#define PATH_MAX MAX_PATH
#endif
#else
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
static int qisc_execute_binary(const char *bin_path, const char *profile_out,
                               bool quiet);
static int qisc_merge_runtime_profile(QiscProfile *profile,
                                      const char *runtime_path);

typedef struct {
  char **paths;
  int count;
  int capacity;
} ImportSet;

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

static int qisc_execute_binary(const char *bin_path, const char *profile_out,
                               bool quiet) {
#ifdef _WIN32
  intptr_t status;
  char *old_profile = NULL;
  const char *argv[] = {bin_path, NULL};

  (void)quiet;

  if (profile_out && *profile_out) {
    const char *existing = getenv("QISC_PROFILE_OUT");
    if (existing) {
      old_profile = strdup(existing);
    }
    _putenv_s("QISC_PROFILE_OUT", profile_out);
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

  return status == -1 ? 1 : 0;
#else
  char cmd[2048];
  int status;

  if (profile_out && *profile_out) {
    snprintf(cmd, sizeof(cmd), "QISC_PROFILE_OUT='%s' '%s'%s", profile_out,
             bin_path, quiet ? " >/dev/null 2>/dev/null" : "");
  } else {
    snprintf(cmd, sizeof(cmd), "'%s'%s", bin_path,
             quiet ? " >/dev/null 2>/dev/null" : "");
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

  uint64_t current_ir_hash = ir_hash_module(cg.mod);

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

    /* Always link I/O runtime for stdin/stdout helpers */
    char io_path[512];
    snprintf(io_path, sizeof(io_path), "lib/qisc_io.o");
    FILE *iof = fopen(io_path, "r");
    if (!iof) {
      snprintf(io_path, sizeof(io_path), "%s/../lib/qisc_io.o",
               "/home/Trushi/ai/QISC");
    } else {
      fclose(iof);
    }
    {
      char temp[1024];
      snprintf(temp, sizeof(temp), "%s %s", runtime_libs, io_path);
      strncpy(runtime_libs, temp, sizeof(runtime_libs) - 1);
    }

    /* Always link stream runtime for explicit lazy streams */
    char stream_path[512];
    snprintf(stream_path, sizeof(stream_path), "lib/qisc_stream.o");
    FILE *sf = fopen(stream_path, "r");
    if (!sf) {
      snprintf(stream_path, sizeof(stream_path), "%s/../lib/qisc_stream.o",
               "/home/Trushi/ai/QISC");
    } else {
      fclose(sf);
    }
    {
      char temp[1024];
      snprintf(temp, sizeof(temp), "%s %s", runtime_libs, stream_path);
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

      if (options->collect_profile && options->converge) {
        if (qisc_execute_binary(bin_path, runtime_profile_path, true) == 0) {
          qisc_merge_runtime_profile(&profile, runtime_profile_path);
          remove(runtime_profile_path);
        }
      }
    } else {
      fprintf(stderr, "Linking failed\n");
    }
    /* Remove object file */
    remove(obj_path);
  }

  /* Save profile if --profile */
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
      living_ir_analyze(living);
      living_ir_evolve(living);
      living_ir_destroy(living);
    }
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

    /* Always link I/O runtime for stdin/stdout helpers */
    char io_path[512];
    snprintf(io_path, sizeof(io_path), "lib/qisc_io.o");
    FILE *iof = fopen(io_path, "r");
    if (!iof) {
      snprintf(io_path, sizeof(io_path), "%s/../lib/qisc_io.o",
               "/home/Trushi/ai/QISC");
    } else {
      fclose(iof);
    }
    {
      char temp[1024];
      snprintf(temp, sizeof(temp), "%s %s", runtime_libs, io_path);
      strncpy(runtime_libs, temp, sizeof(runtime_libs) - 1);
    }

    /* Always link stream runtime for explicit lazy streams */
    char stream_path[512];
    snprintf(stream_path, sizeof(stream_path), "lib/qisc_stream.o");
    FILE *sf = fopen(stream_path, "r");
    if (!sf) {
      snprintf(stream_path, sizeof(stream_path), "%s/../lib/qisc_stream.o",
               "/home/Trushi/ai/QISC");
    } else {
      fclose(sf);
    }
    {
      char temp[1024];
      snprintf(temp, sizeof(temp), "%s %s", runtime_libs, stream_path);
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

      if (options->collect_profile && options->converge) {
        if (qisc_execute_binary(bin_path, runtime_profile_path, true) == 0) {
          qisc_merge_runtime_profile(&profile, runtime_profile_path);
          remove(runtime_profile_path);
        }
      }
    }
    /* Remove object file */
    remove(obj_path);
  }

  /* Save profile if --profile */
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
  if (options->collect_profile || options->use_profile || options->converge) {
    QiscResult result = qisc_compile_file(path, options);
    if (result != QISC_OK) {
      return 1;
    }

    char bin_path[512];
    char profile_path[512];
    char runtime_profile_path[576];
    qisc_binary_path_from_source(path, bin_path, sizeof(bin_path));
    qisc_profile_paths_from_source(path, profile_path, sizeof(profile_path),
                                   runtime_profile_path,
                                   sizeof(runtime_profile_path));

    int ret = qisc_execute_binary(bin_path,
                                  options->collect_profile
                                      ? runtime_profile_path
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
