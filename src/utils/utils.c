/*
 * QISC Utility Functions Implementation
 */

#include "utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Memory allocation with error checking */
void *qisc_alloc(size_t size) {
  void *ptr = malloc(size);
  if (ptr == NULL && size > 0) {
    fprintf(stderr, "Fatal: Out of memory\n");
    exit(1);
  }
  return ptr;
}

void *qisc_realloc(void *ptr, size_t size) {
  void *new_ptr = realloc(ptr, size);
  if (new_ptr == NULL && size > 0) {
    fprintf(stderr, "Fatal: Out of memory\n");
    exit(1);
  }
  return new_ptr;
}

void qisc_free(void *ptr) { free(ptr); }

char *qisc_strdup(const char *str) {
  size_t len = strlen(str) + 1;
  char *dup = qisc_alloc(len);
  memcpy(dup, str, len);
  return dup;
}

/* Read entire file into memory */
char *qisc_read_file(const char *path) {
  FILE *file = fopen(path, "rb");
  if (file == NULL) {
    return NULL;
  }

  fseek(file, 0, SEEK_END);
  long size = ftell(file);
  rewind(file);

  char *buffer = qisc_alloc(size + 1);
  size_t read = fread(buffer, 1, size, file);
  buffer[read] = '\0';

  fclose(file);
  return buffer;
}

/* Check if file exists */
bool qisc_file_exists(const char *path) {
  FILE *file = fopen(path, "r");
  if (file) {
    fclose(file);
    return true;
  }
  return false;
}

/* Write content to file */
bool qisc_write_file(const char *path, const char *content) {
  FILE *file = fopen(path, "wb");
  if (file == NULL) {
    return false;
  }

  size_t len = strlen(content);
  size_t written = fwrite(content, 1, len, file);
  fclose(file);

  return written == len;
}

/* String utilities */
bool qisc_str_ends_with(const char *str, const char *suffix) {
  size_t str_len = strlen(str);
  size_t suffix_len = strlen(suffix);
  if (suffix_len > str_len)
    return false;
  return strcmp(str + str_len - suffix_len, suffix) == 0;
}

char *qisc_str_concat(const char *a, const char *b) {
  size_t len_a = strlen(a);
  size_t len_b = strlen(b);
  char *result = qisc_alloc(len_a + len_b + 1);
  memcpy(result, a, len_a);
  memcpy(result + len_a, b, len_b + 1);
  return result;
}
