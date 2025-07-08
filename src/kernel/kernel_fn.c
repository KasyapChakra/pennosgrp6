/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Kernel related functions
 * File Name:           kernel_fn.c
 * File Content:        Implementation of kernel related functions
 * =============================================================== */

#include "./kernel/kernel_fn.h"
#include "./kernel/spthread.h"
#include "./kernel/PCB.h"
#include "./kernel/pcb_queue.h"
#include "./kernel/scheduler.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

// set queues as global variable so that scheduler thread and other threads can access the queues
pcb_queue_t priority_queue_array[NUM_PRIORITY_QUEUES]; 


void cancel_and_join_pcb(pcb_t* pcb_ptr) {
    dprintf(STDERR_FILENO, "------ Cancelling thread PID# %d ------\n", thrd_pid(pcb_ptr));
    // spthread_cancel() just sends the cancellation request
    // but doesn't wait for the thread to reach the cancellation point
    spthread_cancel(thrd_handle(pcb_ptr)); 
    // use spthread_continue() + spthread_suspend():
    // to sort of try and force the thread to hit a cancellation point
    spthread_continue(thrd_handle(pcb_ptr));
    // spthread_suspend() will cause the target thread to call sigsuspend, which is a cancellation point
    // forces the spthread to hit a cancellation point
    spthread_suspend(thrd_handle(pcb_ptr)); 

    dprintf(STDERR_FILENO, "------ Joining thread PID# %d ------\n", thrd_pid(pcb_ptr));
    spthread_join(thrd_handle(pcb_ptr), NULL);
}


void cancel_and_join_thrd(spthread_t thread) {
    dprintf(STDERR_FILENO, "------ Cancelling thread ------\n");
    // spthread_cancel() just sends the cancellation request
    // but doesn't wait for the thread to reach the cancellation point
    spthread_cancel(thread); 
    // use spthread_continue() + spthread_suspend():
    // to sort of try and force the thread to hit a cancellation point
    spthread_continue(thread);
    // spthread_suspend() will cause the target thread to call sigsuspend, which is a cancellation point
    // forces the spthread to hit a cancellation point
    spthread_suspend(thread); 

    dprintf(STDERR_FILENO, "------ Joining thread ------\n");
    spthread_join(thread, NULL);
}

