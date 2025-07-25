/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Scheduler related functions
 * File Name:           scheduler.h
 * File Content:        Header file for the scheduler related functions
 * =============================================================== */

#ifndef SCHEDULER_H_
#define SCHEDULER_H_

#include <signal.h>
#include "./pcb_queue.h"

#define QUEUE_PICK_PATTERN_LENGTH 19
extern const int queue_pick_pattern[QUEUE_PICK_PATTERN_LENGTH];


typedef struct scheduler_para_st {
    const int num_queues; // number of priority (running) queues
    pcb_queue_t* q_array; // pointer to priority (running) queues
    const int q_pick_pattern_len; // queue pick pattern length
    const int* q_pick_pattern_array; // pointer to queue pick pattern array
    const int quantum_msec; // quantum length in millisecond (ms)
} scheduler_para_t;


// ============================ Functions ============================ //
/**
 * This function handles the SIGALRM signal for the scheduler thread.
 * It does nothing but is used to prevent the scheduler thread from being terminated by SIGALRM.
 *
 * @param signum The signal number (not used).
 */
void handler_sigalrm_scheduler(int signum);

/**
 * This function is the main function for the scheduler.
 * It implements the scheduling algorithm based on the provided parameters.
 *
 * @param arg_ptr Pointer to a scheduler_para_t structure containing scheduling parameters.
 * @return NULL (the return value is not used).
 */
void scheduler_fn(scheduler_para_t* arg_ptr);




#endif  // SCHEDULER_H_
