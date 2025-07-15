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



int pcb_init(spthread_t thread, pcb_t** result_pcb, int priority_code, pid_t pid, 
             char* command) {

    pcb_t* temp_pcb_ptr = calloc(1, sizeof(pcb_t));
    if (temp_pcb_ptr == NULL) {
        // calloc() failed
        return -1;
    }

    temp_pcb_ptr->thrd = thread;
    temp_pcb_ptr->status = THRD_STOPPED;
    temp_pcb_ptr->priority_level = priority_code;
    temp_pcb_ptr->pid = pid;
    temp_pcb_ptr->pgid = 0;
    temp_pcb_ptr->ppid = 0;
    temp_pcb_ptr->num_child_pids = 0;
    temp_pcb_ptr->child_pids = NULL;
    temp_pcb_ptr->fds = NULL;
    temp_pcb_ptr->next_pcb_ptr = NULL;
    temp_pcb_ptr->command = command;

    *result_pcb = temp_pcb_ptr;
    return 0;
}

void pcb_destroy(pcb_t* self_ptr) {

    // likely need to free child_pds and fds
    free(self_ptr);
    self_ptr = NULL;
}

void print_pcb_info(pcb_t* self_ptr) {
    dprintf(STDERR_FILENO, "\t------ Print PCB info ------\n");
    dprintf(STDERR_FILENO, "\tThread CMD: %s\n", thrd_CMD(self_ptr));
    dprintf(STDERR_FILENO, "\tThread Status: %d\n", thrd_status(self_ptr));
    dprintf(STDERR_FILENO, "\tThread Priority Level: %d\n", thrd_priority(self_ptr));
    dprintf(STDERR_FILENO, "\tThread PID: %d\n", thrd_pid(self_ptr));
    dprintf(STDERR_FILENO, "\tThread PGID: %d\n", thrd_pgid(self_ptr));
    dprintf(STDERR_FILENO, "\tThread parent PID: %d\n\n", thrd_ppid(self_ptr));
}

















