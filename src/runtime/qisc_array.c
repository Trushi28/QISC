/*
 * QISC Array Runtime — Implementation
 *
 * Provides dynamic arrays with header-based length tracking,
 * bounds checking, and automatic resizing.
 */

#include "qisc_array.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

bool __qisc_bounds_check_enabled = true;

void __qisc_set_bounds_check(bool enabled) {
    __qisc_bounds_check_enabled = enabled;
}

static void bounds_check_fail(size_t index, size_t length) {
    fprintf(stderr, "QISC runtime error: array index %zu out of bounds (length %zu)\n",
            index, length);
    exit(1);
}

void* __qisc_array_new(size_t elem_size, size_t initial_capacity) {
    if (initial_capacity == 0) initial_capacity = 8;
    
    size_t total = sizeof(QiscArrayHeader) + (elem_size * initial_capacity);
    QiscArrayHeader *hdr = (QiscArrayHeader*)malloc(total);
    if (!hdr) return NULL;
    
    hdr->length = 0;
    hdr->capacity = initial_capacity;
    hdr->elem_size = elem_size;
    hdr->ref_count = 1;
    
    /* Zero-initialize the data area */
    void *data = QISC_ARRAY_DATA(hdr);
    memset(data, 0, elem_size * initial_capacity);
    
    return data;
}

void* __qisc_array_from(void *elements, size_t elem_size, size_t count) {
    if (count == 0) {
        return __qisc_array_new(elem_size, 8);
    }
    
    void *arr = __qisc_array_new(elem_size, count);
    if (!arr) return NULL;
    
    memcpy(arr, elements, elem_size * count);
    QISC_ARRAY_HEADER(arr)->length = count;
    return arr;
}

size_t __qisc_array_len(void *array) {
    if (!array) return 0;
    return QISC_ARRAY_HEADER(array)->length;
}

size_t __qisc_array_capacity(void *array) {
    if (!array) return 0;
    return QISC_ARRAY_HEADER(array)->capacity;
}

void* __qisc_array_get(void *array, size_t index) {
    if (!array) return NULL;
    
    QiscArrayHeader *hdr = QISC_ARRAY_HEADER(array);
    
    if (__qisc_bounds_check_enabled && index >= hdr->length) {
        bounds_check_fail(index, hdr->length);
    }
    
    return (char*)array + (index * hdr->elem_size);
}

void __qisc_array_set(void *array, size_t index, void *value) {
    if (!array || !value) return;
    
    QiscArrayHeader *hdr = QISC_ARRAY_HEADER(array);
    
    if (__qisc_bounds_check_enabled && index >= hdr->length) {
        bounds_check_fail(index, hdr->length);
    }
    
    char *dest = (char*)array + (index * hdr->elem_size);
    memcpy(dest, value, hdr->elem_size);
}

void* __qisc_array_resize(void *array, size_t new_capacity) {
    if (!array) return __qisc_array_new(sizeof(int64_t), new_capacity);
    
    QiscArrayHeader *hdr = QISC_ARRAY_HEADER(array);
    
    if (new_capacity < hdr->length) {
        new_capacity = hdr->length;
    }
    
    size_t total = sizeof(QiscArrayHeader) + (hdr->elem_size * new_capacity);
    QiscArrayHeader *new_hdr = (QiscArrayHeader*)realloc(hdr, total);
    if (!new_hdr) return array;
    
    /* Zero-initialize new space if growing */
    if (new_capacity > new_hdr->capacity) {
        void *new_data = (char*)QISC_ARRAY_DATA(new_hdr) + 
                         (new_hdr->capacity * new_hdr->elem_size);
        memset(new_data, 0, (new_capacity - new_hdr->capacity) * new_hdr->elem_size);
    }
    
    new_hdr->capacity = new_capacity;
    return QISC_ARRAY_DATA(new_hdr);
}

void* __qisc_array_push(void *array, void *element) {
    if (!array) {
        array = __qisc_array_new(sizeof(int64_t), 8);
        if (!array) return NULL;
    }
    
    QiscArrayHeader *hdr = QISC_ARRAY_HEADER(array);
    
    if (hdr->length >= hdr->capacity) {
        size_t new_cap = hdr->capacity * 2;
        array = __qisc_array_resize(array, new_cap);
        if (!array) return NULL;
        hdr = QISC_ARRAY_HEADER(array);
    }
    
    char *dest = (char*)array + (hdr->length * hdr->elem_size);
    memcpy(dest, element, hdr->elem_size);
    hdr->length++;
    
    return array;
}

void* __qisc_array_pop(void *array) {
    if (!array) return NULL;
    
    QiscArrayHeader *hdr = QISC_ARRAY_HEADER(array);
    
    if (hdr->length == 0) return NULL;
    
    hdr->length--;
    return (char*)array + (hdr->length * hdr->elem_size);
}

void* __qisc_array_slice(void *array, size_t start, size_t end) {
    if (!array) return NULL;
    
    QiscArrayHeader *hdr = QISC_ARRAY_HEADER(array);
    
    if (start >= hdr->length) start = hdr->length;
    if (end > hdr->length) end = hdr->length;
    if (end <= start) {
        return __qisc_array_new(hdr->elem_size, 8);
    }
    
    size_t count = end - start;
    void *new_arr = __qisc_array_new(hdr->elem_size, count);
    if (!new_arr) return NULL;
    
    char *src = (char*)array + (start * hdr->elem_size);
    memcpy(new_arr, src, count * hdr->elem_size);
    QISC_ARRAY_HEADER(new_arr)->length = count;
    
    return new_arr;
}

void* __qisc_array_concat(void *a, void *b) {
    if (!a && !b) return NULL;
    if (!a) return __qisc_array_slice(b, 0, __qisc_array_len(b));
    if (!b) return __qisc_array_slice(a, 0, __qisc_array_len(a));
    
    QiscArrayHeader *hdr_a = QISC_ARRAY_HEADER(a);
    QiscArrayHeader *hdr_b = QISC_ARRAY_HEADER(b);
    
    if (hdr_a->elem_size != hdr_b->elem_size) {
        fprintf(stderr, "QISC runtime error: cannot concat arrays with different element sizes\n");
        return NULL;
    }
    
    size_t total_len = hdr_a->length + hdr_b->length;
    void *new_arr = __qisc_array_new(hdr_a->elem_size, total_len);
    if (!new_arr) return NULL;
    
    memcpy(new_arr, a, hdr_a->length * hdr_a->elem_size);
    char *dest = (char*)new_arr + (hdr_a->length * hdr_a->elem_size);
    memcpy(dest, b, hdr_b->length * hdr_b->elem_size);
    
    QISC_ARRAY_HEADER(new_arr)->length = total_len;
    
    return new_arr;
}

void __qisc_array_free(void *array) {
    if (!array) return;
    QiscArrayHeader *hdr = QISC_ARRAY_HEADER(array);
    free(hdr);
}

void __qisc_array_retain(void *array) {
    if (!array) return;
    QISC_ARRAY_HEADER(array)->ref_count++;
}

void __qisc_array_release(void *array) {
    if (!array) return;
    QiscArrayHeader *hdr = QISC_ARRAY_HEADER(array);
    if (--hdr->ref_count == 0) {
        free(hdr);
    }
}
