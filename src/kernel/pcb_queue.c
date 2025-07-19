/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Implement a data structure for PCB queue
 * File Name:           pcb_queue.c
 * File Content:        Implementation of functions for PCB queue
 * =============================================================== */

#include "./pcb_queue.h"
#include "./kernel_fn.h"

#include <stdlib.h>
#include <stdio.h> // for dprintf()
#include <unistd.h> // for STDERR_FILENO

pcb_queue_t pcb_queue_init(queue_type_t queue_type_code) {

    pcb_queue_t new_pcb_queue = (pcb_queue_t) {
        .q_type = queue_type_code,
        .q_head_ptr = NULL,
        .q_end_ptr = NULL,
        .length = 0,
        .data_destroy_fn = &pcb_destroy,
    };
    return new_pcb_queue;
}


pcb_t* pcb_queue_pop(pcb_queue_t* self_ptr) {
    // pop out the head of the queue

    if (self_ptr == NULL) {
        return NULL;
    }  

    if (queue_is_empty(self_ptr)) {
        return NULL;
    }

    // non-empty queue    
    pcb_t* result_pcb_ptr = queue_head(self_ptr); // get the target PCB
    self_ptr->q_head_ptr = thrd_next(queue_head(self_ptr)); // update queue head
    if (queue_len(self_ptr) == 1) {        
        self_ptr->q_end_ptr = NULL; // update queue end
    }
    self_ptr->length --;
    result_pcb_ptr->next_pcb_ptr = NULL; // disconnect the link between result PCB and the queue
    return result_pcb_ptr;    
}


void pcb_queue_push(pcb_queue_t* self_ptr, pcb_t* pcb_ptr) {
    // push to the end of the queue

    spthread_disable_interrupts_self();
    // if already in the queue, do nothing and return
    if (pcb_in_prio_queue(pcb_ptr, self_ptr)) {
        return;
    }

    if (queue_is_empty(self_ptr)) {
        self_ptr->q_head_ptr = pcb_ptr; // empty queue ==> update queue head
    } else {
        self_ptr->q_end_ptr->next_pcb_ptr = pcb_ptr; // non-empty queue ==> update the next_pointer of the queue end 
    }
    self_ptr->q_end_ptr = pcb_ptr; // update queue end
    self_ptr->length++;
    spthread_enable_interrupts_self(); 
    return;
}


void pcb_queue_destroy(pcb_queue_t* self_ptr) {

    if (self_ptr == NULL) {
        return;
    }    

    while (!queue_is_empty(self_ptr)) {        
        // pcb_t* curr_pcb_ptr = pcb_queue_pop(self_ptr);
        // self_ptr->data_destroy_fn(curr_pcb_ptr);
        pcb_queue_pop(self_ptr); // do not free each PCB because pcb_vec_destroy() will do
    }
    return;
}


pcb_t* pcb_queue_pop_by_pid(pcb_queue_t* self_ptr, pid_t target_pid) {    
    // pop out a PCB by its PID

    if (self_ptr == NULL) {
        return NULL;
    }  

    if (queue_is_empty(self_ptr)) {
        return NULL;
    }

    if (thrd_pid(queue_head(self_ptr)) == target_pid) {
        // the target PID is the head of the queue
        pcb_t* result_pcb_ptr = pcb_queue_pop(self_ptr);
        return result_pcb_ptr;
    }

    // non-empty queue AND queue head is not the target
    pcb_t* curr_pcb_ptr = queue_head(self_ptr);
    while ((thrd_next(curr_pcb_ptr) != NULL) && (thrd_pid(thrd_next(curr_pcb_ptr)) != target_pid)) {
        curr_pcb_ptr = thrd_next(curr_pcb_ptr);
    }

    if (thrd_next(curr_pcb_ptr) == NULL) {
        // no such PID found in the queue
        return NULL;
    }

    // found the target PID
    pcb_t* result_pcb_ptr = thrd_next(curr_pcb_ptr); // get the target PCB
    curr_pcb_ptr->next_pcb_ptr = thrd_next(thrd_next(curr_pcb_ptr)); // update the next_pointer for the preceding PCB 
    if (result_pcb_ptr == queue_end(self_ptr)) {
        self_ptr->q_end_ptr = curr_pcb_ptr; // target PCB is at the end of the queue ==> update queue end
    }
    self_ptr->length --;
    result_pcb_ptr->next_pcb_ptr = NULL; // disconnect the link between result PCB and the queue
    return result_pcb_ptr;  

}

void print_queue_info(pcb_queue_t* self_ptr) {
    pcb_t* curr_pcb_ptr = queue_head(self_ptr);
    dprintf(STDERR_FILENO, "============ Print Queue info ============\n");
    dprintf(STDERR_FILENO, "Queue Type: %d\n", queue_type(self_ptr));
    dprintf(STDERR_FILENO, "Queue Length: %zu\n", queue_len(self_ptr));
    dprintf(STDERR_FILENO, "~~~~~~ Now print each PCB info ~~~~~~\n");
    while (curr_pcb_ptr != NULL) {
        print_pcb_info(curr_pcb_ptr);
        curr_pcb_ptr = thrd_next(curr_pcb_ptr);
    }

}














