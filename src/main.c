/*
 * QISC Compiler - Main Entry Point
 */

#include "cli/cli.h"
#include "qisc.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) { return qisc_cli_run(argc, argv); }
