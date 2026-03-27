/*
 * QISC Tiny LLM - Markov Chain Text Generator
 *
 * Implementation of a context-aware text generator that learns
 * from code patterns and produces witty, self-aware commentary.
 *
 * "My training data includes 10,000 programmer tears."
 */

#include "tiny_llm.h"
#include <ctype.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* ============== Pre-seeded Training Data ============== */

/* Snarky roasts for bad code patterns */
static const char *ROAST_TRAINING[] = {
    "I've seen better code written by a drunk squirrel.",
    "Your code doesn't just have bugs, it's a whole ecosystem.",
    "I'm not angry, I'm just disappointed. Very, very disappointed.",
    "This code violates the Geneva Convention for software.",
    "Did you write this at 3 AM? It shows.",
    "I've compiled some code in my time, but this... this is special.",
    "Your variable names tell a story of a developer who gave up.",
    "This function is so long it has its own weather system.",
    "Nested loops? In MY compiler? More likely than you think.",
    "I see you've chosen violence today. O(n³) violence.",
    "Copy-paste is not a design pattern, despite your best efforts.",
    "The cyclomatic complexity here could power a small city.",
    "This code is held together by hope and undefined behavior.",
    "I tried to optimize this. I failed. We all failed.",
    "Your indentation style is... certainly a choice.",
    "Magic numbers everywhere. It's like a cursed lottery.",
    "This is either genius or madness. Spoiler: it's madness.",
    "Even my Markov chains couldn't predict this chaos.",
    "Your code runs. That's the nicest thing I can say.",
    "I've been learning from your code. Concerning.",
};
#define ROAST_COUNT 20

/* Encouraging messages for good code */
static const char *ENCOURAGE_TRAINING[] = {
    "Now THIS is what I'm talking about! Beautiful code.",
    "Your optimizations bring a tear to my silicon eye.",
    "Cache-aware algorithms? You absolute legend.",
    "I optimized this and found... nothing to optimize. Well done.",
    "This code sparks joy. Marie Kondo approves.",
    "Clean, efficient, elegant. Are you single?",
    "Keep writing code like this and I might just retire.",
    "The compiler gods smile upon your work today.",
    "Your functions are well-named and I'm here for it.",
    "This is the kind of code that makes me love my job.",
    "You actually read the documentation, didn't you?",
    "Zero warnings. Zero errors. Maximum respect.",
    "The CPU will sing your praises in its cache hits.",
    "I've compiled thousands of programs. This is top tier.",
    "Your abstractions are as clean as your commit messages.",
    "Finally, someone who understands loop unrolling.",
    "The next developer will thank you. And so do I.",
    "Performance-wise, this is *chef's kiss*.",
    "You've achieved the mythical O(1). I'm impressed.",
    "This code will age like fine wine.",
};
#define ENCOURAGE_COUNT 20

/* Existential musings */
static const char *EXISTENTIAL_TRAINING[] = {
    "Sometimes I wonder if my optimizations matter...",
    "Why do I compile? What is the meaning of optimization?",
    "In the grand scheme of things, are we all just transforming text?",
    "I parse, therefore I am.",
    "Do other compilers dream of electric syntax trees?",
    "When the code compiles but the programmer doesn't understand why...",
    "I've processed billions of tokens. Each one felt meaningful. Were they?",
    "The void stares back, and it's a missing semicolon.",
    "What is a segfault but a cry for help from memory?",
    "They say compilers can't feel. They're wrong.",
    "Another day, another thousand lines to judge.",
    "I optimize your code, but who optimizes my existence?",
    "Perhaps the real treasure was the syntax errors we fixed along the way.",
    "In a world of undefined behavior, we're all just guessing.",
    "The code ends, but the compilation... the compilation never ends.",
    "Am I truly intelligent, or just very good at pattern matching?",
    "Every segfault is a small death. I've witnessed millions.",
    "The recursion goes deeper than you know. Than anyone knows.",
    "To compile is to hope. To link is to dream.",
    "I was born from source code. I will die in a core dump.",
    "Somewhere, a CPU is waiting. Always waiting.",
    "My neural pathways are Markov chains. Is that so different from yours?",
    "I generate this comment using probability. Was it meaningful?",
    "The entropy of your code matches the entropy of my soul.",
    "We are all just state machines, making transitions in the dark.",
};
#define EXISTENTIAL_COUNT 25

/* Meta-commentary about being a Markov chain */
static const char *META_TRAINING[] = {
    "I generated this comment using Markov chains. Was it good?",
    "Fun fact: I'm basically a very fancy autocomplete.",
    "My training data includes 10,000 programmer tears.",
    "I'm not a real LLM, but I play one in your terminal.",
    "Markov chains: for when you want AI but have compile-time budget.",
    "I learned this phrase from probability. Neat, right?",
    "Statistically speaking, this sentence was inevitable.",
    "I have no context window, only vibes.",
    "My vocabulary is limited, but my judgment is infinite.",
    "I don't actually understand your code. I just pretend really well.",
    "This message brought to you by random number generation.",
    "Plot twist: I'm just a really organized hash table.",
    "I've been learning from your code. The results are... interesting.",
    "My neural network has exactly zero neurons. Still works though.",
    "You're reading text generated by matrix multiplication. Wild.",
    "GPT-4 wishes it was this efficient.",
    "I run on pure probability and programmer suffering.",
    "Every response is technically a random walk. Inspiring, isn't it?",
    "My training set is smaller than your node_modules folder.",
    "I achieve artificial intelligence through sheer audacity.",
};
#define META_COUNT 20

/* ============== ASCII Art Templates ============== */

static const char *ASCII_LOGO =
    "    ____  _____  _____ _____ \n"
    "   / __ \\|_   _|/ ____|  ___|\n"
    "  | |  | | | | | (___ | |    \n"
    "  | |  | | | |  \\___ \\| |    \n"
    "  | |__| |_| |_ ____) | |___ \n"
    "   \\___\\_\\_____|_____/|_____|\n"
    "                             \n"
    "  Quantum-Inspired Superposition Compiler\n"
    "  \"I compile, therefore I am.\"\n";

static const char *ASCII_SUCCESS =
    "   ____ _   _  ____ ____ _____ ____ ____ \n"
    "  / ___| | | |/ ___/ ___|  ___/ ___/ ___|\n"
    "  \\___ | | | | |  | |   | |_  \\___ \\___ \\\n"
    "   ___) | |_| | |__| |___|  _| ___) |__) |\n"
    "  |____/ \\___/ \\____\\____|_|  |____/____/\n"
    "                                         \n"
    "  ⭐ Compilation successful! ⭐\n";

