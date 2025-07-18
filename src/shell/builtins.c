#include "builtins.h"
#include "../user/syscall_kernel.h"
#include "../util/utils.h"
#include "../kernel/kernel_definition.h"
#include <unistd.h>

void* ps_builtin([[maybe_unused]] void* arg) {
    s_printprocess();
    return NULL;
}

void* sleep_builtin([[maybe_unused]] void* arg) {
    s_sleep(*(clock_tick_t*)arg);
    return NULL;
}

void* busy_builtin([[maybe_unused]] void* arg) {
    while (1) {
        /* burn some cpu */
    }
    return NULL;
}

/* zombify/orphanify already implemented in user/shell.c – we just declare them extern */
extern void* zombify(void*);
extern void* orphanify(void*);