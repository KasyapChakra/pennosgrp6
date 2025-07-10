/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Implement a data structure for PCB queue
 * File Name:           pcb_queue.h
 * File Content:        Header file for PCB queue
 * =============================================================== */

#ifndef QUEUE_H_
#define QUEUE_H_

#include <stdbool.h>
#include <stddef.h>  // for size_t

#include "./spthread.h"
#include "./PCB.h"

typedef enum {
    QUEUE_PRIORITY_0 = 0, 
    QUEUE_PRIORITY_1 = 1,
    QUEUE_PRIORITY_2 = 2,     
    QUEUE_BLOCKED = 3,
    QUEUE_ZOMBIE = 4    
} queue_type_t;

typedef void (*data_destroy_fn_ptr)(pcb_t*);

typedef struct pcb_queue_st {
    queue_type_t q_type; // 0 (high) | 1 (mid) | 2 (low) | 3 (blocked) | 4 (zombie)
    pcb_t* q_head_ptr;
    pcb_t* q_end_ptr;
    size_t length;
    data_destroy_fn_ptr data_destroy_fn;
} pcb_queue_t;

// ========================= Functions-like macros ========================= //
#define queue_len(pcb_queue_ptr) ((pcb_queue_ptr)->length)
#define queue_is_empty(pcb_queue_ptr) ((pcb_queue_ptr)->length == 0)
#define queue_type(pcb_queue_ptr) ((pcb_queue_ptr)->q_type)
#define queue_head(pcb_queue_ptr) ((pcb_queue_ptr)->q_head_ptr)
#define queue_end(pcb_queue_ptr) ((pcb_queue_ptr)->q_end_ptr)

// ============================ Functions ============================ //
pcb_queue_t pcb_queue_init(queue_type_t queue_type_code);
pcb_t* pcb_queue_pop(pcb_queue_t* self);
pcb_t* pcb_queue_pop_by_pid(pcb_queue_t* self, pid_t target_pid);
void pcb_queue_push(pcb_queue_t* self, pcb_t* pcb_ptr);
void pcb_queue_destroy(pcb_queue_t* self);
void print_queue_info(pcb_queue_t* self_ptr);

#endif  // QUEUE_H_