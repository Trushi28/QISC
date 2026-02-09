/*
 * QISC Personality System
 *
 * Makes compilation enjoyable with contextual messages,
 * achievements, and Easter eggs.
 */

#ifndef QISC_PERSONALITY_H
#define QISC_PERSONALITY_H

#include "qisc.h"
#include <stdarg.h>

/* Print with personality */
void qisc_personality_print(QiscPersonality personality, const char *format,
                            ...);

/* Specific message types */
void qisc_msg_compiling(QiscPersonality p, const char *filename);
void qisc_msg_success(QiscPersonality p, double elapsed_seconds);
void qisc_msg_error(QiscPersonality p, const char *error);
void qisc_msg_warning(QiscPersonality p, const char *warning);
void qisc_msg_convergence(QiscPersonality p, int iterations, double speedup);
void qisc_msg_progress(QiscPersonality p, int percent, const char *phase);

/* Achievement system */
void qisc_achievement_unlock(const char *id, const char *name,
                             const char *description);
void qisc_achievement_check(void);

#endif /* QISC_PERSONALITY_H */
