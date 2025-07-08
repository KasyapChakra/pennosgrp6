/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Kernel related functions
 * File Name:           kernel_fn.h
 * File Content:        Header file for kernel related functions
 * =============================================================== */

#ifndef KERNEL_FN_H_
#define KERNEL_FN_H_


#include "./kernel/spthread.h"
#include "./kernel/PCB.h"
#include "./kernel/pcb_queue.h"

#define NUM_PRIORITY_QUEUES 3


// set queues as global variable so that scheduler thread and other threads can access the queues
extern pcb_queue_t priority_queue_array[NUM_PRIORITY_QUEUES]; 

// Functions
void cancel_and_join_pcb(pcb_t* pcb_ptr);
void cancel_and_join_thrd(spthread_t thread);




#endif  // KERNEL_FN_H_