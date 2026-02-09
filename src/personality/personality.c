/*
 * QISC Personality System Implementation
 */

#include "personality.h"
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

/* ANSI color codes */
#define COLOR_RESET "\033[0m"
#define COLOR_RED "\033[31m"
#define COLOR_GREEN "\033[32m"
#define COLOR_YELLOW "\033[33m"
#define COLOR_BLUE "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN "\033[36m"
#define COLOR_BOLD "\033[1m"

/* Random sage quote */
static const char *sage_quotes[] = {
    "\"Premature optimization is the root of all evil\" - Knuth",
    "\"First, make it work. Then, make it right. Then, make it fast.\"",
    "\"The compiler knows more than you think, but less than you hope.\"",
    "\"Every line of code is a liability, every optimization a gift.\"",
    "\"In the pursuit of speed, do not forget correctness.\"",
};

static const char *snarky_comments[] = {
    "Let's see what we're working with here...",
    "Oh, this should be interesting.",
    "Compiling... and judging.",
    "I've seen things. I'm ready for anything.",
    "Another day, another compilation.",
};

static const char *friendly_messages[] = {
    "Let's build something great!",
    "Ready to compile!",
    "Here we go!",
    "Starting fresh!",
    "Excited to see your code!",
};

static const char *get_random_quote(const char **quotes, int count) {
  return quotes[rand() % count];
}

/* Print with personality */
void qisc_personality_print(QiscPersonality personality, const char *format,
                            ...) {
  va_list args;
  va_start(args, format);

  switch (personality) {
  case QISC_PERSONALITY_OFF:
  case QISC_PERSONALITY_MINIMAL:
    vprintf(format, args);
    break;

  case QISC_PERSONALITY_FRIENDLY:
    printf("%s", COLOR_CYAN);
    vprintf(format, args);
    printf("%s", COLOR_RESET);
    break;

  case QISC_PERSONALITY_SNARKY:
    printf("%s", COLOR_YELLOW);
    vprintf(format, args);
    printf("%s", COLOR_RESET);
    break;

  case QISC_PERSONALITY_SAGE:
    printf("%s", COLOR_MAGENTA);
    vprintf(format, args);
    printf("%s", COLOR_RESET);
    break;

  case QISC_PERSONALITY_CRYPTIC:
    printf("%s", COLOR_BLUE);
    vprintf(format, args);
    printf("%s", COLOR_RESET);
    break;
  }

  va_end(args);
}

