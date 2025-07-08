/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Scheduler related functions
 * File Name:           scheduler.c
 * File Content:        Implementation of the scheduler related functions
 * =============================================================== */

#include "./kernel/scheduler.h"
#include "./kernel/spthread.h"
#include "./kernel/PCB.h"
#include "./kernel/pcb_queue.h"
#include "./kernel/kernel_fn.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>


#define USEC_PER_MSEC 1000  // 1000 microseconds (us) per 1 millisecond (ms)

const int queue_pick_pattern[QUEUE_PICK_PATTERN_LENGTH] = {0, 1, 0, 2, 1, 0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 1, 0, 2};

volatile sig_atomic_t cumulative_tick_global = 0; // for DEBUG, can be deleted later ////////////////


void handler_sigalrm(int signum) {
    // the SIGALRM handler does nothing
    // the handler is for scheduler thread
}

void* thrd_scheduler_fn(void* arg) {
    scheduler_para_t* arg_ptr = (scheduler_para_t*) arg; 

    // scheduler thread needs to receive SIGALRM (from timer) 
    // but not be terminated by SIGALRM
    // hence install empty handler that does nothing but not block SIGALRM

    // signal mask that blocks all signals EXCEPT SIGALRM
    sigset_t sig_set_ex_sigalrm;
    sigfillset(&sig_set_ex_sigalrm);
    sigdelset(&sig_set_ex_sigalrm, SIGALRM);    

    // install handler for SIGALRM
    // ==> does nothing, but not block or ignore SIGALRM
    // ==> SIGALRM cannot terminate the scheduler thread
    struct sigaction sigaction_st_sigalrm = (struct sigaction){
        .sa_handler = handler_sigalrm,
        // this mask blocks all signals except SIGALRM
        // but SIGALRM handler automatically blocks SIGALRM during handler execution
        // so essentially this mask blocks everything during handler execution
        .sa_mask = sig_set_ex_sigalrm, 
        .sa_flags = SA_RESTART,
    };      
    sigaction(SIGALRM, &sigaction_st_sigalrm, NULL);

    // make sure SIGALRM is unblocked for scheduler thread
    sigset_t sig_set_only_sigalrm;
    sigemptyset(&sig_set_only_sigalrm);
    sigaddset(&sig_set_only_sigalrm, SIGALRM);
    pthread_sigmask(SIG_UNBLOCK, &sig_set_only_sigalrm, NULL);

    // set and start timer (interval = 100 ms)
    struct itimerval scheduler_itimer;
    scheduler_itimer.it_interval = (struct timeval) {
        .tv_sec = 0,
        .tv_usec = USEC_PER_MSEC * arg_ptr->quantum_msec
    };
    scheduler_itimer.it_value = scheduler_itimer.it_interval;
    setitimer(ITIMER_REAL, &scheduler_itimer, NULL);    
    

    pcb_t* curr_pcb_ptr;
    while (true) {

        for (int i = 0; i < arg_ptr->q_pick_pattern_len; i++) {  

            spthread_disable_interrupts_self();          
            pcb_queue_t curr_queue = arg_ptr->q_array[arg_ptr->q_pick_pattern_array[i]];
            if (queue_is_empty(&curr_queue)) {
                spthread_enable_interrupts_self();
                continue;
            }
            curr_pcb_ptr = queue_head(&curr_queue);
            spthread_enable_interrupts_self();

            spthread_continue(thrd_handle(curr_pcb_ptr));
            
            ////////// for DEBUG ///////////////
            spthread_disable_interrupts_self();
            cumulative_tick_global = (cumulative_tick_global + 1) % 10000;
            dprintf(STDERR_FILENO, "Scheduler tick: # %d\n", cumulative_tick_global);        
            spthread_enable_interrupts_self();
            /////////////////////////     

            sigsuspend(&sig_set_ex_sigalrm);            
            spthread_suspend(thrd_handle(curr_pcb_ptr));

            spthread_disable_interrupts_self(); 
            curr_pcb_ptr = pcb_queue_pop(&curr_queue);
            pcb_queue_push(&curr_queue, curr_pcb_ptr);
            spthread_enable_interrupts_self();

        }

    }    

    spthread_exit(NULL);
    return NULL;   
}