/*
 * QISC Personality-Aware Error Messages - Implementation
 *
 * Makes compiler errors feel less frustrating and more like helpful guidance.
 */

#include "error_personality.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ANSI color codes */
#define COLOR_RESET   "\033[0m"
#define COLOR_RED     "\033[31m"
#define COLOR_GREEN   "\033[32m"
#define COLOR_YELLOW  "\033[33m"
#define COLOR_BLUE    "\033[34m"
#define COLOR_MAGENTA "\033[35m"
#define COLOR_CYAN    "\033[36m"
#define COLOR_BOLD    "\033[1m"
#define COLOR_DIM     "\033[2m"

/* Box drawing characters */
#define BOX_TL "┌"
#define BOX_TR "┐"
#define BOX_BL "└"
#define BOX_BR "┘"
#define BOX_H  "─"
#define BOX_V  "│"

#define BOX_WIDTH 60

/*
 * Personality-specific error messages
 */

/* Friendly personality - supportive and encouraging */
static const char *syntax_error_friendly[] __attribute__((unused)) = {
    "Oops! Looks like there's a syntax hiccup here.",
    "I got a bit confused at this point in your code.",
    "Small syntax issue - easy fix!",
    "Almost there! Just a tiny syntax thing to sort out.",
    "Don't worry, happens to everyone - syntax slip-up here.",
};
#define SYNTAX_FRIENDLY_COUNT 5

static const char *type_error_friendly[] __attribute__((unused)) = {
    "Hmm, these types don't quite match up.",
    "There's a small type mix-up here.",
    "The types here need a little adjustment.",
    "Found a type that doesn't fit - let me help!",
};
#define TYPE_FRIENDLY_COUNT 4

static const char *undefined_error_friendly[] __attribute__((unused)) = {
    "I couldn't find this name anywhere - maybe a typo?",
    "This variable doesn't seem to exist yet.",
    "I don't recognize this - did you forget to declare it?",
    "Looking for this name, but can't find it!",
};
#define UNDEFINED_FRIENDLY_COUNT 4

static const char *unused_error_friendly[] __attribute__((unused)) = {
    "This variable is feeling a bit lonely - never used!",
    "You declared this but forgot to use it.",
    "This is sitting here unused - intentional?",
};
#define UNUSED_FRIENDLY_COUNT 3

static const char *unreachable_error_friendly[] __attribute__((unused)) = {
    "This code will never run - it's unreachable.",
    "Found some code that can't be reached.",
    "This part of your code is taking a permanent vacation.",
};
#define UNREACHABLE_FRIENDLY_COUNT 3

static const char *overflow_error_friendly[] __attribute__((unused)) = {
    "Watch out - this might overflow!",
    "This value could get too big for its container.",
    "Potential overflow detected - let's be careful here.",
};
#define OVERFLOW_FRIENDLY_COUNT 3

static const char *null_error_friendly[] __attribute__((unused)) = {
    "This might be null - could cause a crash!",
    "Careful! This could be a null pointer.",
    "Possible null dereference here - let's add a check.",
};
#define NULL_FRIENDLY_COUNT 3

static const char *bounds_error_friendly[] __attribute__((unused)) = {
    "This index might be out of bounds!",
    "Array access could go past the end.",
    "Make sure this index is within the array size.",
};
#define BOUNDS_FRIENDLY_COUNT 3

static const char *performance_error_friendly[] __attribute__((unused)) = {
    "This could be a bit slow - want some optimization tips?",
    "Performance heads-up: this might be inefficient.",
    "Just a friendly note - there's a faster way to do this.",
};
#define PERFORMANCE_FRIENDLY_COUNT 3

/* Snarky personality - witty and sarcastic */
static const char *syntax_error_snarky[] __attribute__((unused)) = {
    "Did your cat walk across the keyboard?",
    "This syntax is... creative. Not correct, but creative.",
    "Syntax error. Have you tried turning it off and on again?",
    "I've seen better syntax from a random number generator.",
    "Close, but no semicolon. I mean cigar.",
    "The parser looked at this and just gave up.",
};
#define SYNTAX_SNARKY_COUNT 6

static const char *type_error_snarky[] __attribute__((unused)) = {
    "You can't add \"banana\" to a number. That's not how math works.",
    "Types don't match. Shocking, I know.",
    "I'm not a wizard - I can't turn strings into integers.",
    "These types are incompatible, like pineapple on pizza. (Fight me.)",
    "Type mismatch. The compiler is judging you.",
};
#define TYPE_SNARKY_COUNT 5

static const char *undefined_error_snarky[] __attribute__((unused)) = {
    "This variable doesn't exist. Neither does your attention to detail.",
    "404: Variable not found. Have you checked under the couch?",
    "I looked everywhere. Even asked Stack Overflow. Nothing.",
    "This name is as undefined as your coding standards.",
    "Maybe declare it before using it? Just a thought.",
};
#define UNDEFINED_SNARKY_COUNT 5

