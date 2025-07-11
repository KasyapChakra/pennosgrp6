#include "./kernel_fn.h"
#include "./pcb_queue.h"
#include "./scheduler.h"
#include "./PCB.h"
#include "./spthread.h"
#include "../common/pennfat_errors.h"
#include "./klogger.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

// simple singly-linked list of all pcbs – head pointer
static pcb_t* g_pcb_list_head = NULL;

// ───────────────── helper internals ────────────────────────────────
static void list_push_pcb(pcb_t* p) {
    p->next_pcb_ptr = g_pcb_list_head;
    g_pcb_list_head = p;
}

static void list_remove_pcb(pcb_t* p) {
    pcb_t** curr = &g_pcb_list_head;
    while (*curr) {
        if (*curr == p) {
            *curr = p->next_pcb_ptr;
            p->next_pcb_ptr = NULL;
            return;
        }
        curr = &((*curr)->next_pcb_ptr);
    }
}

static pcb_t* find_pcb_by_pid(pid_t pid) {
    pcb_t* curr = g_pcb_list_head;
    while (curr) {
        if (thrd_pid(curr) == pid) {
            return curr;
        }
        curr = thrd_next(curr);
    }
    return NULL;
}

static pcb_t* find_pcb_by_thread(pthread_t thr) {
    pcb_t* curr = g_pcb_list_head;
    while (curr) {
        if (pthread_equal(curr->thrd.thread, thr)) {
            return curr;
        }
        curr = thrd_next(curr);
    }
    return NULL;
}

// ───────────────── api implementations ────────────────────────────

pcb_t* k_get_self_pcb(void) {
    pthread_t me = pthread_self();
    return find_pcb_by_thread(me);
}

pid_t k_get_pid(pcb_t* proc) {
    if (!proc) return -1;
    return thrd_pid(proc);
}

pcb_t* k_proc_create(pcb_t* parent) {
    pcb_t* p = calloc(1, sizeof(pcb_t));
    if (!p) return NULL;

    p->status         = THRD_STOPPED;
    p->priority_level = QUEUE_PRIORITY_1;

    // assign fresh pid (shared global from kernel_fn.c)
    pid_count++;
    p->pid  = pid_count;
    p->pgid = p->pid;
    p->ppid = parent ? thrd_pid(parent) : 0;

    // thread will be assigned later by k_set_routine_and_run
    list_push_pcb(p);
    // log create
    extern volatile int cumulative_tick_global;
    klog("[%5d]\tCREATE\t%d\t%d\tprocess", cumulative_tick_global, p->pid, p->priority_level);
    return p;
}

int k_proc_cleanup(pcb_t* proc) {
    if (!proc) return -1;
    list_remove_pcb(proc);
    pcb_destroy(proc);
    return 0;
}

int k_set_routine_and_run(pcb_t* proc, void* (*start_routine)(void*), void* arg) {
    if (!proc || !start_routine) return -1;

    spthread_t thrd;
    if (spthread_create(&thrd, NULL, start_routine, arg) != 0) {
        return -1;
    }

    proc->thrd   = thrd;
    proc->status = THRD_STOPPED;

    int prio = proc->priority_level;
    if (prio < 0 || prio >= NUM_PRIORITY_QUEUES) prio = QUEUE_PRIORITY_1;

    spthread_disable_interrupts_self();
    pcb_queue_push(&priority_queue_array[prio], proc);
    spthread_enable_interrupts_self();

    return 0;
}

// ───────────────── wait / signal stubs ────────────────────────────
/* very simple wait implementation: parent calls waitpid for a specific child or any (-1).
   if matching zombie child is found it is reaped (pcb destroyed) and its pid returned.
   if none and nohang is true -> return 0, else busy-loop with usleep until available. */
pid_t k_waitpid(pid_t pid, int* wstatus, bool nohang) {
    pcb_t* self = k_get_self_pcb();
    if (!self) return -1;

    while (true) {
        pcb_t* curr = g_pcb_list_head;
        while (curr) {
            bool is_child = (curr->ppid == thrd_pid(self));
            bool pid_match = (pid == -1) || (thrd_pid(curr) == pid);
            if (is_child && pid_match && curr->status == THRD_ZOMBIE) {
                pid_t cid = thrd_pid(curr);
                if (wstatus) *wstatus = 0; // no exit code yet
                klog("[%5d]\tWAITED\t%d\t%d\tprocess", cumulative_tick_global, cid, curr->priority_level);
                k_proc_cleanup(curr);
                return cid;
            }
            curr = thrd_next(curr);
        }

        if (nohang) return 0; // nothing yet, caller doesn’t want to block
        /* crude sleep before retry to avoid busy spin */
        usleep(1000); // 1ms
    }
}

