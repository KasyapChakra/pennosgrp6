/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             PennOS
 * File Name:           test.c
 * File Content:        PennOS main
 * =============================================================== */


#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>

#include "./kernel/spthread.h"
#include "./kernel/PCB.h"
#include "./kernel/pcb_queue.h"
#include "./kernel/kernel_fn.h"
#include "./kernel/scheduler.h"


volatile int count_p0 = 0;
volatile int count_p1 = 0;
volatile int count_p2 = 0;


#define BUF_SIZE 4096



void* thrd_print_p0([[maybe_unused]] void* arg) {
    while (true) {
        spthread_disable_interrupts_self();
        count_p0++;
        //dprintf(STDERR_FILENO, "\t------ Thread 0 output: # %d ------\n", count_p0);        
        spthread_enable_interrupts_self();        
    }
    return NULL;
}

void* thrd_print_p1([[maybe_unused]] void* arg) {
    while (true) {
        spthread_disable_interrupts_self();
        count_p1++;
        //dprintf(STDERR_FILENO, "\t------ Thread 1 output: # %d ------\n", count_p1);        
        spthread_enable_interrupts_self();        
    }
    return NULL;
}

void* thrd_print_p2([[maybe_unused]] void* arg) {
    while (true) {
        spthread_disable_interrupts_self();
        count_p2++;
        //dprintf(STDERR_FILENO, "\t------ Thread 2 output: # %d ------\n", count_p2);        
        spthread_enable_interrupts_self();        
    }
    return NULL;
}


int main(void) {

    // block SIGALRM
    sigset_t sig_set_only_sigalrm;
    sigemptyset(&sig_set_only_sigalrm);
    sigaddset(&sig_set_only_sigalrm, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &sig_set_only_sigalrm, NULL);       

    pid_t pid_count = 0;

    priority_queue_array[0] = pcb_queue_init(QUEUE_PRIORITY_0);
    priority_queue_array[1] = pcb_queue_init(QUEUE_PRIORITY_1);
    priority_queue_array[2] = pcb_queue_init(QUEUE_PRIORITY_2);

    
    // scheduler thread
    spthread_t thrd_scheduler;
    scheduler_para_t new_scheduler_para = (scheduler_para_t) {
        .num_queues = NUM_PRIORITY_QUEUES,
        .q_array = priority_queue_array,
        .q_pick_pattern_len = QUEUE_PICK_PATTERN_LENGTH,
        .q_pick_pattern_array = queue_pick_pattern,
        .quantum_msec = 100,
    };    
    spthread_create(&thrd_scheduler, NULL, thrd_scheduler_fn, &new_scheduler_para);
    
    pid_count++;

    
    // test threads for the 3 queues
    pcb_t* temp_pcb_ptr; 

    
    spthread_t temp_spthread;  

    // test thread for queue 0
    spthread_create(&temp_spthread, NULL, thrd_print_p0, NULL);
    pid_count++;
    pcb_init(temp_spthread, &temp_pcb_ptr, 0, pid_count);
    pcb_queue_push(&priority_queue_array[0], temp_pcb_ptr);
    
    
    // test thread for queue 1
    spthread_create(&temp_spthread, NULL, thrd_print_p1, NULL);
    pid_count++;
    pcb_init(temp_spthread, &temp_pcb_ptr, 1, pid_count);
    pcb_queue_push(&priority_queue_array[1], temp_pcb_ptr);
    
    
    // test thread for queue 2
    spthread_create(&temp_spthread, NULL, thrd_print_p2, NULL);
    pid_count++;
    pcb_init(temp_spthread, &temp_pcb_ptr, 2, pid_count);  
    pcb_queue_push(&priority_queue_array[2], temp_pcb_ptr);  
    

    // print info for the 3 queues
    print_queue_info(&priority_queue_array[0]);
    print_queue_info(&priority_queue_array[1]);
    print_queue_info(&priority_queue_array[2]);


    spthread_continue(thrd_scheduler); // start scheduler 

    // main thread enters into block mode until user input ctrl-D
    dprintf(STDERR_FILENO, "########### Main thread block mode starts ###########\n");
    char buffer[BUF_SIZE];
    while (true) {
        ssize_t n = read(STDIN_FILENO, buffer, BUF_SIZE);
        if (n == 0) {
            // user enter ctrl-D
            break;
        }        
    }

    dprintf(STDERR_FILENO, "########### Received ctrl-D, starts to exit ###########\n");    

    // clean up
    
    cancel_and_join_thrd(thrd_scheduler); // termintate scheduler
    
    for (int i = 0; i < NUM_PRIORITY_QUEUES; i++) {        
        temp_pcb_ptr = queue_head(&priority_queue_array[i]);
        while (temp_pcb_ptr != NULL) {             
            cancel_and_join_pcb(temp_pcb_ptr);        
            temp_pcb_ptr = thrd_next(temp_pcb_ptr);
        }    
        pcb_queue_destroy(&priority_queue_array[i]); 
    }  
    
    

    dprintf(STDERR_FILENO, "Final total tick: # %d\n", cumulative_tick_global);  
    dprintf(STDERR_FILENO, "\tFinal count for queue 0: # %d\n", count_p0);  
    dprintf(STDERR_FILENO, "\tFinal count for queue 1: # %d\n", count_p1);  
    dprintf(STDERR_FILENO, "\tFinal count for queue 2: # %d\n", count_p2);  
    dprintf(STDERR_FILENO, "\tFinal queue 0 / queue 1: # %f\n", (float) count_p0 / count_p1);  
    dprintf(STDERR_FILENO, "\tFinal queue 1 / queue 2: # %f\n", (float) count_p1 / count_p2);  
    dprintf(STDERR_FILENO, "\tFinal queue 0 / queue 2: # %f\n", (float) count_p0 / count_p2);  

}