/* Compiling message */
void qisc_msg_compiling(QiscPersonality p, const char *filename) {
  switch (p) {
  case QISC_PERSONALITY_OFF:
    break;
  case QISC_PERSONALITY_MINIMAL:
    printf("Compiling %s\n", filename);
    break;
  case QISC_PERSONALITY_FRIENDLY:
    printf("%s%s %s%s\n", COLOR_CYAN, get_random_quote(friendly_messages, 5),
           filename, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SNARKY:
    printf("%s%s%s\nCompiling: %s\n", COLOR_YELLOW,
           get_random_quote(snarky_comments, 5), COLOR_RESET, filename);
    break;
  case QISC_PERSONALITY_SAGE:
    printf("%s%s%s\n\nCompiling: %s\n", COLOR_MAGENTA,
           get_random_quote(sage_quotes, 5), COLOR_RESET, filename);
    break;
  case QISC_PERSONALITY_CRYPTIC:
    printf("%s[The compiler awakens...]%s\n", COLOR_BLUE, COLOR_RESET);
    printf("Target acquired: %s\n", filename);
    break;
  }
}

/* Success message */
void qisc_msg_success(QiscPersonality p, double elapsed_seconds) {
  switch (p) {
  case QISC_PERSONALITY_OFF:
    break;
  case QISC_PERSONALITY_MINIMAL:
    printf("Done (%.2fs)\n", elapsed_seconds);
    break;
  case QISC_PERSONALITY_FRIENDLY:
    printf("\n%s┌─────────────────────────────────────┐%s\n", COLOR_GREEN,
           COLOR_RESET);
    printf("%s│    🎉 Compilation Successful! 🎉    │%s\n", COLOR_GREEN,
           COLOR_RESET);
    printf("%s├─────────────────────────────────────┤%s\n", COLOR_GREEN,
           COLOR_RESET);
    printf("%s│  Time: %6.2f seconds                │%s\n", COLOR_GREEN,
           elapsed_seconds, COLOR_RESET);
    printf("%s│  Status: Ready to run!              │%s\n", COLOR_GREEN,
           COLOR_RESET);
    printf("%s└─────────────────────────────────────┘%s\n", COLOR_GREEN,
           COLOR_RESET);
    break;
  case QISC_PERSONALITY_SNARKY:
    if (elapsed_seconds < 1.0) {
      printf("\n%sDone in %.2fs. That was almost too easy.%s\n", COLOR_GREEN,
             elapsed_seconds, COLOR_RESET);
    } else if (elapsed_seconds < 5.0) {
      printf("\n%sFinished in %.2fs. Not bad, not bad.%s\n", COLOR_GREEN,
             elapsed_seconds, COLOR_RESET);
    } else {
      printf("\n%s%.2fs later... finally done. Your code is... special.%s\n",
             COLOR_YELLOW, elapsed_seconds, COLOR_RESET);
    }
    break;
  case QISC_PERSONALITY_SAGE:
    printf("\n%s\"The journey of optimization ends, for now.\"%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("Compilation complete in %.2f seconds.\n", elapsed_seconds);
    break;
  case QISC_PERSONALITY_CRYPTIC:
    printf("\n%s[Binary materialized from the void]%s\n", COLOR_BLUE,
           COLOR_RESET);
    printf("Time displacement: %.2fs\n", elapsed_seconds);
    break;
  }
}

/* Error message */
void qisc_msg_error(QiscPersonality p, const char *error) {
  switch (p) {
  case QISC_PERSONALITY_OFF:
  case QISC_PERSONALITY_MINIMAL:
    fprintf(stderr, "Error: %s\n", error);
    break;
  case QISC_PERSONALITY_FRIENDLY:
    fprintf(stderr, "\n%s❌ Oops! Something went wrong:%s\n", COLOR_RED,
            COLOR_RESET);
    fprintf(stderr, "   %s\n", error);
    fprintf(stderr, "%sDon't worry, we'll figure this out together!%s\n",
            COLOR_CYAN, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SNARKY:
    fprintf(stderr, "\n%sWell, that didn't work.%s\n", COLOR_RED, COLOR_RESET);
    fprintf(stderr, "Error: %s\n", error);
    fprintf(stderr, "%s(I'm not mad, just disappointed)%s\n", COLOR_YELLOW,
            COLOR_RESET);
    break;
  case QISC_PERSONALITY_SAGE:
    fprintf(stderr, "\n%s\"Even the wisest make mistakes.\"%s\n", COLOR_MAGENTA,
            COLOR_RESET);
    fprintf(stderr, "Error: %s\n", error);
    break;
  case QISC_PERSONALITY_CRYPTIC:
    fprintf(stderr, "\n%s[The void rejects your offering]%s\n", COLOR_RED,
            COLOR_RESET);
    fprintf(stderr, "Cause: %s\n", error);
    break;
  }
}

/* Convergence message */
void qisc_msg_convergence(QiscPersonality p, int iterations, double speedup) {
  switch (p) {
  case QISC_PERSONALITY_OFF:
    break;
  case QISC_PERSONALITY_MINIMAL:
    printf("Converged: %d iterations, %.1fx speedup\n", iterations, speedup);
    break;
  case QISC_PERSONALITY_FRIENDLY:
    printf("\n%s┌───────────────────────────────────────────┐%s\n", COLOR_GREEN,
           COLOR_RESET);
    printf("%s│       🎯 CONVERGENCE ACHIEVED! 🎯         │%s\n", COLOR_GREEN,
           COLOR_RESET);
    printf("%s├───────────────────────────────────────────┤%s\n", COLOR_GREEN,
           COLOR_RESET);
    printf("%s│  Iterations: %-5d                        │%s\n", COLOR_GREEN,
           iterations, COLOR_RESET);
    printf("%s│  Speedup:    %.1fx                         │%s\n", COLOR_GREEN,
           speedup, COLOR_RESET);
    printf("%s│  Status:     OPTIMAL                      │%s\n", COLOR_GREEN,
           COLOR_RESET);
    printf("%s└───────────────────────────────────────────┘%s\n", COLOR_GREEN,
           COLOR_RESET);
    break;
  case QISC_PERSONALITY_SNARKY:
    printf("\n%sConverged after %d iterations.%s\n", COLOR_GREEN, iterations,
           COLOR_RESET);
    if (speedup > 5.0) {
      printf("%.1fx faster. I'm impressed. Genuinely.\n", speedup);
    } else if (speedup > 2.0) {
      printf("%.1fx speedup. Not bad.\n", speedup);
    } else {
      printf("%.1fx speedup. Your code was already pretty good. Or terrible. "
             "Hard to tell.\n",
             speedup);
    }
    break;
  case QISC_PERSONALITY_SAGE:
    printf("\n%s\"After %d cycles, enlightenment is reached.\"%s\n",
           COLOR_MAGENTA, iterations, COLOR_RESET);
    printf("Performance multiplied %.1fx\n", speedup);
    break;
  case QISC_PERSONALITY_CRYPTIC:
    printf("\n%s[The compiler has reached its final form]%s\n", COLOR_BLUE,
           COLOR_RESET);
    printf("Dimensions collapsed: %d\n", iterations);
    printf("Reality amplification: %.1fx\n", speedup);
    break;
  }
}

/* Progress message */
void qisc_msg_progress(QiscPersonality p, int percent, const char *phase) {
  if (p == QISC_PERSONALITY_OFF)
    return;

  /* Simple progress bar */
  int filled = percent / 5;
  printf("\r[");
  for (int i = 0; i < 20; i++) {
    if (i < filled)
      printf("█");
    else
      printf("░");
  }
  printf("] %3d%% %s", percent, phase);
  fflush(stdout);

  if (percent == 100)
    printf("\n");
}

/* Achievement unlock */
void qisc_achievement_unlock(const char *id, const char *name,
                             const char *description) {
  (void)id; /* For tracking purposes */
  printf("\n%s🏆 Achievement Unlocked: \"%s\"%s\n", COLOR_YELLOW, name,
         COLOR_RESET);
  printf("   %s\n", description);
}

void qisc_achievement_check(void) {
  /* TODO: Implement achievement checking based on compilation stats */
}
