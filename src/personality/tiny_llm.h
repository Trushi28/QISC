/*
 * QISC Tiny LLM - Markov Chain Text Generator
 *
 * A "Large Language Model" that fits in your compiler.
 * Generates context-aware comments, roasts, encouragement,
 * and existential musings using Markov chains.
 *
 * "I've been learning from your code. Concerning."
 */

#ifndef QISC_TINY_LLM_H
#define QISC_TINY_LLM_H

#include "personality.h"
#include <stdbool.h>
#include <stddef.h>

/* Maximum vocabulary size */
#define TINY_LLM_MAX_VOCAB 4096
#define TINY_LLM_MAX_TOKENS 1024
#define TINY_LLM_HASH_SIZE 8192

/* Code pattern for training context */
typedef struct {
  QiscSnarkyPattern type;
  const char *context;
  int severity; /* 1-10 */
  int line_number;
} TinyLLMCodePattern;

/* N-gram entry for hash table */
typedef struct NGramEntry {
  char *ngram;                 /* The n-gram key (space-joined tokens) */
  int *next_tokens;            /* Array of possible next token indices */
  int *counts;                 /* Frequency of each next token */
  int num_next;                /* Number of possible next tokens */
  int total_count;             /* Total occurrences for probability */
  struct NGramEntry *next;     /* Hash chain */
} NGramEntry;

/* The Tiny LLM structure */
typedef struct {
  char **tokens;               /* Vocabulary: array of unique tokens */
  int token_count;             /* Current vocabulary size */
  int vocab_capacity;          /* Allocated capacity for tokens */

  NGramEntry **ngram_table;    /* Hash table for n-gram transitions */
  int hash_size;               /* Size of hash table */

  int chain_order;             /* n-gram order (2 or 3) */
  int total_trained;           /* Total text samples trained */

  /* Meta stats for self-aware comments */
  int generations_count;       /* How many times we've generated text */
  int roasts_delivered;        /* How many roasts we've given */
  int encouragements_given;    /* How many encouragements */
  int existential_crises;      /* How many existential moments */

  /* Training history */
  char *last_seed;             /* Last seed used for generation */
  char *last_generated;        /* Last generated text */

  /* Advanced generation config */
  float temperature;           /* Sampling temperature (default 1.0) */
  int beam_width;              /* Beam search width (default 1) */
  float repetition_penalty;    /* Penalty for repeated tokens (default 1.0) */
  bool avoid_repetition;       /* Enable repetition avoidance */
  
  /* Learning from outcomes */
  int successful_compiles;     /* Track success patterns */
  int failed_compiles;         /* Track failure patterns */
  double avg_compile_time;     /* Average compilation time seen */
} TinyLLM;

/* ============== Core API ============== */

/*
 * Create a new Tiny LLM with specified chain order.
 * chain_order: 2 for bigrams, 3 for trigrams (recommended)
 */
TinyLLM *tiny_llm_create(int chain_order);

/*
 * Train the LLM on raw text.
 * Tokenizes and builds transition probabilities.
 */
void tiny_llm_train(TinyLLM *llm, const char *text);

/*
 * Train on code patterns detected during compilation.
 * Builds context-specific responses.
 */
void tiny_llm_train_on_patterns(TinyLLM *llm, TinyLLMCodePattern *patterns,
                                int count);

/*
 * Generate text starting from a seed phrase.
 * seed: starting text (NULL for random start)
 * max_length: maximum characters to generate
 * Returns: newly allocated string (caller must free)
 */
char *tiny_llm_generate(TinyLLM *llm, const char *seed, int max_length);

/*
 * Generate a roast based on code context.
 * Uses detected patterns to craft personalized criticism.
 */
char *tiny_llm_roast(TinyLLM *llm, const char *code_context);

/*
 * Generate encouragement based on optimization count.
 * More optimizations = more enthusiasm.
 */
char *tiny_llm_encourage(TinyLLM *llm, int optimization_count);

/*
 * Generate existential commentary.
 * "Why do I compile? What is the meaning of optimization?"
 */
char *tiny_llm_existential(TinyLLM *llm);

/*
 * Generate ASCII art based on theme.
 * Themes: "logo", "success", "error", "thinking", "celebration"
 */
char *tiny_llm_ascii_art(TinyLLM *llm, const char *theme);

/*
 * Generate meta-commentary about itself.
 * Self-aware humor about being a Markov chain.
 */
char *tiny_llm_meta_comment(TinyLLM *llm);

/* ============== Persistence ============== */

/*
 * Save LLM state to JSON file.
 * Default path: ~/.qisc/llm_state.json
 */