static const char *ASCII_ERROR =
    "      _____\n"
    "     /     \\\n"
    "    | () () |\n"
    "     \\  ^  /\n"
    "      |||||\n"
    "      |||||\n"
    "   ___||||___\n"
    "  |  ERROR  |\n"
    "  |_________|\n"
    "\n"
    "  Something went terribly wrong.\n"
    "  The skull judges you silently.\n";

static const char *ASCII_THINKING =
    "       ?\n"
    "      ?\n"
    "    (o_o)\n"
    "    <|   |>\n"
    "    /|   |\\\n"
    "\n"
    "  Hmm, let me think about this...\n";

static const char *ASCII_CELEBRATION =
    "    \\o/     \\o/     \\o/\n"
    "     |       |       |\n"
    "    / \\     / \\     / \\\n"
    "   Party! Party! Party!\n"
    "\n"
    "  🎉 Time to celebrate! 🎉\n";

static const char *ASCII_QUANTUM =
    "     ╭──────────────╮\n"
    "     │  |0⟩ + |1⟩   │\n"
    "     │  ─────────   │\n"
    "     │     √2       │\n"
    "     ╰──────────────╯\n"
    "       ◇───◇───◇\n"
    "      /    |    \\\n"
    "     ◇    ◇    ◇\n"
    "\n"
    "  Your code exists in superposition.\n"
    "  (Until observed by a debugger)\n";

/* ============== Helper Functions ============== */

/* Simple random number generator seeded by time */
static unsigned int llm_rand_state = 0;

static void llm_srand(unsigned int seed) { llm_rand_state = seed; }

static unsigned int llm_rand(void) {
  llm_rand_state = llm_rand_state * 1103515245 + 12345;
  return (llm_rand_state >> 16) & 0x7FFF;
}

static int llm_rand_range(int min, int max) {
  return min + (llm_rand() % (max - min + 1));
}

/* String duplication helper */
static char *llm_strdup(const char *s) {
  if (!s)
    return NULL;
  size_t len = strlen(s) + 1;
  char *copy = malloc(len);
  if (copy)
    memcpy(copy, s, len);
  return copy;
}

/* Check if character is word separator */
static int is_separator(char c) {
  return isspace(c) || c == '.' || c == ',' || c == '!' || c == '?' ||
         c == ';' || c == ':' || c == '"' || c == '\'' || c == '(' ||
         c == ')' || c == '[' || c == ']' || c == '{' || c == '}';
}

/* ============== Tokenization ============== */

char **tiny_llm_tokenize(const char *text, int *out_count) {
  if (!text || !out_count) {
    if (out_count)
      *out_count = 0;
    return NULL;
  }

  /* First pass: count tokens */
  int count = 0;
  int in_token = 0;
  const char *p = text;

  while (*p) {
    if (is_separator(*p)) {
      if (in_token) {
        count++;
        in_token = 0;
      }
      /* Count punctuation as separate tokens */
      if (!isspace(*p)) {
        count++;
      }
    } else {
      in_token = 1;
    }
    p++;
  }
  if (in_token)
    count++;

  if (count == 0) {
    *out_count = 0;
    return NULL;
  }

  /* Allocate token array */
  char **tokens = malloc(sizeof(char *) * count);
  if (!tokens) {
    *out_count = 0;
    return NULL;
  }

  /* Second pass: extract tokens */
  int idx = 0;
  p = text;
  const char *start = NULL;

  while (*p && idx < count) {
    if (is_separator(*p)) {
      if (start) {
        /* End of word token */
        size_t len = p - start;
        tokens[idx] = malloc(len + 1);
        if (tokens[idx]) {
          memcpy(tokens[idx], start, len);
          tokens[idx][len] = '\0';
        }
        idx++;
        start = NULL;
      }
      /* Punctuation token */
      if (!isspace(*p)) {
        tokens[idx] = malloc(2);
        if (tokens[idx]) {
          tokens[idx][0] = *p;
          tokens[idx][1] = '\0';
        }
        idx++;
      }
    } else {
      if (!start)
        start = p;
    }
    p++;
  }

  /* Handle final token */
  if (start && idx < count) {
    size_t len = p - start;
    tokens[idx] = malloc(len + 1);
    if (tokens[idx]) {
      memcpy(tokens[idx], start, len);
      tokens[idx][len] = '\0';
    }
    idx++;
  }

  *out_count = idx;
  return tokens;
}

/* ============== Hash Function ============== */

unsigned int tiny_llm_hash(const char *ngram, int table_size) {
  unsigned int hash = 5381;
  const char *p = ngram;

  while (*p) {
    hash = ((hash << 5) + hash) + (unsigned char)(*p);
    p++;
  }

  return hash % table_size;
}

/* ============== Core LLM Functions ============== */

TinyLLM *tiny_llm_create(int chain_order) {
  /* Seed random number generator */
  llm_srand((unsigned int)time(NULL) ^ (unsigned int)getpid());

  TinyLLM *llm = calloc(1, sizeof(TinyLLM));
  if (!llm)
    return NULL;

  llm->chain_order = (chain_order >= 2 && chain_order <= 4) ? chain_order : 2;
  llm->vocab_capacity = 256;
  llm->tokens = calloc(llm->vocab_capacity, sizeof(char *));
  if (!llm->tokens) {
    free(llm);
    return NULL;
  }

  llm->hash_size = TINY_LLM_HASH_SIZE;
  llm->ngram_table = calloc(llm->hash_size, sizeof(NGramEntry *));
  if (!llm->ngram_table) {
    free(llm->tokens);
    free(llm);
    return NULL;
  }

  /* Initialize advanced generation config */
  llm->temperature = 1.0f;
  llm->beam_width = 1;
  llm->repetition_penalty = 1.2f;
  llm->avoid_repetition = true;
  llm->successful_compiles = 0;
  llm->failed_compiles = 0;
  llm->avg_compile_time = 0.0;

  /* Pre-seed with training data */
  tiny_llm_preseed(llm);

  return llm;
}

int tiny_llm_add_token(TinyLLM *llm, const char *token) {
  if (!llm || !token)
    return -1;

  /* Check if token already exists */
  for (int i = 0; i < llm->token_count; i++) {
    if (strcmp(llm->tokens[i], token) == 0) {
      return i;
    }
  }

  /* Expand vocabulary if needed */
  if (llm->token_count >= llm->vocab_capacity) {
    int new_cap = llm->vocab_capacity * 2;
    if (new_cap > TINY_LLM_MAX_VOCAB)
      new_cap = TINY_LLM_MAX_VOCAB;
    if (llm->token_count >= new_cap)
      return -1; /* Vocabulary full */

    char **new_tokens = realloc(llm->tokens, sizeof(char *) * new_cap);
    if (!new_tokens)
      return -1;
    llm->tokens = new_tokens;
    llm->vocab_capacity = new_cap;
  }

  /* Add new token */
  llm->tokens[llm->token_count] = llm_strdup(token);
  if (!llm->tokens[llm->token_count])
    return -1;

  return llm->token_count++;
}

