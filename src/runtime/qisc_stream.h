/*
 * QISC Stream Runtime
 * Lazy evaluation primitives for pipeline operations
 */

#ifndef QISC_STREAM_H
#define QISC_STREAM_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

/* Stream states */
typedef enum {
    STREAM_READY,      /* Has data available */
    STREAM_EMPTY,      /* Exhausted */
    STREAM_ERROR,      /* Error occurred */
} StreamState;

/* Stream element */
typedef struct {
    void *data;
    size_t size;
} StreamElement;

/* Stream source (produces elements) */
typedef struct QiscStream {
    void *context;                                     /* Internal state */
    StreamElement (*next)(struct QiscStream *self);    /* Get next element */
    void (*reset)(struct QiscStream *self);            /* Reset to start */
    void (*free)(struct QiscStream *self);             /* Cleanup */
    StreamState state;
} QiscStream;

/* Create stream from array */
QiscStream *stream_from_array(void *array, size_t elem_size, size_t count);

/* Create range stream (like Python's range) */
QiscStream *stream_range(int64_t start, int64_t end, int64_t step);

/* Infinite stream from generator function */
typedef void* (*GeneratorFunc)(void *state);
QiscStream *stream_generate(GeneratorFunc gen, void *initial_state);

/* Transform operations (return new lazy stream) */
typedef void* (*MapFunc)(void *element);
typedef bool (*FilterFunc)(void *element);
typedef void* (*ReduceFunc)(void *acc, void *element);

QiscStream *stream_map(QiscStream *source, MapFunc fn);
QiscStream *stream_filter(QiscStream *source, FilterFunc fn);

/* Terminal operations (consume stream, produce result) */
void *stream_reduce(QiscStream *source, ReduceFunc fn, void *initial);
void *stream_collect(QiscStream *source, size_t *out_count);
int64_t stream_count(QiscStream *source);
void *stream_first(QiscStream *source);
void *stream_last(QiscStream *source);
bool stream_any(QiscStream *source, FilterFunc pred);
bool stream_all(QiscStream *source, FilterFunc pred);

/* Stage fusion (combine adjacent map/filter operations) */
QiscStream *stream_fuse(QiscStream *s1, QiscStream *s2);

/* Cleanup */
void stream_free(QiscStream *stream);

/* ============================================================
 * Backpressure Handling
 * Producer slows when consumer can't keep up
 * ============================================================ */

/* Default buffer size for backpressure */
#define QISC_DEFAULT_BUFFER_SIZE 1024

/* Buffer states */
typedef enum {
    BUFFER_READY,      /* Space available / data available */
    BUFFER_FULL,       /* No space for producer */
    BUFFER_EMPTY,      /* No data for consumer */
    BUFFER_CLOSED,     /* No more data will be produced */
} BufferState;

/* Bounded buffer for pipeline stages (circular buffer) */
typedef struct {
    void **slots;              /* Array of element pointers */
    size_t *sizes;             /* Size of each element */
    size_t capacity;           /* Maximum number of elements */
    size_t head;               /* Read position */
    size_t tail;               /* Write position */
    size_t count;              /* Current element count */
    BufferState state;
    
    /* Synchronization (for multi-threaded use) */
    void *mutex;               /* Platform-specific mutex */
    void *not_full_cond;       /* Condition: buffer not full */
    void *not_empty_cond;      /* Condition: buffer not empty */
    
    /* Stats for debugging */
    uint64_t total_writes;
    uint64_t total_reads;
    uint64_t blocked_writes;   /* Times producer blocked */
    uint64_t blocked_reads;    /* Times consumer blocked */
} StreamBuffer;

/* Create bounded buffer with specified capacity */
StreamBuffer *stream_buffer_create(size_t capacity);

/* Write to buffer (blocks if full) */
bool stream_buffer_write(StreamBuffer *buf, void *data, size_t size);

/* Write without blocking (returns false if full) */
bool stream_buffer_try_write(StreamBuffer *buf, void *data, size_t size);

/* Read from buffer (blocks if empty) */
StreamElement stream_buffer_read(StreamBuffer *buf);

/* Read without blocking (returns NULL data if empty) */
StreamElement stream_buffer_try_read(StreamBuffer *buf);

/* Close buffer (no more writes accepted) */
void stream_buffer_close(StreamBuffer *buf);

/* Check buffer state */
bool stream_buffer_is_full(StreamBuffer *buf);
bool stream_buffer_is_empty(StreamBuffer *buf);
bool stream_buffer_is_closed(StreamBuffer *buf);
size_t stream_buffer_count(StreamBuffer *buf);

/* Free buffer and all contents */
void stream_buffer_free(StreamBuffer *buf);

/* ============================================================
 * Backpressure-Aware Stream Operations
 * ============================================================ */

/* Backpressure context for streams */
typedef struct {
    size_t buffer_size;        /* Configurable buffer size */
    bool lazy_pull;            /* Enable pull-based evaluation */
    size_t chunk_size;         /* Chunk size for auto-chunking */
    size_t items_consumed;     /* Track items for early termination */
    size_t max_items;          /* Limit (0 = unlimited) */
} BackpressureConfig;

/* Global default config (can be modified via pragma) */
extern BackpressureConfig __qisc_backpressure_config;

/* Set buffer size (called from #pragma buffer_size:N) */
void qisc_stream_set_buffer_size(size_t size);

/* Get current buffer size */
size_t qisc_stream_get_buffer_size(void);

/* Wait for backpressure (producer calls this) */
void qisc_stream_backpressure_wait(StreamBuffer *buf);

/* Signal ready for more data (consumer calls this) */
void qisc_stream_signal_ready(StreamBuffer *buf);

/* ============================================================
 * Buffered Pipeline Stream
 * Wraps source stream with bounded buffer for backpressure
 * ============================================================ */

typedef struct QiscBufferedStream QiscBufferedStream;

/* Create buffered stream wrapper */
QiscBufferedStream *stream_buffered_create(QiscStream *source, size_t buffer_size);

/* Get next element (pulls from buffer, blocks if needed) */
StreamElement stream_buffered_next(QiscBufferedStream *bs);

/* Check if stream is exhausted */
bool stream_buffered_is_done(QiscBufferedStream *bs);

/* Free buffered stream (also frees source) */
void stream_buffered_free(QiscBufferedStream *bs);

/* ============================================================
 * Chunked File Stream
 * Reads file in chunks for memory efficiency
 * ============================================================ */

typedef struct QiscChunkedFile QiscChunkedFile;

/* Open file for chunked reading */
QiscChunkedFile *stream_chunked_file_open(const char *path, size_t chunk_size);

/* Create stream from chunked file */
QiscStream *stream_from_chunked_file(QiscChunkedFile *cf);

/* Create line stream from file (reads chunks, yields lines) */
QiscStream *stream_file_lines(const char *path, size_t buffer_size);

/* Close and free chunked file */
void stream_chunked_file_close(QiscChunkedFile *cf);

/* ============================================================
 * Take/Limit Stream (Early Termination)
 * Supports pull-based lazy evaluation with limit
 * ============================================================ */

/* Create stream that yields at most n elements */
QiscStream *stream_take(QiscStream *source, size_t n);

/* Create stream that skips first n elements */
QiscStream *stream_skip(QiscStream *source, size_t n);

/* Create stream that yields elements while predicate is true */
QiscStream *stream_take_while(QiscStream *source, FilterFunc pred);

/* Create stream that skips elements while predicate is true */
QiscStream *stream_skip_while(QiscStream *source, FilterFunc pred);

#endif /* QISC_STREAM_H */