int k_kill(pid_t pid, int sig) {
    pcb_t* target = find_pcb_by_pid(pid);
    if (!target) return -1;

    switch (sig) {
        case SIGTERM:
        default:
            /* mark zombie and cancel thread */
            target->status = THRD_ZOMBIE;
            klog("[%5d]\tSIGNALED\t%d\t%d\tprocess", cumulative_tick_global, target->pid, target->priority_level);
            klog("[%5d]\tZOMBIE\t%d\t%d\tprocess", cumulative_tick_global, target->pid, target->priority_level);
            spthread_cancel(target->thrd);
            /* immediate cleanup by parent if parent waiting */
            return 0;
    }
}

int k_tcsetpid([[maybe_unused]] pid_t pid) {
    return -1; // terminal control not implemented yet
}

// ───────────────── scheduling helpers ─────────────────────────────

int k_nice(pid_t pid, int priority) {
    if (priority < 0 || priority >= NUM_PRIORITY_QUEUES) {
        return -1;
    }

    pcb_t* pcb_ptr = NULL;

    // search and relocate
    for (int i = 0; i < NUM_PRIORITY_QUEUES; i++) {
        spthread_disable_interrupts_self();
        pcb_ptr = pcb_queue_pop_by_pid(&priority_queue_array[i], pid);
        spthread_enable_interrupts_self();
        if (pcb_ptr) {
            break;
        }
    }

    if (!pcb_ptr) {
        return -1; // pid not found
    }

    int old = pcb_ptr->priority_level;
    pcb_ptr->priority_level = priority;
    extern volatile int cumulative_tick_global;
    klog("[%5d]\tNICE\t%d\t%d\t%d\tprocess", cumulative_tick_global, pcb_ptr->pid, old, priority);

    spthread_disable_interrupts_self();
    pcb_queue_push(&priority_queue_array[priority], pcb_ptr);
    spthread_enable_interrupts_self();

    return 0;
}

void k_printprocess(void) {
    dprintf(STDERR_FILENO, "===== process list =====\n");
    pcb_t* curr = g_pcb_list_head;
    while (curr) {
        dprintf(STDERR_FILENO, "pid %d | ppid %d | prio %d | status %d\n", thrd_pid(curr), thrd_ppid(curr), thrd_priority(curr), thrd_status(curr));
        curr = thrd_next(curr);
    }
}

void k_exit(void) {
    pcb_t* self = k_get_self_pcb();
    if (!self) {
        spthread_exit(NULL);
        return;
    }

    extern volatile int cumulative_tick_global;
    /* re-parent any live children to init (pid 1) */
    pcb_t* curr = g_pcb_list_head;
    while (curr) {
        if (curr->ppid == thrd_pid(self)) {
            curr->ppid = 1; // init
            if (curr->status == THRD_ZOMBIE) {
                /* init immediately reaps */
                k_proc_cleanup(curr);
            }
        }
        curr = thrd_next(curr);
    }

    self->status = THRD_ZOMBIE;
    klog("[%5d]\tZOMBIE\t%d\t%d\tprocess", cumulative_tick_global, self->pid, self->priority_level);
    spthread_exit(NULL);
}

void k_sleep([[maybe_unused]] clock_tick_t ticks) {
    // stub – feature not implemented yet
    (void)ticks;
}

int k_pipe([[maybe_unused]] int fds[2]) {
    return -1; // not implemented yet
}

// ───────────────── filesystem NO-OP stubs ─────────────────────────
int k_open(const char* fname, int mode) {
    (void)fname; (void)mode;
    return PennFatErr_NOT_IMPL; /* -16 */
}
int k_close(int fd) { (void)fd; return PennFatErr_NOT_IMPL; }
int k_read(int fd, int n, char* buf) { (void)fd; (void)n; (void)buf; return PennFatErr_NOT_IMPL; }
int k_write(int fd, const char* str, int n) { (void)fd; (void)str; (void)n; return PennFatErr_NOT_IMPL; }
int k_lseek(int fd, int offset, int whence) { (void)fd; (void)offset; (void)whence; return PennFatErr_NOT_IMPL; }
int k_unlink(const char* fname) { (void)fname; return PennFatErr_NOT_IMPL; }
int k_rename(const char* oldname, const char* newname) { (void)oldname; (void)newname; return PennFatErr_NOT_IMPL; }
int k_touch(const char* fname) { (void)fname; return PennFatErr_NOT_IMPL; }
int k_ls(const char* fname) { (void)fname; return PennFatErr_NOT_IMPL; }
int k_chmod(const char* fname, unsigned char perm) { (void)fname; (void)perm; return PennFatErr_NOT_IMPL; } 

/* expose ability to register a pcb created outside k_proc_create (bootstrap) */
void k_register_pcb(pcb_t* p) {
    if (!p) return;
    list_push_pcb(p);
} 