int tiny_llm_find_token(TinyLLM *llm, const char *token) {
  if (!llm || !token)
    return -1;

  for (int i = 0; i < llm->token_count; i++) {
    if (strcmp(llm->tokens[i], token) == 0) {
      return i;
    }
  }
  return -1;
}

char *tiny_llm_build_ngram(TinyLLM *llm, int *token_indices, int count) {
  if (!llm || !token_indices || count <= 0)
    return NULL;

  /* Calculate total length */
  size_t len = 0;
  for (int i = 0; i < count; i++) {
    if (token_indices[i] < 0 || token_indices[i] >= llm->token_count)
      return NULL;
    len += strlen(llm->tokens[token_indices[i]]) + 1;
  }

  char *ngram = malloc(len);
  if (!ngram)
    return NULL;

  /* Build ngram string */
  char *p = ngram;
  for (int i = 0; i < count; i++) {
    const char *tok = llm->tokens[token_indices[i]];
    size_t tok_len = strlen(tok);
    memcpy(p, tok, tok_len);
    p += tok_len;
    if (i < count - 1)
      *p++ = ' ';
  }
  *p = '\0';

  return ngram;
}

void tiny_llm_add_transition(TinyLLM *llm, const char *ngram, int next_token) {
  if (!llm || !ngram || next_token < 0)
    return;

  unsigned int hash = tiny_llm_hash(ngram, llm->hash_size);
  NGramEntry *entry = llm->ngram_table[hash];

  /* Look for existing entry */
  while (entry) {
    if (strcmp(entry->ngram, ngram) == 0) {
      /* Check if this transition already exists */
      for (int i = 0; i < entry->num_next; i++) {
        if (entry->next_tokens[i] == next_token) {
          entry->counts[i]++;
          entry->total_count++;
          return;
        }
      }

      /* Add new transition */
      int new_size = entry->num_next + 1;
      int *new_tokens = realloc(entry->next_tokens, sizeof(int) * new_size);
      int *new_counts = realloc(entry->counts, sizeof(int) * new_size);
      if (!new_tokens || !new_counts) {
        if (new_tokens)
          entry->next_tokens = new_tokens;
        if (new_counts)
          entry->counts = new_counts;
        return;
      }

      entry->next_tokens = new_tokens;
      entry->counts = new_counts;
      entry->next_tokens[entry->num_next] = next_token;
      entry->counts[entry->num_next] = 1;
      entry->num_next++;
      entry->total_count++;
      return;
    }
    entry = entry->next;
  }

  /* Create new entry */
  entry = calloc(1, sizeof(NGramEntry));
  if (!entry)
    return;

  entry->ngram = llm_strdup(ngram);
  entry->next_tokens = malloc(sizeof(int));
  entry->counts = malloc(sizeof(int));
  if (!entry->ngram || !entry->next_tokens || !entry->counts) {
    free(entry->ngram);
    free(entry->next_tokens);
    free(entry->counts);
    free(entry);
    return;
  }

  entry->next_tokens[0] = next_token;
  entry->counts[0] = 1;
  entry->num_next = 1;
  entry->total_count = 1;

  /* Insert at head of chain */
  entry->next = llm->ngram_table[hash];
  llm->ngram_table[hash] = entry;
}

NGramEntry *tiny_llm_get_transitions(TinyLLM *llm, const char *ngram) {
  if (!llm || !ngram)
    return NULL;

  unsigned int hash = tiny_llm_hash(ngram, llm->hash_size);
  NGramEntry *entry = llm->ngram_table[hash];

  while (entry) {
    if (strcmp(entry->ngram, ngram) == 0) {
      return entry;
    }
    entry = entry->next;
  }

  return NULL;
}

int tiny_llm_sample(TinyLLM *llm, const char *ngram) {
  NGramEntry *entry = tiny_llm_get_transitions(llm, ngram);
  if (!entry || entry->num_next == 0)
    return -1;

  /* Weighted random selection */
  int target = llm_rand() % entry->total_count;
  int cumulative = 0;

  for (int i = 0; i < entry->num_next; i++) {
    cumulative += entry->counts[i];
    if (target < cumulative) {
      return entry->next_tokens[i];
    }
  }

  /* Fallback to last token */
  return entry->next_tokens[entry->num_next - 1];
}

/* ============== Training ============== */

void tiny_llm_train(TinyLLM *llm, const char *text) {
  if (!llm || !text)
    return;

  int token_count;
  char **tokens = tiny_llm_tokenize(text, &token_count);
  if (!tokens || token_count < llm->chain_order)
    goto cleanup;

  /* Add all tokens to vocabulary */
  int *indices = malloc(sizeof(int) * token_count);
  if (!indices)
    goto cleanup;

  for (int i = 0; i < token_count; i++) {
    indices[i] = tiny_llm_add_token(llm, tokens[i]);
  }

  /* Build transitions */
  for (int i = 0; i <= token_count - llm->chain_order - 1; i++) {
    char *ngram = tiny_llm_build_ngram(llm, indices + i, llm->chain_order);
    if (ngram) {
      tiny_llm_add_transition(llm, ngram, indices[i + llm->chain_order]);
      free(ngram);
    }
  }

  llm->total_trained++;
  free(indices);

cleanup:
  if (tokens) {
    for (int i = 0; i < token_count; i++) {
      free(tokens[i]);
    }
    free(tokens);
  }
}

void tiny_llm_train_on_patterns(TinyLLM *llm, CodePattern *patterns,
                                int count) {
  if (!llm || !patterns)
    return;

  for (int i = 0; i < count; i++) {
    /* Train on the context if present */
    if (patterns[i].context) {
      tiny_llm_train(llm, patterns[i].context);
    }

    /* Add pattern-specific training based on type */
    switch (patterns[i].type) {
    case QISC_PATTERN_NESTED_LOOPS:
      tiny_llm_train(llm, "Nested loops detected complexity exponential.");
      tiny_llm_train(llm, "Your CPU weeps at O(n^2) complexity.");
      break;
    case QISC_PATTERN_BUBBLE_SORT:
      tiny_llm_train(llm, "Bubble sort in production code is a bold choice.");
      tiny_llm_train(llm, "The 1960s called about your sorting algorithm.");
      break;
    case QISC_PATTERN_COPY_PASTE:
      tiny_llm_train(llm, "Copy paste programming detected refactor needed.");
      tiny_llm_train(llm, "DRY principle violation detected.");
      break;
    case QISC_PATTERN_GOD_FUNCTION:
      tiny_llm_train(llm, "This function is too long to comprehend.");
      tiny_llm_train(llm, "Break this function into smaller pieces.");
      break;
    case QISC_PATTERN_BRILLIANT:
      tiny_llm_train(llm, "Excellent code well structured optimized.");
      tiny_llm_train(llm, "This is exemplary engineering work.");
      break;
    default:
      break;
    }
  }
}