static const char *unused_error_snarky[] __attribute__((unused)) = {
    "Variable declared, never used. Much like my dating profile.",
    "This variable is collecting dust. Impressive.",
    "You declared it with such confidence. Then abandoned it.",
    "This variable has trust issues now.",
};
#define UNUSED_SNARKY_COUNT 4

static const char *unreachable_error_snarky[] __attribute__((unused)) = {
    "This code is as reachable as my work-life balance.",
    "Dead code. RIP. Press F to pay respects.",
    "This code will never run. Like me on a Monday morning.",
    "Unreachable code detected. It's living its best life though.",
};
#define UNREACHABLE_SNARKY_COUNT 4

static const char *overflow_error_snarky[] __attribute__((unused)) = {
    "Integer overflow: because who needs correct math anyway?",
    "This might overflow. Just like your coffee cup at 3am.",
    "Congratulations! You've discovered integer limits!",
};
#define OVERFLOW_SNARKY_COUNT 3

static const char *null_error_snarky[] __attribute__((unused)) = {
    "Null pointer? In MY code? It's more likely than you think.",
    "Segfault incoming in 3... 2... 1...",
    "Tony Hoare called this a billion-dollar mistake. You're contributing.",
};
#define NULL_SNARKY_COUNT 3

static const char *bounds_error_snarky[] __attribute__((unused)) = {
    "Array index out of bounds. Classic.",
    "Trying to access element -1? Bold strategy.",
    "Your index is running off the array like it's fleeing responsibility.",
};
#define BOUNDS_SNARKY_COUNT 3

static const char *performance_error_snarky[] __attribute__((unused)) = {
    "This is O(n²). Is your CPU your enemy?",
    "Performance warning: This is slower than a sloth in molasses.",
    "Congratulations on your new space heater code!",
};
#define PERFORMANCE_SNARKY_COUNT 3

/* Sage personality - wise and philosophical */
static const char *syntax_error_sage[] __attribute__((unused)) = {
    "\"Syntax is the clothing of thought.\" - Your code needs a wardrobe change.",
    "Even the greatest programmers forget semicolons.",
    "The parser seeks harmony, but finds discord here.",
    "In the dance of symbols, a step has been missed.",
    "The ancients wrote: 'First, master the semicolon.'",
};
#define SYNTAX_SAGE_COUNT 5

static const char *type_error_sage[] __attribute__((unused)) = {
    "Types are the contract between intention and implementation.",
    "Like water and oil, these types refuse to mix.",
    "The type system protects you from yourself.",
    "\"Know your types, know yourself.\" - Ancient programmer proverb",
};
#define TYPE_SAGE_COUNT 4

static const char *undefined_error_sage[] __attribute__((unused)) = {
    "One cannot use what does not yet exist.",
    "The name you seek has not been written into being.",
    "Declaration before use: the first law of variables.",
    "This reference points to the void. The void stares back.",
};
#define UNDEFINED_SAGE_COUNT 4

static const char *unused_error_sage[] __attribute__((unused)) = {
    "A variable unused is a thought unfinished.",
    "This declaration serves no purpose. Remove it, and simplify.",
    "Code, like life, should not carry unnecessary burdens.",
};
#define UNUSED_SAGE_COUNT 3

static const char *unreachable_error_sage[] __attribute__((unused)) = {
    "Code that cannot run is a path that leads nowhere.",
    "The execution will never walk this road.",
    "Dead code is a tombstone in your program. Honor it by removal.",
};
#define UNREACHABLE_SAGE_COUNT 3

static const char *overflow_error_sage[] __attribute__((unused)) = {
    "Even numbers have their limits. Respect them.",
    "The vessel cannot hold more than its capacity.",
    "Integer overflow: when ambition exceeds capability.",
};
#define OVERFLOW_SAGE_COUNT 3

static const char *null_error_sage[] __attribute__((unused)) = {
    "Null is the void from which all crashes are born.",
    "Handle null as you would handle uncertainty: with care.",
    "The pointer points to nothing. This is a dangerous nothing.",
};
#define NULL_SAGE_COUNT 3

static const char *bounds_error_sage[] __attribute__((unused)) = {
    "Arrays have boundaries. So too should your indices.",
    "To reach beyond the array is to grasp at shadows.",
    "Know your bounds, and your code shall not stumble.",
};
#define BOUNDS_SAGE_COUNT 3

static const char *performance_error_sage[] __attribute__((unused)) = {
    "Efficiency is the art of doing more with less.",
    "The fastest code is often the simplest.",
    "Measure twice, optimize once.",
};
#define PERFORMANCE_SAGE_COUNT 3

