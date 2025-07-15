/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Implement a data structure for process control block (PCB)
 * File Name:           PCB.c
 * File Content:        Implementation of functions for pcb struct
 * =============================================================== */

#include "./PCB.h"

#include <stdlib.h>
#include <stdio.h> // for dprintf()
#include <unistd.h> // for STDERR_FILENO


int pcb_init_empty(pcb_t** result_pcb, int priority_code, pid_t pid) {

    pcb_t* temp_pcb_ptr = calloc(1, sizeof(pcb_t));
    if (temp_pcb_ptr == NULL) {
        // calloc() failed
        perror("pcb_init() memory allocation failed");
        return -1;
    }

    // --- ID ---    
    temp_pcb_ptr->pid = pid;
    temp_pcb_ptr->pgid = pid;
    temp_pcb_ptr->ppid = 0;

    // --- attributes ---
    temp_pcb_ptr->priority_level = priority_code;     

    // --- child info ---
    temp_pcb_ptr->num_child_pids = 0;    

    // --- for priority queue link list ---
    temp_pcb_ptr->next_pcb_ptr = NULL;       

    // --- status related ---
    temp_pcb_ptr->status = THRD_STOPPED;
    temp_pcb_ptr->pre_status = thrd_status(temp_pcb_ptr);    
    temp_pcb_ptr->exit_code = 0;
    temp_pcb_ptr->term_signal = 0;
    temp_pcb_ptr->stop_signal = 0;
    temp_pcb_ptr->cont_signal = 0;

    // --- others (to be decided) ---
    temp_pcb_ptr->fds = NULL;

    *result_pcb = temp_pcb_ptr;
    return 0;
}


int pcb_init(spthread_t thread, pcb_t** result_pcb, int priority_code, pid_t pid, 
             char* command) {

    if (pcb_init_empty(result_pcb, priority_code, pid) == -1) {
        return -1;
    }
    
    (*result_pcb)->thrd = thread;
    (*result_pcb)->command = command; 
    return 0;
}

void pcb_destroy(pcb_t* self_ptr) {

    // likely need to free child_pds and fds
    free(self_ptr);
    self_ptr = NULL;
}


bool is_thrd_status_changed(pcb_t* pcb_ptr) {  
    if ((thrd_status(pcb_ptr) == THRD_STOPPED) || (thrd_status(pcb_ptr) == THRD_ZOMBIE)) {
        return (thrd_status(pcb_ptr) == thrd_pre_status(pcb_ptr));
    }

    // thread status is now either RUNNING or BLOCKED
    // -- change between RUNNING and BLOCKED does NOT count as change
    // -- change from STOPPED to RUNNING/BLOCKED only counts if there is SIGCONT    
    if ((thrd_pre_status(pcb_ptr) == THRD_STOPPED) && (pcb_ptr->cont_signal == P_SIGCONT)) {
        return true;
    } 

    return false;    
}

void reset_pcb_status_signal(pcb_t* pcb_ptr) {
    pcb_ptr->pre_status = thrd_status(pcb_ptr);
    pcb_ptr->exit_code = 0;
    pcb_ptr->term_signal = 0;
    pcb_ptr->stop_signal = 0;
    pcb_ptr->cont_signal = 0;
}


void print_pcb_info(pcb_t* self_ptr) {
    dprintf(STDERR_FILENO, "\t------ Print PCB info ------\n");
    dprintf(STDERR_FILENO, "\tThread PID: %d\n", thrd_pid(self_ptr));
    dprintf(STDERR_FILENO, "\tThread PGID: %d\n", thrd_pgid(self_ptr));
    dprintf(STDERR_FILENO, "\tThread PPID: %d\n", thrd_ppid(self_ptr));    
    dprintf(STDERR_FILENO, "\tThread Priority Level: %d\n", thrd_priority(self_ptr));    
    dprintf(STDERR_FILENO, "\tThread command: %s\n", self_ptr->command);    
    dprintf(STDERR_FILENO, "\tThread Status: %d\n\n", thrd_status(self_ptr));

}

