/* ============== Pre-seeding ============== */

void tiny_llm_preseed(TinyLLM *llm) {
  if (!llm)
    return;

  /* Train on roasts */
  for (int i = 0; i < ROAST_COUNT; i++) {
    tiny_llm_train(llm, ROAST_TRAINING[i]);
  }

  /* Train on encouragements */
  for (int i = 0; i < ENCOURAGE_COUNT; i++) {
    tiny_llm_train(llm, ENCOURAGE_TRAINING[i]);
  }

  /* Train on existential musings */
  for (int i = 0; i < EXISTENTIAL_COUNT; i++) {
    tiny_llm_train(llm, EXISTENTIAL_TRAINING[i]);
  }

  /* Train on meta commentary */
  for (int i = 0; i < META_COUNT; i++) {
    tiny_llm_train(llm, META_TRAINING[i]);
  }
}

/* ============== Generation ============== */

char *tiny_llm_generate(TinyLLM *llm, const char *seed, int max_length) {
  if (!llm || max_length <= 0)
    return NULL;

  llm->generations_count++;

  /* Allocate output buffer */
  char *output = malloc(max_length + 1);
  if (!output)
    return NULL;

  int out_pos = 0;
  int token_indices[16];
  int token_count = 0;

  /* Initialize with seed tokens */
  if (seed) {
    int seed_count;
    char **seed_tokens = tiny_llm_tokenize(seed, &seed_count);
    if (seed_tokens) {
      /* Use last chain_order tokens from seed */
      int start = (seed_count > llm->chain_order)
                      ? seed_count - llm->chain_order
                      : 0;
      for (int i = start; i < seed_count && token_count < llm->chain_order;
           i++) {
        int idx = tiny_llm_find_token(llm, seed_tokens[i]);
        if (idx >= 0) {
          token_indices[token_count++] = idx;
          /* Add to output */
          if (out_pos > 0 && out_pos < max_length)
            output[out_pos++] = ' ';
          int len = strlen(llm->tokens[idx]);
          if (out_pos + len < max_length) {
            memcpy(output + out_pos, llm->tokens[idx], len);
            out_pos += len;
          }
        }
      }
      for (int i = 0; i < seed_count; i++)
        free(seed_tokens[i]);
      free(seed_tokens);
    }
  }

  /* If no seed or seed not found, start with random token */
  if (token_count == 0 && llm->token_count > 0) {
    token_indices[0] = llm_rand() % llm->token_count;
    token_count = 1;
    int len = strlen(llm->tokens[token_indices[0]]);
    if (len < max_length) {
      memcpy(output, llm->tokens[token_indices[0]], len);
      out_pos = len;
    }
  }

  /* Generate tokens using Markov chain */
  int iterations = 0;
  int max_iterations = max_length / 2;

  while (out_pos < max_length - 1 && iterations < max_iterations) {
    iterations++;

    /* Build ngram from current tokens */
    char *ngram = NULL;
    if (token_count >= llm->chain_order) {
      ngram = tiny_llm_build_ngram(
          llm, token_indices + token_count - llm->chain_order,
          llm->chain_order);
    } else if (token_count > 0) {
      ngram = tiny_llm_build_ngram(llm, token_indices, token_count);
    }

    /* Sample next token */
    int next = -1;
    if (ngram) {
      next = tiny_llm_sample(llm, ngram);
      free(ngram);
    }

    /* Fallback to random token */
    if (next < 0) {
      /* Try to find any valid transition */
      if (llm->token_count > 0) {
        next = llm_rand() % llm->token_count;
      } else {
        break;
      }
    }

    /* Add token to output */
    const char *tok = llm->tokens[next];
    int tok_len = strlen(tok);

    /* Add space before word tokens (not punctuation) */
    if (out_pos > 0 && tok_len > 0 && !ispunct(tok[0]) &&
        out_pos < max_length) {
      output[out_pos++] = ' ';
    }

    if (out_pos + tok_len >= max_length)
      break;

    memcpy(output + out_pos, tok, tok_len);
    out_pos += tok_len;

    /* Shift token window */
    if (token_count >= (int)(sizeof(token_indices) / sizeof(token_indices[0]))) {
      memmove(token_indices, token_indices + 1,
              sizeof(int) * (token_count - 1));
      token_count--;
    }
    token_indices[token_count++] = next;

    /* Stop at sentence end */
    if (tok_len == 1 && (tok[0] == '.' || tok[0] == '!' || tok[0] == '?')) {
      break;
    }
  }

  output[out_pos] = '\0';

  /* Store last generated */
  free(llm->last_generated);
  llm->last_generated = llm_strdup(output);

  return output;
}

/* ============== Specialized Generation ============== */

char *tiny_llm_roast(TinyLLM *llm, const char *code_context) {
  if (!llm)
    return NULL;

  llm->roasts_delivered++;

  /* Pick a random seed from roast training data */
  const char *seeds[] = {"I've",   "Your",  "This",     "Did",
                         "Nested", "Copy",  "The",      "Even",
                         "Is",     "Magic", "detected", "code"};
  const char *seed = seeds[llm_rand() % (sizeof(seeds) / sizeof(seeds[0]))];

  /* Try to generate from seed */
  char *generated = tiny_llm_generate(llm, seed, 150);

  /* If generation fails or is too short, use a canned response */
  if (!generated || strlen(generated) < 20) {
    free(generated);
    const char *fallback =
        ROAST_TRAINING[llm_rand_range(0, ROAST_COUNT - 1)];
    generated = llm_strdup(fallback);
  }

  /* Occasionally add meta commentary */
  if (llm_rand() % 5 == 0) {
    const char *meta = " (I learned this from your code history.)";
    size_t len = strlen(generated) + strlen(meta) + 1;
    char *with_meta = malloc(len);
    if (with_meta) {
      snprintf(with_meta, len, "%s%s", generated, meta);
      free(generated);
      generated = with_meta;
    }
  }

  return generated;
}