/* Cryptic personality - mysterious and vague */
static const char *syntax_error_cryptic[] __attribute__((unused)) = {
    "The symbols do not align with the stars.",
    "A disturbance in the syntax field...",
    "The parser whispers of imbalance.",
    "Your incantation lacks a crucial rune.",
};
#define SYNTAX_CRYPTIC_COUNT 4

static const char *type_error_cryptic[] __attribute__((unused)) = {
    "These essences cannot merge.",
    "The types speak different languages.",
    "A transformation is required, but none is given.",
};
#define TYPE_CRYPTIC_COUNT 3

static const char *undefined_error_cryptic[] __attribute__((unused)) = {
    "You call upon a name unknown to this realm.",
    "The symbol exists only in your imagination.",
    "A reference to the unmanifested.",
};
#define UNDEFINED_CRYPTIC_COUNT 3

/* Minimal personality - just the facts */
static const char *error_minimal[] __attribute__((unused)) = {
    "Error",
};
#define MINIMAL_COUNT 1

/*
 * Global state
 */
static ErrorHistory g_error_history;
static bool g_initialized = false;
static unsigned int g_random_seed = 0;

/*
 * Helper functions
 */

static unsigned int simple_rand(void) {
    g_random_seed = g_random_seed * 1103515245 + 12345;
    return (g_random_seed / 65536) % 32768;
}

static int random_index(int max) {
    if (max <= 1) return 0;
    return simple_rand() % max;
}

static void print_horizontal_line(const char *left, const char *right) {
    printf("%s%s", COLOR_CYAN, left);
    for (int i = 0; i < BOX_WIDTH - 2; i++) {
        printf("%s", BOX_H);
    }
    printf("%s%s\n", right, COLOR_RESET);
}

static void print_box_line(const char *text) {
    int len = (int)strlen(text);
    int padding = BOX_WIDTH - 4 - len;
    if (padding < 0) padding = 0;
    
    printf("%s%s%s %s%*s %s%s%s\n", 
           COLOR_CYAN, BOX_V, COLOR_RESET,
           text, padding, "",
           COLOR_CYAN, BOX_V, COLOR_RESET);
}

static void print_box_line_colored(const char *color, const char *text) {
    int len = (int)strlen(text);
    int padding = BOX_WIDTH - 4 - len;
    if (padding < 0) padding = 0;
    
    printf("%s%s%s %s%s%s%*s %s%s%s\n", 
           COLOR_CYAN, BOX_V, COLOR_RESET,
           color, text, COLOR_RESET, padding, "",
           COLOR_CYAN, BOX_V, COLOR_RESET);
}

static void print_empty_line(void) {
    printf("%s%s%s%*s%s%s%s\n",
           COLOR_CYAN, BOX_V, COLOR_RESET,
           BOX_WIDTH - 2, "",
           COLOR_CYAN, BOX_V, COLOR_RESET);
}

/*
 * Error message retrieval
 */

const char *get_error_message(QiscPersonality p, ErrorType type) {
    return get_error_message_variant(p, type);
}

