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




#endif  // KERNEL_FN_H_