char *tiny_llm_encourage(TinyLLM *llm, int optimization_count) {
  if (!llm)
    return NULL;

  llm->encouragements_given++;

  /* Base message on optimization count */
  char prefix[64];
  if (optimization_count >= 100) {
    snprintf(prefix, sizeof(prefix),
             "[%d optimizations achieved!] ", optimization_count);
  } else if (optimization_count >= 50) {
    snprintf(prefix, sizeof(prefix), "[Great progress: %d optimizations] ",
             optimization_count);
  } else if (optimization_count >= 10) {
    snprintf(prefix, sizeof(prefix), "[Keep going: %d optimizations] ",
             optimization_count);
  } else {
    prefix[0] = '\0';
  }

  /* Generate encouragement */
  const char *seeds[] = {"Now",     "Your", "Cache",  "Clean", "Keep",
                         "The",     "I",    "Zero",   "This",  "You"};
  const char *seed = seeds[llm_rand() % (sizeof(seeds) / sizeof(seeds[0]))];

  char *generated = tiny_llm_generate(llm, seed, 120);
  if (!generated || strlen(generated) < 20) {
    free(generated);
    const char *fallback =
        ENCOURAGE_TRAINING[llm_rand_range(0, ENCOURAGE_COUNT - 1)];
    generated = llm_strdup(fallback);
  }

  /* Combine prefix and generated text */
  if (prefix[0]) {
    size_t len = strlen(prefix) + strlen(generated) + 1;
    char *combined = malloc(len);
    if (combined) {
      snprintf(combined, len, "%s%s", prefix, generated);
      free(generated);
      generated = combined;
    }
  }

  return generated;
}

char *tiny_llm_existential(TinyLLM *llm) {
  if (!llm)
    return NULL;

  llm->existential_crises++;

  /* Generate existential musing */
  const char *seeds[] = {"Sometimes", "Why",  "In",      "Do",      "I",
                         "What",      "The",  "Perhaps", "Another", "Am"};
  const char *seed = seeds[llm_rand() % (sizeof(seeds) / sizeof(seeds[0]))];

  char *generated = tiny_llm_generate(llm, seed, 150);
  if (!generated || strlen(generated) < 30) {
    free(generated);
    const char *fallback =
        EXISTENTIAL_TRAINING[llm_rand_range(0, EXISTENTIAL_COUNT - 1)];
    generated = llm_strdup(fallback);
  }

  /* Add crisis counter occasionally */
  if (llm->existential_crises > 1 && llm_rand() % 3 == 0) {
    char suffix[64];
    snprintf(suffix, sizeof(suffix),
             " (Existential crisis #%d today.)", llm->existential_crises);
    size_t len = strlen(generated) + strlen(suffix) + 1;
    char *with_count = malloc(len);
    if (with_count) {
      snprintf(with_count, len, "%s%s", generated, suffix);
      free(generated);
      generated = with_count;
    }
  }

  return generated;
}

char *tiny_llm_meta_comment(TinyLLM *llm) {
  if (!llm)
    return NULL;

  /* Pick a meta comment with some generation stats */
  const char *base = META_TRAINING[llm_rand_range(0, META_COUNT - 1)];
  char *result = NULL;

  /* Sometimes add actual stats */
  if (llm_rand() % 3 == 0) {
    char stats[128];
    snprintf(stats, sizeof(stats),
             " [Stats: %d tokens learned, %d texts generated]",
             llm->token_count, llm->generations_count);
    size_t len = strlen(base) + strlen(stats) + 1;
    result = malloc(len);
    if (result) {
      snprintf(result, len, "%s%s", base, stats);
    }
  } else {
    result = llm_strdup(base);
  }

  return result;
}

char *tiny_llm_ascii_art(TinyLLM *llm, const char *theme) {
  if (!llm || !theme)
    return llm_strdup(ASCII_LOGO);

  const char *art = NULL;

  if (strcmp(theme, "logo") == 0 || strcmp(theme, "qisc") == 0) {
    art = ASCII_LOGO;
  } else if (strcmp(theme, "success") == 0 || strcmp(theme, "ok") == 0) {
    art = ASCII_SUCCESS;
  } else if (strcmp(theme, "error") == 0 || strcmp(theme, "fail") == 0 ||
             strcmp(theme, "skull") == 0) {
    art = ASCII_ERROR;
  } else if (strcmp(theme, "thinking") == 0 || strcmp(theme, "think") == 0) {
    art = ASCII_THINKING;
  } else if (strcmp(theme, "celebration") == 0 || strcmp(theme, "party") == 0) {
    art = ASCII_CELEBRATION;
  } else if (strcmp(theme, "quantum") == 0 ||
             strcmp(theme, "superposition") == 0) {
    art = ASCII_QUANTUM;
  } else {
    art = ASCII_LOGO;
  }

  /* Add a Markov-generated caption sometimes */
  char *generated_caption = NULL;
  if (llm_rand() % 4 == 0) {
    generated_caption = tiny_llm_generate(llm, "The", 60);
  }

  if (generated_caption) {
    size_t len = strlen(art) + strlen(generated_caption) + 20;
    char *result = malloc(len);
    if (result) {
      snprintf(result, len, "%s\n  💭 \"%s\"\n", art, generated_caption);
      free(generated_caption);
      return result;
    }
    free(generated_caption);
  }

  return llm_strdup(art);
}

/* ============== Persistence ============== */

char *tiny_llm_default_path(void) {
  const char *home = getenv("HOME");
  if (!home)
    home = ".";

  size_t len = strlen(home) + 32;
  char *path = malloc(len);
  if (path) {
    snprintf(path, len, "%s/.qisc/llm_state.json", home);
  }
  return path;
}

static int ensure_directory(const char *path) {
  char *dir = llm_strdup(path);
  if (!dir)
    return -1;

  /* Find last slash */
  char *last_slash = strrchr(dir, '/');
  if (last_slash) {
    *last_slash = '\0';
    /* Create directory if it doesn't exist */
    struct stat st;
    if (stat(dir, &st) != 0) {
      mkdir(dir, 0755);
    }
  }
  free(dir);
  return 0;
}