const char *get_error_message_variant(QiscPersonality p, ErrorType type) {
    int idx;
    
    switch (p) {
    case QISC_PERSONALITY_OFF:
    case QISC_PERSONALITY_MINIMAL:
        return error_minimal[0];
        
    case QISC_PERSONALITY_FRIENDLY:
        switch (type) {
        case ERR_TYPE_SYNTAX:
            idx = random_index(SYNTAX_FRIENDLY_COUNT);
            return syntax_error_friendly[idx];
        case ERR_TYPE_TYPE_MISMATCH:
            idx = random_index(TYPE_FRIENDLY_COUNT);
            return type_error_friendly[idx];
        case ERR_TYPE_UNDEFINED:
            idx = random_index(UNDEFINED_FRIENDLY_COUNT);
            return undefined_error_friendly[idx];
        case ERR_TYPE_UNUSED:
            idx = random_index(UNUSED_FRIENDLY_COUNT);
            return unused_error_friendly[idx];
        case ERR_TYPE_UNREACHABLE:
            idx = random_index(UNREACHABLE_FRIENDLY_COUNT);
            return unreachable_error_friendly[idx];
        case ERR_TYPE_OVERFLOW:
            idx = random_index(OVERFLOW_FRIENDLY_COUNT);
            return overflow_error_friendly[idx];
        case ERR_TYPE_NULL:
            idx = random_index(NULL_FRIENDLY_COUNT);
            return null_error_friendly[idx];
        case ERR_TYPE_BOUNDS:
            idx = random_index(BOUNDS_FRIENDLY_COUNT);
            return bounds_error_friendly[idx];
        case ERR_TYPE_PERFORMANCE:
            idx = random_index(PERFORMANCE_FRIENDLY_COUNT);
            return performance_error_friendly[idx];
        default:
            return "Something went wrong here.";
        }
        
    case QISC_PERSONALITY_SNARKY:
        switch (type) {
        case ERR_TYPE_SYNTAX:
            idx = random_index(SYNTAX_SNARKY_COUNT);
            return syntax_error_snarky[idx];
        case ERR_TYPE_TYPE_MISMATCH:
            idx = random_index(TYPE_SNARKY_COUNT);
            return type_error_snarky[idx];
        case ERR_TYPE_UNDEFINED:
            idx = random_index(UNDEFINED_SNARKY_COUNT);
            return undefined_error_snarky[idx];
        case ERR_TYPE_UNUSED:
            idx = random_index(UNUSED_SNARKY_COUNT);
            return unused_error_snarky[idx];
        case ERR_TYPE_UNREACHABLE:
            idx = random_index(UNREACHABLE_SNARKY_COUNT);
            return unreachable_error_snarky[idx];
        case ERR_TYPE_OVERFLOW:
            idx = random_index(OVERFLOW_SNARKY_COUNT);
            return overflow_error_snarky[idx];
        case ERR_TYPE_NULL:
            idx = random_index(NULL_SNARKY_COUNT);
            return null_error_snarky[idx];
        case ERR_TYPE_BOUNDS:
            idx = random_index(BOUNDS_SNARKY_COUNT);
            return bounds_error_snarky[idx];
        case ERR_TYPE_PERFORMANCE:
            idx = random_index(PERFORMANCE_SNARKY_COUNT);
            return performance_error_snarky[idx];
        default:
            return "Error. You did something wrong. Shocker.";
        }
        
    case QISC_PERSONALITY_SAGE:
        switch (type) {
        case ERR_TYPE_SYNTAX:
            idx = random_index(SYNTAX_SAGE_COUNT);
            return syntax_error_sage[idx];
        case ERR_TYPE_TYPE_MISMATCH:
            idx = random_index(TYPE_SAGE_COUNT);
            return type_error_sage[idx];
        case ERR_TYPE_UNDEFINED:
            idx = random_index(UNDEFINED_SAGE_COUNT);
            return undefined_error_sage[idx];
        case ERR_TYPE_UNUSED:
            idx = random_index(UNUSED_SAGE_COUNT);
            return unused_error_sage[idx];
        case ERR_TYPE_UNREACHABLE:
            idx = random_index(UNREACHABLE_SAGE_COUNT);
            return unreachable_error_sage[idx];
        case ERR_TYPE_OVERFLOW:
            idx = random_index(OVERFLOW_SAGE_COUNT);
            return overflow_error_sage[idx];
        case ERR_TYPE_NULL:
            idx = random_index(NULL_SAGE_COUNT);
            return null_error_sage[idx];
        case ERR_TYPE_BOUNDS:
            idx = random_index(BOUNDS_SAGE_COUNT);
            return bounds_error_sage[idx];
        case ERR_TYPE_PERFORMANCE:
            idx = random_index(PERFORMANCE_SAGE_COUNT);
            return performance_error_sage[idx];
        default:
            return "An error has occurred. Reflect upon it.";
        }
        
    case QISC_PERSONALITY_CRYPTIC:
        switch (type) {
        case ERR_TYPE_SYNTAX:
            idx = random_index(SYNTAX_CRYPTIC_COUNT);
            return syntax_error_cryptic[idx];
        case ERR_TYPE_TYPE_MISMATCH:
            idx = random_index(TYPE_CRYPTIC_COUNT);
            return type_error_cryptic[idx];
        case ERR_TYPE_UNDEFINED:
            idx = random_index(UNDEFINED_CRYPTIC_COUNT);
            return undefined_error_cryptic[idx];
        default:
            return "Something stirs in the shadows of your code...";
        }
        
    default:
        return "Error";
    }
}

/*
 * Context-aware suggestions
 */

const char *suggest_for_type_error(const char *expected, const char *got) {
    if (!expected || !got) {
        return "Check that your types match what the function expects.";
    }
    
    /* int <-> string conversions */
    if (strcmp(expected, "int") == 0 && strcmp(got, "string") == 0) {
        return "Did you mean to call parseInt() or atoi()?";
    }
    if (strcmp(expected, "string") == 0 && strcmp(got, "int") == 0) {
        return "Use toString() or sprintf() to convert numbers to strings.";
    }
    
    /* bool <-> int conversions */
    if (strcmp(expected, "bool") == 0 && strcmp(got, "int") == 0) {
        return "Remember: 0 is false, everything else is true. Use '!= 0' to be explicit.";
    }
    if (strcmp(expected, "int") == 0 && strcmp(got, "bool") == 0) {
        return "Booleans are implicitly convertible to int (true=1, false=0).";
    }
    
    /* float <-> int */
    if (strcmp(expected, "float") == 0 && strcmp(got, "int") == 0) {
        return "Integer will be promoted to float. Add '.0' to make it explicit.";
    }
    if (strcmp(expected, "int") == 0 && strcmp(got, "float") == 0) {
        return "Use (int) cast or floor()/ceil()/round() to convert float to int.";
    }
    
    /* Pointer issues */
    if (strstr(expected, "*") != NULL && strstr(got, "*") == NULL) {
        return "Expected a pointer. Did you forget the '&' address-of operator?";
    }
    if (strstr(expected, "*") == NULL && strstr(got, "*") != NULL) {
        return "Got a pointer. Did you forget to dereference with '*'?";
    }
    
    /* Array to pointer decay */
    if (strstr(expected, "[]") != NULL || strstr(got, "[]") != NULL) {
        return "Arrays decay to pointers. Make sure dimensions match.";
    }
    
    return "Check that your types match what the function expects.";
}

