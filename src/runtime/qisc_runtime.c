/*
 * QISC Runtime Library — Profiling Support
 *
 * Provides runtime functions for profile instrumentation.
 * This is linked into programs compiled with --profile.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define MAX_FUNCTIONS 256

typedef struct {
    const char *name;
    long call_count;
    clock_t start_time;
    clock_t total_time;
} FunctionProfile;

static FunctionProfile profiles[MAX_FUNCTIONS];
static int profile_count = 0;
static int profiling_active = 1;

static FunctionProfile *find_or_create_profile(const char *name) {
    for (int i = 0; i < profile_count; i++) {
        if (strcmp(profiles[i].name, name) == 0) {
            return &profiles[i];
        }
    }
    if (profile_count < MAX_FUNCTIONS) {
        profiles[profile_count].name = name;
        profiles[profile_count].call_count = 0;
        profiles[profile_count].start_time = 0;
        profiles[profile_count].total_time = 0;
        return &profiles[profile_count++];
    }
    return NULL;
}

void __qisc_profile_fn_enter(const char *name) {
    if (!profiling_active) return;
    FunctionProfile *p = find_or_create_profile(name);
    if (p) {
        p->call_count++;
        p->start_time = clock();
    }
}

void __qisc_profile_fn_exit(const char *name) {
    if (!profiling_active) return;
    FunctionProfile *p = find_or_create_profile(name);
    if (p && p->start_time != 0) {
        p->total_time += clock() - p->start_time;
        p->start_time = 0;
    }
}

/* Called at program exit to dump profile data */
__attribute__((destructor))
static void __qisc_profile_dump(void) {
    if (profile_count == 0) return;
    
    fprintf(stderr, "\n[QISC Profile Summary]\n");
    fprintf(stderr, "%-20s %10s %12s\n", "Function", "Calls", "Time (ms)");
    fprintf(stderr, "--------------------------------------------\n");
    
    for (int i = 0; i < profile_count; i++) {
        double time_ms = (double)profiles[i].total_time / CLOCKS_PER_SEC * 1000.0;
        fprintf(stderr, "%-20s %10ld %12.3f\n", 
                profiles[i].name, 
                profiles[i].call_count,
                time_ms);
    }
    fprintf(stderr, "\n");
    fflush(stderr);
}
