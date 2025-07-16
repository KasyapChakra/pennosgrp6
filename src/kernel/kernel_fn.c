/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Kernel related functions
 * File Name:           kernel_fn.c
 * File Content:        Implementation of kernel related functions
 * =============================================================== */
#define _GNU_SOURCE

#include "./kernel_fn.h"
#include "./spthread.h"
#include "./PCB.h"
#include "./pcb_queue.h"
#include "./pcb_vec.h"
#include "./scheduler.h"
#include "../user/shell.h"
#include "../shell/shell.h"
#include "./klogger.h"
#include "../common/pennfat_errors.h"
#include "../util/panic.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>


volatile pid_t pid_count;
volatile bool pennos_done;
volatile k_errno_t k_errno;

#define SHELL_THREAD_NAME "shell"
#define INIT_THREAD_NAME "init"

pcb_queue_t priority_queue_array[NUM_PRIORITY_QUEUES]; 

pcb_queue_t blocked_queue; // queue of blocked/sleeping threads
pcb_queue_t stopped_queue; // queue of stopped threads
// this vector holds all the PCBs (threads) that have not been reaped
pcb_vec_t all_unreaped_pcb_vector;


volatile int count_p0 = 0; // debug //////////
volatile int count_p1 = 0; // debug //////////
volatile int count_p2 = 0; // debug //////////


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

void pennos_kernel(void) {
    pennos_done = false;          

    // pid_count starts at 1 because init thread is the 1st thread
    pid_count = 1;    

    // initialize 3 Round Robin queues for RUNNING threads, plus blocked queue
    for (int i = 0; i < NUM_PRIORITY_QUEUES; i++) {
        priority_queue_array[i] = pcb_queue_init(i); // need to be destroyed later
    }  

    blocked_queue = pcb_queue_init(QUEUE_BLOCKED);
    stopped_queue = pcb_queue_init(QUEUE_STOPPED);
    // initialize PCB vector to hold all unreaped PCBs
    all_unreaped_pcb_vector = pcb_vec_new(0, pcb_destroy); // need to be destroyed later 
    
    pcb_t* temp_pcb_ptr; 

    // ------ set up init thread ------ //
    spthread_t thrd_init;
    spthread_create(&thrd_init, NULL, thrd_init_fn, NULL);        
    if (pcb_init(thrd_init, &temp_pcb_ptr, QUEUE_PRIORITY_0, pid_count++, INIT_THREAD_NAME) == -1) {
        panic("pcb_init() failed!\n");
    }
    pcb_queue_push(&priority_queue_array[QUEUE_PRIORITY_0], temp_pcb_ptr); // push to priority queue
    k_register_pcb(temp_pcb_ptr); // push to pcb vector


    // ------ set up and run scheduler function ------ //    
    scheduler_para_t new_scheduler_para = (scheduler_para_t) {
        .num_queues = NUM_PRIORITY_QUEUES,
        .q_array = priority_queue_array,
        .q_pick_pattern_len = QUEUE_PICK_PATTERN_LENGTH,
        .q_pick_pattern_array = queue_pick_pattern,
        .quantum_msec = 100,
    };  
    scheduler_fn(&new_scheduler_para);
    spthread_continue(thrd_init); // make sure thread init is not suspended

    // scheduler running ...
    // ...
    // till shell user exit by ctrl-D    

    spthread_join(thrd_init, NULL); // wait for init thread to join


    pcb_vec_destroy(&all_unreaped_pcb_vector);

    // for debug ///////////////////////
    dprintf(STDERR_FILENO, "Final total tick: # %d\n", cumulative_tick_global);  
    dprintf(STDERR_FILENO, "\tFinal count for queue 0: # %d\n", count_p0);  
    dprintf(STDERR_FILENO, "\tFinal count for queue 1: # %d\n", count_p1);  
    dprintf(STDERR_FILENO, "\tFinal count for queue 2: # %d\n", count_p2);  
    dprintf(STDERR_FILENO, "\tFinal queue 0 / queue 1: # %f\n", (float) count_p0 / count_p1);  
    dprintf(STDERR_FILENO, "\tFinal queue 1 / queue 2: # %f\n", (float) count_p1 / count_p2);  
    dprintf(STDERR_FILENO, "\tFinal queue 0 / queue 2: # %f\n", (float) count_p0 / count_p2);  

    dprintf(STDERR_FILENO, "########## PennOS exit ##########\n");

    exit(EXIT_SUCCESS);
}

