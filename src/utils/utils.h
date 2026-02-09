/*
 * QISC Utility Functions
 */

#ifndef QISC_UTILS_H
#define QISC_UTILS_H

#include <stdbool.h>
#include <stddef.h>

/* Memory utilities */
void *qisc_alloc(size_t size);
void *qisc_realloc(void *ptr, size_t size);
void qisc_free(void *ptr);
char *qisc_strdup(const char *str);

/* File utilities */
char *qisc_read_file(const char *path);
bool qisc_file_exists(const char *path);
bool qisc_write_file(const char *path, const char *content);

/* String utilities */
bool qisc_str_ends_with(const char *str, const char *suffix);
char *qisc_str_concat(const char *a, const char *b);

#endif /* QISC_UTILS_H */