const char *suggest_for_undefined(const char *name, const char *similar) {
    static char buffer[256];
    
    if (similar && strlen(similar) > 0) {
        snprintf(buffer, sizeof(buffer), 
                 "Did you mean '%s'? It looks similar to what you typed.", similar);
        return buffer;
    }
    
    if (name) {
        /* Check for common typos */
        if (strcmp(name, "lenght") == 0) {
            return "Did you mean 'length'? (Common typo!)";
        }
        if (strcmp(name, "retrun") == 0) {
            return "Did you mean 'return'? (The r and u got swapped!)";
        }
        if (strcmp(name, "pritnf") == 0 || strcmp(name, "pirntf") == 0) {
            return "Did you mean 'printf'? (Common typo!)";
        }
        if (strcmp(name, "mian") == 0) {
            return "Did you mean 'main'? (Happens to the best of us!)";
        }
        
        /* Check for case sensitivity issues */
        if (name[0] >= 'A' && name[0] <= 'Z') {
            snprintf(buffer, sizeof(buffer),
                     "Try lowercase '%c%s'? C is case-sensitive.", 
                     name[0] + 32, name + 1);
            return buffer;
        }
    }
    
    return "Make sure the variable is declared before use.";
}

const char *suggest_for_syntax(SyntaxErrorSubtype subtype) {
    switch (subtype) {
    case SYNTAX_MISSING_SEMICOLON:
        return "Add a semicolon ';' at the end of the statement.";
    case SYNTAX_MISSING_BRACE_OPEN:
        return "You need an opening brace '{' to start this block.";
    case SYNTAX_MISSING_BRACE_CLOSE:
        return "You closed a brace you never opened.\n"
               "It's like ending a conversation with 'goodbye'\n"
               "before saying 'hello'.";
    case SYNTAX_MISSING_PAREN_OPEN:
        return "Missing opening parenthesis '('.";
    case SYNTAX_MISSING_PAREN_CLOSE:
        return "Missing closing parenthesis ')'. Count your parens!";
    case SYNTAX_MISSING_BRACKET:
        return "Array brackets must be balanced. Check [ and ].";
    case SYNTAX_UNEXPECTED_TOKEN:
        return "This token doesn't belong here. Check the previous line too.";
    case SYNTAX_INVALID_EXPRESSION:
        return "This expression isn't valid. Check operators and operands.";
    default:
        return "Check syntax near this location.";
    }
}

const char *suggest_for_performance(const char *pattern) {
    if (!pattern) {
        return "Consider profiling your code to find bottlenecks.";
    }
    
    if (strcmp(pattern, "nested_loop") == 0) {
        return "Nested loops cause O(n²) complexity. Consider using a hash map\n"
               "or restructuring to reduce iterations.";
    }
    if (strcmp(pattern, "string_concat") == 0) {
        return "String concatenation in loops is slow. Use StringBuilder or\n"
               "pre-allocate the buffer.";
    }
    if (strcmp(pattern, "repeated_allocation") == 0) {
        return "Allocating memory in hot loops is expensive. Consider pooling\n"
               "or pre-allocating outside the loop.";
    }
    
    return "Profile your code to identify the actual bottleneck.";
}

/*
 * Error history tracking
 */

void error_record(ErrorType type) {
    if (!g_initialized) {
        error_personality_init();
    }
    
    if (type < 0 || type >= ERR_TYPE_COUNT) return;
    
    /* Find or create entry */
    for (int i = 0; i < QISC_ERROR_HISTORY_SIZE; i++) {
        if (g_error_history.entries[i].type == type) {
            g_error_history.entries[i].count++;
            g_error_history.entries[i].last_occurrence = time(NULL);
            g_error_history.total_errors++;
            g_error_history.session_errors++;
            return;
        }
        if (g_error_history.entries[i].count == 0) {
            g_error_history.entries[i].type = type;
            g_error_history.entries[i].count = 1;
            g_error_history.entries[i].last_occurrence = time(NULL);
            g_error_history.total_errors++;
            g_error_history.session_errors++;
            return;
        }
    }
}

