/*
 * QISC CLI Implementation
 */

#include "cli.h"
#include "../codegen/codegen.h"
#include "../interpreter/interpreter.h"
#include "../lexer/lexer.h"
#include "../parser/parser.h"
#include "../personality/personality.h"
#include "../typechecker/typechecker.h"
#include "../utils/utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
  printf("  build     Compile a .qisc file\n");
  printf("  run       Compile and immediately run\n");
  printf("  version   Show version information\n");
  printf("  help      Show this help message\n\n");
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

  default:
    qisc_cli_help();
    return 1;
  }
}

/* Compile a file */
QiscResult qisc_compile_file(const char *path, QiscOptions *options) {
  (void)options;

  /* Read source file */
  char *source = qisc_read_file(path);
  if (!source) {
    fprintf(stderr, "Could not read file: %s\n", path);
    return QISC_ERROR_FILE_NOT_FOUND;
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
    return QISC_ERROR_SYNTAX;
  }

  /* Type check */
  TypeChecker tc;
  typechecker_init(&tc);
  typecheck(&tc, program);
  typechecker_report(&tc);

  /* LLVM Codegen */
  Codegen cg;
  codegen_init(&cg, path);

  if (codegen_emit(&cg, program)) {
    fprintf(stderr, "Codegen failed: %s\n", cg.error_msg);
    codegen_free(&cg);
    ast_free(program);
    free(source);
    return QISC_ERROR_SYNTAX;
  }

  /* Dump IR to stdout */
  printf("=== LLVM IR ===\n");
  codegen_dump_ir(&cg);

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

    char link_cmd[1024];
    snprintf(link_cmd, sizeof(link_cmd), "cc %s -o %s -lm", obj_path, bin_path);
    printf("Linking: %s\n", link_cmd);
    int ret = system(link_cmd);
    if (ret == 0) {
      printf("Binary written to: %s\n", bin_path);
    } else {
      fprintf(stderr, "Linking failed\n");
    }
    /* Remove object file */
    remove(obj_path);
  }

  /* Cleanup */
  codegen_free(&cg);
  ast_free(program);
  free(source);

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
