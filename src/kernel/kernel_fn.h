/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Kernel related functions
 * File Name:           kernel_fn.h
 * File Content:        Header file for kernel related functions
 * =============================================================== */

#ifndef KERNEL_FN_H_
#define KERNEL_FN_H_

#include <stdbool.h>

#include "./spthread.h"
#include "./PCB.h"
#include "./pcb_queue.h"
#include "./pcb_vec.h"
#include "./kernel_definition.h"


#define NUM_PRIORITY_QUEUES 3




// ============================ Global Variables ============================ // 
extern volatile pid_t pid_count;
extern volatile bool pennos_done;
extern volatile k_errno_t k_errno;

/* total number of clock ticks (each tick = quantum length, default 100 ms) since PennOS boot */
extern volatile clock_tick_t global_clock; // unit: 100 ms

/* temporary alias until we migrate all call-sites */
//extern volatile sig_atomic_t cumulative_tick_global; // for DEBUG, can be deleted later ////////////////


// set queues as global variable so that scheduler thread and other threads can access the queues
extern pcb_queue_t priority_queue_array[NUM_PRIORITY_QUEUES]; 

// global queue holding all PCBs currently blocked (waiting/sleeping)
extern pcb_queue_t blocked_queue;
// queue holding threads that are STOPPED via P_SIGSTOP
extern pcb_queue_t stopped_queue;
// queue holding zombie threads to be reaped
extern pcb_queue_t stopped_queue;

// this vector holds all the PCBs (threads) that have not been reaped
extern pcb_vec_t all_unreaped_pcb_vector;



// ============================ Functions ============================ //
/**
* This function cancels the thread associated with the PCB and waits for it to finish.
* It is used to clean up the PCB when it is no longer needed.
* 
* @param pcb_ptr Pointer to the PCB to be cancelled and joined.
*/
void cancel_and_join_pcb(pcb_t* pcb_ptr);

/**
* This function cancels the thread and waits for it to finish.
* It is used to clean up the thread when it is no longer needed.
*
* @param thread The thread to be cancelled and joined.
*/
void cancel_and_join_thrd(spthread_t thread);

/**
* This function is the main function for init thread.
* It sets up the necessary data structures and starts shell thread.
*/
void* thrd_init_fn([[maybe_unused]] void* arg);


void pennos_kernel(void);


void* thrd_print_p0([[maybe_unused]] void* arg);
void* thrd_print_p1([[maybe_unused]] void* arg);
void* thrd_print_p2([[maybe_unused]] void* arg);

// ============================ API ============================ //


// lifecycle helpers
pcb_t* k_get_self_pcb(void);
pcb_t* k_proc_create(pcb_t* parent_pcb_ptr, int priority_code);
int k_proc_cleanup(pcb_t* pcb_ptr);
int k_set_routine_and_run(pcb_t* pcb_ptr, void* (*start_routine)(void*), void* arg);
void k_register_pcb(pcb_t* pcb_ptr);

// pid helpers
pid_t   k_get_pid(pcb_t* pcb_ptr);

// helpers
bool pcb_in_queue(pcb_t* self_ptr, pcb_queue_t* queue_ptr);


// ============================ Process & syscall helpers (new) ============================ //
// NOTE: these are early stubs – implementations live in kernel_syscall.c
// they will be fleshed out incrementally. all comments are kept lowercase per user request.



// wait / signal / scheduling helpers (stub for now)
pid_t   k_waitpid(pid_t pid, int* wstatus, bool nohang);
int     k_kill(pid_t pid, k_signal_t sig);
int     k_tcsetpid(pid_t pid);
int     k_nice(pid_t pid, int new_priority);
void    k_sleep(clock_tick_t length_in_second);
int     k_pipe(int fds[2]);

// misc introspection / exit
void    k_printprocess(void);
void    k_exit(void);



/*
// filesystem stubs (not implemented yet)
int k_open(const char* fname, int mode);
int k_close(int fd);
int k_read(int fd, int n, char* buf);
int k_write(int fd, const char* str, int n);
int k_lseek(int fd, int offset, int whence);
int k_unlink(const char* fname);
int k_rename(const char* oldname, const char* newname);
int k_touch(const char* fname);
int k_ls(const char* fname);
int k_chmod(const char* fname, unsigned char perm);
*/
// ───────── wrapper‐exit & spawn‐wrapper helpers ─────────
/**
 * Arguments struct for routine_exit_wrapper_func:
 *   holds the real function pointer and its argument.
 */
typedef struct routine_exit_wrapper_args {
  void* (*real_func)(void*);
  void* real_arg;
} routine_exit_wrapper_args_t;

/**
 * Allocate and initialize a wrapper_args struct so that
 * routine_exit_wrapper_func can invoke real_func(arg) and then
 * perform cleanup.
 */
routine_exit_wrapper_args_t* wrap_routine_exit_args(void* (*real_func)(void*),
                                                    void* real_arg);

/**
 * Thread entry point you pass to spthread_create when you
 * want to “wrap” exit.  It unpacks a routine_exit_wrapper_args_t,
 * calls real_func(arg), then does any bookkeeping.
 */
void* routine_exit_wrapper_func(void* wrapper_args);
/**
 * Argument struct for spawn_entry_wrapper:
 * wraps the “real” argument pointer so we can unpack it.
 */
typedef struct kernel_spawn_wrapper_arg {
  void* (*real_func)(void*);
  void* real_arg;
} kernel_spawn_wrapper_arg;

/**
 * A generic spawn‐entry wrapper: you can log argv[0] or
 * set up any special state before handing off to the real func.
 */
void* spawn_entry_wrapper_kernel(void* wrapper_args);

/**
 * Quick sanity‐check to see if a pointer really looks like
 * a NULL‐terminated C string.
 */
bool looks_like_cstring(const char* s);

// ───────── init process naming & lifecycle logging ─────────
/** PID and name for the “init” process **/
#define INIT_PID 1
#define INIT_PROCESS_NAME "INIT"


void set_process_name(pcb_t* proc, const char* name);

/**
 * Emit a lifecycle event for a PCB, e.g. CREATED, ZOMBIE, etc.
 */
void lifecycle_event_log(pcb_t* proc, const char* event, void* info);



#endif  // KERNEL_FN_H_