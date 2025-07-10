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


extern volatile sig_atomic_t cumulative_tick_global; // for DEBUG, can be deleted later ////////////////

typedef struct scheduler_para_st {
    const int num_queues; // number of priority (running) queues
    pcb_queue_t* q_array; // pointer to priority (running) queues
    const int q_pick_pattern_len; // queue pick pattern length
    const int* q_pick_pattern_array; // pointer to queue pick pattern array
    const int quantum_msec; // quantum length in millisecond (ms)
} scheduler_para_t;


// ============================ Functions ============================ //
void handler_sigalrm_scheduler(int signum);
void* thrd_scheduler_fn([[maybe_unused]] void* arg);




#endif  // SCHEDULER_H_