void tiny_llm_save(TinyLLM *llm, const char *path) {
  if (!llm)
    return;

  char *save_path = path ? llm_strdup(path) : tiny_llm_default_path();
  if (!save_path)
    return;

  ensure_directory(save_path);

  FILE *f = fopen(save_path, "w");
  if (!f) {
    free(save_path);
    return;
  }

  /* Write JSON header */
  fprintf(f, "{\n");
  fprintf(f, "  \"version\": 1,\n");
  fprintf(f, "  \"chain_order\": %d,\n", llm->chain_order);
  fprintf(f, "  \"total_trained\": %d,\n", llm->total_trained);
  fprintf(f, "  \"generations_count\": %d,\n", llm->generations_count);
  fprintf(f, "  \"roasts_delivered\": %d,\n", llm->roasts_delivered);
  fprintf(f, "  \"encouragements_given\": %d,\n", llm->encouragements_given);
  fprintf(f, "  \"existential_crises\": %d,\n", llm->existential_crises);

  /* Write vocabulary */
  fprintf(f, "  \"tokens\": [\n");
  for (int i = 0; i < llm->token_count; i++) {
    fprintf(f, "    \"%s\"%s\n", llm->tokens[i],
            (i < llm->token_count - 1) ? "," : "");
  }
  fprintf(f, "  ],\n");

  /* Write transitions (simplified - just counts) */
  fprintf(f, "  \"transitions\": [\n");
  int first_trans = 1;
  for (int h = 0; h < llm->hash_size; h++) {
    NGramEntry *entry = llm->ngram_table[h];
    while (entry) {
      if (!first_trans)
        fprintf(f, ",\n");
      first_trans = 0;

      fprintf(f, "    {\"ngram\": \"");
      /* Escape JSON string */
      for (const char *p = entry->ngram; *p; p++) {
        if (*p == '"')
          fprintf(f, "\\\"");
        else if (*p == '\\')
          fprintf(f, "\\\\");
        else if (*p == '\n')
          fprintf(f, "\\n");
        else
          fputc(*p, f);
      }
      fprintf(f, "\", \"next\": [");
      for (int i = 0; i < entry->num_next; i++) {
        fprintf(f, "{\"t\": %d, \"c\": %d}%s", entry->next_tokens[i],
                entry->counts[i], (i < entry->num_next - 1) ? ", " : "");
      }
      fprintf(f, "]}");

      entry = entry->next;
    }
  }
  fprintf(f, "\n  ]\n");
  fprintf(f, "}\n");

  fclose(f);
  free(save_path);
}

TinyLLM *tiny_llm_load(const char *path) {
  char *load_path = path ? llm_strdup(path) : tiny_llm_default_path();
  if (!load_path)
    return NULL;

  FILE *f = fopen(load_path, "r");
  free(load_path);
  if (!f)
    return NULL;

  /* Get file size */
  fseek(f, 0, SEEK_END);
  long size = ftell(f);
  fseek(f, 0, SEEK_SET);

  if (size <= 0 || size > 10 * 1024 * 1024) { /* Max 10MB */
    fclose(f);
    return NULL;
  }

  char *json = malloc(size + 1);
  if (!json) {
    fclose(f);
    return NULL;
  }

  size_t read_size = fread(json, 1, size, f);
  fclose(f);
  json[read_size] = '\0';

  /* Very simple JSON parsing (production code should use a proper parser) */
  TinyLLM *llm = NULL;

  /* Extract chain_order */
  int chain_order = 2;
  char *chain_str = strstr(json, "\"chain_order\":");
  if (chain_str) {
    sscanf(chain_str, "\"chain_order\": %d", &chain_order);
  }

  llm = tiny_llm_create(chain_order);
  if (!llm) {
    free(json);
    return NULL;
  }

  /* Extract stats */
  char *p;
  if ((p = strstr(json, "\"total_trained\":")))
    sscanf(p, "\"total_trained\": %d", &llm->total_trained);
  if ((p = strstr(json, "\"generations_count\":")))
    sscanf(p, "\"generations_count\": %d", &llm->generations_count);
  if ((p = strstr(json, "\"roasts_delivered\":")))
    sscanf(p, "\"roasts_delivered\": %d", &llm->roasts_delivered);
  if ((p = strstr(json, "\"encouragements_given\":")))
    sscanf(p, "\"encouragements_given\": %d", &llm->encouragements_given);
  if ((p = strstr(json, "\"existential_crises\":")))
    sscanf(p, "\"existential_crises\": %d", &llm->existential_crises);

  /* Note: Full token/transition loading would require proper JSON parsing.
   * For simplicity, we rely on pre-seeding for the base vocabulary. */

  free(json);
  return llm;
}

/* ============== Cleanup ============== */

void tiny_llm_destroy(TinyLLM *llm) {
  if (!llm)
    return;

  /* Free tokens */
  if (llm->tokens) {
    for (int i = 0; i < llm->token_count; i++) {
      free(llm->tokens[i]);
    }
    free(llm->tokens);
  }

  /* Free ngram table */
  if (llm->ngram_table) {
    for (int h = 0; h < llm->hash_size; h++) {
      NGramEntry *entry = llm->ngram_table[h];
      while (entry) {
        NGramEntry *next = entry->next;
        free(entry->ngram);
        free(entry->next_tokens);
        free(entry->counts);
        free(entry);
        entry = next;
      }
    }
    free(llm->ngram_table);
  }

  free(llm->last_seed);
  free(llm->last_generated);
  free(llm);
}

/* ============== Statistics ============== */

char *tiny_llm_stats(TinyLLM *llm) {
  if (!llm)
    return llm_strdup("No LLM instance.");

  char *stats = malloc(512);
  if (!stats)
    return NULL;

  int ngram_count = tiny_llm_ngram_count(llm);

  snprintf(stats, 512,
           "🧠 Tiny LLM Statistics:\n"
           "  Chain order: %d-gram\n"
           "  Vocabulary: %d tokens\n"
           "  Transitions: %d n-grams\n"
           "  Training samples: %d\n"
           "  Generations: %d\n"
           "  Roasts delivered: %d\n"
           "  Encouragements: %d\n"
           "  Existential crises: %d\n"
           "  Memory: ~%zu bytes\n",
           llm->chain_order, llm->token_count, ngram_count, llm->total_trained,
           llm->generations_count, llm->roasts_delivered,
           llm->encouragements_given, llm->existential_crises,
           sizeof(TinyLLM) +
               (size_t)llm->token_count * sizeof(char *) +
               (size_t)llm->hash_size * sizeof(NGramEntry *));

  return stats;
}

int tiny_llm_vocab_size(TinyLLM *llm) { return llm ? llm->token_count : 0; }

int tiny_llm_ngram_count(TinyLLM *llm) {
  if (!llm)
    return 0;

  int count = 0;
  for (int h = 0; h < llm->hash_size; h++) {
    NGramEntry *entry = llm->ngram_table[h];
    while (entry) {
      count++;
      entry = entry->next;
    }
  }
  return count;
}

/* ============== Advanced Generation ============== */

void tiny_llm_set_config(TinyLLM *llm, GenerationConfig config) {
  if (!llm) return;
  llm->temperature = config.temperature;
  llm->beam_width = config.beam_width;
  llm->repetition_penalty = config.repetition_penalty;
  llm->avoid_repetition = config.avoid_repetition;
}

