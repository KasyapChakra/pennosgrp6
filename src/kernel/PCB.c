/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Implement a data structure for process control block (PCB)
 * File Name:           PCB.c
 * File Content:        Implementation of functions for pcb struct
 * =============================================================== */

#include "./PCB.h"
#include "./kernel_fn.h"
#include "../util/panic.h"
#include "./pcb_vec.h"

#include <stdlib.h>
#include <stdio.h> // for dprintf()
#include <unistd.h> // for STDERR_FILENO


int pcb_init_empty(pcb_t** result_pcb, pcb_t* parent_pcb_ptr, int priority_code, pid_t pid) {

    pcb_t* self_pcb_ptr = calloc(1, sizeof(pcb_t));
    if (self_pcb_ptr == NULL) {
        // calloc() failed
        perror("pcb_init() memory allocation failed");
        return -1;
    }
    

    // --- ID ---    
    self_pcb_ptr->pid = pid;
    self_pcb_ptr->pgid = pid;
    self_pcb_ptr->ppid = parent_pcb_ptr? thrd_pid(parent_pcb_ptr) : 0;

    // --- attributes ---
    self_pcb_ptr->priority_level = priority_code;     

    // --- child info ---
    self_pcb_ptr->num_child_pids = 0;  
    for (int i = 0; i < NUM_CHILDREN_MAX; i++) {
        self_pcb_ptr->child_pids[i] = -1; // empty child pid set to -1
    }

    // handle parent (if exists) PCB's child pid info
    if (parent_pcb_ptr != NULL) {
        if (pcb_add_child_pid(parent_pcb_ptr, pid) == -1) {
            perror("pcb_init() failed to modify parent's child info");
            return -1;
        }
    }    

    // --- for priority queue link list ---
    self_pcb_ptr->next_pcb_ptr = NULL;       

    // --- status related ---
    self_pcb_ptr->status = THRD_STOPPED;
    self_pcb_ptr->pre_status = thrd_status(self_pcb_ptr);    
    self_pcb_ptr->exit_code = -1;
    self_pcb_ptr->term_signal = 0;
    self_pcb_ptr->stop_signal = 0;
    self_pcb_ptr->cont_signal = 0;
    self_pcb_ptr->errno = 0; 
    self_pcb_ptr->sleep_stamp = 0;    
    self_pcb_ptr->sleep_length = 0;  

    // --- others (to be decided) ---
    self_pcb_ptr->fds = NULL;    

    *result_pcb = self_pcb_ptr;
    return 0;
}


int pcb_init(spthread_t thread, pcb_t** result_pcb, pcb_t* parent_pcb_ptr, int priority_code, pid_t pid, 
             char* command) {

    if (pcb_init_empty(result_pcb, parent_pcb_ptr, priority_code, pid) == -1) {
        return -1;
    }
    
    (*result_pcb)->thrd = thread;
    (*result_pcb)->command = command; 
    (*result_pcb)->status = THRD_RUNNING;
    return 0;
}

void pcb_disconnect_parent(pcb_t* self_ptr) {
    // handle parent (if exists) PCB's child pid info 
    pcb_t* parent_pcb_ptr = pcb_vec_seek_pcb_by_pid(&all_unreaped_pcb_vector, thrd_ppid(self_ptr)); 

    if (parent_pcb_ptr != NULL) {
        pcb_remove_child_pid(parent_pcb_ptr, thrd_pid(self_ptr));
    }
}

void pcb_disconnect_child(pcb_t* self_ptr) {
    // handle child (if exists) PCB's parent pid info
    for (int i = 0; i < thrd_num_child(self_ptr); i++) {
        pcb_t* child_pcb_ptr = pcb_vec_seek_pcb_by_pid(&all_unreaped_pcb_vector, self_ptr->child_pids[i]);
        if (child_pcb_ptr != NULL) {
            child_pcb_ptr->ppid = 1; // assign child parent to init (pid = 1)            
        }   
        self_ptr->child_pids[i] = -1;
    }
    self_ptr->num_child_pids = 0;
}



void pcb_destroy(pcb_t* self_ptr) {

    //pcb_disconnect_parent(self_ptr);
    //pcb_disconnect_child(self_ptr);

    free(self_ptr);    
}


