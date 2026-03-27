/*
 * QISC Stream Runtime Implementation
 * Lazy evaluation for pipeline operations
 */

#include "qisc_stream.h"
#include <stdlib.h>
#include <string.h>

/* ============================================================
 * Array Stream Implementation
 * ============================================================ */

typedef struct {
    void *array;
    size_t elem_size;
    size_t count;
    size_t index;
    bool owns_data;
} ArrayContext;

static StreamElement array_next(QiscStream *self) {
    ArrayContext *ctx = (ArrayContext *)self->context;
    
    if (ctx->index >= ctx->count) {
        self->state = STREAM_EMPTY;
        return (StreamElement){NULL, 0};
    }
    
    void *elem_ptr = (char *)ctx->array + (ctx->index * ctx->elem_size);
    ctx->index++;
    
    return (StreamElement){elem_ptr, ctx->elem_size};
}

static void array_reset(QiscStream *self) {
    ArrayContext *ctx = (ArrayContext *)self->context;
    ctx->index = 0;
    self->state = STREAM_READY;
}

static void array_free(QiscStream *self) {
    ArrayContext *ctx = (ArrayContext *)self->context;
    if (ctx->owns_data && ctx->array) {
        free(ctx->array);
    }
    free(ctx);
}

QiscStream *stream_from_array(void *array, size_t elem_size, size_t count) {
    if (!array || elem_size == 0) {
        return NULL;
    }
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    ArrayContext *ctx = malloc(sizeof(ArrayContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->array = array;
    ctx->elem_size = elem_size;
    ctx->count = count;
    ctx->index = 0;
    ctx->owns_data = false;
    
    stream->context = ctx;
    stream->next = array_next;
    stream->reset = array_reset;
    stream->free = array_free;
    stream->state = (count > 0) ? STREAM_READY : STREAM_EMPTY;
    
    return stream;
}

/* ============================================================
 * Range Stream Implementation
 * ============================================================ */

typedef struct {
    int64_t current;
    int64_t end;
    int64_t step;
    int64_t start;
    int64_t value_storage;  /* Storage for the current value */
} RangeContext;

static StreamElement range_next(QiscStream *self) {
    RangeContext *ctx = (RangeContext *)self->context;
    
    bool exhausted = (ctx->step > 0) ? (ctx->current >= ctx->end) 
                                      : (ctx->current <= ctx->end);
    
    if (exhausted) {
        self->state = STREAM_EMPTY;
        return (StreamElement){NULL, 0};
    }
    
    ctx->value_storage = ctx->current;
    ctx->current += ctx->step;
    
    return (StreamElement){&ctx->value_storage, sizeof(int64_t)};
}

static void range_reset(QiscStream *self) {
    RangeContext *ctx = (RangeContext *)self->context;
    ctx->current = ctx->start;
    self->state = STREAM_READY;
}

static void range_free(QiscStream *self) {
    free(self->context);
}

QiscStream *stream_range(int64_t start, int64_t end, int64_t step) {
    if (step == 0) {
        return NULL;  /* Infinite loop prevention */
    }
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    RangeContext *ctx = malloc(sizeof(RangeContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->current = start;
    ctx->start = start;
    ctx->end = end;
    ctx->step = step;
    ctx->value_storage = 0;
    
    stream->context = ctx;
    stream->next = range_next;
    stream->reset = range_reset;
    stream->free = range_free;
    
    /* Check if range is immediately empty */
    bool empty = (step > 0) ? (start >= end) : (start <= end);
    stream->state = empty ? STREAM_EMPTY : STREAM_READY;
    
    return stream;
}

/* ============================================================
 * Generator Stream Implementation
 * ============================================================ */

typedef struct {
    GeneratorFunc generator;
    void *state;
    void *current_value;
} GeneratorContext;

static StreamElement generator_next(QiscStream *self) {
    GeneratorContext *ctx = (GeneratorContext *)self->context;
    
    ctx->current_value = ctx->generator(ctx->state);
    
    if (ctx->current_value == NULL) {
        self->state = STREAM_EMPTY;
        return (StreamElement){NULL, 0};
    }
    
    return (StreamElement){ctx->current_value, sizeof(void *)};
}

static void generator_reset(QiscStream *self) {
    /* Generators cannot be reset without storing initial state */
    (void)self;
}

static void generator_free(QiscStream *self) {
    free(self->context);
}

QiscStream *stream_generate(GeneratorFunc gen, void *initial_state) {
    if (!gen) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    GeneratorContext *ctx = malloc(sizeof(GeneratorContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->generator = gen;
    ctx->state = initial_state;
    ctx->current_value = NULL;
    
    stream->context = ctx;
    stream->next = generator_next;
    stream->reset = generator_reset;
    stream->free = generator_free;
    stream->state = STREAM_READY;
    
    return stream;
}

/* ============================================================
 * Map Stream Implementation (Lazy Transform)
 * ============================================================ */

typedef struct {
    QiscStream *source;
    MapFunc mapper;
    void *mapped_value;
} MapContext;

static StreamElement map_next(QiscStream *self) {
    MapContext *ctx = (MapContext *)self->context;
    
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
        return (StreamElement){NULL, 0};
    }
    
    StreamElement elem = ctx->source->next(ctx->source);
    
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
        return (StreamElement){NULL, 0};
    }
    
    ctx->mapped_value = ctx->mapper(elem.data);
    
    return (StreamElement){ctx->mapped_value, sizeof(void *)};
}

static void map_reset(QiscStream *self) {
    MapContext *ctx = (MapContext *)self->context;
    if (ctx->source->reset) {
        ctx->source->reset(ctx->source);
    }
    self->state = ctx->source->state;
}

static void map_free(QiscStream *self) {
    MapContext *ctx = (MapContext *)self->context;
    stream_free(ctx->source);
    free(ctx);
}

QiscStream *stream_map(QiscStream *source, MapFunc fn) {
    if (!source || !fn) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    MapContext *ctx = malloc(sizeof(MapContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->source = source;
    ctx->mapper = fn;
    ctx->mapped_value = NULL;
    
    stream->context = ctx;
    stream->next = map_next;
    stream->reset = map_reset;
    stream->free = map_free;
    stream->state = source->state;
    
    return stream;
}

/* ============================================================
 * Filter Stream Implementation (Lazy Predicate)
 * ============================================================ */

typedef struct {
    QiscStream *source;
    FilterFunc predicate;
} FilterContext;

static StreamElement filter_next(QiscStream *self) {
    FilterContext *ctx = (FilterContext *)self->context;
    
    while (ctx->source->state == STREAM_READY) {
        StreamElement elem = ctx->source->next(ctx->source);
        
        if (ctx->source->state != STREAM_READY) {
            break;
        }
        
        if (ctx->predicate(elem.data)) {
            return elem;
        }
    }
    
    self->state = ctx->source->state;
    return (StreamElement){NULL, 0};
}

static void filter_reset(QiscStream *self) {
    FilterContext *ctx = (FilterContext *)self->context;
    if (ctx->source->reset) {
        ctx->source->reset(ctx->source);
    }
    self->state = ctx->source->state;
}

static void filter_free(QiscStream *self) {
    FilterContext *ctx = (FilterContext *)self->context;
    stream_free(ctx->source);
    free(ctx);
}

QiscStream *stream_filter(QiscStream *source, FilterFunc fn) {
    if (!source || !fn) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    FilterContext *ctx = malloc(sizeof(FilterContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->source = source;
    ctx->predicate = fn;
    
    stream->context = ctx;
    stream->next = filter_next;
    stream->reset = filter_reset;
    stream->free = filter_free;
    stream->state = source->state;
    
    return stream;
}

/* ============================================================
 * Terminal Operations (Consume Stream)
 * ============================================================ */

void *stream_reduce(QiscStream *source, ReduceFunc fn, void *initial) {
    if (!source || !fn) return initial;
    
    void *accumulator = initial;
    
    while (source->state == STREAM_READY) {
        StreamElement elem = source->next(source);
        
        if (source->state != STREAM_READY) {
            break;
        }
        
        accumulator = fn(accumulator, elem.data);
    }
    
    return accumulator;
}

void *stream_collect(QiscStream *source, size_t *out_count) {
    if (!source) {
        if (out_count) *out_count = 0;
        return NULL;
    }
    
    size_t capacity = 16;
    size_t count = 0;
    size_t elem_size = 0;
    void *result = NULL;
    
    while (source->state == STREAM_READY) {
        StreamElement elem = source->next(source);
        
        if (source->state != STREAM_READY || elem.data == NULL) {
            break;
        }
        
        /* Initialize on first element */
        if (result == NULL) {
            elem_size = elem.size;
            result = malloc(capacity * elem_size);
            if (!result) {
                if (out_count) *out_count = 0;
                return NULL;
            }
        }
        
        /* Grow buffer if needed */
        if (count >= capacity) {
            capacity *= 2;
            void *new_result = realloc(result, capacity * elem_size);
            if (!new_result) {
                free(result);
                if (out_count) *out_count = 0;
                return NULL;
            }
            result = new_result;
        }
        
        /* Copy element to result array */
        memcpy((char *)result + (count * elem_size), elem.data, elem_size);
        count++;
    }
    
    if (out_count) *out_count = count;
    return result;
}

int64_t stream_count(QiscStream *source) {
    if (!source) return 0;
    
    int64_t count = 0;
    
    while (source->state == STREAM_READY) {
        source->next(source);
        
        if (source->state == STREAM_READY) {
            count++;
        }
    }
    
    return count;
}

void *stream_first(QiscStream *source) {
    if (!source || source->state != STREAM_READY) {
        return NULL;
    }
    
    StreamElement elem = source->next(source);
    
    if (source->state != STREAM_READY) {
        return NULL;
    }
    
    return elem.data;
}

void *stream_last(QiscStream *source) {
    if (!source) return NULL;
    
    void *last = NULL;
    
    while (source->state == STREAM_READY) {
        StreamElement elem = source->next(source);
        
        if (source->state == STREAM_READY) {
            last = elem.data;
        }
    }
    
    return last;
}

bool stream_any(QiscStream *source, FilterFunc pred) {
    if (!source || !pred) return false;
    
    while (source->state == STREAM_READY) {
        StreamElement elem = source->next(source);
        
        if (source->state != STREAM_READY) {
            break;
        }
        
        if (pred(elem.data)) {
            return true;
        }
    }
    
    return false;
}

bool stream_all(QiscStream *source, FilterFunc pred) {
    if (!source || !pred) return true;
    
    while (source->state == STREAM_READY) {
        StreamElement elem = source->next(source);
        
        if (source->state != STREAM_READY) {
            break;
        }
        
        if (!pred(elem.data)) {
            return false;
        }
    }
    
    return true;
}

/* ============================================================
 * Stream Fusion (Combine Adjacent Operations)
 * ============================================================ */

typedef struct {
    QiscStream *s1;
    QiscStream *s2;
} FusedContext;

static StreamElement fused_next(QiscStream *self) {
    FusedContext *ctx = (FusedContext *)self->context;
    
    /* Try s1 first, then s2 */
    if (ctx->s1->state == STREAM_READY) {
        StreamElement elem = ctx->s1->next(ctx->s1);
        if (ctx->s1->state == STREAM_READY) {
            return elem;
        }
    }
    
    if (ctx->s2->state == STREAM_READY) {
        StreamElement elem = ctx->s2->next(ctx->s2);
        if (ctx->s2->state == STREAM_READY) {
            return elem;
        }
    }
    
    self->state = STREAM_EMPTY;
    return (StreamElement){NULL, 0};
}

static void fused_reset(QiscStream *self) {
    FusedContext *ctx = (FusedContext *)self->context;
    if (ctx->s1->reset) ctx->s1->reset(ctx->s1);
    if (ctx->s2->reset) ctx->s2->reset(ctx->s2);
    self->state = STREAM_READY;
}

static void fused_free(QiscStream *self) {
    FusedContext *ctx = (FusedContext *)self->context;
    stream_free(ctx->s1);
    stream_free(ctx->s2);
    free(ctx);
}

QiscStream *stream_fuse(QiscStream *s1, QiscStream *s2) {
    if (!s1 || !s2) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    FusedContext *ctx = malloc(sizeof(FusedContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->s1 = s1;
    ctx->s2 = s2;
    
    stream->context = ctx;
    stream->next = fused_next;
    stream->reset = fused_reset;
    stream->free = fused_free;
    stream->state = STREAM_READY;
    
    return stream;
}

/* ============================================================
 * Cleanup
 * ============================================================ */

void stream_free(QiscStream *stream) {
    if (!stream) return;
    
    if (stream->free) {
        stream->free(stream);
    }
    
    free(stream);
}

/* ============================================================
 * Backpressure - Global Configuration
 * ============================================================ */

BackpressureConfig __qisc_backpressure_config = {
    .buffer_size = QISC_DEFAULT_BUFFER_SIZE,
    .lazy_pull = true,
    .chunk_size = 4096,
    .items_consumed = 0,
    .max_items = 0,
};

void qisc_stream_set_buffer_size(size_t size) {
    if (size > 0) {
        __qisc_backpressure_config.buffer_size = size;
    }
}

size_t qisc_stream_get_buffer_size(void) {
    return __qisc_backpressure_config.buffer_size;
}

/* ============================================================
 * Stream Buffer Implementation (Bounded Circular Buffer)
 * ============================================================ */

StreamBuffer *stream_buffer_create(size_t capacity) {
    if (capacity == 0) {
        capacity = __qisc_backpressure_config.buffer_size;
    }
    
    StreamBuffer *buf = malloc(sizeof(StreamBuffer));
    if (!buf) return NULL;
    
    buf->slots = calloc(capacity, sizeof(void *));
    buf->sizes = calloc(capacity, sizeof(size_t));
    
    if (!buf->slots || !buf->sizes) {
        free(buf->slots);
        free(buf->sizes);
        free(buf);
        return NULL;
    }
    
    buf->capacity = capacity;
    buf->head = 0;
    buf->tail = 0;
    buf->count = 0;
    buf->state = BUFFER_EMPTY;
    
    buf->mutex = NULL;
    buf->not_full_cond = NULL;
    buf->not_empty_cond = NULL;
    
    buf->total_writes = 0;
    buf->total_reads = 0;
    buf->blocked_writes = 0;
    buf->blocked_reads = 0;
    
    return buf;
}

bool stream_buffer_is_full(StreamBuffer *buf) {
    return buf && buf->count >= buf->capacity;
}

bool stream_buffer_is_empty(StreamBuffer *buf) {
    return buf && buf->count == 0;
}

bool stream_buffer_is_closed(StreamBuffer *buf) {
    return buf && buf->state == BUFFER_CLOSED;
}

size_t stream_buffer_count(StreamBuffer *buf) {
    return buf ? buf->count : 0;
}

bool stream_buffer_try_write(StreamBuffer *buf, void *data, size_t size) {
    if (!buf || buf->state == BUFFER_CLOSED) {
        return false;
    }
    
    if (buf->count >= buf->capacity) {
        buf->state = BUFFER_FULL;
        return false;
    }
    
    /* Copy data into buffer slot */
    void *copy = malloc(size);
    if (!copy) return false;
    memcpy(copy, data, size);
    
    buf->slots[buf->tail] = copy;
    buf->sizes[buf->tail] = size;
    buf->tail = (buf->tail + 1) % buf->capacity;
    buf->count++;
    buf->total_writes++;
    
    buf->state = (buf->count >= buf->capacity) ? BUFFER_FULL : BUFFER_READY;
    
    return true;
}

bool stream_buffer_write(StreamBuffer *buf, void *data, size_t size) {
    if (!buf || buf->state == BUFFER_CLOSED) {
        return false;
    }
    
    /* Spin wait if buffer is full (blocking write) */
    while (buf->count >= buf->capacity && buf->state != BUFFER_CLOSED) {
        buf->blocked_writes++;
        qisc_stream_backpressure_wait(buf);
    }
    
    if (buf->state == BUFFER_CLOSED) {
        return false;
    }
    
    return stream_buffer_try_write(buf, data, size);
}

StreamElement stream_buffer_try_read(StreamBuffer *buf) {
    StreamElement result = {NULL, 0};
    
    if (!buf || buf->count == 0) {
        if (buf) buf->state = BUFFER_EMPTY;
        return result;
    }
    
    result.data = buf->slots[buf->head];
    result.size = buf->sizes[buf->head];
    
    buf->slots[buf->head] = NULL;
    buf->sizes[buf->head] = 0;
    buf->head = (buf->head + 1) % buf->capacity;
    buf->count--;
    buf->total_reads++;
    
    buf->state = (buf->count == 0) ? BUFFER_EMPTY : BUFFER_READY;
    
    /* Signal producer that space is available */
    qisc_stream_signal_ready(buf);
    
    return result;
}

StreamElement stream_buffer_read(StreamBuffer *buf) {
    StreamElement result = {NULL, 0};
    
    if (!buf) return result;
    
    /* Spin wait if buffer is empty (blocking read) */
    while (buf->count == 0 && buf->state != BUFFER_CLOSED) {
        buf->blocked_reads++;
        /* Brief yield to avoid busy-waiting */
        /* In threaded env, would use condition variable */
    }
    
    if (buf->count == 0 && buf->state == BUFFER_CLOSED) {
        return result;
    }
    
    return stream_buffer_try_read(buf);
}

void stream_buffer_close(StreamBuffer *buf) {
    if (buf) {
        buf->state = BUFFER_CLOSED;
    }
}

void stream_buffer_free(StreamBuffer *buf) {
    if (!buf) return;
    
    /* Free any remaining elements in buffer */
    for (size_t i = 0; i < buf->capacity; i++) {
        if (buf->slots[i]) {
            free(buf->slots[i]);
        }
    }
    
    free(buf->slots);
    free(buf->sizes);
    free(buf);
}

/* ============================================================
 * Backpressure Wait/Signal
 * ============================================================ */

void qisc_stream_backpressure_wait(StreamBuffer *buf) {
    if (!buf) return;
    
    /* Simple spin with yield - in production would use pthread_cond_wait */
    /* For single-threaded pipeline, this enables cooperative multitasking */
    volatile int spin = 0;
    while (spin++ < 1000 && stream_buffer_is_full(buf)) {
        /* Spin briefly to allow other code to run */
    }
}

void qisc_stream_signal_ready(StreamBuffer *buf) {
    if (!buf) return;
    
    /* In threaded environment, would signal condition variable */
    /* For single-threaded, state update is sufficient */
    if (buf->count < buf->capacity && buf->state != BUFFER_CLOSED) {
        buf->state = BUFFER_READY;
    }
}

/* ============================================================
 * Buffered Pipeline Stream
 * ============================================================ */

struct QiscBufferedStream {
    QiscStream *source;
    StreamBuffer *buffer;
    bool source_exhausted;
    size_t prefetch_count;     /* How many to prefetch */
};

/* Internal: prefetch elements from source into buffer */
static void buffered_prefetch(QiscBufferedStream *bs) {
    if (!bs || bs->source_exhausted) return;
    
    /* Fill buffer up to half capacity or prefetch_count */
    size_t to_fetch = bs->prefetch_count;
    if (to_fetch == 0) {
        to_fetch = bs->buffer->capacity / 2;
        if (to_fetch == 0) to_fetch = 1;
    }
    
    while (to_fetch > 0 && !stream_buffer_is_full(bs->buffer)) {
        if (bs->source->state != STREAM_READY) {
            bs->source_exhausted = true;
            break;
        }
        
        StreamElement elem = bs->source->next(bs->source);
        
        if (bs->source->state != STREAM_READY || elem.data == NULL) {
            bs->source_exhausted = true;
            break;
        }
        
        stream_buffer_try_write(bs->buffer, elem.data, elem.size);
        to_fetch--;
    }
    
    if (bs->source_exhausted && stream_buffer_is_empty(bs->buffer)) {
        stream_buffer_close(bs->buffer);
    }
}

QiscBufferedStream *stream_buffered_create(QiscStream *source, size_t buffer_size) {
    if (!source) return NULL;
    
    QiscBufferedStream *bs = malloc(sizeof(QiscBufferedStream));
    if (!bs) return NULL;
    
    bs->source = source;
    bs->buffer = stream_buffer_create(buffer_size);
    bs->source_exhausted = false;
    bs->prefetch_count = buffer_size / 2;
    
    if (!bs->buffer) {
        free(bs);
        return NULL;
    }
    
    /* Initial prefetch */
    buffered_prefetch(bs);
    
    return bs;
}

StreamElement stream_buffered_next(QiscBufferedStream *bs) {
    StreamElement result = {NULL, 0};
    
    if (!bs) return result;
    
    /* Try to read from buffer */
    result = stream_buffer_try_read(bs->buffer);
    
    if (result.data == NULL && !bs->source_exhausted) {
        /* Buffer empty, try to prefetch more */
        buffered_prefetch(bs);
        result = stream_buffer_try_read(bs->buffer);
    }
    
    /* Trigger prefetch for next call (pull-based) */
    if (!stream_buffer_is_full(bs->buffer) && !bs->source_exhausted) {
        buffered_prefetch(bs);
    }
    
    return result;
}

bool stream_buffered_is_done(QiscBufferedStream *bs) {
    return bs && bs->source_exhausted && stream_buffer_is_empty(bs->buffer);
}

void stream_buffered_free(QiscBufferedStream *bs) {
    if (!bs) return;
    
    stream_buffer_free(bs->buffer);
    stream_free(bs->source);
    free(bs);
}

/* ============================================================
 * Chunked File Stream Implementation
 * ============================================================ */

#include <stdio.h>

struct QiscChunkedFile {
    FILE *fp;
    char *chunk_buffer;
    size_t chunk_size;
    size_t bytes_read;
    bool eof_reached;
    char *current_chunk;       /* Points to current data */
    size_t current_size;       /* Size of current chunk */
};

QiscChunkedFile *stream_chunked_file_open(const char *path, size_t chunk_size) {
    if (!path) return NULL;
    
    FILE *fp = fopen(path, "rb");
    if (!fp) return NULL;
    
    if (chunk_size == 0) {
        chunk_size = __qisc_backpressure_config.chunk_size;
    }
    
    QiscChunkedFile *cf = malloc(sizeof(QiscChunkedFile));
    if (!cf) {
        fclose(fp);
        return NULL;
    }
    
    cf->chunk_buffer = malloc(chunk_size);
    if (!cf->chunk_buffer) {
        fclose(fp);
        free(cf);
        return NULL;
    }
    
    cf->fp = fp;
    cf->chunk_size = chunk_size;
    cf->bytes_read = 0;
    cf->eof_reached = false;
    cf->current_chunk = NULL;
    cf->current_size = 0;
    
    return cf;
}

/* Internal: read next chunk from file */
static bool chunked_file_read_next(QiscChunkedFile *cf) {
    if (!cf || cf->eof_reached) return false;
    
    cf->bytes_read = fread(cf->chunk_buffer, 1, cf->chunk_size, cf->fp);
    
    if (cf->bytes_read == 0) {
        cf->eof_reached = true;
        return false;
    }
    
    cf->current_chunk = cf->chunk_buffer;
    cf->current_size = cf->bytes_read;
    
    return true;
}

void stream_chunked_file_close(QiscChunkedFile *cf) {
    if (!cf) return;
    
    if (cf->fp) fclose(cf->fp);
    free(cf->chunk_buffer);
    free(cf);
}

/* Chunked file as stream context */
typedef struct {
    QiscChunkedFile *cf;
    char *storage;             /* Storage for returning chunk data */
    size_t storage_size;
} ChunkedFileContext;

static StreamElement chunked_file_next(QiscStream *self) {
    ChunkedFileContext *ctx = (ChunkedFileContext *)self->context;
    
    if (!chunked_file_read_next(ctx->cf)) {
        self->state = STREAM_EMPTY;
        return (StreamElement){NULL, 0};
    }
    
    /* Copy chunk data to storage for safe return */
    if (ctx->storage_size < ctx->cf->current_size) {
        free(ctx->storage);
        ctx->storage = malloc(ctx->cf->current_size);
        ctx->storage_size = ctx->cf->current_size;
    }
    
    if (ctx->storage) {
        memcpy(ctx->storage, ctx->cf->current_chunk, ctx->cf->current_size);
    }
    
    return (StreamElement){ctx->storage, ctx->cf->current_size};
}

static void chunked_file_reset(QiscStream *self) {
    ChunkedFileContext *ctx = (ChunkedFileContext *)self->context;
    if (ctx->cf && ctx->cf->fp) {
        fseek(ctx->cf->fp, 0, SEEK_SET);
        ctx->cf->eof_reached = false;
        ctx->cf->bytes_read = 0;
    }
    self->state = STREAM_READY;
}

static void chunked_file_free_stream(QiscStream *self) {
    ChunkedFileContext *ctx = (ChunkedFileContext *)self->context;
    stream_chunked_file_close(ctx->cf);
    free(ctx->storage);
    free(ctx);
}

QiscStream *stream_from_chunked_file(QiscChunkedFile *cf) {
    if (!cf) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    ChunkedFileContext *ctx = malloc(sizeof(ChunkedFileContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->cf = cf;
    ctx->storage = NULL;
    ctx->storage_size = 0;
    
    stream->context = ctx;
    stream->next = chunked_file_next;
    stream->reset = chunked_file_reset;
    stream->free = chunked_file_free_stream;
    stream->state = STREAM_READY;
    
    return stream;
}

/* ============================================================
 * File Lines Stream (Chunked Reading, Line Yielding)
 * ============================================================ */

typedef struct {
    QiscChunkedFile *cf;
    char *line_buffer;         /* Buffer for accumulating lines */
    size_t line_buffer_size;
    size_t line_buffer_used;
    char *chunk_pos;           /* Current position in chunk */
    size_t chunk_remaining;    /* Bytes remaining in chunk */
    char *current_line;        /* Storage for current line */
    size_t current_line_size;
} FileLinesContext;

static StreamElement file_lines_next(QiscStream *self) {
    FileLinesContext *ctx = (FileLinesContext *)self->context;
    
    while (1) {
        /* Search for newline in remaining chunk data */
        while (ctx->chunk_remaining > 0) {
            char c = *ctx->chunk_pos;
            ctx->chunk_pos++;
            ctx->chunk_remaining--;
            
            if (c == '\n' || c == '\r') {
                /* Skip \r\n combination */
                if (c == '\r' && ctx->chunk_remaining > 0 && *ctx->chunk_pos == '\n') {
                    ctx->chunk_pos++;
                    ctx->chunk_remaining--;
                }
                
                /* Return accumulated line */
                if (ctx->line_buffer_used > 0 || ctx->current_line_size > 0) {
                    /* Null-terminate line */
                    if (ctx->line_buffer_used >= ctx->current_line_size) {
                        ctx->current_line_size = ctx->line_buffer_used + 1;
                        ctx->current_line = realloc(ctx->current_line, ctx->current_line_size);
                    }
                    if (ctx->current_line) {
                        memcpy(ctx->current_line, ctx->line_buffer, ctx->line_buffer_used);
                        ctx->current_line[ctx->line_buffer_used] = '\0';
                    }
                    
                    size_t line_len = ctx->line_buffer_used;
                    ctx->line_buffer_used = 0;
                    
                    return (StreamElement){ctx->current_line, line_len + 1};
                }
                continue;  /* Empty line, skip */
            }
            
            /* Add character to line buffer */
            if (ctx->line_buffer_used >= ctx->line_buffer_size) {
                ctx->line_buffer_size = ctx->line_buffer_size ? ctx->line_buffer_size * 2 : 256;
                ctx->line_buffer = realloc(ctx->line_buffer, ctx->line_buffer_size);
                if (!ctx->line_buffer) {
                    self->state = STREAM_ERROR;
                    return (StreamElement){NULL, 0};
                }
            }
            ctx->line_buffer[ctx->line_buffer_used++] = c;
        }
        
        /* Read next chunk */
        if (!chunked_file_read_next(ctx->cf)) {
            /* EOF - return any remaining data as final line */
            if (ctx->line_buffer_used > 0) {
                if (ctx->line_buffer_used >= ctx->current_line_size) {
                    ctx->current_line_size = ctx->line_buffer_used + 1;
                    ctx->current_line = realloc(ctx->current_line, ctx->current_line_size);
                }
                if (ctx->current_line) {
                    memcpy(ctx->current_line, ctx->line_buffer, ctx->line_buffer_used);
                    ctx->current_line[ctx->line_buffer_used] = '\0';
                }
                
                size_t line_len = ctx->line_buffer_used;
                ctx->line_buffer_used = 0;
                
                self->state = STREAM_EMPTY;  /* Mark exhausted after this */
                return (StreamElement){ctx->current_line, line_len + 1};
            }
            
            self->state = STREAM_EMPTY;
            return (StreamElement){NULL, 0};
        }
        
        ctx->chunk_pos = ctx->cf->current_chunk;
        ctx->chunk_remaining = ctx->cf->current_size;
    }
}

static void file_lines_reset(QiscStream *self) {
    FileLinesContext *ctx = (FileLinesContext *)self->context;
    if (ctx->cf && ctx->cf->fp) {
        fseek(ctx->cf->fp, 0, SEEK_SET);
        ctx->cf->eof_reached = false;
        ctx->cf->bytes_read = 0;
    }
    ctx->line_buffer_used = 0;
    ctx->chunk_pos = NULL;
    ctx->chunk_remaining = 0;
    self->state = STREAM_READY;
}

static void file_lines_free(QiscStream *self) {
    FileLinesContext *ctx = (FileLinesContext *)self->context;
    stream_chunked_file_close(ctx->cf);
    free(ctx->line_buffer);
    free(ctx->current_line);
    free(ctx);
}

QiscStream *stream_file_lines(const char *path, size_t buffer_size) {
    QiscChunkedFile *cf = stream_chunked_file_open(path, buffer_size);
    if (!cf) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) {
        stream_chunked_file_close(cf);
        return NULL;
    }
    
    FileLinesContext *ctx = malloc(sizeof(FileLinesContext));
    if (!ctx) {
        stream_chunked_file_close(cf);
        free(stream);
        return NULL;
    }
    
    ctx->cf = cf;
    ctx->line_buffer = NULL;
    ctx->line_buffer_size = 0;
    ctx->line_buffer_used = 0;
    ctx->chunk_pos = NULL;
    ctx->chunk_remaining = 0;
    ctx->current_line = NULL;
    ctx->current_line_size = 0;
    
    stream->context = ctx;
    stream->next = file_lines_next;
    stream->reset = file_lines_reset;
    stream->free = file_lines_free;
    stream->state = STREAM_READY;
    
    return stream;
}

/* ============================================================
 * Take/Skip Stream Implementations (Early Termination)
 * ============================================================ */

typedef struct {
    QiscStream *source;
    size_t limit;
    size_t count;
} TakeContext;

static StreamElement take_next(QiscStream *self) {
    TakeContext *ctx = (TakeContext *)self->context;
    
    if (ctx->count >= ctx->limit) {
        self->state = STREAM_EMPTY;
        return (StreamElement){NULL, 0};
    }
    
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
        return (StreamElement){NULL, 0};
    }
    
    StreamElement elem = ctx->source->next(ctx->source);
    
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
        return (StreamElement){NULL, 0};
    }
    
    ctx->count++;
    
    /* Check if we've reached limit (for early termination) */
    if (ctx->count >= ctx->limit) {
        self->state = STREAM_EMPTY;
    }
    
    return elem;
}

static void take_reset(QiscStream *self) {
    TakeContext *ctx = (TakeContext *)self->context;
    ctx->count = 0;
    if (ctx->source->reset) {
        ctx->source->reset(ctx->source);
    }
    self->state = (ctx->limit > 0) ? STREAM_READY : STREAM_EMPTY;
}

static void take_free(QiscStream *self) {
    TakeContext *ctx = (TakeContext *)self->context;
    stream_free(ctx->source);
    free(ctx);
}

QiscStream *stream_take(QiscStream *source, size_t n) {
    if (!source) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    TakeContext *ctx = malloc(sizeof(TakeContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->source = source;
    ctx->limit = n;
    ctx->count = 0;
    
    stream->context = ctx;
    stream->next = take_next;
    stream->reset = take_reset;
    stream->free = take_free;
    stream->state = (n > 0 && source->state == STREAM_READY) ? STREAM_READY : STREAM_EMPTY;
    
    return stream;
}

typedef struct {
    QiscStream *source;
    size_t skip_count;
    size_t skipped;
    bool skip_done;
} SkipContext;

static StreamElement skip_next(QiscStream *self) {
    SkipContext *ctx = (SkipContext *)self->context;
    
    /* Skip initial elements if not done */
    while (!ctx->skip_done && ctx->skipped < ctx->skip_count) {
        if (ctx->source->state != STREAM_READY) {
            self->state = ctx->source->state;
            return (StreamElement){NULL, 0};
        }
        
        ctx->source->next(ctx->source);
        ctx->skipped++;
        
        if (ctx->source->state != STREAM_READY) {
            self->state = ctx->source->state;
            return (StreamElement){NULL, 0};
        }
    }
    ctx->skip_done = true;
    
    /* Return elements from source */
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
        return (StreamElement){NULL, 0};
    }
    
    StreamElement elem = ctx->source->next(ctx->source);
    
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
    }
    
    return elem;
}

static void skip_reset(QiscStream *self) {
    SkipContext *ctx = (SkipContext *)self->context;
    ctx->skipped = 0;
    ctx->skip_done = false;
    if (ctx->source->reset) {
        ctx->source->reset(ctx->source);
    }
    self->state = ctx->source->state;
}

static void skip_free(QiscStream *self) {
    SkipContext *ctx = (SkipContext *)self->context;
    stream_free(ctx->source);
    free(ctx);
}

QiscStream *stream_skip(QiscStream *source, size_t n) {
    if (!source) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    SkipContext *ctx = malloc(sizeof(SkipContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->source = source;
    ctx->skip_count = n;
    ctx->skipped = 0;
    ctx->skip_done = false;
    
    stream->context = ctx;
    stream->next = skip_next;
    stream->reset = skip_reset;
    stream->free = skip_free;
    stream->state = source->state;
    
    return stream;
}

typedef struct {
    QiscStream *source;
    FilterFunc predicate;
    bool done;
} TakeWhileContext;

static StreamElement take_while_next(QiscStream *self) {
    TakeWhileContext *ctx = (TakeWhileContext *)self->context;
    
    if (ctx->done) {
        self->state = STREAM_EMPTY;
        return (StreamElement){NULL, 0};
    }
    
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
        return (StreamElement){NULL, 0};
    }
    
    StreamElement elem = ctx->source->next(ctx->source);
    
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
        return (StreamElement){NULL, 0};
    }
    
    if (!ctx->predicate(elem.data)) {
        ctx->done = true;
        self->state = STREAM_EMPTY;
        return (StreamElement){NULL, 0};
    }
    
    return elem;
}

static void take_while_reset(QiscStream *self) {
    TakeWhileContext *ctx = (TakeWhileContext *)self->context;
    ctx->done = false;
    if (ctx->source->reset) {
        ctx->source->reset(ctx->source);
    }
    self->state = ctx->source->state;
}

static void take_while_free(QiscStream *self) {
    TakeWhileContext *ctx = (TakeWhileContext *)self->context;
    stream_free(ctx->source);
    free(ctx);
}

QiscStream *stream_take_while(QiscStream *source, FilterFunc pred) {
    if (!source || !pred) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    TakeWhileContext *ctx = malloc(sizeof(TakeWhileContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->source = source;
    ctx->predicate = pred;
    ctx->done = false;
    
    stream->context = ctx;
    stream->next = take_while_next;
    stream->reset = take_while_reset;
    stream->free = take_while_free;
    stream->state = source->state;
    
    return stream;
}

typedef struct {
    QiscStream *source;
    FilterFunc predicate;
    bool skip_done;
} SkipWhileContext;

static StreamElement skip_while_next(QiscStream *self) {
    SkipWhileContext *ctx = (SkipWhileContext *)self->context;
    
    /* Skip elements while predicate is true */
    while (!ctx->skip_done && ctx->source->state == STREAM_READY) {
        StreamElement elem = ctx->source->next(ctx->source);
        
        if (ctx->source->state != STREAM_READY) {
            self->state = ctx->source->state;
            return (StreamElement){NULL, 0};
        }
        
        if (!ctx->predicate(elem.data)) {
            ctx->skip_done = true;
            return elem;  /* Return first non-matching element */
        }
    }
    
    /* After skip phase, pass through all elements */
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
        return (StreamElement){NULL, 0};
    }
    
    StreamElement elem = ctx->source->next(ctx->source);
    
    if (ctx->source->state != STREAM_READY) {
        self->state = ctx->source->state;
    }
    
    return elem;
}

static void skip_while_reset(QiscStream *self) {
    SkipWhileContext *ctx = (SkipWhileContext *)self->context;
    ctx->skip_done = false;
    if (ctx->source->reset) {
        ctx->source->reset(ctx->source);
    }
    self->state = ctx->source->state;
}

static void skip_while_free(QiscStream *self) {
    SkipWhileContext *ctx = (SkipWhileContext *)self->context;
    stream_free(ctx->source);
    free(ctx);
}

QiscStream *stream_skip_while(QiscStream *source, FilterFunc pred) {
    if (!source || !pred) return NULL;
    
    QiscStream *stream = malloc(sizeof(QiscStream));
    if (!stream) return NULL;
    
    SkipWhileContext *ctx = malloc(sizeof(SkipWhileContext));
    if (!ctx) {
        free(stream);
        return NULL;
    }
    
    ctx->source = source;
    ctx->predicate = pred;
    ctx->skip_done = false;
    
    stream->context = ctx;
    stream->next = skip_while_next;
    stream->reset = skip_while_reset;
    stream->free = skip_while_free;
    stream->state = source->state;
    
    return stream;
}
