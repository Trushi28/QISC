/*
 * QISC Array Runtime
 * Arrays with length tracking, bounds checking, and resize
 */

#ifndef QISC_ARRAY_H
#define QISC_ARRAY_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Array header (stored before array data) */
typedef struct {
    size_t length;       /* Number of elements */
    size_t capacity;     /* Allocated capacity */
    size_t elem_size;    /* Size of each element in bytes */
    uint32_t ref_count;  /* Reference counting for GC */
} QiscArrayHeader;

/* Get header from array data pointer */
#define QISC_ARRAY_HEADER(arr) \
    ((QiscArrayHeader*)((char*)(arr) - sizeof(QiscArrayHeader)))

/* Get data pointer from header */
#define QISC_ARRAY_DATA(hdr) \
    ((void*)((char*)(hdr) + sizeof(QiscArrayHeader)))

/* Create new array with given capacity */
void* __qisc_array_new(size_t elem_size, size_t initial_capacity);

/* Create array from elements */
void* __qisc_array_from(void *elements, size_t elem_size, size_t count);

/* Get array length */
size_t __qisc_array_len(void *array);

/* Get array capacity */
size_t __qisc_array_capacity(void *array);

/* Get element at index (with bounds check) */
void* __qisc_array_get(void *array, size_t index);

/* Set element at index (with bounds check) */
void __qisc_array_set(void *array, size_t index, void *value);

/* Push element to end (grows if needed) */
void* __qisc_array_push(void *array, void *element);

/* Pop element from end */
void* __qisc_array_pop(void *array);

/* Resize array to new capacity */
void* __qisc_array_resize(void *array, size_t new_capacity);

/* Slice array (creates new array) */
void* __qisc_array_slice(void *array, size_t start, size_t end);

/* Concatenate two arrays */
void* __qisc_array_concat(void *a, void *b);

/* Free array */
void __qisc_array_free(void *array);

/* Reference counting */
void __qisc_array_retain(void *array);
void __qisc_array_release(void *array);

/* Bounds checking control */
extern bool __qisc_bounds_check_enabled;
void __qisc_set_bounds_check(bool enabled);

#endif /* QISC_ARRAY_H */