bool is_thrd_status_changed(pcb_t* self_ptr) {  
    if ((thrd_status(self_ptr) == THRD_STOPPED) || (thrd_status(self_ptr) == THRD_ZOMBIE)) {
        return (thrd_status(self_ptr) == thrd_pre_status(self_ptr));
    }

    // thread status is now either RUNNING or BLOCKED
    // -- change between RUNNING and BLOCKED does NOT count as change
    // -- change from STOPPED to RUNNING/BLOCKED only counts if there is SIGCONT    
    if ((thrd_pre_status(self_ptr) == THRD_STOPPED) && (self_ptr->cont_signal == P_SIGCONT)) {
        return true;
    } 

    return false;    
}

void reset_pcb_status_signal(pcb_t* self_ptr) {
    self_ptr->pre_status = thrd_status(self_ptr);
    self_ptr->exit_code = 0;
    self_ptr->term_signal = 0;
    self_ptr->stop_signal = 0;
    self_ptr->cont_signal = 0;
}

int pcb_add_child_pid(pcb_t* self_ptr, pid_t pid) {
    if (thrd_num_child(self_ptr) == NUM_CHILDREN_MAX) {
        return -1; // child full
    }

    self_ptr->child_pids[thrd_num_child(self_ptr)] = pid;
    self_ptr->num_child_pids++;
    return 0;
}

int pcb_remove_child_pid(pcb_t* self_ptr, pid_t pid) {
    if (thrd_num_child(self_ptr) == 0) {
        return -1; // child empty
    }

    int child_index;
    for (child_index = 0; child_index < thrd_num_child(self_ptr); child_index++) {
        if (self_ptr->child_pids[child_index] == pid) {
            break;
        }
    }

    if (child_index == thrd_num_child(self_ptr)) {
        return -1; // child pid not found
    }

    for (int i = child_index; i < thrd_num_child(self_ptr) - 1; i++) {
        self_ptr->child_pids[i] = self_ptr->child_pids[i+1];
    }
    self_ptr->child_pids[thrd_num_child(self_ptr) - 1] = -1;
    self_ptr->num_child_pids--;
    return 0;
}




void print_pcb_info(pcb_t* self_ptr) {
    dprintf(STDERR_FILENO, "\t------ Print PCB info ------\n");
    dprintf(STDERR_FILENO, "\tThread CMD: %s\n", thrd_CMD(self_ptr));
    dprintf(STDERR_FILENO, "\tThread PID: %d\n", thrd_pid(self_ptr));
    dprintf(STDERR_FILENO, "\tThread PGID: %d\n", thrd_pgid(self_ptr));
    dprintf(STDERR_FILENO, "\tThread PPID: %d\n", thrd_ppid(self_ptr));    
    dprintf(STDERR_FILENO, "\tThread Priority Level: %d\n", thrd_priority(self_ptr));    
    dprintf(STDERR_FILENO, "\tThread command: %s\n", thrd_CMD(self_ptr));        
    dprintf(STDERR_FILENO, "\tThread Status: %d\n", thrd_status(self_ptr));
    dprintf(STDERR_FILENO, "\tThread Number of child: %d\n", thrd_num_child(self_ptr));
    dprintf(STDERR_FILENO, "\t\t--- List of child PIDs ---\n");
    for (int i = 0; i < thrd_num_child(self_ptr); i++) {
        dprintf(STDERR_FILENO, "\t\tChild # %d PID: %d\n", i, self_ptr->child_pids[i]);
    }
    dprintf(STDERR_FILENO, "\n");
}

void print_pcb_info_single_line(pcb_t* self_ptr) {
    const char* status_str;
    switch (thrd_status(self_ptr)) {
        case THRD_RUNNING: status_str = "R"; break;
        case THRD_STOPPED: status_str = "S"; break;
        case THRD_BLOCKED: status_str = "B"; break;
        case THRD_ZOMBIE:  status_str = "Z"; break;
        case THRD_REAPED:  status_str = "T"; break;
    }    
    dprintf(STDERR_FILENO, "%d\t%d\t%d\t%s\t%s\n", thrd_pid(self_ptr), thrd_ppid(self_ptr), thrd_priority(self_ptr), status_str, thrd_CMD(self_ptr));
}