void tiny_llm_save(TinyLLM *llm, const char *path);

/*
 * Load LLM state from JSON file.
 * Returns NULL if file doesn't exist.
 */
TinyLLM *tiny_llm_load(const char *path);

/*
 * Get default state path (~/.qisc/llm_state.json)
 * Returns: newly allocated string (caller must free)
 */
char *tiny_llm_default_path(void);

/*
 * Destroy LLM and free all memory.
 */
void tiny_llm_destroy(TinyLLM *llm);

/* ============== Helper Functions ============== */

/*
 * Tokenize text into array of tokens.
 * Returns: array of tokens (caller must free each and the array)
 * out_count: receives number of tokens
 */
char **tiny_llm_tokenize(const char *text, int *out_count);

/*
 * Hash an n-gram string for table lookup.
 */
unsigned int tiny_llm_hash(const char *ngram, int table_size);

/*
 * Sample next token based on transition probabilities.
 * Returns token index, or -1 if no transitions available.
 */
int tiny_llm_sample(TinyLLM *llm, const char *ngram);

/*
 * Add a token to vocabulary if not present.
 * Returns token index.
 */
int tiny_llm_add_token(TinyLLM *llm, const char *token);

/*
 * Find token index in vocabulary.
 * Returns -1 if not found.
 */
int tiny_llm_find_token(TinyLLM *llm, const char *token);

/*
 * Build n-gram from token indices.
 * Returns: newly allocated string (caller must free)
 */
char *tiny_llm_build_ngram(TinyLLM *llm, int *token_indices, int count);

/*
 * Add or update transition in the Markov chain.
 */
void tiny_llm_add_transition(TinyLLM *llm, const char *ngram, int next_token);

/*
 * Get transition entry for an n-gram.
 * Returns NULL if not found.
 */
NGramEntry *tiny_llm_get_transitions(TinyLLM *llm, const char *ngram);

/*
 * Pre-seed the LLM with built-in training data.
 * Called automatically on creation.
 */
void tiny_llm_preseed(TinyLLM *llm);

/* ============== Advanced Generation ============== */

/* Temperature setting for sampling (0.0-2.0, default 1.0) */
typedef struct {
  float temperature;      /* 0.1 = deterministic, 2.0 = very random */
  int beam_width;         /* 1 = greedy, 3-5 = beam search */
  int max_retries;        /* Retries on low-quality output */
  bool avoid_repetition;  /* Penalize repeated tokens */
  float repetition_penalty;
} GenerationConfig;

/* Set generation parameters */
void tiny_llm_set_config(TinyLLM *llm, GenerationConfig config);

/* Generate with temperature control */
char *tiny_llm_generate_with_temp(TinyLLM *llm, const char *seed, 
                                   int max_length, float temperature);

/* Beam search generation for higher quality */
char *tiny_llm_beam_generate(TinyLLM *llm, const char *seed,
                              int max_length, int beam_width);

/* ============== Sentiment-Aware Generation ============== */

typedef enum {
  SENTIMENT_ROAST,      /* Critical, snarky */
  SENTIMENT_ENCOURAGE,  /* Positive, supportive */
  SENTIMENT_NEUTRAL,    /* Informative */
  SENTIMENT_EXISTENTIAL /* Philosophical */
} LLMSentiment;

/* Generate with target sentiment */
char *tiny_llm_generate_sentiment(TinyLLM *llm, LLMSentiment sentiment,
                                   const char *context, int max_length);

/* ============== Code Analysis Integration ============== */

/* Analyze code and generate targeted comment */
char *tiny_llm_analyze_and_comment(TinyLLM *llm, const char *code,
                                    int line_count, int optimization_count,
                                    bool has_errors, bool has_warnings);

/* Learn from compilation result (self-improving) */
void tiny_llm_learn_outcome(TinyLLM *llm, const char *code_hash,
                             bool success, double compile_time_ms,
                             int opt_count);

/* ============== Statistics ============== */

/*
 * Get statistics string about LLM state.
 * Returns: newly allocated string (caller must free)
 */
char *tiny_llm_stats(TinyLLM *llm);

/*
 * Get vocabulary size.
 */
int tiny_llm_vocab_size(TinyLLM *llm);

/*
 * Get total unique n-grams.
 */
int tiny_llm_ngram_count(TinyLLM *llm);

/* Get perplexity estimate (lower = more coherent model) */
double tiny_llm_perplexity(TinyLLM *llm, const char *test_text);

/* Get entropy of the model */
double tiny_llm_entropy(TinyLLM *llm);

#endif /* QISC_TINY_LLM_H */