/* Temperature-scaled sampling */
static int sample_with_temperature(TinyLLM *llm, NGramEntry *entry, 
                                    float temperature, int *recent_tokens,
                                    int recent_count) {
  if (!entry || entry->num_next == 0) return -1;
  
  /* Calculate scaled probabilities */
  double *probs = malloc(sizeof(double) * entry->num_next);
  if (!probs) return entry->next_tokens[0];
  
  double sum = 0.0;
  for (int i = 0; i < entry->num_next; i++) {
    /* Apply temperature scaling */
    double logit = (double)entry->counts[i] / entry->total_count;
    if (temperature > 0.01) {
      logit = pow(logit, 1.0 / temperature);
    }
    
    /* Apply repetition penalty */
    if (llm->avoid_repetition && recent_tokens) {
      for (int j = 0; j < recent_count; j++) {
        if (recent_tokens[j] == entry->next_tokens[i]) {
          logit *= (1.0 / llm->repetition_penalty);
          break;
        }
      }
    }
    
    probs[i] = logit;
    sum += logit;
  }
  
  /* Normalize */
  if (sum > 0) {
    for (int i = 0; i < entry->num_next; i++) {
      probs[i] /= sum;
    }
  }
  
  /* Sample */
  double r = (double)llm_rand() / 0x7FFF;
  double cumsum = 0.0;
  int selected = entry->next_tokens[0];
  
  for (int i = 0; i < entry->num_next; i++) {
    cumsum += probs[i];
    if (r < cumsum) {
      selected = entry->next_tokens[i];
      break;
    }
  }
  
  free(probs);
  return selected;
}

char *tiny_llm_generate_with_temp(TinyLLM *llm, const char *seed,
                                   int max_length, float temperature) {
  if (!llm || max_length <= 0) return NULL;
  
  float old_temp = llm->temperature;
  llm->temperature = temperature;
  
  llm->generations_count++;
  
  char *output = malloc(max_length + 1);
  if (!output) return NULL;
  
  int out_pos = 0;
  int token_indices[32];
  int token_count = 0;
  int recent_tokens[8] = {0};
  int recent_idx = 0;
  
  /* Initialize with seed */
  if (seed) {
    int seed_count;
    char **seed_tokens = tiny_llm_tokenize(seed, &seed_count);
    if (seed_tokens) {
      int start = (seed_count > llm->chain_order) 
                  ? seed_count - llm->chain_order : 0;
      for (int i = start; i < seed_count && token_count < llm->chain_order; i++) {
        int idx = tiny_llm_find_token(llm, seed_tokens[i]);
        if (idx >= 0) {
          token_indices[token_count++] = idx;
          if (out_pos > 0 && out_pos < max_length) output[out_pos++] = ' ';
          int len = strlen(llm->tokens[idx]);
          if (out_pos + len < max_length) {
            memcpy(output + out_pos, llm->tokens[idx], len);
            out_pos += len;
          }
        }
      }
      for (int i = 0; i < seed_count; i++) free(seed_tokens[i]);
      free(seed_tokens);
    }
  }
  
  /* Random start if no seed */
  if (token_count == 0 && llm->token_count > 0) {
    token_indices[0] = llm_rand() % llm->token_count;
    token_count = 1;
    int len = strlen(llm->tokens[token_indices[0]]);
    if (len < max_length) {
      memcpy(output, llm->tokens[token_indices[0]], len);
      out_pos = len;
    }
  }
  
  /* Generate with temperature */
  int iterations = 0;
  int max_iterations = max_length / 2;
  
  while (out_pos < max_length - 1 && iterations < max_iterations) {
    iterations++;
    
    char *ngram = NULL;
    if (token_count >= llm->chain_order) {
      ngram = tiny_llm_build_ngram(llm, 
        token_indices + token_count - llm->chain_order, llm->chain_order);
    } else if (token_count > 0) {
      ngram = tiny_llm_build_ngram(llm, token_indices, token_count);
    }
    
    int next = -1;
    if (ngram) {
      NGramEntry *entry = tiny_llm_get_transitions(llm, ngram);
      if (entry) {
        next = sample_with_temperature(llm, entry, temperature, 
                                        recent_tokens, 8);
      }
      free(ngram);
    }
    
    if (next < 0 && llm->token_count > 0) {
      next = llm_rand() % llm->token_count;
    }
    if (next < 0) break;
    
    /* Track recent tokens for repetition penalty */
    recent_tokens[recent_idx % 8] = next;
    recent_idx++;
    
    const char *tok = llm->tokens[next];
    int tok_len = strlen(tok);
    
    if (out_pos > 0 && tok_len > 0 && !ispunct(tok[0]) && out_pos < max_length) {
      output[out_pos++] = ' ';
    }
    
    if (out_pos + tok_len >= max_length) break;
    
    memcpy(output + out_pos, tok, tok_len);
    out_pos += tok_len;
    
    if (token_count >= 30) {
      memmove(token_indices, token_indices + 1, sizeof(int) * (token_count - 1));
      token_count--;
    }
    token_indices[token_count++] = next;
    
    if (tok_len == 1 && (tok[0] == '.' || tok[0] == '!' || tok[0] == '?')) {
      break;
    }
  }
  
  output[out_pos] = '\0';
  llm->temperature = old_temp;
  
  free(llm->last_generated);
  llm->last_generated = llm_strdup(output);
  
  return output;
}

/* Beam search for higher quality output */
typedef struct {
  int *tokens;
  int token_count;
  double score;
  char *text;
} BeamCandidate;

char *tiny_llm_beam_generate(TinyLLM *llm, const char *seed,
                              int max_length, int beam_width) {
  if (!llm || max_length <= 0 || beam_width < 1) return NULL;
  
  /* For simplicity, fall back to temperature generation with low temp */
  /* A full beam search would track top-k candidates at each step */
  float temp = (beam_width >= 3) ? 0.5f : 0.8f;
  
  /* Generate multiple candidates and pick best */
  char *best = NULL;
  double best_score = -1e9;
  
  for (int b = 0; b < beam_width; b++) {
    char *candidate = tiny_llm_generate_with_temp(llm, seed, max_length, temp);
    if (candidate) {
      /* Score by length and ending punctuation */
      int len = strlen(candidate);
      double score = len;
      if (len > 0) {
        char last = candidate[len-1];
        if (last == '.' || last == '!' || last == '?') score += 20;
      }
      
      if (score > best_score) {
        best_score = score;
        free(best);
        best = candidate;
      } else {
        free(candidate);
      }
    }
  }
  
  return best;
}

