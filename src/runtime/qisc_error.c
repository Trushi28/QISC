/*
 * QISC Error Handling Runtime
 * Implements try/catch using setjmp/longjmp
 */

#include "qisc_error.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

QiscErrorContext __qisc_error_ctx = {.depth = 0};

jmp_buf* __qisc_try_push(void) {
    if (__qisc_error_ctx.depth >= QISC_MAX_TRY_DEPTH) {
        fprintf(stderr, "Error: try/catch nesting too deep\n");
        exit(1);
    }
    return &__qisc_error_ctx.jump_buffers[__qisc_error_ctx.depth++];
}

void __qisc_try_pop(void) {
    if (__qisc_error_ctx.depth > 0) {
        __qisc_error_ctx.depth--;
    }
}

void __qisc_fail(const char *message, int code) {
    if (__qisc_error_ctx.depth == 0) {
        fprintf(stderr, "Uncaught error: %s\n", message);
        exit(code ? code : 1);
    }
    
    int idx = __qisc_error_ctx.depth - 1;
    QiscError *err = &__qisc_error_ctx.errors[idx];
    strncpy(err->message, message, sizeof(err->message) - 1);
    err->message[sizeof(err->message) - 1] = '\0';
    err->code = code;
    
    longjmp(__qisc_error_ctx.jump_buffers[idx], 1);
}

const char* __qisc_get_error(void) {
    // Return the error message from the current error context
    // When called from catch, we've jumped back but depth still points to active try
    int idx = __qisc_error_ctx.depth > 0 ? __qisc_error_ctx.depth - 1 : 0;
    return __qisc_error_ctx.errors[idx].message;
}

bool __qisc_in_try(void) {
    return __qisc_error_ctx.depth > 0;
}