int error_get_count(ErrorType type) {
    for (int i = 0; i < QISC_ERROR_HISTORY_SIZE; i++) {
        if (g_error_history.entries[i].type == type) {
            return g_error_history.entries[i].count;
        }
    }
    return 0;
}

int error_get_session_total(void) {
    return g_error_history.session_errors;
}

void error_reset_session(void) {
    g_error_history.session_errors = 0;
}

bool error_is_struggling(ErrorType type) {
    int count = error_get_count(type);
    return count >= 5;
}

const char *get_learning_message(ErrorType type) {
    int count = error_get_count(type);
    
    if (count < 5) return NULL;
    
    static char buffer[512];
    
    switch (type) {
    case ERR_TYPE_SYNTAX:
        if (count >= 10) {
            snprintf(buffer, sizeof(buffer),
                     "You've had %d syntax errors this session. Consider:\n"
                     "  - Using an editor with syntax highlighting\n"
                     "  - Adding an auto-formatter to catch these early", count);
        } else {
            snprintf(buffer, sizeof(buffer),
                     "You've made this mistake %d times. Tip: Read the line above\n"
                     "the error - often that's where the real problem is.", count);
        }
        return buffer;
        
    case ERR_TYPE_TYPE_MISMATCH:
        snprintf(buffer, sizeof(buffer),
                 "Type errors are your most common mistake (%d times).\n"
                 "Consider adding explicit type annotations to catch these\n"
                 "at declaration time.", count);
        return buffer;
        
    case ERR_TYPE_UNDEFINED:
        snprintf(buffer, sizeof(buffer),
                 "Undefined variable errors (%d times). Quick tips:\n"
                 "  - Declare variables at the top of blocks\n"
                 "  - Use consistent naming conventions\n"
                 "  - Check for typos in variable names", count);
        return buffer;
        
    case ERR_TYPE_NULL:
        snprintf(buffer, sizeof(buffer),
                 "Null pointer risks detected %d times. Consider:\n"
                 "  - Always check pointers before dereferencing\n"
                 "  - Use assert() for assumptions\n"
                 "  - Initialize pointers to NULL", count);
        return buffer;
        
    case ERR_TYPE_BOUNDS:
        snprintf(buffer, sizeof(buffer),
                 "Array bounds issues (%d times). Remember:\n"
                 "  - Arrays are 0-indexed: last element is array[size-1]\n"
                 "  - Use sizeof(arr)/sizeof(arr[0]) for array size\n"
                 "  - Validate indices from external input", count);
        return buffer;
        
    default:
        return NULL;
    }
}

/*
 * Visual formatting
 */

void error_print_context(int line, int col, const char *code,
                         const char *highlight_color) {
    if (!code) return;
    
    const char *color = highlight_color ? highlight_color : COLOR_RED;
    
    printf("%s%5d %s%s %s\n", COLOR_DIM, line, BOX_V, COLOR_RESET, code);
    
    /* Print caret pointing to error column */
    printf("%s      %s%s ", COLOR_DIM, BOX_V, COLOR_RESET);
    for (int i = 0; i < col - 1; i++) {
        printf(" ");
    }
    printf("%s^%s\n", color, COLOR_RESET);
}

void error_print_box(QiscPersonality p, const char *title, const char *message) {
    const char *title_color;
    const char *error_icon;
    
    switch (p) {
    case QISC_PERSONALITY_FRIENDLY:
        title_color = COLOR_YELLOW;
        error_icon = "⚠️ ";
        break;
    case QISC_PERSONALITY_SNARKY:
        title_color = COLOR_RED;
        error_icon = "💥 ";
        break;
    case QISC_PERSONALITY_SAGE:
        title_color = COLOR_MAGENTA;
        error_icon = "🔮 ";
        break;
    case QISC_PERSONALITY_CRYPTIC:
        title_color = COLOR_BLUE;
        error_icon = "✨ ";
        break;
    default:
        title_color = COLOR_RED;
        error_icon = "";
        break;
    }
    
    printf("\n");
    print_horizontal_line(BOX_TL, BOX_TR);
    
    /* Title line */
    char title_buf[128];
    snprintf(title_buf, sizeof(title_buf), "%s%s%s%s%s",
             error_icon, title_color, COLOR_BOLD, title, COLOR_RESET);
    print_box_line(title_buf);
    
    print_empty_line();
    
    /* Message - split on newlines */
    const char *start = message;
    const char *nl;
    while ((nl = strchr(start, '\n')) != NULL) {
        char line_buf[256];
        int len = (int)(nl - start);
        if (len >= (int)sizeof(line_buf)) len = sizeof(line_buf) - 1;
        strncpy(line_buf, start, len);
        line_buf[len] = '\0';
        print_box_line(line_buf);
        start = nl + 1;
    }
    if (*start) {
        print_box_line(start);
    }
    
    print_horizontal_line(BOX_BL, BOX_BR);
    printf("\n");
}