void* thrd_init_fn([[maybe_unused]] void* arg) {

    // block SIGALRM | SIGINT | SIGTSTP (the mask is inherited by all child threads)
    sigset_t sig_set_init;
    sigemptyset(&sig_set_init);
    sigaddset(&sig_set_init, SIGALRM);
    sigaddset(&sig_set_init, SIGINT);
    sigaddset(&sig_set_init, SIGTSTP);
    pthread_sigmask(SIG_BLOCK, &sig_set_init, NULL);   

    pcb_t* temp_pcb_ptr;
    
    // ------ set up shell thread ------ //
    spthread_t thrd_shell;
    spthread_create(&thrd_shell, NULL, thrd_shell_fn, NULL);        
    if (pcb_init(thrd_shell, &temp_pcb_ptr, QUEUE_PRIORITY_0, pid_count++, SHELL_THREAD_NAME) == -1) {
        panic("pcb_init() failed!\n");
    }    
    pcb_queue_push(&priority_queue_array[QUEUE_PRIORITY_0], temp_pcb_ptr); // push to priority queue
    k_register_pcb(temp_pcb_ptr); // push to pcb_vec
        


    ///////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////
    
    // test threads for the 3 queues
    
    // spthread_t temp_spthread;  

    // test thread for queue 0
    spthread_create(&temp_spthread, NULL, thrd_print_p0, NULL);    
    pcb_init(temp_spthread, &temp_pcb_ptr, 0, pid_count++, "Test1");
    pcb_queue_push(&priority_queue_array[0], temp_pcb_ptr);
    k_register_pcb(temp_pcb_ptr);
       
    // test thread for queue 1
    spthread_create(&temp_spthread, NULL, thrd_print_p1, NULL);    
    pcb_init(temp_spthread, &temp_pcb_ptr, 1, pid_count++, "Test2");
    pcb_queue_push(&priority_queue_array[1], temp_pcb_ptr);
    k_register_pcb(temp_pcb_ptr);
        
    // test thread for queue 2
    spthread_create(&temp_spthread, NULL, thrd_print_p2, NULL);    
    pcb_init(temp_spthread, &temp_pcb_ptr, 2, pid_count++, "Test3");  
    pcb_queue_push(&priority_queue_array[2], temp_pcb_ptr);      
    k_register_pcb(temp_pcb_ptr);

    // print info for the 3 queues for debug ///////////////////
    print_queue_info(&priority_queue_array[0]);
    print_queue_info(&priority_queue_array[1]);
    print_queue_info(&priority_queue_array[2]);   


    ///////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////  

    pcb_t* self_pcb_ptr = k_get_self_pcb();
    self_pcb_ptr->status = THRD_BLOCKED; // modify self status to BLOCKED as it will block and wait for shell to join
    spthread_join(thrd_shell, NULL); // wait for shell thread to join
    self_pcb_ptr->status = THRD_RUNNING; // modify self status to RUNNING as shell thread has joined


    // clean up
    // cancel and join all PCBs in pcb vector
    // starts with i = 2, because init at i = 0 and shell at i = 1
    for (int i = 2; i < pcb_vec_len(&all_unreaped_pcb_vector); i++) {
        cancel_and_join_pcb((&all_unreaped_pcb_vector)->pcb_ptr_array[i]);
    }
    
    /*
    // clean up
    for (int i = 0; i < NUM_PRIORITY_QUEUES; i++) {    

        //print_queue_info(&priority_queue_array[i]);

        temp_pcb_ptr = queue_head(&priority_queue_array[i]);
        while (temp_pcb_ptr != NULL) {             
            cancel_and_join_pcb(temp_pcb_ptr);        
            temp_pcb_ptr = thrd_next(temp_pcb_ptr);
        }    
        pcb_queue_destroy(&priority_queue_array[i]); 
    }  
    */

    dprintf(STDERR_FILENO, "~~~~~~~~~~ Init thread exit ~~~~~~~~~~\n");

    spthread_exit(NULL);
    return NULL;    
}


/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////////

void* thrd_print_p0([[maybe_unused]] void* arg) {
    while (true) {
        spthread_disable_interrupts_self();
        count_p0++;
        usleep(10000);
        //dprintf(STDERR_FILENO, "\t------ Thread 0 output: # %d ------\n", count_p0);        
        spthread_enable_interrupts_self();        
    }
    return NULL;
}


void* thrd_print_p1([[maybe_unused]] void* arg) {
    while (true) {
        spthread_disable_interrupts_self();
        count_p1++;
        usleep(10000);
        //dprintf(STDERR_FILENO, "\t------ Thread 1 output: # %d ------\n", count_p1);        
        spthread_enable_interrupts_self();        
    }
    return NULL;
}

void* thrd_print_p2([[maybe_unused]] void* arg) {
    while (true) {
        spthread_disable_interrupts_self();
        count_p2++;
        usleep(10000);
        //dprintf(STDERR_FILENO, "\t------ Thread 2 output: # %d ------\n", count_p2);        
        spthread_enable_interrupts_self();        
    }
    return NULL;
}


