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

#define NUM_PRIORITY_QUEUES 3

// ============================ Global Variables ============================ // 
extern volatile pid_t pid_count;
extern volatile bool pennos_done;

// set queues as global variable so that scheduler thread and other threads can access the queues
extern pcb_queue_t priority_queue_array[NUM_PRIORITY_QUEUES]; 



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
* This function initializes the PennOS kernel.
* It sets up the necessary data structures and starts the scheduler thread.
*/
void pennos_init();


void* thrd_print_p0([[maybe_unused]] void* arg);
void* thrd_print_p1([[maybe_unused]] void* arg);
void* thrd_print_p2([[maybe_unused]] void* arg);

// ============================ Process & syscall helpers (new) ============================ //
// NOTE: these are early stubs – implementations live in kernel_syscall.c
// they will be fleshed out incrementally. all comments are kept lowercase per user request.

typedef unsigned int clock_tick_t;

// lifecycle helpers
pcb_t* k_get_self_pcb(void);
pcb_t* k_proc_create(pcb_t* parent);
int     k_proc_cleanup(pcb_t* proc);
int     k_set_routine_and_run(pcb_t* proc, void* (*start_routine)(void*), void* arg);

// pid helpers
pid_t   k_get_pid(pcb_t* proc);

// wait / signal / scheduling helpers (stub for now)
pid_t   k_waitpid(pid_t pid, int* wstatus, bool nohang);
int     k_kill(pid_t pid, int sig);
int     k_tcsetpid(pid_t pid);
int     k_nice(pid_t pid, int priority);
void    k_sleep(clock_tick_t ticks);
int     k_pipe(int fds[2]);

// misc introspection / exit
void    k_printprocess(void);
void    k_exit(void);

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

void k_register_pcb(pcb_t* p);

#endif  // KERNEL_FN_H_