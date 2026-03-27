/*
 * QISC Easter Egg System
 *
 * Injects fun, memorable messages into assembly output.
 * These are the messages developers find when examining compiled code.
 */

#ifndef QISC_EASTER_EGGS_H
#define QISC_EASTER_EGGS_H

#include "qisc.h"
#include <llvm-c/Core.h>
#include <stdbool.h>
#include <stddef.h>

/* Easter egg location in generated code */
typedef enum {
  EGG_LOC_PROLOGUE, /* Function entry */
  EGG_LOC_EPILOGUE, /* Function exit */
  EGG_LOC_LOOP,     /* Loop constructs */
  EGG_LOC_BRANCH,   /* Conditional branches */
  EGG_LOC_RANDOM,   /* Random placement */
} EggLocation;

/* Easter egg definition */
typedef struct {
  const char *trigger;  /* Code pattern that triggers (function name, etc.) */
  const char *message;  /* Easter egg message */
  EggLocation location; /* Where to inject */
} EasterEgg;

/* ======== ASCII Art Logo ======== */

extern const char *easter_egg_qisc_logo;

/* ======== ROT13 Hidden Messages ======== */

extern const char *easter_egg_rot13_messages[];
extern const size_t easter_egg_rot13_count;

/* Decode ROT13 string (caller must free result) */
char *easter_egg_rot13_decode(const char *encoded);

/* ======== API Functions ======== */

/* Initialize Easter egg system (seeds RNG, etc.) */
void easter_eggs_init(void);

/* Get Easter egg for a specific function name */
const char *easter_egg_for_function(const char *func_name);

/* Get a random Easter egg */
const char *easter_egg_random(void);

/* Get time-based Easter egg (NULL if none applies) */
const char *easter_egg_time_based(void);

/* Get day-of-week Easter egg (NULL if none applies) */
const char *easter_egg_day_based(void);

/* Get compilation count Easter egg (NULL if none applies) */
const char *easter_egg_compile_count(int count);

/* ======== LLVM Integration ======== */

/* Inject Easter eggs into LLVM module
 * cryptic_mode: if true, use obfuscated/mysterious messages */
void easter_eggs_inject(LLVMModuleRef module, bool cryptic_mode);

/* Add Easter egg as module-level comment */
void easter_egg_add_module_comment(LLVMModuleRef module, const char *comment);

/* Add Easter egg as inline assembly comment in a function */
void easter_egg_add_inline_comment(LLVMBuilderRef builder, const char *comment);

/* ======== Binary Easter Eggs ======== */

/* Write Easter egg to specific offset in compiled binary */
void easter_egg_in_binary(const char *output_path);

/* Generate custom ELF section with Easter eggs
 * Returns allocated section data (caller must free) */
char *easter_generate_binary_section(size_t *out_size);

/* Embed ASCII art into binary section */
char *easter_embed_ascii_art(const char *art_name, size_t *out_size);

/* ROT13 and other encoding utilities */
char *easter_encode_secret(const char *message, const char *method);

/* Add custom section to LLVM module for binary Easter eggs */
void easter_add_elf_section(LLVMModuleRef module, const char *section_name,
                            const char *data, size_t size);

/* Personality-aware binary Easter egg injection */
void easter_inject_binary_eggs(LLVMModuleRef module, int personality_mood);

/* Achievement unlock codes embedded in binary */
const char *easter_get_achievement_code(const char *achievement_id);

/* Get all built-in Easter eggs */
const EasterEgg *easter_eggs_get_builtins(size_t *count);

/* Check if Easter eggs are enabled (can be toggled via env var) */
bool easter_eggs_enabled(void);

/* Statistics */
typedef struct {
  int total_injected;
  int logo_injected;
  int time_based_injected;
  int function_based_injected;
  int random_injected;
} EasterEggStats;

/* Get injection statistics for current compilation */
EasterEggStats easter_eggs_get_stats(void);

/* Reset statistics */
void easter_eggs_reset_stats(void);

/* ==========================================================================
 * Cryptic Mode Easter Egg Integration
 * ========================================================================== */

/* Cryptic mode specific eggs for assembly injection */
typedef struct {
    const char *rot13_message;   /* ROT13 encoded message */
    const char *decoded;         /* Decoded form (for reference) */
} CrypticEgg;

/* Get a cryptic Easter egg for function injection */
const char *easter_egg_cryptic_for_function(const char *func_name);

/* Get a cryptic assembly comment */
const char *easter_egg_cryptic_asm_comment(void);

/* Get a cryptic ROT13 message (encoded) */
const char *easter_egg_cryptic_rot13(void);

/* Inject cryptic mode eggs into LLVM module */
void easter_eggs_inject_cryptic(LLVMModuleRef module);

/* Get cryptic debug symbol name */
const char *easter_egg_cryptic_symbol(int index);

#endif /* QISC_EASTER_EGGS_H */
