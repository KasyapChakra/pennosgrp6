#include "./kernel_fn.h"
#include "./pcb_queue.h"
#include "./scheduler.h"
#include "./PCB.h"
#include "./spthread.h"
#include "../common/pennfat_errors.h"
#include "./klogger.h"
#include "./kernel_definition.h"
#include "../util/panic.h"

#include <pthread.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>


// simple singly-linked list of all pcbs – head pointer
static pcb_t* g_pcb_list_head = NULL;
/*

// ───────────────── helper internals ────────────────────────────────
static void list_push_pcb(pcb_t* p) {
    p->next_pcb_ptr = g_pcb_list_head;
    g_pcb_list_head = p;
}
// ===> replace by: pcb_vec_push_back(pcb_vec_t* self, pcb_t* new_ele)

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
// ===> replace by: pcb_vec_remove_by_pcb(pcb_vec_t* self, pcb_t* target_pcb_ptr)


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
// ===> replace by: pcb_vec_seek_pcb_by_pid

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
// ===> replace by: pcb_vec_seek_pcb_by_thrd
*/

// ───────────────── api implementations ────────────────────────────

/*
pcb_t* k_get_self_pcb(void) {
    pthread_t me = pthread_self();
    return find_pcb_by_thread(me);
}
*/

pcb_t* k_get_self_pcb(void) {
    pthread_t self = pthread_self();
    return pcb_vec_seek_pcb_by_thrd(&all_unreaped_pcb_vector, self);
}



/*
pid_t k_get_pid(pcb_t* proc) {
    if (!proc) return -1;
    return thrd_pid(proc);
}
*/

pid_t k_get_pid(pcb_t* pcb_ptr) {
    if (pcb_ptr == NULL) {
        return -1;
    }
    return thrd_pid(pcb_ptr);
}


/*
pcb_t* k_proc_create(pcb_t* parent, int priority_code) {
    pcb_t* p = calloc(1, sizeof(pcb_t));
    if (!p) return NULL;

    p->status         = THRD_STOPPED;
    p->priority_level = priority_code;

    // assign fresh pid (shared global from kernel_fn.c)
    spthread_disable_interrupts_self();
    pid_count++;
    spthread_enable_interrupts_self();
    p->pid  = pid_count;
    p->pgid = p->pid;
    p->ppid = parent ? thrd_pid(parent) : 0;
    p->num_child_pids = 0;
    p->child_pids = NULL;
    p->fds = NULL;
    p->next_pcb_ptr = NULL;    

    // thread will be assigned later by k_set_routine_and_run
    list_push_pcb(p);
    // log create
    extern volatile int cumulative_tick_global;
    klog("[%5d]\tCREATE\t%d\t%d\tprocess", cumulative_tick_global, p->pid, p->priority_level);
    return p;
}
*/

pcb_t* k_proc_create(pcb_t* parent_pcb_ptr, int priority_code) {
    pcb_t* pcb_ptr;
    spthread_disable_interrupts_self();    
    if (pcb_init_empty(&pcb_ptr, priority_code, pid_count++) == -1) {
        panic("pcb_init() failed!\n");
    }
    spthread_enable_interrupts_self();    
    //pcb_ptr->ppid = parent_pcb_ptr ? thrd_pid(parent_pcb_ptr) : 0; //<-- included in pcb_init_empty()

    // thread will be assigned later by k_set_routine_and_run    
    pcb_vec_push_back(&all_unreaped_pcb_vector, pcb_ptr);
    // log create
    extern volatile int cumulative_tick_global;
    klog("[%5d]\tCREATE\t%d\t%d\tprocess", cumulative_tick_global, thrd_pid(pcb_ptr), thrd_priority(pcb_ptr));
    return pcb_ptr;
}


/*
int k_proc_cleanup(pcb_t* pcb_ptr) {
    if (!pcb_ptr) {
        return -1;
    }
    // can only cleanup terminated/finished thread
    if (thrd_status(pcb_ptr) != THRD_ZOMBIE) {
        return -2;
    }
    // change parent of child threads to init thread
    for (int i = 0; i < thrd_num_child(pct_ptr); i++) {
        // set child ppid to the pid of init thread (i.e. 0)
        pcb_t* child_pcb_ptr = find_pcb_by_pid(pcb_ptr->child_pids[i]);
        if (child_pcb_ptr != NULL) {
            child_pcb_ptr->ppid = 0;     
        }        
    }

    list_remove_pcb(pcb_ptr);
    pcb_destroy(pcb_ptr);
    return 0;
}

*/

