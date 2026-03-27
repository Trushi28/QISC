/*
 * QISC Error Handling Runtime
 * Implements try/catch using setjmp/longjmp
 */

#ifndef QISC_ERROR_H
#define QISC_ERROR_H

#include <setjmp.h>
#include <stdbool.h>

/* Maximum nesting depth for try/catch */
#define QISC_MAX_TRY_DEPTH 32

/* Error info structure */
typedef struct {
    char message[256];
    int code;
    const char *file;
    int line;
} QiscError;

/* Try/catch context stack */
typedef struct {
    jmp_buf jump_buffers[QISC_MAX_TRY_DEPTH];
    QiscError errors[QISC_MAX_TRY_DEPTH];
    int depth;
} QiscErrorContext;

/* Global error context (thread-local in multi-threaded code) */
extern QiscErrorContext __qisc_error_ctx;

/* Push a new try context, returns jump buffer */
jmp_buf* __qisc_try_push(void);

/* Pop try context */
void __qisc_try_pop(void);

/* Throw error (calls longjmp) */
void __qisc_fail(const char *message, int code) __attribute__((noreturn));

/* Get current error (in catch block) */
const char* __qisc_get_error(void);

/* Check if we're in a try block */
bool __qisc_in_try(void);

/* Macros for use in generated code */
#define QISC_TRY \
    { \
        jmp_buf *__qisc_jb = __qisc_try_push(); \
        if (setjmp(*__qisc_jb) == 0) {

#define QISC_CATCH \
        } else { \
            QiscError *__qisc_err = __qisc_get_error();

#define QISC_END_TRY \
        } \
        __qisc_try_pop(); \
    }

#endif /* QISC_ERROR_H */
