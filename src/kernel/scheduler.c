/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Scheduler related functions
 * File Name:           scheduler.c
 * File Content:        Implementation of the scheduler related functions
 * =============================================================== */
#define _GNU_SOURCE

#include "./scheduler.h"
#include "./spthread.h"
#include "./PCB.h"
#include "./pcb_queue.h"
#include "./kernel_fn.h"
#include "./klogger.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>


#define USEC_PER_MSEC 1000  // 1000 microseconds (us) per 1 millisecond (ms)

const int queue_pick_pattern[QUEUE_PICK_PATTERN_LENGTH] = {0, 1, 0, 2, 1, 0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 1, 0, 2};

volatile unsigned long g_ticks = 0;   // total number of 100-ms clock ticks since boot
#define TICKS_INC() do { g_ticks++; } while(0)
// Keep old symbol for now so we don’t have to touch every call-site at once
#define cumulative_tick_global ((int)g_ticks)


void handler_sigalrm_scheduler(int signum) {
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
        .sa_handler = handler_sigalrm_scheduler,
        // this mask blocks all signals except SIGALRM
        // but SIGALRM handler automatically blocks SIGALRM during handler execution
        // so essentially this mask blocks everything during handler execution
        .sa_mask = sig_set_ex_sigalrm, 
        .sa_flags = SA_RESTART,
    };      
    sigaction(SIGALRM, &sigaction_st_sigalrm, NULL);

    // block SIGINT and SIGTSTP for scheduler thread
    sigset_t sig_set_scheduler;
    sigemptyset(&sig_set_scheduler);
    sigaddset(&sig_set_scheduler, SIGINT);
    sigaddset(&sig_set_scheduler, SIGTSTP);
    pthread_sigmask(SIG_BLOCK, &sig_set_scheduler, NULL); 

    // unblock SIGALRM for scheduler thread    
    sigemptyset(&sig_set_scheduler);
    sigaddset(&sig_set_scheduler, SIGALRM);
    pthread_sigmask(SIG_UNBLOCK, &sig_set_scheduler, NULL);

    // set and start timer (interval = 100 ms)
    struct itimerval scheduler_itimer;
    scheduler_itimer.it_interval = (struct timeval) {
        .tv_sec = 0,
        .tv_usec = USEC_PER_MSEC * arg_ptr->quantum_msec
    };
    scheduler_itimer.it_value = scheduler_itimer.it_interval;
    setitimer(ITIMER_REAL, &scheduler_itimer, NULL);    

    pcb_t* curr_pcb_ptr;
    while (!pennos_done) {

        for (int i = 0; i < arg_ptr->q_pick_pattern_len; i++) {  

            /* one clock tick just occurred – update global counter */
            spthread_disable_interrupts_self();
            TICKS_INC();

            /* move any sleepers whose wake_tick has arrived back to run queues */
            size_t blk_len = queue_len(&blocked_queue);
            for (size_t bi = 0; bi < blk_len; bi++) {
                if (queue_is_empty(&blocked_queue)) break;
                pcb_t* sleeper = queue_head(&blocked_queue);
                if (sleeper->wake_tick <= g_ticks) {
                    pcb_queue_pop(&blocked_queue);
                    sleeper->status = THRD_RUNNING;
                    pcb_queue_push(&priority_queue_array[sleeper->priority_level], sleeper);
                    klog("[%5d]\tUNBLOCKED\t%d\t%d\tprocess", cumulative_tick_global, thrd_pid(sleeper), sleeper->priority_level);
                } else {
                    /* not ready – rotate to maintain fairness */
                    pcb_queue_pop(&blocked_queue);
                    pcb_queue_push(&blocked_queue, sleeper);
                }
            }
            spthread_enable_interrupts_self();

            if (pennos_done) {
                break;
            }

            // check to see if all queues are empty
            bool all_queues_empty = true;
            spthread_disable_interrupts_self(); // protection ON    
            for (int j = 0; j < arg_ptr->num_queues; j++) {                
                if (!queue_is_empty(&arg_ptr->q_array[j])) {                    
                    all_queues_empty = false;
                    break;
                }                
            }
            spthread_enable_interrupts_self(); // protection OFF
            // if all queues are empty ==> sigsuspend for a quantum            
            if (all_queues_empty) {

                ///////////////////////// for DEBUG /////////////////////////
                spthread_disable_interrupts_self();
                dprintf(STDERR_FILENO, "Scheduler tick on empty queues: # %d\n", cumulative_tick_global);        
                spthread_enable_interrupts_self();
                /////////////////////////////////////////////////////////////        

                sigsuspend(&sig_set_ex_sigalrm);    
                continue;                         
            }

            // at least one queue is not empty 
            // ==> keep looping till find the non-empty queue at its "quantum slot"
            spthread_disable_interrupts_self(); // protection ON         
            pcb_queue_t* curr_queue_ptr = &arg_ptr->q_array[arg_ptr->q_pick_pattern_array[i]];
            if (queue_is_empty(curr_queue_ptr)) {
                spthread_enable_interrupts_self(); // protection OFF
                continue;
            }

            /* Find the next runnable PCB in this queue.  
             *   – THRD_RUNNING = choose it for execution
             *   – THRD_STOPPED / THRD_BLOCKED = round-robin skip (pop & push)
             *   – THRD_ZOMBIE = remove from queue entirely
             */
            size_t qlen = queue_len(curr_queue_ptr);
            size_t tries = qlen;
            curr_pcb_ptr = NULL;
            while (tries-- > 0 && !queue_is_empty(curr_queue_ptr)) {
                pcb_t* candidate = queue_head(curr_queue_ptr);
                thrd_status_t st = thrd_status(candidate);
                if (st == THRD_ZOMBIE) {
                    /* discard zombies – they will be reaped elsewhere */
                    pcb_queue_pop(curr_queue_ptr);
                    continue;
                }
                if (st == THRD_RUNNING) {
                    curr_pcb_ptr = candidate;
                    break;
                }
                /* stopped or blocked – round-robin skip */
                pcb_queue_pop(curr_queue_ptr);
                pcb_queue_push(curr_queue_ptr, candidate);
            }
            if (curr_pcb_ptr == NULL) {
                /* no runnable task found in this queue for this tick */
                spthread_enable_interrupts_self(); // protection OFF
                continue;
            }
            spthread_enable_interrupts_self(); // protection OFF

            spthread_continue(thrd_handle(curr_pcb_ptr));
            klog("[%5d]\tSCHEDULE\t%d\t%d\tprocess", cumulative_tick_global, thrd_pid(curr_pcb_ptr), queue_type(curr_queue_ptr));
            
            ///////////////////////// for DEBUG /////////////////////////
            // spthread_disable_interrupts_self();
            // cumulative_tick_global = (cumulative_tick_global + 1) % 10000;
            // dprintf(STDERR_FILENO, "Scheduler tick: # %d\n", cumulative_tick_global);        
            // spthread_enable_interrupts_self();
            /////////////////////////////////////////////////////////////     

            sigsuspend(&sig_set_ex_sigalrm);            
            spthread_suspend(thrd_handle(curr_pcb_ptr));

            spthread_disable_interrupts_self(); // protection ON
            /* remove head (should be curr_pcb_ptr) */
            pcb_queue_pop(curr_queue_ptr);
            /* re-queue only if it is still runnable/eligible */
            thrd_status_t post_status = thrd_status(curr_pcb_ptr);
            if (post_status == THRD_RUNNING || post_status == THRD_STOPPED || post_status == THRD_BLOCKED) {
                pcb_queue_push(curr_queue_ptr, curr_pcb_ptr);
            }
            /* if ZOMBIE – drop, it will be reaped later */
            spthread_enable_interrupts_self(); // protection OFF

        }

    }    

    dprintf(STDERR_FILENO, "########### Scheduler exit ###########\n");

    spthread_exit(NULL);
    return NULL;   
}