int k_proc_cleanup(pcb_t* pcb_ptr) {
    if (pcb_ptr == NULL) {
        return -1;
    }
    // can only cleanup terminated/finished thread
    if (thrd_status(pcb_ptr) != THRD_ZOMBIE) {
        return -2;
    }
    // update parent of child threads to the init thread
    for (int i = 0; i < thrd_num_child(pcb_ptr); i++) {
        // set child ppid to the pid of init thread (i.e. 0)
        pcb_t* child_pcb_ptr = pcb_vec_seek_pcb_by_pid(&all_unreaped_pcb_vector, pcb_ptr->child_pids[i]);
        if (child_pcb_ptr != NULL) {
            child_pcb_ptr->ppid = 0;     
        }        
    }

    pcb_vec_remove_by_pcb(&all_unreaped_pcb_vector, pcb_ptr);
    pcb_destroy(pcb_ptr);
    return 0;
}



/*
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
*/

int k_set_routine_and_run(pcb_t* pcb_ptr, void* (*start_routine)(void*), void* arg) {
    if (!pcb_ptr || !start_routine) {
        return -1;
    }

    spthread_t thrd;
    if (spthread_create(&thrd, NULL, start_routine, arg) != 0) {
        return -1;
    }

    pcb_ptr->thrd = thrd;
    pcb_ptr->status = THRD_STOPPED;

    int priority = thrd_priority(pcb_ptr);
    if (priority < 0 || priority >= NUM_PRIORITY_QUEUES) {
        priority = QUEUE_PRIORITY_1;
    }

    spthread_disable_interrupts_self();
    pcb_queue_push(&priority_queue_array[priority], pcb_ptr);
    spthread_enable_interrupts_self();

    return 0;
}



/*
void k_register_pcb(pcb_t* p) {
    if (!p) return;
    list_push_pcb(p);
} 
*/

void k_register_pcb(pcb_t* pcb_ptr) {
    if (pcb_ptr == NULL) {
        return;
    }    
    pcb_vec_push_back(&all_unreaped_pcb_vector, pcb_ptr);
} 



// ───────────────── wait / signal stubs ────────────────────────────
/* very simple wait implementation: parent calls waitpid for a specific child or any (-1).
   if matching zombie child is found it is reaped (pcb destroyed) and its pid returned.
   if none and nohang is true -> return 0, else busy-loop with usleep until available. */

/*
pid_t k_waitpid(pid_t pid, int* wstatus, bool nohang) {
    pcb_t* self = k_get_self_pcb();
    if (!self) return -1;

    while (true) {
        pcb_t* curr = g_pcb_list_head;
        while (curr) {
            bool is_child = (thrd_ppid(curr) == thrd_pid(self));
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
        // crude sleep before retry to avoid busy spin 
        usleep(1000); // 1ms
    }
}
*/

// not completed yet
pid_t k_waitpid(pid_t pid, int* wstatus, bool nohang) {
    // the functions acts as if waitpid with both WUNTRACED and WCONTINUED flags

    pcb_t* self_pcb_ptr = k_get_self_pcb();
    if (self_pcb_ptr == NULL) {
        return -1;
    }
    
    while (true) {
        bool no_target_child = true; // relevent for ECHILD
        bool no_update = true; // relevant for NOHANG

        for (int i = 0; i < pcb_vec_len(&all_unreaped_pcb_vector); i++) {
            pcb_t* curr_pcb_ptr = all_unreaped_pcb_vector.pcb_ptr_array[i];
            bool is_child = (thrd_ppid(curr_pcb_ptr) == thrd_pid(self_pcb_ptr));
            bool pid_match = ((pid == -1) || (thrd_pid(curr_pcb_ptr) == pid) || (thrd_pgid(curr_pcb_ptr) == -pid));
            if (is_child && pid_match) {
                no_target_child = false;

                if (!is_thrd_status_changed(curr_pcb_ptr)) {
                    continue;
                }

                no_update = false;                         

                if (thrd_status(curr_pcb_ptr) == THRD_STOPPED) {
                    if (curr_pcb_ptr->stop_signal != P_SIGSTOP) {
                        panic("Thread was stopped but not by P_SIGSTOP!\n");
                    }
                    *wstatus = (curr_pcb_ptr->stop_signal << 8) | 0x7F; 
                    reset_pcb_status_signal(curr_pcb_ptr);           
                    return thrd_pid(curr_pcb_ptr);
                }

                if (thrd_status(curr_pcb_ptr) == THRD_ZOMBIE) {
                    if (curr_pcb_ptr->term_signal == 0) {
                        // exited normally
                        *wstatus = curr_pcb_ptr->exit_code << 8; 
                        reset_pcb_status_signal(curr_pcb_ptr);            
                    } else {
                        // terminated by signal
                        if (curr_pcb_ptr->term_signal != P_SIGTERM) {
                            panic("Thread was terminated but not by P_SIGTERM!\n");
                        }
                        *wstatus = curr_pcb_ptr->term_signal;
                        reset_pcb_status_signal(curr_pcb_ptr); 
                    }              
                    
                    pid_t result_pid = thrd_pid(curr_pcb_ptr);                    

                    // pop up the pcb and clear it
                    /////////////////////////////////////////
                    /////////////////////////////////////////
                    /////////////////////////////////////////

                    return result_pid;
                }
                            
                if ((thrd_status(curr_pcb_ptr) == THRD_RUNNING) || (thrd_status(curr_pcb_ptr) == THRD_BLOCKED)) {
                    if (curr_pcb_ptr->cont_signal != P_SIGCONT) {
                        panic("Thread was continued but not by P_SIGCONT!\n");
                    }
                    *wstatus = 0xFFFF; 
                    reset_pcb_status_signal(curr_pcb_ptr);           
                    return thrd_pid(curr_pcb_ptr);
                }  
            }
        }

        if (no_target_child) {
            k_errno = P_ECHILD;
            return -1;
        }

        if (nohang && no_update) {
            return 0;   // there still exist non-zombie children
                        // there are no update from these children since last wait                         
        }

        /* crude sleep before retry to avoid busy spin */
        usleep(1000); // 1ms
    }
}



