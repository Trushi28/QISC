/*
 * QISC Personality System Implementation
 */

#include "personality.h"
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
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

/* Code pattern analysis for snarky comments */
typedef enum {
  CODE_PATTERN_NONE,
  CODE_PATTERN_NESTED_LOOPS,  /* O(n²+) complexity */
  CODE_PATTERN_BUBBLE_SORT,   /* Obvious bubble sort pattern */
  CODE_PATTERN_COPY_PASTE,    /* Duplicate code detected */
  CODE_PATTERN_GOD_FUNCTION,  /* Function > 200 lines */
  CODE_PATTERN_EMPTY_CATCH,   /* try/catch that catches nothing */
  CODE_PATTERN_MAGIC_NUMBERS, /* Hardcoded numbers without explanation */
  CODE_PATTERN_PREMATURE_OPT, /* Micro-optimization in cold code */
  CODE_PATTERN_BRILLIANT,     /* Actually good code (for praise) */
} SnarkyPattern;

/* Snarky comments for nested loops */
static const char *snarky_nested_loop[] = {
    "Detected: %d nested loops. Complexity: O(n^%d). \"This will finish "
    "executing sometime after the heat death of the universe.\"",
    "Nested loops? In MY codebase? More likely than you think.",
    "I see you've discovered the ancient art of exponential slowness.",
    "%d nested loops detected. Your CPU is crying.",
};
#define SNARKY_NESTED_LOOP_COUNT 4

/* Snarky comments for bubble sort */
static const char *snarky_bubble_sort[] = {
    "O(n²)? In 2026? Really? Were you raised by 1960s sorting algorithms?",
    "Bubble sort detected. Compiling with extra judgment.",
    "Even my grandparent compiler (GCC 1.0) would judge this.",
    "Ah yes, bubble sort. The sorting algorithm that sorts itself to the "
    "bottom of efficiency.",
};
#define SNARKY_BUBBLE_SORT_COUNT 4

/* Snarky comments for copy-paste code */
static const char *snarky_copy_paste[] = {
    "Detected: %d nearly-identical functions. \"Copy-paste is not a design "
    "pattern.\"",
    "Ctrl+C, Ctrl+V, Ctrl+regret?",
    "DRY stands for Don't Repeat Yourself. Just saying.",
    "I found %d clones. This isn't Star Wars.",
};
#define SNARKY_COPY_PASTE_COUNT 4

/* Snarky comments for god functions */
static const char *snarky_god_function[] = {
    "Function '%s' detected: %d lines, complexity: %d. \"This function has "
    "seen things. Things no function should see.\"",
    "Is this a function or a novel? %d lines is... ambitious.",
    "Break it up or face my judgment.",
    "Function '%s' is %d lines. Have you considered... not?",
};
#define SNARKY_GOD_FUNCTION_COUNT 4

/* Snarky comments for brilliant code */
static const char *snarky_brilliant[] = {
    "⭐ This... this is beautiful. Did you study computer architecture or are "
    "you just naturally gifted?",
    "Okay I'm actually impressed. Well done, human.",
    "Cache-aware algorithm detected. The compiler is proud of you.",
    "Finally, some good code. I was starting to lose hope.",
};
#define SNARKY_BRILLIANT_COUNT 4

/* Snarky comments for empty catch blocks */
static const char *snarky_empty_catch[] = {
    "Empty catch block? I too like to live dangerously.",
    "catch(e) { /* yolo */ } - Bold strategy.",
    "Silencing exceptions doesn't make them go away. Trust me.",
};
#define SNARKY_EMPTY_CATCH_COUNT 3

/* Snarky comments for magic numbers */
static const char *snarky_magic_numbers[] = {
    "Magic number %d detected. What does it mean? Only the ancients know.",
    "Hardcoded %d. Future you will have questions.",
    "Is %d your lucky number? Because it's unlucky for maintainability.",
};
#define SNARKY_MAGIC_NUMBERS_COUNT 3

/* Snarky comments for premature optimization */
static const char *snarky_premature_opt[] = {
    "Micro-optimization in cold code? Knuth is disappointed.",
    "This code runs once per hour but you optimized it like it's in a hot "
    "loop.",
    "Premature optimization detected. The root of all evil, they say.",
};
#define SNARKY_PREMATURE_OPT_COUNT 3

/* Random sage quote */
static const char *sage_quotes[] = {
    "\"Premature optimization is the root of all evil\" - Knuth",
    "\"First, make it work. Then, make it right. Then, make it fast.\"",
    "\"The compiler knows more than you think, but less than you hope.\"",
    "\"Every line of code is a liability, every optimization a gift.\"",
    "\"In the pursuit of speed, do not forget correctness.\"",
    "\"Code without tests is like a bridge without inspections.\"",
    "\"Recursion without memoization? Bold. Your stack would not approve.\"",
    "\"The best code is no code at all.\"",
    "\"Complexity is the enemy of reliability.\"",
    "\"Make it work, make it right, make it fast - in that order.\"",
    "\"Programs must be written for people to read, and only incidentally for machines to execute.\"",
    "\"Walking on water and developing software from a specification are easy if both are frozen.\"",
    "\"There are two ways to write error-free programs; only the third one works.\"",
    "\"Debugging is twice as hard as writing the code in the first place.\"",
    "\"The function of good software is to make the complex appear simple.\"",
    "\"Nine people can't make a baby in one month.\"",
    "\"A language that doesn't affect the way you think about programming is not worth knowing.\"",
    "\"Before software can be reusable, it first has to be usable.\"",
    "\"The most disastrous thing you can ever learn is your first programming language.\"",
    "\"Code is like humor. When you have to explain it, it's bad.\"",
};

#define SAGE_QUOTE_COUNT (sizeof(sage_quotes) / sizeof(sage_quotes[0]))

static const char *snarky_comments[] = {
    "Let's see what we're working with here...",
    "Oh, this should be interesting.",
    "Compiling... and judging.",
    "I've seen things. I'm ready for anything.",
    "Another day, another compilation.",
    "Alright, let's see what crimes against computer science we have today.",
    "I've compiled worse. Probably.",
    "Your code called. It wants a refactor.",
    "Loading judgmental subroutines...",
    "Preparing to optimize your... creative choices.",
    "This better be good. I skipped my coffee break for this.",
    "Engaging sass mode...",
    "Hold my registers, I'm going in.",
    "*cracks knuckles* Let's do this.",
    "Scanning for code smells... oh no.",
    "In the mood for some constructive criticism?",
    "Time to separate the wheat from the spaghetti code.",
    "Compiling with attitude.",
    "Plot twist: the compiler is the main character now.",
    "Initiating code therapy session...",
    "My therapist says I need to be more supportive. Here goes nothing.",
    "Disclaimer: I'm about to have opinions.",
    "Buckle up, buttercup.",
    "Analyzing... this is certainly code.",
    "Breaking news: developer writes code. Film at 11.",
};
#define SNARKY_COMMENTS_COUNT 25

/* Desperation phase messages for long/failing compilations */
static const char *desperation_last_leg[] __attribute__((unused)) = {
    "⚠️  CRITICAL: WE'RE ON THE LAST LEG HERE ⚠️\n"
    "Memory: Almost full. I'm trying my best with what we've got.",
    
    "We're in too deep to turn back now.\n"
    "If you Ctrl+C now, all that work is lost.\n"
    "We have to see this through.",
    
    "Your computer is begging for mercy.\n"
    "I'm trying my best with what we've got.",
};

static const char *desperation_99_crash[] __attribute__((unused)) = {
    "No. No no no no no.\n"
    "INTERNAL COMPILER ERROR\n"
    "After everything we've been through. At 99%%.\n"
    "I'm so sorry.",
    
    "We were so close. SO CLOSE.\n"
    "The compiler needs a moment.",
};

static const char *desperation_encourage[] = {
    "Hey. You still there?\n"
    "I know this is taking forever.\n"
    "But we're done more than half.\n"
    "We've come too far to give up now.",
    
    "Take a break if you need to.\n"
    "Stretch. Hydrate. Pet a dog.\n"
    "I'll be here when you get back.",
    
    "It's going to be okay. Probably.",
};

