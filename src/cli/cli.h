/*
 * QISC CLI Handler
 */

#ifndef QISC_CLI_H
#define QISC_CLI_H

#include "../include/qisc.h"

/* CLI commands */
typedef enum {
  CLI_CMD_NONE,
  CLI_CMD_BUILD,
  CLI_CMD_RUN,
  CLI_CMD_VERSION,
  CLI_CMD_HELP,
} CliCommand;

/* Parsed CLI arguments */
typedef struct {
  CliCommand command;
  const char *input_file;
  const char *output_file;
  QiscOptions options;
} CliArgs;

/* Run CLI with arguments */
int qisc_cli_run(int argc, char **argv);

/* Parse arguments */
CliArgs qisc_cli_parse(int argc, char **argv);

/* Print help */
void qisc_cli_help(void);

/* Print version */
void qisc_cli_version(void);

#endif /* QISC_CLI_H */
