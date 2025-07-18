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
#include "./kernel_definition.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <unistd.h>


#define USEC_PER_MSEC 1000  // 1000 microseconds (us) per 1 millisecond (ms)

const int queue_pick_pattern[QUEUE_PICK_PATTERN_LENGTH] = {0, 1, 0, 2, 1, 0, 1, 0, 2, 0, 1, 0, 2, 0, 1, 0, 1, 0, 2};




void handler_sigalrm_scheduler(int signum) {
    // the SIGALRM handler does nothing
    // the handler is for scheduler thread
}


void scheduler_fn(scheduler_para_t* arg_ptr) {    

    // scheduler needs to receive SIGALRM (from timer) 
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

    // block SIGINT and SIGTSTP for scheduler thread - only shell should handle these
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
    pcb_t* curr_run_pcb_ptr;
    while (!pennos_done) {

        for (int i = 0; i < arg_ptr->q_pick_pattern_len; i++) {  
            //////////////////////////////////////////////////////////////////
            spthread_disable_interrupts_self(); // protection ON       
            // process pcb vec
            for (int i = 0; i < pcb_vec_len(&all_unreaped_pcb_vector); i++) {
                curr_pcb_ptr = (&all_unreaped_pcb_vector)->pcb_ptr_array[i];
                if ((thrd_status(curr_pcb_ptr) == THRD_RUNNING) && (!pcb_in_prio_queue(curr_pcb_ptr, &priority_queue_array[thrd_priority(curr_pcb_ptr)]))) { 
                    // found a thread that is ready for run but not in its corresponding priority queue
                    // ==> push back to priority queue
                    pcb_queue_push(&priority_queue_array[thrd_priority(curr_pcb_ptr)], curr_pcb_ptr);  
                    continue;
                } 
                
                if ((thrd_status(curr_pcb_ptr) == THRD_BLOCKED) && thrd_sleepstamp(curr_pcb_ptr) != 0) {                                        
                    if (global_clock - thrd_sleepstamp(curr_pcb_ptr) >= thrd_sleeplength(curr_pcb_ptr)) {
                        curr_pcb_ptr->status = THRD_RUNNING;
                        pcb_queue_push(&priority_queue_array[thrd_priority(curr_pcb_ptr)], curr_pcb_ptr);                          
                    }
                    continue;
                }
                
                if (thrd_status(curr_pcb_ptr) == THRD_REAPED) {
                    // remove and destroy reaped thread from pcb vec                    
                    pcb_vec_remove_by_pcb(&all_unreaped_pcb_vector, curr_pcb_ptr);
                    pcb_destroy(curr_pcb_ptr);  
                    i--;
                }
            }
            spthread_enable_interrupts_self(); // protection OFF
            //////////////////////////////////////////////////////////////////

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

                ///////////// update global clock before sigsuspend /////////
                spthread_disable_interrupts_self();
                global_clock++;
                //dprintf(STDERR_FILENO, "Scheduler tick on empty queues: # %u\n", global_clock);        
                spthread_enable_interrupts_self();
                /////////////////////////////////////////////////////////////        
                sigsuspend(&sig_set_ex_sigalrm);    
                continue;                         
            }

            // at least one queue is not empty 
            // ==> keep looping till find the non-empty queue at its "quantum slot"

            ////////////////////////////////////////////////////////////////////
            // make sure scheduler only continues a runnable thread and meanwhile "refresh" queue
            spthread_disable_interrupts_self(); // protection ON         
            pcb_queue_t* curr_queue_ptr = &arg_ptr->q_array[arg_ptr->q_pick_pattern_array[i]];
            curr_pcb_ptr = queue_head(curr_queue_ptr);
            curr_run_pcb_ptr = NULL;

            while (curr_pcb_ptr != NULL) {
                if (thrd_status(curr_pcb_ptr) == THRD_RUNNING) {
                    curr_run_pcb_ptr = curr_pcb_ptr;
                    curr_pcb_ptr = thrd_next(curr_pcb_ptr);
                } else {
                    pcb_t* temp_pcb_ptr = curr_pcb_ptr;
                    curr_pcb_ptr = thrd_next(curr_pcb_ptr);
                    pcb_queue_pop_by_pid(curr_queue_ptr, thrd_pid(temp_pcb_ptr));
                    if (thrd_status(temp_pcb_ptr) == THRD_REAPED) {
                        pcb_vec_remove_by_pcb(&all_unreaped_pcb_vector, temp_pcb_ptr);
                        pcb_destroy(temp_pcb_ptr);
                    }
                }
            }

            if (curr_run_pcb_ptr == NULL) {
                // this queue is "cleaned" and no runnable thread found
                spthread_enable_interrupts_self(); // protection OFF
                continue;
            }     
            ////////////////////////////////////////////////////////////////////
            // now we have a runnable thread from this queue
            spthread_continue(thrd_handle(curr_run_pcb_ptr));
            spthread_enable_interrupts_self(); // protection OFF


            klog("[%5d]\tSCHEDULE\t%d\t%d\tprocess", global_clock, thrd_pid(curr_run_pcb_ptr), queue_type(curr_queue_ptr));
            
            ///////////// update global clock before sigsuspend /////////
            spthread_disable_interrupts_self();
            global_clock++;
            //dprintf(STDERR_FILENO, "Scheduler tick: # %u\n", global_clock);        
            spthread_enable_interrupts_self();
            /////////////////////////////////////////////////////////////     
            sigsuspend(&sig_set_ex_sigalrm);    

            spthread_disable_interrupts_self(); // protection ON  
            if (pcb_queue_pop(curr_queue_ptr) == NULL) {
                continue;
            }

            if (thrd_status(curr_run_pcb_ptr) == THRD_RUNNING) {
                // only suspend and push back to priority queue if the thread status is still RUNNING
                spthread_suspend(thrd_handle(curr_run_pcb_ptr));                
                pcb_queue_push(curr_queue_ptr, curr_run_pcb_ptr);    

            } else if ((thrd_status(curr_run_pcb_ptr) == THRD_BLOCKED) && thrd_sleepstamp(curr_run_pcb_ptr) != 0) {                                        
                if (global_clock - thrd_sleepstamp(curr_run_pcb_ptr) >= thrd_sleeplength(curr_run_pcb_ptr)) {
                    curr_run_pcb_ptr->status = THRD_RUNNING;
                    pcb_queue_push(curr_queue_ptr, curr_run_pcb_ptr);                           
                }       

            } else if (thrd_status(curr_run_pcb_ptr) == THRD_REAPED) {
                pcb_vec_remove_by_pcb(&all_unreaped_pcb_vector, curr_run_pcb_ptr);
                pcb_destroy(curr_run_pcb_ptr);
            }
            
            spthread_enable_interrupts_self(); // protection OFF




        } // end of for loop

    }   // end of while loop 

    dprintf(STDERR_FILENO, "~~~~~~~~~~ Scheduler function exit ~~~~~~~~~~\n");

}