void error_print_suggestion(QiscPersonality p, const char *suggestion) {
    if (!suggestion) return;
    
    const char *prefix;
    switch (p) {
    case QISC_PERSONALITY_SNARKY:
        prefix = "Fine, I'll help: ";
        break;
    case QISC_PERSONALITY_SAGE:
        prefix = "The path forward: ";
        break;
    case QISC_PERSONALITY_CRYPTIC:
        prefix = "The stars suggest: ";
        break;
    default:
        prefix = "Suggestion: ";
        break;
    }
    
    printf("  %s%s%s%s%s\n", COLOR_GREEN, COLOR_BOLD, prefix, COLOR_RESET, suggestion);
}

void error_print_did_you_mean(QiscPersonality p, const char *original,
                              const char *suggestion) {
    if (!original || !suggestion) return;
    
    const char *prefix;
    switch (p) {
    case QISC_PERSONALITY_SNARKY:
        prefix = "I'm guessing you meant";
        break;
    case QISC_PERSONALITY_SAGE:
        prefix = "Perhaps you seek";
        break;
    case QISC_PERSONALITY_CRYPTIC:
        prefix = "The runes reveal";
        break;
    default:
        prefix = "Did you mean";
        break;
    }
    
    printf("  %s%s %s'%s'%s instead of %s'%s'%s?\n",
           COLOR_DIM, prefix, COLOR_GREEN, suggestion, COLOR_DIM,
           COLOR_RED, original, COLOR_RESET);
}

/*
 * Main error display functions
 */

void qisc_error_with_personality(QiscPersonality p, ErrorContext *ctx) {
    if (!ctx) return;
    
    /* Record error for learning */
    error_record(ctx->type);
    
    /* Get personality message */
    const char *msg = get_error_message(p, ctx->type);
    
    /* Format title */
    char title[64];
    const char *type_name;
    switch (ctx->type) {
    case ERR_TYPE_SYNTAX:      type_name = "Syntax Error"; break;
    case ERR_TYPE_TYPE_MISMATCH: type_name = "Type Error"; break;
    case ERR_TYPE_UNDEFINED:   type_name = "Undefined"; break;
    case ERR_TYPE_UNUSED:      type_name = "Unused Variable"; break;
    case ERR_TYPE_UNREACHABLE: type_name = "Unreachable Code"; break;
    case ERR_TYPE_OVERFLOW:    type_name = "Overflow Warning"; break;
    case ERR_TYPE_NULL:        type_name = "Null Pointer"; break;
    case ERR_TYPE_BOUNDS:      type_name = "Bounds Error"; break;
    case ERR_TYPE_PERFORMANCE: type_name = "Performance"; break;
    default:                   type_name = "Error"; break;
    }
    
    if (ctx->filename) {
        snprintf(title, sizeof(title), "%s [%s:%d:%d]",
                 type_name, ctx->filename, ctx->line, ctx->column);
    } else {
        snprintf(title, sizeof(title), "%s [line %d, col %d]",
                 type_name, ctx->line, ctx->column);
    }
    
    /* Print the error box */
    printf("\n");
    print_horizontal_line(BOX_TL, BOX_TR);
    print_box_line_colored(COLOR_BOLD, title);
    print_empty_line();
    print_box_line(msg);
    
    /* Show code context if available */
    if (ctx->code_context) {
        print_empty_line();
        
        char line_buf[128];
        snprintf(line_buf, sizeof(line_buf), "  Line %d: %s", 
                 ctx->line, ctx->code_context);
        print_box_line(line_buf);
        
        /* Print caret indicator */
        char caret_buf[128];
        int caret_offset = ctx->column + 10;
        if (caret_offset > 50) caret_offset = 50;
        snprintf(caret_buf, sizeof(caret_buf), "%*s%s^%s", 
                 caret_offset, "", COLOR_RED, COLOR_RESET);
        print_box_line(caret_buf);
    }
    
    /* Technical message if different from personality message */
    if (ctx->message && strcmp(ctx->message, msg) != 0) {
        print_empty_line();
        char detail_buf[256];
        snprintf(detail_buf, sizeof(detail_buf), "%s%s%s", 
                 COLOR_DIM, ctx->message, COLOR_RESET);
        print_box_line(detail_buf);
    }
    
    /* Snarky special: pretend reluctance to help */
    if (p == QISC_PERSONALITY_SNARKY) {
        print_empty_line();
        print_box_line_colored(COLOR_DIM, "(I found it, but I wanted you to learn)");
    }
    
    /* Suggestion */
    if (ctx->suggestion) {
        print_empty_line();
        char suggest_buf[256];
        snprintf(suggest_buf, sizeof(suggest_buf), "%s💡 %s%s",
                 COLOR_GREEN, ctx->suggestion, COLOR_RESET);
        print_box_line(suggest_buf);
    }
    
    /* Type-specific suggestions */
    if (ctx->type == ERR_TYPE_TYPE_MISMATCH && 
        ctx->expected_type && ctx->actual_type) {
        const char *type_suggest = suggest_for_type_error(ctx->expected_type, 
                                                          ctx->actual_type);
        if (type_suggest) {
            print_empty_line();
            print_box_line(type_suggest);
        }
    }
    
    /* Did-you-mean for undefined */
    if (ctx->type == ERR_TYPE_UNDEFINED && ctx->similar_name) {
        print_empty_line();
        char dym_buf[128];
        snprintf(dym_buf, sizeof(dym_buf), "Did you mean '%s'?", ctx->similar_name);
        print_box_line_colored(COLOR_GREEN, dym_buf);
    }
    
    /* Learning message for repeated errors */
    const char *learning = get_learning_message(ctx->type);
    if (learning) {
        print_empty_line();
        print_box_line_colored(COLOR_YELLOW, "📚 Learning tip:");
        
        /* Print learning message line by line */
        const char *start = learning;
        const char *nl;
        while ((nl = strchr(start, '\n')) != NULL) {
            char line_buf[256];
            int len = (int)(nl - start);
            if (len >= (int)sizeof(line_buf)) len = sizeof(line_buf) - 1;
            strncpy(line_buf, start, len);
            line_buf[len] = '\0';
            print_box_line(line_buf);
            start = nl + 1;
        }
        if (*start) {
            print_box_line(start);
        }
    }
    
    print_horizontal_line(BOX_BL, BOX_BR);
    printf("\n");
}

