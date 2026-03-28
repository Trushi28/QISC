/*
 * QISC Runtime I/O Support
 *
 * Small runtime helpers for Unix-style text workflows.
 */

#include "qisc_array.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *qisc_read_all(FILE *fp) {
  size_t capacity = 4096;
  size_t length = 0;
  char *buffer;

  if (!fp)
    return NULL;

  buffer = malloc(capacity);
  if (!buffer)
    return NULL;

  while (!feof(fp)) {
    size_t remaining = capacity - length;
    if (remaining < 1024) {
      char *grown;
      capacity *= 2;
      grown = realloc(buffer, capacity);
      if (!grown) {
        free(buffer);
        return NULL;
      }
      buffer = grown;
      remaining = capacity - length;
    }

    size_t nread = fread(buffer + length, 1, remaining - 1, fp);
    length += nread;

    if (ferror(fp)) {
      free(buffer);
      return NULL;
    }
  }

  buffer[length] = '\0';
  return buffer;
}

static char *qisc_read_line(FILE *fp) {
  size_t capacity = 256;
  size_t length = 0;
  char *buffer;
  int ch;

  if (!fp)
    return NULL;

  buffer = malloc(capacity);
  if (!buffer)
    return NULL;

  ch = fgetc(fp);
  if (ch == EOF) {
    free(buffer);
    return NULL;
  }

  do {
    if (ch == '\n')
      break;

    if (length + 2 >= capacity) {
      char *grown;
      capacity *= 2;
      grown = realloc(buffer, capacity);
      if (!grown) {
        free(buffer);
        return NULL;
      }
      buffer = grown;
    }

    buffer[length++] = (char)ch;
    ch = fgetc(fp);
  } while (ch != EOF);

  if (length > 0 && buffer[length - 1] == '\r')
    length--;

  buffer[length] = '\0';
  return buffer;
}

static void *qisc_read_lines(FILE *fp) {
  void *lines;
  char *line;

  if (!fp)
    return NULL;

  lines = __qisc_array_new(sizeof(char *), 8);
  if (!lines)
    return NULL;

  while ((line = qisc_read_line(fp)) != NULL) {
    void *next = __qisc_array_push(lines, &line);
    if (!next) {
      free(line);
      __qisc_array_free(lines);
      return NULL;
    }
    lines = next;
  }

  return lines;
}

char *__qisc_io_read_stdin(void) { return qisc_read_all(stdin); }

void *__qisc_io_read_stdin_lines(void) { return qisc_read_lines(stdin); }

char *__qisc_io_read_file(const char *path) {
  FILE *fp;
  char *text;

  if (!path)
    return NULL;

  fp = fopen(path, "rb");
  if (!fp)
    return NULL;

  text = qisc_read_all(fp);
  fclose(fp);
  return text;
}

void *__qisc_io_read_file_lines(const char *path) {
  FILE *fp;
  void *lines;

  if (!path)
    return NULL;

  fp = fopen(path, "rb");
  if (!fp)
    return NULL;

  lines = qisc_read_lines(fp);
  fclose(fp);
  return lines;
}

int64_t __qisc_io_write_stdout(const char *text) {
  size_t length;

  if (!text)
    return 0;

  length = strlen(text);
  fwrite(text, 1, length, stdout);
  fflush(stdout);
  return (int64_t)length;
}

int64_t __qisc_io_write_stderr(const char *text) {
  size_t length;

  if (!text)
    return 0;

  length = strlen(text);
  fwrite(text, 1, length, stderr);
  fflush(stderr);
  return (int64_t)length;
}