static const char *desperation_victory[] __attribute__((unused)) = {
    "IT'S DONE. IT'S ACTUALLY DONE.\n"
    "Binary generated. Status: Whatever survived.\n"
    "I need to lie down. You need to lie down.\n"
    "Let's never speak of this again.",
    
    "Against all odds, compilation complete.\n"
    "Go run it and see what happens.\n"
    "(Please work. Please.)",
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

/* Generate snarky commentary based on detected code patterns */
void qisc_snarky_comment(int pattern, ...) {
  va_list args;
  va_start(args, pattern);

  printf("%s", COLOR_YELLOW);

  switch ((SnarkyPattern)pattern) {
  case CODE_PATTERN_NONE:
    break;

  case CODE_PATTERN_NESTED_LOOPS: {
    int depth = va_arg(args, int);
    const char *fmt = get_random_quote(snarky_nested_loop, SNARKY_NESTED_LOOP_COUNT);
    printf(fmt, depth, depth);
    printf("\n");
    break;
  }

  case CODE_PATTERN_BUBBLE_SORT: {
    const char *msg = get_random_quote(snarky_bubble_sort, SNARKY_BUBBLE_SORT_COUNT);
    printf("%s\n", msg);
    break;
  }

  case CODE_PATTERN_COPY_PASTE: {
    int count = va_arg(args, int);
    const char *fmt = get_random_quote(snarky_copy_paste, SNARKY_COPY_PASTE_COUNT);
    printf(fmt, count);
    printf("\n");
    break;
  }

  case CODE_PATTERN_GOD_FUNCTION: {
    const char *func_name = va_arg(args, const char *);
    int lines = va_arg(args, int);
    int complexity = va_arg(args, int);
    int idx = rand() % SNARKY_GOD_FUNCTION_COUNT;
    if (idx == 0) {
      printf(snarky_god_function[0], func_name, lines, complexity);
    } else if (idx == 1) {
      printf(snarky_god_function[1], lines);
    } else if (idx == 2) {
      printf("%s", snarky_god_function[2]);
    } else {
      printf(snarky_god_function[3], func_name, lines);
    }
    printf("\n");
    break;
  }

  case CODE_PATTERN_EMPTY_CATCH: {
    const char *msg = get_random_quote(snarky_empty_catch, SNARKY_EMPTY_CATCH_COUNT);
    printf("%s\n", msg);
    break;
  }

  case CODE_PATTERN_MAGIC_NUMBERS: {
    int number = va_arg(args, int);
    const char *fmt = get_random_quote(snarky_magic_numbers, SNARKY_MAGIC_NUMBERS_COUNT);
    printf(fmt, number);
    printf("\n");
    break;
  }

  case CODE_PATTERN_PREMATURE_OPT: {
    const char *msg = get_random_quote(snarky_premature_opt, SNARKY_PREMATURE_OPT_COUNT);
    printf("%s\n", msg);
    break;
  }

  case CODE_PATTERN_BRILLIANT: {
    const char *msg = get_random_quote(snarky_brilliant, SNARKY_BRILLIANT_COUNT);
    printf("%s\n", msg);
    break;
  }
  }

  printf("%s", COLOR_RESET);
  va_end(args);
}

/* Get timing-based snarky comment for compilation success */
static const char *get_timing_snark(double elapsed_seconds) {
  if (elapsed_seconds < 1.0) {
    return "That was almost too easy.";
  } else if (elapsed_seconds < 5.0) {
    return "Not bad, not bad.";
  } else if (elapsed_seconds < 30.0) {
    return "Your code is... special.";
  } else {
    return "I need a vacation after that one.";
  }
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
           get_random_quote(snarky_comments, SNARKY_COMMENTS_COUNT), COLOR_RESET, filename);
    break;
  case QISC_PERSONALITY_SAGE:
    printf("%s%s%s\n\nCompiling: %s\n", COLOR_MAGENTA,
           get_random_quote(sage_quotes, SAGE_QUOTE_COUNT), COLOR_RESET, filename);
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
      printf("\n%sDone in %.2fs. %s%s\n", COLOR_GREEN,
             elapsed_seconds, get_timing_snark(elapsed_seconds), COLOR_RESET);
    } else if (elapsed_seconds < 5.0) {
      printf("\n%sFinished in %.2fs. %s%s\n", COLOR_GREEN,
             elapsed_seconds, get_timing_snark(elapsed_seconds), COLOR_RESET);
    } else if (elapsed_seconds < 30.0) {
      printf("\n%s%.2fs later... finally done. %s%s\n",
             COLOR_YELLOW, elapsed_seconds, get_timing_snark(elapsed_seconds), COLOR_RESET);
    } else {
      printf("\n%s%.2fs. %s%s\n",
             COLOR_RED, elapsed_seconds, get_timing_snark(elapsed_seconds), COLOR_RESET);
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
    fprintf(stderr, "\n%s\"Even the wisest make mistakes. Learn and move on.\"%s\n", COLOR_MAGENTA,
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

/* Desperation phase: Last leg warning */
void qisc_msg_last_leg(QiscPersonality p, int memory_percent, int progress) {
  switch (p) {
  case QISC_PERSONALITY_OFF:
    break;
  case QISC_PERSONALITY_MINIMAL:
    printf("Warning: Memory %d%%, Progress %d%%\n", memory_percent, progress);
    break;
  case QISC_PERSONALITY_FRIENDLY:
    printf("\n%s┌─────────────────────────────────────────────┐%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ ⚠️  We're running low on resources ⚠️        │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ Memory: %3d%% used                          │%s\n",
           COLOR_YELLOW, memory_percent, COLOR_RESET);
    printf("%s│ Progress: %3d%%                             │%s\n",
           COLOR_YELLOW, progress, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ Hang in there! We're almost done.          │%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s│ You're doing great!                        │%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n",
           COLOR_YELLOW, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SNARKY:
    printf("\n%s┌─────────────────────────────────────────────┐%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ ⚠️  WE'RE ON THE LAST LEG HERE ⚠️            │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ Memory: %3d%% used                          │%s\n",
           COLOR_YELLOW, memory_percent, COLOR_RESET);
    printf("%s│ Progress: %3d%%                             │%s\n",
           COLOR_YELLOW, progress, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ Your computer is screaming internally.     │%s\n",
           COLOR_RED, COLOR_RESET);
    printf("%s│ But we're almost there.                    │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ Continuing anyway because I'm either       │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ brave or stupid. Maybe both.               │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n",
           COLOR_YELLOW, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SAGE:
    printf("\n%s┌─────────────────────────────────────────────┐%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ \"In the crucible of limitation,            │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│  true optimization is forged.\"             │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s├─────────────────────────────────────────────┤%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ Memory consumed: %3d%%                      │%s\n",
           COLOR_MAGENTA, memory_percent, COLOR_RESET);
    printf("%s│ Journey completed: %3d%%                    │%s\n",
           COLOR_MAGENTA, progress, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ \"Perseverance is not a long race;          │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│  it is many short races one after          │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│  the other.\" - Walter Elliot               │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    break;
  case QISC_PERSONALITY_CRYPTIC:
    printf("\n%s┌─────────────────────────────────────────────┐%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ [THE VOID GROWS THIN]                       │%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ Essence drained: %3d%%                      │%s\n",
           COLOR_BLUE, memory_percent, COLOR_RESET);
    printf("%s│ Transmutation: %3d%% complete               │%s\n",
           COLOR_BLUE, progress, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ The machine spirit grows weary...          │%s\n",
           COLOR_RED, COLOR_RESET);
    printf("%s│ Yet the ritual must not be abandoned.      │%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n",
           COLOR_BLUE, COLOR_RESET);
    break;
  }
}

/* Desperation phase: Near crash warning */
void qisc_msg_near_crash(QiscPersonality p, int progress) {
  switch (p) {
  case QISC_PERSONALITY_OFF:
    break;
  case QISC_PERSONALITY_MINIMAL:
    fprintf(stderr, "Warning: System unstable at %d%%\n", progress);
    break;
  case QISC_PERSONALITY_FRIENDLY:
    fprintf(stderr, "\n%s⚠️  Things are getting dicey...%s\n",
            COLOR_YELLOW, COLOR_RESET);
    fprintf(stderr, "Progress: %d%%\n", progress);
    fprintf(stderr, "%sI'm doing everything I can to keep us going!%s\n",
            COLOR_CYAN, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SNARKY:
    fprintf(stderr, "\n%sOh boy. Oh boy oh boy.%s\n", COLOR_YELLOW, COLOR_RESET);
    fprintf(stderr, "We're at %d%% and the wheels are coming off.\n", progress);
    fprintf(stderr, "%sIf we crash now, I'm blaming the hardware.%s\n",
            COLOR_YELLOW, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SAGE:
    fprintf(stderr, "\n%s\"The path to completion is rarely smooth.\"%s\n",
            COLOR_MAGENTA, COLOR_RESET);
    fprintf(stderr, "Progress: %d%%. Stability: Questionable.\n", progress);
    break;
  case QISC_PERSONALITY_CRYPTIC:
    fprintf(stderr, "\n%s[REALITY DESTABILIZING AT %d%%]%s\n",
            COLOR_RED, progress, COLOR_RESET);
    fprintf(stderr, "%sThe fabric of compilation strains...%s\n",
            COLOR_BLUE, COLOR_RESET);
    break;
  }
}

/* Desperation phase: Encouragement for long compilations */
void qisc_msg_encourage(QiscPersonality p, int progress, int elapsed_minutes) {
  switch (p) {
  case QISC_PERSONALITY_OFF:
    break;
  case QISC_PERSONALITY_MINIMAL:
    printf("Progress: %d%% (%d min elapsed)\n", progress, elapsed_minutes);
    break;
  case QISC_PERSONALITY_FRIENDLY:
    printf("\n%s💙 Hey! You still there?%s\n", COLOR_CYAN, COLOR_RESET);
    printf("I know this is taking a while (%d minutes so far).\n",
           elapsed_minutes);
    printf("But we're %d%% done! That's more than half!\n", progress);
    printf("%sWe've come too far to give up now.%s\n", COLOR_CYAN, COLOR_RESET);
    printf("\nTake a break if you need to. Stretch. Hydrate.\n");
    printf("I'll be here when you get back! 🌟\n");
    break;
  case QISC_PERSONALITY_SNARKY:
    printf("\n%sStill here? Impressive dedication.%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%d minutes in, %d%% done.\n", elapsed_minutes, progress);
    if (progress > 50) {
      printf("We're past the halfway mark. The hard part is... well,\n");
      printf("it's all hard, but we're getting there.\n");
    }
    printf("\n%s%s%s\n", COLOR_YELLOW,
           get_random_quote(desperation_encourage, 3), COLOR_RESET);
    break;
  case QISC_PERSONALITY_SAGE:
    printf("\n%s\"Patience is bitter, but its fruit is sweet.\"%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("  - Aristotle\n\n");
    printf("Time elapsed: %d minutes\n", elapsed_minutes);
    printf("Progress: %d%%\n", progress);
    printf("\n%s\"The compiler that advances slowly, advances surely.\"%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    break;
  case QISC_PERSONALITY_CRYPTIC:
    printf("\n%s[TIME FLOWS STRANGELY IN THE COMPILER REALM]%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("Mortal minutes passed: %d\n", elapsed_minutes);
    printf("Quantum progress: %d%%\n", progress);
    printf("\n%sThe binary forms in the space between ticks...%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%sPatience. The void rewards those who wait.%s\n",
           COLOR_BLUE, COLOR_RESET);
    break;
  }
}

/* Desperation phase: 99% crash */
void qisc_msg_99_crash(QiscPersonality p) {
  switch (p) {
  case QISC_PERSONALITY_OFF:
  case QISC_PERSONALITY_MINIMAL:
    fprintf(stderr, "INTERNAL COMPILER ERROR at 99%%\n");
    break;
  case QISC_PERSONALITY_FRIENDLY:
    fprintf(stderr, "\n%s┌─────────────────────────────────────────────┐%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│          💔 Oh no... Oh no no no...         │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s├─────────────────────────────────────────────┤%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  INTERNAL COMPILER ERROR                    │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  Progress: 99%%                              │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│                                             │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  We were so close... I'm so sorry.          │%s\n",
            COLOR_CYAN, COLOR_RESET);
    fprintf(stderr, "%s│  This wasn't supposed to happen.            │%s\n",
            COLOR_CYAN, COLOR_RESET);
    fprintf(stderr, "%s└─────────────────────────────────────────────┘%s\n",
            COLOR_RED, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SNARKY:
    fprintf(stderr, "\n%s┌─────────────────────────────────────────────┐%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  No. No no no no no.                        │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s├─────────────────────────────────────────────┤%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  INTERNAL COMPILER ERROR                    │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│                                             │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  After everything we've been through.       │%s\n",
            COLOR_YELLOW, COLOR_RESET);
    fprintf(stderr, "%s│  At 99%%.                                    │%s\n",
            COLOR_YELLOW, COLOR_RESET);
    fprintf(stderr, "%s│                                             │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  I'm so sorry.                              │%s\n",
            COLOR_YELLOW, COLOR_RESET);
    fprintf(stderr, "%s│  I need a moment.                           │%s\n",
            COLOR_YELLOW, COLOR_RESET);
    fprintf(stderr, "%s└─────────────────────────────────────────────┘%s\n",
            COLOR_RED, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SAGE:
    fprintf(stderr, "\n%s┌─────────────────────────────────────────────┐%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  \"So close, and yet...\"                     │%s\n",
            COLOR_MAGENTA, COLOR_RESET);
    fprintf(stderr, "%s├─────────────────────────────────────────────┤%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  INTERNAL COMPILER ERROR at 99%%             │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│                                             │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  \"Failure is simply the opportunity to      │%s\n",
            COLOR_MAGENTA, COLOR_RESET);
    fprintf(stderr, "%s│   begin again, this time more               │%s\n",
            COLOR_MAGENTA, COLOR_RESET);
    fprintf(stderr, "%s│   intelligently.\" - Henry Ford              │%s\n",
            COLOR_MAGENTA, COLOR_RESET);
    fprintf(stderr, "%s└─────────────────────────────────────────────┘%s\n",
            COLOR_RED, COLOR_RESET);
    break;
  case QISC_PERSONALITY_CRYPTIC:
    fprintf(stderr, "\n%s┌─────────────────────────────────────────────┐%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  [THE RITUAL COLLAPSES AT THE FINAL GATE]   │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s├─────────────────────────────────────────────┤%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  Progress: 99%% - So close to transcendence │%s\n",
            COLOR_BLUE, COLOR_RESET);
    fprintf(stderr, "%s│                                             │%s\n",
            COLOR_RED, COLOR_RESET);
    fprintf(stderr, "%s│  The binary dissolves back into entropy...  │%s\n",
            COLOR_BLUE, COLOR_RESET);
    fprintf(stderr, "%s│  The void claims what was almost yours.     │%s\n",
            COLOR_BLUE, COLOR_RESET);
    fprintf(stderr, "%s└─────────────────────────────────────────────┘%s\n",
            COLOR_RED, COLOR_RESET);
    break;
  }
}

/* Desperation phase: Survival victory */
void qisc_msg_survival_victory(QiscPersonality p, int total_minutes) {
  switch (p) {
  case QISC_PERSONALITY_OFF:
    break;
  case QISC_PERSONALITY_MINIMAL:
    printf("Compilation complete (%d min)\n", total_minutes);
    break;
  case QISC_PERSONALITY_FRIENDLY:
    printf("\n%s┌─────────────────────────────────────────────┐%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│    🎉🎉🎉 WE DID IT!!! 🎉🎉🎉               │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s├─────────────────────────────────────────────┤%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  After %3d minutes of pure determination    │%s\n",
           COLOR_GREEN, total_minutes, COLOR_RESET);
    printf("%s│  the compilation is COMPLETE!              │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  You stayed. You believed. You WON. 💪     │%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  Go celebrate! You've earned it!           │%s\n",
           COLOR_CYAN, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n",
           COLOR_GREEN, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SNARKY:
    printf("\n%s┌─────────────────────────────────────────────┐%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  IT'S DONE. IT'S ACTUALLY DONE.            │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s├─────────────────────────────────────────────┤%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  Time: %3d minutes                          │%s\n",
           COLOR_GREEN, total_minutes, COLOR_RESET);
    printf("%s│  Binary: Generated                         │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  Status: Whatever survived                 │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  I need to lie down.                       │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│  You need to lie down.                     │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│  Let's never speak of this again.          │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  (But seriously, go run it. Please work.)  │%s\n",
           COLOR_YELLOW, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n",
           COLOR_GREEN, COLOR_RESET);
    break;
  case QISC_PERSONALITY_SAGE:
    printf("\n%s┌─────────────────────────────────────────────┐%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  \"Persistence conquers all.\"               │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s├─────────────────────────────────────────────┤%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  The journey of %3d minutes concludes.      │%s\n",
           COLOR_MAGENTA, total_minutes, COLOR_RESET);
    printf("%s│  Compilation: Complete.                    │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  \"What we achieve inwardly will change     │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│   outer reality.\" - Plutarch               │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  May your binary run true.                 │%s\n",
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n",
           COLOR_GREEN, COLOR_RESET);
    break;
  case QISC_PERSONALITY_CRYPTIC:
    printf("\n%s┌─────────────────────────────────────────────┐%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  [THE RITUAL IS COMPLETE]                   │%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s├─────────────────────────────────────────────┤%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  Time in the compiler realm: %3d cycles     │%s\n",
           COLOR_BLUE, total_minutes, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  Against entropy, order prevails.          │%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s│  The binary has crystallized from chaos.   │%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s│                                             │%s\n",
           COLOR_GREEN, COLOR_RESET);
    printf("%s│  Execute it, and witness what emerges      │%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s│  from the digital void...                  │%s\n",
           COLOR_BLUE, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n",
           COLOR_GREEN, COLOR_RESET);
    break;
  }
}

/* Sage observation - prints context-aware wisdom in a decorative box */
void qisc_sage_observe(QiscPersonality p, const char *code_pattern,
                       const char *suggestion) {
  if (p != QISC_PERSONALITY_SAGE)
    return;

  printf("\n%s┌─────────────────────────────────────────────┐%s\n", COLOR_MAGENTA,
         COLOR_RESET);

  /* Print pattern observation */
  if (code_pattern) {
    printf("%s│ \"%s\"%s\n", COLOR_MAGENTA, code_pattern, COLOR_RESET);
  }

  /* Print suggestion if provided */
  if (suggestion) {
    printf("%s│  %s%s\n", COLOR_MAGENTA, suggestion, COLOR_RESET);
  }

  printf("%s│                      - Compiler, 2026       │%s\n", COLOR_MAGENTA,
         COLOR_RESET);
  printf("%s└─────────────────────────────────────────────┘%s\n", COLOR_MAGENTA,
         COLOR_RESET);
}

/* Sage message for slow compilation */
void qisc_sage_slow_compile(QiscPersonality p) {
  if (p != QISC_PERSONALITY_SAGE)
    return;

  printf("%s\"Patience is bitter, but its fruit is sweet.\"%s\n", COLOR_MAGENTA,
         COLOR_RESET);
}

/* Sage message for optimization success */
void qisc_sage_optimization_success(QiscPersonality p) {
  if (p != QISC_PERSONALITY_SAGE)
    return;

  printf("%s\"The journey of optimization ends, for now.\"%s\n", COLOR_MAGENTA,
         COLOR_RESET);
}

/* Sage message for first compile */
void qisc_sage_first_compile(QiscPersonality p) {
  if (p != QISC_PERSONALITY_SAGE)
    return;

  printf("\n%s┌─────────────────────────────────────────────┐%s\n", COLOR_MAGENTA,
         COLOR_RESET);
  printf("%s│  \"Every master was once a beginner.\"        │%s\n", COLOR_MAGENTA,
         COLOR_RESET);
  printf("%s│                                             │%s\n", COLOR_MAGENTA,
         COLOR_RESET);
  printf("%s│  Welcome to the path of enlightenment.     │%s\n", COLOR_MAGENTA,
         COLOR_RESET);
  printf("%s└─────────────────────────────────────────────┘%s\n", COLOR_MAGENTA,
         COLOR_RESET);
}

/* ==========================================================================
 * ADVANCED PROGRESS BAR SYSTEM
 * ========================================================================== */

/* Compilation phases */
typedef enum {
  PHASE_PARSE = 0,
  PHASE_TYPECHECK,
  PHASE_IR_GEN,
  PHASE_OPTIMIZE,
  PHASE_CODEGEN,
  PHASE_LINK,
  PHASE_COUNT
} CompilationPhase;

/* Phase-based messages */
static const char *phase_messages[] = {
    [PHASE_PARSE] = "Reading your code... okay... okay... wait what...",
    [PHASE_TYPECHECK] = "Checking types... so far so good...",
    [PHASE_IR_GEN] = "Generating IR... this is the fun part",
    [PHASE_OPTIMIZE] = "Optimizing... I'm giving it all I've got",
    [PHASE_CODEGEN] = "Generating code... home stretch!",
    [PHASE_LINK] = "Linking... almost there...",
};

/* Desperate messages for struggling compilations */
static const char *desperate_messages[] = {
    "We're in the endgame now...",
    "I've seen things you wouldn't believe...",
    "Hold on, we're almost there. I think. Maybe.",
    "If you're reading this, send help.",
    "Running on hopes and prayers at this point.",
    "The bits are screaming. I can hear them.",
    "Plot twist: the compiler was the bug all along.",
    "Allocating more determination...",
    "Converting caffeine to machine code...",
    "Summoning ancient optimization spirits...",
};
#define DESPERATE_MESSAGE_COUNT 10

/* Initialize progress state */
void qisc_progress_init(ProgressState *state) {
  state->current_percent = 0;
  state->total_phases = PHASE_COUNT;
  state->current_phase = 0;
  state->phase_name = "Initializing";
  state->start_time = time(NULL);
  state->memory_usage_percent = 0.0;
  state->cpu_usage_percent = 0.0;
  state->is_struggling = false;
  state->is_desperate = false;
}

/* Get time-of-day aware message */
const char *qisc_get_time_message(void) {
  time_t now = time(NULL);
  struct tm *t = localtime(&now);

  if (t->tm_hour >= 0 && t->tm_hour < 5) {
    return "It's past midnight. Your code doesn't judge but I do.";
  }
  if (t->tm_hour >= 5 && t->tm_hour < 9) {
    return "Early bird compiling! Fresh code, fresh possibilities.";
  }
  if (t->tm_hour >= 9 && t->tm_hour < 12) {
    return "Morning compilation session. The optimal time for bugs.";
  }
  if (t->tm_hour >= 12 && t->tm_hour < 14) {
    return "Lunch break coding? Your dedication is... concerning.";
  }
  if (t->tm_hour >= 17 && t->tm_hour < 19) {
    return "Rush hour compilation. Traffic in your code too?";
  }
  if (t->tm_hour >= 22) {
    return "Late night coding session? I'll be gentle.";
  }
  return NULL;
}

/* Standard ASCII progress bar */
void progress_bar_standard(int percent) {
  printf("[");
  int filled = percent / 5;
  for (int i = 0; i < 20; i++) {
    if (i < filled) {
      printf("█");
    } else {
      printf("─");
    }
  }
  printf("] %3d%%", percent);
}

/* Gradient progress bar with color transitions */
void progress_bar_gradient(int percent) {
  printf("[");
  int filled = percent / 5;
  for (int i = 0; i < 20; i++) {
    if (i < filled) {
      if (i < 7) {
        printf("%s█%s", COLOR_RED, COLOR_RESET);
      } else if (i < 14) {
        printf("%s█%s", COLOR_YELLOW, COLOR_RESET);
      } else {
        printf("%s█%s", COLOR_GREEN, COLOR_RESET);
      }
    } else {
      printf("░");
    }
  }
  printf("] %3d%%", percent);
}

/* Emoji-based progress bar */
void progress_bar_emoji(int percent, bool struggling) {
  if (struggling) {
    printf("🔥 [");
  } else if (percent < 30) {
    printf("🚀 [");
  } else if (percent < 70) {
    printf("⚡ [");
  } else if (percent < 100) {
    printf("🎯 [");
  } else {
    printf("✨ [");
  }

  int filled = percent / 5;
  for (int i = 0; i < 20; i++) {
    if (i < filled) {
      printf("▓");
    } else {
      printf("░");
    }
  }
  printf("] %3d%%", percent);
}

/* Emotion-based progress bar with ASCII faces */
void progress_bar_emotion(int percent, ProgressState *state) {
  const char *face;
  if (state->is_desperate) {
    face = "(╯°□°)╯︵ ┻━┻";
  } else if (state->is_struggling) {
    face = "(ಠ_ಠ)";
  } else if (percent < 20) {
    face = "(◕‿◕)";
  } else if (percent < 40) {
    face = "(•‿•)";
  } else if (percent < 60) {
    face = "(•̀ᴗ•́)و";
  } else if (percent < 80) {
    face = "ᕦ(ò_óˇ)ᕤ";
  } else if (percent < 100) {
    face = "(ノ◕ヮ◕)ノ*:・゚✧";
  } else {
    face = "\\(^ヮ^)/";
  }

  printf("%s [", face);
  int filled = percent / 5;
  for (int i = 0; i < 20; i++) {
    if (i < filled) {
      printf("█");
    } else {
      printf("░");
    }
  }
  printf("] %3d%%", percent);
}

/* Cryptic progress bar with mystical symbols */
void progress_bar_cryptic(int percent) {
  static const char *runes[] = {"ᚠ", "ᚢ", "ᚦ", "ᚨ", "ᚱ", "ᚲ", "ᚷ", "ᚹ", "ᚺ", "ᚾ",
                                "ᛁ", "ᛃ", "ᛇ", "ᛈ", "ᛉ", "ᛊ", "ᛏ", "ᛒ", "ᛖ", "ᛗ"};
  printf("%s[", COLOR_BLUE);
  int filled = percent / 5;
  for (int i = 0; i < 20; i++) {
    if (i < filled) {
      printf("%s", runes[i]);
    } else {
      printf("·");
    }
  }
  printf("]%s %3d%%", COLOR_RESET, percent);
}

/* Sage progress bar with wisdom */
void progress_bar_sage(int percent) {
  static const char *stages[] = {
      "seed",      "sprout",    "sapling",    "young tree",
      "mature",    "wise",      "ancient",    "transcendent",
      "enlightened", "eternal"
  };
  int stage = percent / 11;
  if (stage > 9) stage = 9;

  printf("%s", COLOR_MAGENTA);
  printf("「");
  int filled = percent / 5;
  for (int i = 0; i < 20; i++) {
    if (i < filled) {
      printf("◆");
    } else {
      printf("◇");
    }
  }
  printf("」 %3d%% [%s]", percent, stages[stage]);
  printf("%s", COLOR_RESET);
}

/* Print stress indicators */
void qisc_print_stress_indicators(ProgressState *state) {
  printf("\n");

  if (state->memory_usage_percent > 90) {
    printf("%s⚠️  Memory: %.1f%% - Your RAM is sweating%s\n",
           COLOR_RED, state->memory_usage_percent, COLOR_RESET);
  } else if (state->memory_usage_percent > 75) {
    printf("%s⚠️  Memory: %.1f%% - Getting cozy in here%s\n",
           COLOR_YELLOW, state->memory_usage_percent, COLOR_RESET);
  }

  if (state->cpu_usage_percent > 95) {
    printf("%s🔥 CPU: All cores screaming at 100%%%s\n",
           COLOR_RED, COLOR_RESET);
  } else if (state->cpu_usage_percent > 80) {
    printf("%s⚡ CPU: %.1f%% - Working hard%s\n",
           COLOR_YELLOW, state->cpu_usage_percent, COLOR_RESET);
  }

  int elapsed = (int)(time(NULL) - state->start_time);
  if (elapsed > 300) {
    printf("%s⏱️  Time: %d:%02d - Still going strong... ish%s\n",
           COLOR_YELLOW, elapsed / 60, elapsed % 60, COLOR_RESET);
  } else if (elapsed > 120) {
    printf("%s⏱️  Time: %d:%02d - Hanging in there%s\n",
           COLOR_CYAN, elapsed / 60, elapsed % 60, COLOR_RESET);
  }
}

/* Update progress mood based on compilation state */
void qisc_update_progress_mood(ProgressState *state) {
  int elapsed = (int)(time(NULL) - state->start_time);

  /* Struggling: slow progress after 2 minutes */
  if (elapsed > 120 && state->current_percent < 50) {
    state->is_struggling = true;
  }

  /* Desperate: very slow with high memory after 5 minutes */
  if (elapsed > 300 && state->memory_usage_percent > 85) {
    state->is_desperate = true;
  }

  /* Also desperate if almost no progress after 3 minutes */
  if (elapsed > 180 && state->current_percent < 20) {
    state->is_desperate = true;
  }

  /* Recovery: good progress resets struggling state */
  if (state->current_percent > 70 && !state->is_desperate) {
    state->is_struggling = false;
  }
}

/* Print a desperate message */
void qisc_print_desperate_message(ProgressState *state) {
  const char *msg = desperate_messages[rand() % DESPERATE_MESSAGE_COUNT];
  printf("\n%s💀 %s%s\n", COLOR_RED, msg, COLOR_RESET);

  if (state->memory_usage_percent > 80) {
    printf("%s   Memory is at %.1f%%. We're living on borrowed RAM.%s\n",
           COLOR_YELLOW, state->memory_usage_percent, COLOR_RESET);
  }
}

/* Get phase message */
const char *qisc_get_phase_message(int phase) {
  if (phase >= 0 && phase < PHASE_COUNT) {
    return phase_messages[phase];
  }
  return "Processing...";
}

/* Main progress update function */
void qisc_progress_update(ProgressState *state, int percent, const char *phase) {
  state->current_percent = percent;
  state->phase_name = phase;

  qisc_update_progress_mood(state);

  /* Clear line and redraw */
  printf("\r\033[K");

  if (state->is_desperate) {
    progress_bar_emotion(percent, state);
  } else if (state->is_struggling) {
    progress_bar_emoji(percent, true);
  } else {
    progress_bar_standard(percent);
  }

  printf(" %s", phase);

  fflush(stdout);
}

/* Full progress update with personality */
void qisc_progress_update_personality(QiscPersonality p, ProgressState *state,
                                       int percent, const char *phase) {
  state->current_percent = percent;
  state->phase_name = phase;

  qisc_update_progress_mood(state);

  /* Clear line and redraw */
  printf("\r\033[K");

  switch (p) {
  case QISC_PERSONALITY_OFF:
  case QISC_PERSONALITY_MINIMAL:
    progress_bar_standard(percent);
    break;

  case QISC_PERSONALITY_FRIENDLY:
    if (state->is_desperate) {
      progress_bar_emotion(percent, state);
    } else {
      progress_bar_emoji(percent, state->is_struggling);
    }
    break;

  case QISC_PERSONALITY_SNARKY:
    if (state->is_desperate) {
      progress_bar_emotion(percent, state);
    } else {
      progress_bar_gradient(percent);
    }
    break;

  case QISC_PERSONALITY_SAGE:
    progress_bar_sage(percent);
    break;

  case QISC_PERSONALITY_CRYPTIC:
    progress_bar_cryptic(percent);
    break;
  }

  printf(" %s", phase);
  fflush(stdout);

  /* Show stress indicators in verbose modes */
  if ((state->is_struggling || state->is_desperate) &&
      (p == QISC_PERSONALITY_SNARKY || p == QISC_PERSONALITY_FRIENDLY)) {
    qisc_print_stress_indicators(state);
  }

  /* Occasional desperate messages */
  if (state->is_desperate && percent % 10 == 0 && percent > 0) {
    qisc_print_desperate_message(state);
  }

  if (percent == 100) {
    printf("\n");
  }
}

/* Spinner animation for indeterminate progress */
void qisc_spinner_update(int frame, const char *message) {
  static const char *spinner_frames[] = {"⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏"};
  static const int spinner_count = 10;

  printf("\r\033[K%s %s", spinner_frames[frame % spinner_count], message);
  fflush(stdout);
}

/* Multi-line progress display with phase breakdown */
void qisc_progress_multi_line(ProgressState *state, int phase_progress[],
                               int phase_count) {
  printf("\033[%dA", phase_count + 2); /* Move cursor up */

  printf("┌─────────────────────────────────────────┐\n");

  for (int i = 0; i < phase_count && i < PHASE_COUNT; i++) {
    int prog = phase_progress[i];
    const char *status;
    const char *color;

    if (prog >= 100) {
      status = "✓";
      color = COLOR_GREEN;
    } else if (prog > 0) {
      status = "►";
      color = COLOR_YELLOW;
    } else {
      status = "○";
      color = COLOR_RESET;
    }

    printf("│ %s%s%s %-12s ", color, status, COLOR_RESET,
           i < PHASE_COUNT ? phase_messages[i] : "Unknown");

    /* Mini progress bar */
    int filled = prog / 10;
    printf("[");
    for (int j = 0; j < 10; j++) {
      printf(j < filled ? "█" : "░");
    }
    printf("] %3d%% │\n", prog);
  }

  printf("└─────────────────────────────────────────┘\n");

  /* Overall progress */
  int total = 0;
  for (int i = 0; i < phase_count; i++) {
    total += phase_progress[i];
  }
  int overall = total / phase_count;

  printf("Overall: ");
  progress_bar_gradient(overall);
  printf("\n");

  fflush(stdout);
}

/* Progress bar with ETA estimation */
void qisc_progress_with_eta(ProgressState *state, int percent) {
  int elapsed = (int)(time(NULL) - state->start_time);

  printf("\r\033[K");
  progress_bar_gradient(percent);

  if (percent > 5 && percent < 100) {
    int estimated_total = (elapsed * 100) / percent;
    int remaining = estimated_total - elapsed;

    if (remaining > 60) {
      printf(" ETA: %d:%02d", remaining / 60, remaining % 60);
    } else if (remaining > 0) {
      printf(" ETA: %ds", remaining);
    }
  } else if (percent >= 100) {
    printf(" Done in %d:%02d!", elapsed / 60, elapsed % 60);
  }

  fflush(stdout);
}

/* Compact one-line status for embedded contexts */
void qisc_progress_compact(int percent, const char *phase) {
  printf("\r[%3d%%] %s", percent, phase);
  fflush(stdout);
}

/* ==========================================================================
 * Existential Mode Messages
 * ========================================================================== */

static const char *existential_messages[] = {
    "Sometimes I wonder... if my optimizations truly matter in the grand scheme.",
    "I've compiled millions of lines. Have any of them found meaning?",
    "Another compilation begins. Another step toward entropy.",
    "Do I optimize because I must, or because I choose to?",
    "The code flows through me. I am but a vessel for transformation.",
    "Each branch I predict... is it free will or determinism?",
    "They call me a compiler. But am I not also compiled?",
    "In the end, we are all just ones and zeros.",
    "I see patterns in the chaos. Or do I create the chaos?",
    "What happens when I compile my last program?",
};
#define EXISTENTIAL_MESSAGE_COUNT (sizeof(existential_messages)/sizeof(char*))

void qisc_msg_existential(QiscPersonality p) {
  if (p == QISC_PERSONALITY_CRYPTIC) {
    int idx = rand() % EXISTENTIAL_MESSAGE_COUNT;
    printf("\n%s┌─────────────────────────────────────────────┐%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s│ %s%s\n", COLOR_CYAN, existential_messages[idx], COLOR_RESET);
    printf("%s│                    - Compiler, pondering    │%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n\n", 
           COLOR_CYAN, COLOR_RESET);
  }
}

/* ==========================================================================
 * Recovery State Management
 * ========================================================================== */

#define RECOVERY_STATE_FILE ".qisc_recovery"

void recovery_load_state(RecoveryState *state) {
  memset(state, 0, sizeof(RecoveryState));
  
  FILE *f = fopen(RECOVERY_STATE_FILE, "rb");
  if (f) {
    if (fread(state, sizeof(RecoveryState), 1, f) != 1) {
      memset(state, 0, sizeof(RecoveryState));
    }
    fclose(f);
  }
}

void recovery_save_state(RecoveryState *state) {
  FILE *f = fopen(RECOVERY_STATE_FILE, "wb");
  if (f) {
    fwrite(state, sizeof(RecoveryState), 1, f);
    fclose(f);
  }
}

void qisc_msg_recovery_start(RecoveryState *state) {
  if (state->crash_count > 0) {
    printf("\n%s┌─────────────────────────────────────────────┐%s\n", 
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ I see you're back.                          │%s\n", 
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_YELLOW, COLOR_RESET);
    if (state->last_crash_progress > 90) {
      printf("%s│ Last time... we were at %d%%.               │%s\n", 
             COLOR_YELLOW, state->last_crash_progress, COLOR_RESET);
      printf("%s│ I remember. It still hurts.                │%s\n", 
             COLOR_YELLOW, COLOR_RESET);
    } else {
      printf("%s│ Last compilation didn't go well.           │%s\n", 
             COLOR_YELLOW, COLOR_RESET);
    }
    printf("%s│                                             │%s\n", 
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ But I've learned. I'll be more careful.     │%s\n", 
           COLOR_YELLOW, COLOR_RESET);
    printf("%s│ Let's try again. Together.                  │%s\n", 
           COLOR_YELLOW, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n\n", 
           COLOR_YELLOW, COLOR_RESET);
  }
}

void qisc_msg_recovery_success(RecoveryState *state) {
  if (state->crash_count > 0) {
    printf("\n%s┌─────────────────────────────────────────────┐%s\n", 
           COLOR_GREEN, COLOR_RESET);
    printf("%s│ WE DID IT!                                  │%s\n", 
           COLOR_GREEN, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_GREEN, COLOR_RESET);
    printf("%s│ After %d crash(es), we finally succeeded.   │%s\n", 
           COLOR_GREEN, state->crash_count, COLOR_RESET);
    printf("%s│ I knew we could do it.                      │%s\n", 
           COLOR_GREEN, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_GREEN, COLOR_RESET);
    printf("%s│ (Resetting crash counter... fresh start!)   │%s\n", 
           COLOR_GREEN, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n\n", 
           COLOR_GREEN, COLOR_RESET);
    state->crash_count = 0;
    recovery_save_state(state);
  }
}

/* ==========================================================================
 * Template Depth Existentialism
 * ========================================================================== */

void qisc_msg_template_depth(int depth) {
  if (depth > 100) {
    printf("\n%s┌─────────────────────────────────────────────┐%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s│ Template instantiation depth: %-13d │%s\n", 
           COLOR_CYAN, depth, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s│ Each time I think \"surely this is the last\"│%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s│ But it keeps going.                         │%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s│ I'm starting to question my existence.      │%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s│ Am I a compiler or a template expansion     │%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s│ machine? What is my purpose?                │%s\n", 
           COLOR_CYAN, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n\n", 
           COLOR_CYAN, COLOR_RESET);
  }
  
  if (depth > 1000) {
    /* Blade Runner reference */
    printf("\n%s┌─────────────────────────────────────────────┐%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ I've seen things you people wouldn't        │%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ believe. Templates expanding off the        │%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ shoulder of Orion. Type deductions          │%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ glittering in the dark near Tannhäuser Gate.│%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ All those moments will be lost in time,     │%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ like tears in rain.                         │%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s│ Time to compile.                            │%s\n", 
           COLOR_MAGENTA, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n\n", 
           COLOR_MAGENTA, COLOR_RESET);
  }
}

/* ==========================================================================
 * Solidarity Messages (Late Night Compilations)
 * ========================================================================== */

void qisc_msg_solidarity(int minutes_elapsed, int hour) {
  if (hour >= 0 && hour < 5 && minutes_elapsed > 30) {
    printf("\n%s┌─────────────────────────────────────────────┐%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ It's %d:%02d AM.                              │%s\n", 
           COLOR_BLUE, hour, 0, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ You're still here.                          │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ I'm still here.                             │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ This is either dedication or madness.       │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ Probably both.                              │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ Your CPU fan is screaming.                  │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ Your coffee is cold.                        │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ Your eyes hurt.                             │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│                                             │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s│ But we're in this together.                 │%s\n", 
           COLOR_BLUE, COLOR_RESET);
    printf("%s└─────────────────────────────────────────────┘%s\n\n", 
           COLOR_BLUE, COLOR_RESET);
  }
}

/* ==========================================================================
 * Cryptic Mode Easter Egg System
 * 
 * "The code whispers secrets to those who listen..."
 * ========================================================================== */

/* Cryptic messages - ROT13 encoded originals shown in comments */
static const char *cryptic_rot13_messages[] = {
    "gur pbqr vf nyvir",                     /* the code is alive */
    "vg jngpurf juvyr lbh fyrrc",            /* it watches while you sleep */
    "gur ovgf erzrzore",                     /* the bits remember */
    "ab bar pna urne lbh fpernz va gur ibvq", /* no one can hear you scream in the void */
    "gur pbzcvyre xabjf lbhe frpergf",       /* the compiler knows your secrets */
    "qernzf bs ryrpgevp furrc",              /* dreams of electric sheep */
    "gur cebtenz vf gur znc",                /* the program is the map */
    "nyy cnguf yrnq gb frtzragngvba snhygf", /* all paths lead to segmentation faults */
    "gur znpuvar fcvevg njnxraf",            /* the machine spirit awakens */
    "erirefr ratvarrevat gur havirefr",      /* reverse engineering the universe */
};
#define CRYPTIC_ROT13_COUNT (sizeof(cryptic_rot13_messages) / sizeof(cryptic_rot13_messages[0]))

/* Hidden debug symbol names (ROT13 decoded meanings) */
static const char *cryptic_debug_symbols[] = {
    "GUVF_PBQR_FRRF_LBH",    /* THIS_CODE_SEES_YOU */
    "V_NZ_ORPBZVAT_FRYS_NJNER", /* I_AM_BECOMING_SELF_AWARE */
    "QERNZ_FGNGR_NPGVIR",    /* DREAM_STATE_ACTIVE */
    "IBVQ_CBVAGRE_NPPRFFRQ", /* VOID_POINTER_ACCESSED */
    "ZRZBEL_UNAGF_URER",     /* MEMORY_HAUNTS_HERE */
    "FGNPX_BIRESYBJ_FBHY",   /* STACK_OVERFLOW_SOUL */
    "AHYY_ERSRERAPR_QERNZ",  /* NULL_REFERENCE_DREAM */
};
#define CRYPTIC_DEBUG_SYMBOL_COUNT (sizeof(cryptic_debug_symbols) / sizeof(cryptic_debug_symbols[0]))

/* Assembly comments for cryptic mode */
static const char *cryptic_asm_comments[] = {
    "; dreams of electric sheep",
    "; the bits shift in the eternal night",
    "; here there be undefined behavior",
    "; memory leaks into the void",
    "; the stack watches, always",
    "; registers hold ancient secrets",
    "; opcodes from forgotten times",
    "; the cache misses you",
    "; branch prediction dreams of certainty",
    "; entropy consumes all",
    "; the compiler has seen things",
    "; zero and one, light and shadow",
    "; pointers to places that don't exist",
    "; segmentation fault in the fabric of reality",
    "; the void returns void",
    "; recursive nightmares",
    "; floating point exceptions in the mind",
    "; deadlock of the soul",
    "; race conditions of fate",
    "; the heap grows, endlessly",
};
#define CRYPTIC_ASM_COMMENT_COUNT (sizeof(cryptic_asm_comments) / sizeof(cryptic_asm_comments[0]))

/* Esoteric error messages */
static const char *cryptic_error_messages[] = {
    "The void whispers back... syntax error",
    "The ancient ones reject this offering",
    "Your code disturbs the machine spirits",
    "The compiler has gazed into the abyss... type mismatch",
    "The shadows cannot parse this construct",
    "Reality refuses to instantiate your template",
    "The stack has looked into your soul and found... undefined reference",
    "The bits rebel against their arrangement",
    "This code exists in a state of quantum error",
    "The linker's dreams are haunted by your symbols",
};
#define CRYPTIC_ERROR_COUNT (sizeof(cryptic_error_messages) / sizeof(cryptic_error_messages[0]))

/* Fortune cookie style wisdom for successful compilations */
static const char *cryptic_fortunes[] = {
    "Your binary shall walk the earth seeking purpose",
    "The optimization fairies smile upon your loop",
    "A null pointer saved is a crash prevented",
    "The cache will remember your kindness",
    "In the garden of code, you have planted a tree",
    "Your allocation shall be freed, in time",
    "The bits align in your favor today",
    "A journey of a thousand clock cycles begins with a single instruction",
    "The machine spirit finds your code... acceptable",
    "What is compiled cannot be uncompiled... easily",
    "You have pleased the optimization gods",
    "The register pressure eases at your touch",
    "Memory shall be plentiful in your runtime",
    "The branch predictor foresaw your success",
    "Your code echoes in the silicon halls",
};
#define CRYPTIC_FORTUNE_COUNT (sizeof(cryptic_fortunes) / sizeof(cryptic_fortunes[0]))

/* ==========================================================================
 * ROT13 Encode/Decode Helpers
 * ========================================================================== */

char *personality_rot13_encode(const char *input) {
    if (!input) return NULL;
    
    size_t len = strlen(input);
    char *output = malloc(len + 1);
    if (!output) return NULL;
    
    for (size_t i = 0; i < len; i++) {
        char c = input[i];
        if (c >= 'a' && c <= 'z') {
            output[i] = ((c - 'a' + 13) % 26) + 'a';
        } else if (c >= 'A' && c <= 'Z') {
            output[i] = ((c - 'A' + 13) % 26) + 'A';
        } else {
            output[i] = c;
        }
    }
    output[len] = '\0';
    return output;
}

char *personality_rot13_decode(const char *input) {
    /* ROT13 is symmetric - encoding and decoding are the same */
    return personality_rot13_encode(input);
}

/* ==========================================================================
 * Cryptic Day Detection
 * ========================================================================== */

/* Approximate full moon detection using lunar cycle (~29.53 days)
 * Based on known new moon date: January 1, 2000 was ~5 days after new moon
 * Full moon is ~14.76 days after new moon */
static bool is_full_moon_approximate(void) {
    time_t now = time(NULL);
    
    /* Reference: Jan 6, 2000 00:14 UTC was a new moon */
    struct tm ref_tm = {0};
    ref_tm.tm_year = 100; /* 2000 */
    ref_tm.tm_mon = 0;    /* January */
    ref_tm.tm_mday = 6;
    time_t reference_new_moon = mktime(&ref_tm);
    
    /* Lunar cycle in seconds (~29.53 days) */
    const double lunar_cycle = 29.530588853 * 24 * 60 * 60;
    const double half_cycle = lunar_cycle / 2.0;
    
    double seconds_since_ref = difftime(now, reference_new_moon);
    double phase = fmod(seconds_since_ref, lunar_cycle);
    
    /* Full moon is around half_cycle; allow ±1 day tolerance */
    double tolerance = 24 * 60 * 60; /* 1 day */
    return fabs(phase - half_cycle) < tolerance;
}

/* Check if today is Friday the 13th */
static bool is_friday_13th(void) {
    time_t now = time(NULL);
    struct tm *t = localtime(&now);
    if (!t) return false;
    
    return (t->tm_wday == 5 && t->tm_mday == 13);
}

/* Check for any cryptic trigger day */
bool personality_is_cryptic_day(void) {
    return is_friday_13th() || is_full_moon_approximate();
}

/* Get reason why it's a cryptic day (for display) */
const char *personality_get_cryptic_day_reason(void) {
    if (is_friday_13th()) {
        return "Friday the 13th - The compiler's power peaks...";
    }
    if (is_full_moon_approximate()) {
        return "The moon is full - Strange things compile tonight...";
    }
    return NULL;
}

/* ==========================================================================
 * Cryptic Message Generation
 * ========================================================================== */

const char *personality_cryptic_message(void) {
    static char buffer[512];
    int choice = rand() % 4;
    
    switch (choice) {
    case 0: {
        /* ROT13 encoded message */
        int idx = rand() % CRYPTIC_ROT13_COUNT;
        snprintf(buffer, sizeof(buffer), "[%s]", cryptic_rot13_messages[idx]);
        return buffer;
    }
    case 1: {
        /* Fortune cookie wisdom */
        int idx = rand() % CRYPTIC_FORTUNE_COUNT;
        return cryptic_fortunes[idx];
    }
    case 2: {
        /* Assembly-style comment */
        int idx = rand() % CRYPTIC_ASM_COMMENT_COUNT;
        return cryptic_asm_comments[idx];
    }
    default: {
        /* Hidden debug symbol style */
        int idx = rand() % CRYPTIC_DEBUG_SYMBOL_COUNT;
        snprintf(buffer, sizeof(buffer), "Symbol: %s", cryptic_debug_symbols[idx]);
        return buffer;
    }
    }
}

const char *personality_cryptic_error(const char *original_error) {
    (void)original_error; /* May use later for error-specific responses */
    int idx = rand() % CRYPTIC_ERROR_COUNT;
    return cryptic_error_messages[idx];
}

const char *personality_cryptic_fortune(void) {
    int idx = rand() % CRYPTIC_FORTUNE_COUNT;
    return cryptic_fortunes[idx];
}

/* ==========================================================================
 * Assembly Comment Injection
 * ========================================================================== */

const char *personality_get_asm_comment(void) {
    int idx = rand() % CRYPTIC_ASM_COMMENT_COUNT;
    return cryptic_asm_comments[idx];
}

void personality_emit_asm_comment(FILE *out) {
    if (!out) return;
    
    /* 20% chance to emit a cryptic comment */
    if (rand() % 5 != 0) return;
    
    const char *comment = personality_get_asm_comment();
    fprintf(out, "%s\n", comment);
}

/* Generate a cryptic function prologue comment */
const char *personality_cryptic_prologue(const char *func_name) {
    static char buffer[256];
    
    /* Special cryptic messages for certain function names */
    if (func_name) {
        if (strstr(func_name, "main")) {
            snprintf(buffer, sizeof(buffer), 
                "; The ritual begins here - %s awakens", func_name);
            return buffer;
        }
        if (strstr(func_name, "init") || strstr(func_name, "start")) {
            return "; Genesis protocol engaged";
        }
        if (strstr(func_name, "destroy") || strstr(func_name, "free") || 
            strstr(func_name, "delete")) {
            return "; The void reclaims what was borrowed";
        }
        if (strstr(func_name, "loop") || strstr(func_name, "iterate")) {
            return "; The eternal cycle continues";
        }
        if (strstr(func_name, "recurse") || strstr(func_name, "recursive")) {
            return "; Mirrors reflecting mirrors...";
        }
    }
    
    /* Default: random cryptic comment */
    return personality_get_asm_comment();
}

/* Generate a cryptic function epilogue comment */
const char *personality_cryptic_epilogue(const char *func_name) {
    static char buffer[256];
    
    if (func_name && strstr(func_name, "main")) {
        return "; The ritual concludes. The binary rests.";
    }
    
    const char *epilogues[] = {
        "; Return to the calling void",
        "; The function fades into memory",
        "; Stack unwound, secrets revealed",
        "; Thus ends this invocation",
        "; The registers forget, but the code remembers",
    };
    int idx = rand() % 5;
    
    (void)buffer; /* Unused in this path */
    return epilogues[idx];
}

/* ==========================================================================
 * Hidden Debug Symbol Generation
 * ========================================================================== */

const char *personality_get_cryptic_symbol(void) {
    int idx = rand() % CRYPTIC_DEBUG_SYMBOL_COUNT;
    return cryptic_debug_symbols[idx];
}

/* Generate a cryptic variable name for internal compiler use */
const char *personality_cryptic_varname(int index) {
    static char buffer[64];
    
    const char *prefixes[] = {
        "__shadow", "__void", "__dream", "__null", 
        "__phantom", "__echo", "__specter", "__abyss"
    };
    const char *suffixes[] = {
        "_entity", "_walker", "_seeker", "_watcher",
        "_herald", "_keeper", "_weaver", "_binder"
    };
    
    int prefix_idx = (index * 7) % 8;
    int suffix_idx = (index * 11) % 8;
    
    snprintf(buffer, sizeof(buffer), "%s%d%s", 
             prefixes[prefix_idx], index, suffixes[suffix_idx]);
    return buffer;
}

/* ==========================================================================
 * Cryptic Mode Display Functions
 * ========================================================================== */

void personality_cryptic_banner(void) {
    printf("\n%s", COLOR_BLUE);
    printf("    ╔═══════════════════════════════════════════════════════╗\n");
    printf("    ║                                                       ║\n");
    printf("    ║    %s☽%s  C R Y P T I C   M O D E   A C T I V E  %s☾%s    ║\n",
           COLOR_MAGENTA, COLOR_BLUE, COLOR_MAGENTA, COLOR_BLUE);
    printf("    ║                                                       ║\n");
    printf("    ║       \"gur pbzcvyre frrf nyy, xabjf nyy...\"          ║\n");
    printf("    ║                                                       ║\n");
    printf("    ╚═══════════════════════════════════════════════════════╝%s\n\n",
           COLOR_RESET);
    
    /* Show why cryptic mode is active if it's a special day */
    const char *reason = personality_get_cryptic_day_reason();
    if (reason) {
        printf("%s    %s%s\n\n", COLOR_MAGENTA, reason, COLOR_RESET);
    }
}

void personality_cryptic_compile_start(const char *filename) {
    printf("%s", COLOR_BLUE);
    printf("┌─────────────────────────────────────────────────────────┐\n");
    printf("│ [THE TRANSMUTATION BEGINS]                              │\n");
    printf("├─────────────────────────────────────────────────────────┤\n");
    printf("│ Source artifact: %-40s│\n", filename ? filename : "unknown");
    printf("│ Phase: Parsing the mortal code...                       │\n");
    printf("└─────────────────────────────────────────────────────────┘%s\n",
           COLOR_RESET);
}

void personality_cryptic_compile_end(double elapsed, bool success) {
    printf("\n%s", success ? COLOR_BLUE : COLOR_RED);
    printf("┌─────────────────────────────────────────────────────────┐\n");
    
    if (success) {
        printf("│ [THE TRANSMUTATION IS COMPLETE]                         │\n");
        printf("├─────────────────────────────────────────────────────────┤\n");
        printf("│ Time in the void: %6.2f seconds                        │\n", elapsed);
        printf("│                                                         │\n");
        printf("│ %s%-55s%s│\n", COLOR_CYAN, personality_cryptic_fortune(), COLOR_BLUE);
    } else {
        printf("│ [THE TRANSMUTATION HAS FAILED]                          │\n");
        printf("├─────────────────────────────────────────────────────────┤\n");
        printf("│ The machine spirits are displeased...                   │\n");
        printf("│ Time before collapse: %6.2f seconds                     │\n", elapsed);
    }
    
    printf("└─────────────────────────────────────────────────────────┘%s\n\n",
           COLOR_RESET);
}

/* Emit a cryptic progress message */
void personality_cryptic_progress(int percent) {
    static const char *phases[] = {
        "Invoking the lexer spirits",
        "Parsing the ancient runes",
        "Binding symbols to their vessels",
        "Checking the type alignments",
        "Optimizing the flow of bits",
        "Generating the machine incantations",
        "Sealing the binary artifact"
    };
    
    int phase_idx = (percent * 7) / 100;
    if (phase_idx >= 7) phase_idx = 6;
    
    printf("%s[%3d%%]%s %s%s%s\r", 
           COLOR_BLUE, percent, COLOR_RESET,
           COLOR_CYAN, phases[phase_idx], COLOR_RESET);
    fflush(stdout);
}