void qisc_error_personality(QiscPersonality p, ErrorType type,
                            int line, int col, const char *fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    ErrorContext ctx = {
        .type = type,
        .line = line,
        .column = col,
        .message = message,
        .code_context = NULL,
        .suggestion = NULL,
        .filename = NULL,
        .syntax_subtype = SYNTAX_GENERIC,
        .expected_type = NULL,
        .actual_type = NULL,
        .undefined_name = NULL,
        .similar_name = NULL,
    };
    
    qisc_error_with_personality(p, &ctx);
}

void qisc_error_full(QiscPersonality p, ErrorType type, int line, int col,
                     const char *filename, const char *code_context,
                     const char *fmt, ...) {
    char message[512];
    va_list args;
    va_start(args, fmt);
    vsnprintf(message, sizeof(message), fmt, args);
    va_end(args);
    
    ErrorContext ctx = {
        .type = type,
        .line = line,
        .column = col,
        .message = message,
        .code_context = code_context,
        .suggestion = NULL,
        .filename = filename,
        .syntax_subtype = SYNTAX_GENERIC,
        .expected_type = NULL,
        .actual_type = NULL,
        .undefined_name = NULL,
        .similar_name = NULL,
    };
    
    qisc_error_with_personality(p, &ctx);
}

/*
 * Custom message management
 */

typedef struct {
    QiscPersonality personality;
    ErrorType type;
    char *message;
} CustomMessage;

static CustomMessage *g_custom_messages = NULL;
static int g_custom_message_count = 0;

void error_set_custom_message(QiscPersonality p, ErrorType type,
                              const char *message) {
    /* Find existing */
    for (int i = 0; i < g_custom_message_count; i++) {
        if (g_custom_messages[i].personality == p &&
            g_custom_messages[i].type == type) {
            free(g_custom_messages[i].message);
            g_custom_messages[i].message = strdup(message);
            return;
        }
    }
    
    /* Add new */
    g_custom_messages = realloc(g_custom_messages,
                                sizeof(CustomMessage) * (g_custom_message_count + 1));
    g_custom_messages[g_custom_message_count].personality = p;
    g_custom_messages[g_custom_message_count].type = type;
    g_custom_messages[g_custom_message_count].message = strdup(message);
    g_custom_message_count++;
}

void error_reset_messages(void) {
    for (int i = 0; i < g_custom_message_count; i++) {
        free(g_custom_messages[i].message);
    }
    free(g_custom_messages);
    g_custom_messages = NULL;
    g_custom_message_count = 0;
}

/*
 * Initialization and cleanup
 */

void error_personality_init(void) {
    if (g_initialized) return;
    
    memset(&g_error_history, 0, sizeof(g_error_history));
    g_random_seed = (unsigned int)time(NULL);
    g_initialized = true;
}

void error_personality_cleanup(void) {
    error_reset_messages();
    g_initialized = false;
}