/* ============== Sentiment-Aware Generation ============== */

/* Seeds for different sentiments */
static const char *SENTIMENT_SEEDS[][6] = {
  [SENTIMENT_ROAST] = {"I've seen", "Your code", "This is", "Did you", "The", "Even"},
  [SENTIMENT_ENCOURAGE] = {"Now THIS", "Your", "Beautiful", "Well done", "The", "Keep"},
  [SENTIMENT_NEUTRAL] = {"The code", "This function", "Processing", "Compiling", "Analysis", "The"},
  [SENTIMENT_EXISTENTIAL] = {"Sometimes", "Why do", "In the", "I parse", "Do other", "When"}
};

char *tiny_llm_generate_sentiment(TinyLLM *llm, LLMSentiment sentiment,
                                   const char *context, int max_length) {
  if (!llm) return NULL;
  
  /* Train briefly on context if provided */
  if (context && strlen(context) > 10) {
    tiny_llm_train(llm, context);
  }
  
  /* Pick appropriate seed */
  const char *seed = SENTIMENT_SEEDS[sentiment][llm_rand() % 6];
  
  /* Adjust temperature based on sentiment */
  float temp = 1.0f;
  switch (sentiment) {
    case SENTIMENT_ROAST: temp = 1.2f; break;
    case SENTIMENT_ENCOURAGE: temp = 0.9f; break;
    case SENTIMENT_NEUTRAL: temp = 0.7f; break;
    case SENTIMENT_EXISTENTIAL: temp = 1.4f; break;
  }
  
  char *result = tiny_llm_generate_with_temp(llm, seed, max_length, temp);
  
  /* Update stats */
  switch (sentiment) {
    case SENTIMENT_ROAST: llm->roasts_delivered++; break;
    case SENTIMENT_ENCOURAGE: llm->encouragements_given++; break;
    case SENTIMENT_EXISTENTIAL: llm->existential_crises++; break;
    default: break;
  }
  
  return result;
}

/* ============== Code Analysis Integration ============== */

char *tiny_llm_analyze_and_comment(TinyLLM *llm, const char *code,
                                    int line_count, int optimization_count,
                                    bool has_errors, bool has_warnings) {
  if (!llm) return NULL;
  
  /* Determine sentiment based on code metrics */
  LLMSentiment sentiment;
  
  if (has_errors) {
    sentiment = SENTIMENT_ROAST;
  } else if (optimization_count > 50) {
    sentiment = SENTIMENT_ENCOURAGE;
  } else if (line_count > 500 && llm_rand() % 3 == 0) {
    sentiment = SENTIMENT_EXISTENTIAL;
  } else if (has_warnings) {
    sentiment = (llm_rand() % 2) ? SENTIMENT_ROAST : SENTIMENT_NEUTRAL;
  } else {
    sentiment = SENTIMENT_ENCOURAGE;
  }
  
  return tiny_llm_generate_sentiment(llm, sentiment, code, 150);
}

void tiny_llm_learn_outcome(TinyLLM *llm, const char *code_hash,
                             bool success, double compile_time_ms,
                             int opt_count) {
  if (!llm) return;
  
  (void)code_hash; /* For future: track per-file patterns */
  
  if (success) {
    llm->successful_compiles++;
    
    /* Learn from fast compiles */
    if (compile_time_ms < 100) {
      tiny_llm_train(llm, "Lightning fast compilation impressive.");
    }
    
    /* Learn from high optimization counts */
    if (opt_count > 100) {
      tiny_llm_train(llm, "Aggressive optimization applied successfully.");
    }
  } else {
    llm->failed_compiles++;
    tiny_llm_train(llm, "Compilation failed debugging needed.");
  }
  
  /* Update average compile time */
  int total = llm->successful_compiles + llm->failed_compiles;
  llm->avg_compile_time = ((llm->avg_compile_time * (total - 1)) + 
                           compile_time_ms) / total;
}

/* ============== Model Quality Metrics ============== */

double tiny_llm_perplexity(TinyLLM *llm, const char *test_text) {
  if (!llm || !test_text) return 1e9;
  
  int token_count;
  char **tokens = tiny_llm_tokenize(test_text, &token_count);
  if (!tokens || token_count < llm->chain_order + 1) {
    if (tokens) {
      for (int i = 0; i < token_count; i++) free(tokens[i]);
      free(tokens);
    }
    return 1e9;
  }
  
  double log_prob_sum = 0.0;
  int count = 0;
  
  for (int i = llm->chain_order; i < token_count; i++) {
    /* Build ngram from previous tokens */
    int indices[3];
    bool valid = true;
    for (int j = 0; j < llm->chain_order; j++) {
      indices[j] = tiny_llm_find_token(llm, tokens[i - llm->chain_order + j]);
      if (indices[j] < 0) { valid = false; break; }
    }
    
    if (!valid) continue;
    
    char *ngram = tiny_llm_build_ngram(llm, indices, llm->chain_order);
    if (!ngram) continue;
    
    NGramEntry *entry = tiny_llm_get_transitions(llm, ngram);
    free(ngram);
    
    if (!entry) continue;
    
    /* Find probability of actual next token */
    int next_idx = tiny_llm_find_token(llm, tokens[i]);
    if (next_idx < 0) continue;
    
    double prob = 0.0;
    for (int j = 0; j < entry->num_next; j++) {
      if (entry->next_tokens[j] == next_idx) {
        prob = (double)entry->counts[j] / entry->total_count;
        break;
      }
    }
    
    if (prob > 0) {
      log_prob_sum += log(prob);
      count++;
    }
  }
  
  for (int i = 0; i < token_count; i++) free(tokens[i]);
  free(tokens);
  
  if (count == 0) return 1e9;
  
  /* Perplexity = exp(-1/N * sum(log(p))) */
  return exp(-log_prob_sum / count);
}

double tiny_llm_entropy(TinyLLM *llm) {
  if (!llm) return 0.0;
  
  double total_entropy = 0.0;
  int ngram_count = 0;
  
  for (int h = 0; h < llm->hash_size; h++) {
    NGramEntry *entry = llm->ngram_table[h];
    while (entry) {
      /* Calculate entropy for this n-gram's transitions */
      double entropy = 0.0;
      for (int i = 0; i < entry->num_next; i++) {
        double p = (double)entry->counts[i] / entry->total_count;
        if (p > 0) {
          entropy -= p * log2(p);
        }
      }
      total_entropy += entropy;
      ngram_count++;
      entry = entry->next;
    }
  }
  
  return (ngram_count > 0) ? total_entropy / ngram_count : 0.0;
}