/*
int k_kill(pid_t pid, int sig) {
    //pcb_t* target = find_pcb_by_pid(pid);
    pcb_t* target = pcb_vec_seek_pcb_by_pid(&all_unreaped_pcb_vector, pid);
    if (!target) return -1;

    switch (sig) {
        case SIGTERM:
        default:
            // mark zombie and cancel thread 
            target->status = THRD_ZOMBIE;
            klog("[%5d]\tSIGNALED\t%d\t%d\tprocess", cumulative_tick_global, target->pid, target->priority_level);
            klog("[%5d]\tZOMBIE\t%d\t%d\tprocess", cumulative_tick_global, target->pid, target->priority_level);
            spthread_cancel(target->thrd);
            // immediate cleanup by parent if parent waiting 
            return 0;
    }
}
*/

int k_kill(pid_t pid, k_signal_t sig) {
    
    pcb_t* target = pcb_vec_seek_pcb_by_pid(&all_unreaped_pcb_vector, pid);
    if (!target) return -1;

    switch (sig) {
        case P_SIGSTOP:
            if (thrd_status(target) == THRD_RUNNING) {
                spthread_suspend(thrd_handle(target));
                target->status = THRD_STOPPED;
                target->stop_signal = P_SIGSTOP;
                pcb_queue_pop_by_pid(&priority_queue_array[thrd_priority(target)], pid);                
            }
            return 0;
        case P_SIGCONT:
            if (thrd_status(target) == THRD_STOPPED) {
                target->status = THRD_RUNNING;
                target->cont_signal = P_SIGCONT;
                spthread_suspend(thrd_handle(target));
                pcb_queue_push(&priority_queue_array[thrd_priority(target)], target);
            }
            return 0;
        case P_SIGTERM:
            if (thrd_status(target) != THRD_ZOMBIE) {
                spthread_cancel(thrd_handle(target)); 
                spthread_continue(thrd_handle(target));
                spthread_suspend(thrd_handle(target)); 
                target->status = THRD_ZOMBIE;
                target->term_signal = P_SIGTERM;
                pcb_queue_pop_by_pid(&priority_queue_array[thrd_priority(target)], pid);                
                // need to update init PCB's child pids and child PCB's ppid to init                
                // to do
            }
            return 0;
        default:
            /* mark zombie and cancel thread */
            //target->status = THRD_ZOMBIE;
            //klog("[%5d]\tSIGNALED\t%d\t%d\tprocess", cumulative_tick_global, target->pid, target->priority_level);
            //klog("[%5d]\tZOMBIE\t%d\t%d\tprocess", cumulative_tick_global, target->pid, target->priority_level);
            //spthread_cancel(target->thrd);
            /* immediate cleanup by parent if parent waiting */
            return 0;
    }    
}

///////////////////////NOT UPDATED YET FOR PCB VECTOR///////////////////////
////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////

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
    dprintf(STDERR_FILENO, "PID PPID PRI STAT CMD\n");
    pcb_t* curr = g_pcb_list_head;
    while (curr) {
        // turn the status enum into a string
        const char* status_str;
        switch (thrd_status(curr)) {
            case THRD_RUNNING: status_str = "R"; break;
            case THRD_STOPPED: status_str = "S"; break;
            case THRD_BLOCKED: status_str = "B"; break;
            case THRD_ZOMBIE:  status_str = "Z"; break;
        }
        dprintf(STDERR_FILENO, "    %d   %d   %d   %s   %s\n", thrd_pid(curr), thrd_ppid(curr), thrd_priority(curr), status_str, thrd_CMD(curr));
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
/*
int k_open(const char* fname, int mode) {
    (void)fname; (void)mode;
    return PennFatErr_NOT_IMPL; // -16 
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
*/

/* expose ability to register a pcb created outside k_proc_create (bootstrap) */