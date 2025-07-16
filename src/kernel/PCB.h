/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Implement a data structure for process control block (PCB)
 * File Name:           PCB.h
 * File Content:        Header file for pcb struct
 * =============================================================== */

#ifndef PCB_H_
#define PCB_H_

#include <stdint.h>
#include <sys/types.h>

#include "./spthread.h"
#include "./kernel_definition.h"

#define NUM_CHILDREN_MAX 128

typedef enum {
    THRD_RUNNING = 0, // running | ready
    THRD_STOPPED = 1,
    THRD_BLOCKED = 2, // e.g. sleeping | waiting on something    
    THRD_ZOMBIE = 3    
} thrd_status_t;

typedef struct pcb_st {
    // --- ID ---
    spthread_t thrd;
    pid_t pid;
    pid_t pgid;
    pid_t ppid;

    // --- attributes ---
    int priority_level; // 0 (high) | 1 (mid) | 2 (low)    
    char* command; // command string associated with this PCB

    // --- child info ---
    int num_child_pids;
    pid_t child_pids[NUM_CHILDREN_MAX]; // array of children pids (size = num_child_pids)

    // --- for priority queue link list ---
    struct pcb_st* next_pcb_ptr;    

    // --- status related ---
    thrd_status_t status; // 0 (running) | 1 (stopped) | 2 (blocked) | 3 (zombie)
    thrd_status_t pre_status; // only to be modified by waitpid()
    int exit_code;
    k_signal_t term_signal;
    k_signal_t stop_signal;
    k_signal_t cont_signal;
    int errno;

    // --- others (to be decided) ---
    int* fds; // array of file descriptors

} pcb_t;


// ========================= Functions-like macros ========================= //
#define thrd_handle(pcb_ptr) ((pcb_ptr)->thrd)
#define thrd_pid(pcb_ptr) ((pcb_ptr)->pid)
#define thrd_pgid(pcb_ptr) ((pcb_ptr)->pgid)
#define thrd_ppid(pcb_ptr) ((pcb_ptr)->ppid)
#define thrd_priority(pcb_ptr) ((pcb_ptr)->priority_level)
#define thrd_CMD(pcb_ptr) ((pcb_ptr)->command)
#define thrd_num_child(pcb_ptr) ((pcb_ptr)->num_child_pids)
#define thrd_next(pcb_ptr) ((pcb_ptr)->next_pcb_ptr)

#define thrd_status(pcb_ptr) ((pcb_ptr)->status)
#define thrd_pre_status(pcb_ptr) ((pcb_ptr)->pre_status)
#define thrd_errno(pcb_ptr) ((pcb_ptr)->errno)


// ============================ Functions ============================ //
/** 
 * This function initializes a PCB with the given thread, priority code, and pid.
 * 
 * @param thread The thread to be associated with the PCB.
 * @param result_pcb Pointer to a pointer where the initialized PCB will be stored.
 * @param priority_code The priority level of the PCB (0, 1, or 2).
 * @param pid The process ID for the PCB.
 */
int pcb_init(spthread_t thread, pcb_t** result_pcb, int priority_code, pid_t pid, 
             char* command);

int pcb_init_empty(pcb_t** result_pcb, int priority_code, pid_t pid);

/**
 * This function destroys a PCB, freeing its resources when it is no longer needed.
 * 
 * @param self_ptr Pointer to the PCB to be destroyed.
 */
void pcb_destroy(pcb_t* self_ptr);

/**
 * This function prints the information of a PCB to STDERR.
 * It is useful for debugging and understanding the state of the PCB.
 * 
 * @param self_ptr Pointer to the PCB whose information will be printed.
 */
void print_pcb_info(pcb_t* self_ptr);

void print_pcb_info_single_line(pcb_t* self_ptr);


// change between RUNNING and BLOCKED does NOT count as change
bool is_thrd_status_changed(pcb_t* pcb_ptr);

void reset_pcb_status_signal(pcb_t* pcb_ptr);

int pcb_add_child_pid(pcb_t* self_ptr, pid_t pid);

int pcb_remove_child_pid(pcb_t* self_ptr, pid_t pid);



#endif  // PCB_H_