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

// pcb queues
pcb_queue_t priority_queue_array[NUM_PRIORITY_QUEUES]; // queue of running/runnable threads
pcb_queue_t blocked_queue; // queue of blocked/sleeping threads
pcb_queue_t stopped_queue; // queue of stopped threads
pcb_queue_t zombie_queue; // queue of zombie threads
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
    // initialize blocked (waiting/sleeping), stopped, zombie queues
    blocked_queue = pcb_queue_init(QUEUE_BLOCKED);
    stopped_queue = pcb_queue_init(QUEUE_STOPPED);
    zombie_queue = pcb_queue_init(QUEUE_ZOMBIE);
    // initialize PCB vector to hold all unreaped PCBs
    all_unreaped_pcb_vector = pcb_vec_new(0, pcb_destroy); // need to be destroyed later 
    
    pcb_t* temp_pcb_ptr; 

    // ------ set up init thread ------ //
    spthread_t thrd_init;
    spthread_create(&thrd_init, NULL, thrd_init_fn, NULL);        
    if (pcb_init(thrd_init, &temp_pcb_ptr, NULL, QUEUE_PRIORITY_0, pid_count++, INIT_THREAD_NAME) == -1) {
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
    // scheduler running ...
    // ...
    // till shell user exit by ctrl-D    
    spthread_continue(thrd_init); // make sure thread init is not suspended
    spthread_join(thrd_init, NULL); // wait for init thread to join
    // block wait to join init thread
    // ..
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

    // block SIGALRM for all threads (but not SIGINT/SIGTSTP - shell needs these)
    sigset_t sig_set_init;
    sigemptyset(&sig_set_init);
    sigaddset(&sig_set_init, SIGALRM);
    // Don't block SIGINT and SIGTSTP here - let shell handle them
    pthread_sigmask(SIG_BLOCK, &sig_set_init, NULL);

    pcb_t* temp_pcb_ptr;
    
    // ------ set up shell thread ------ //
    spthread_t thrd_shell;
    pcb_t* parent_pcb_ptr = k_get_self_pcb();
    spthread_create(&thrd_shell, NULL, thrd_shell_fn, NULL);        
    if (pcb_init(thrd_shell, &temp_pcb_ptr, parent_pcb_ptr, QUEUE_PRIORITY_0, pid_count++, SHELL_THREAD_NAME) == -1) {
        panic("pcb_init() failed!\n");
    }    
    pcb_queue_push(&priority_queue_array[QUEUE_PRIORITY_0], temp_pcb_ptr); // push to priority queue
    k_register_pcb(temp_pcb_ptr); // push to pcb_vec
        


    ///////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////
    
    // test threads for the 3 queues
    
    // spthread_t temp_spthread;  

    // test thread for queue 0
    // spthread_create(&temp_spthread, NULL, thrd_print_p0, NULL);    
    // pcb_init(temp_spthread, &temp_pcb_ptr, 0, pid_count++, "Test1");
    // pcb_queue_push(&priority_queue_array[0], temp_pcb_ptr);
    // k_register_pcb(temp_pcb_ptr);
       
    // // test thread for queue 1
    // spthread_create(&temp_spthread, NULL, thrd_print_p1, NULL);    
    // pcb_init(temp_spthread, &temp_pcb_ptr, 1, pid_count++, "Test2");
    // pcb_queue_push(&priority_queue_array[1], temp_pcb_ptr);
    // k_register_pcb(temp_pcb_ptr);
        
    // // test thread for queue 2
    // spthread_create(&temp_spthread, NULL, thrd_print_p2, NULL);    
    // pcb_init(temp_spthread, &temp_pcb_ptr, 2, pid_count++, "Test3");  
    // pcb_queue_push(&priority_queue_array[2], temp_pcb_ptr);      
    // k_register_pcb(temp_pcb_ptr);

    // print info for the 3 queues for debug ///////////////////
    // print_queue_info(&priority_queue_array[0]);
    // print_queue_info(&priority_queue_array[1]);
    // print_queue_info(&priority_queue_array[2]);   


    ///////////////////////////////////////////////////////////
    ///////////////////////////////////////////////////////////  

    pcb_t* self_pcb_ptr = k_get_self_pcb();
    self_pcb_ptr->status = THRD_BLOCKED; // modify self status to BLOCKED as it will block and wait for shell to join
    spthread_join(thrd_shell, NULL); // wait for shell thread to join
    // init thread block wait to for shell thread to join
    // ...
    // till shell user exit by ctrl-D       
    self_pcb_ptr->status = THRD_RUNNING; // modify self status to RUNNING as shell thread has joined


    // clean up
    // cancel and join all PCBs in pcb vector
    // starts with i = 2, because init at i = 0 and shell at i = 1
    for (int i = 2; i < pcb_vec_len(&all_unreaped_pcb_vector); i++) {
        pcb_t* curr_pcb_ptr = (&all_unreaped_pcb_vector)->pcb_ptr_array[i];
        if (thrd_status(curr_pcb_ptr) != THRD_REAPED) {
            cancel_and_join_pcb((&all_unreaped_pcb_vector)->pcb_ptr_array[i]);    
        }        
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
/**
 * Build a wrapper‐args object so routine_exit_wrapper_func can
 * call real_func(real_arg) then do cleanup.
 */
routine_exit_wrapper_args_t* wrap_routine_exit_args(void* (*real_func)(void*),
                                                    void* real_arg) {
  routine_exit_wrapper_args_t* args = malloc(sizeof(*args));
  if (!args) {
    panic("wrap_routine_exit_args: malloc failed\n");
  }
  args->real_func = real_func;
  args->real_arg = real_arg;
  return args;
}

/**
 * Entry point that wraps exit.  Calls the real function and then
 * (for example) invokes k_exit().
 */
void* routine_exit_wrapper_func(void* wrapper_args) {
  routine_exit_wrapper_args_t* args = wrapper_args;
  void* result = args->real_func(args->real_arg);
  free(args);
  // after real_func returns, clean up this thread
  k_exit();
  return result;
}

/**
 * A generic spawn wrapper: unpacks spawn_wrapper_arg, then
 * calls the real thread‐function.
 */
void* spawn_entry_wrapper_kernel(void* wrapper_args) {
  kernel_spawn_wrapper_arg* sw = (kernel_spawn_wrapper_arg*)wrapper_args;
  // call the real function
  void* retval = sw->real_func(sw->real_arg);
  // thread will clean itself up, or you can call k_exit() here
  return retval;
}

/**
 * Heuristic: true if s points to a valid zero‐terminated C string.
 */
bool looks_like_cstring(const char* s) {
  if (!s)
    return false;
  size_t i = 0;
  // scan up to some reasonable max (avoid endless loop)
  while (i < 1024 && s[i]) {
    i++;
  }
  return (i < 1024);
}

/**
 * Set the PCB’s human‐readable name (e.g. for a `ps` listing).
 */
void set_process_name(pcb_t* proc, const char* name) {
  proc->command = (char*)name;
}

/**
 * Log a lifecycle event with your klogger:
 * e.g. "[  123]    CREATED    42    1    process"
 */
void lifecycle_event_log(pcb_t* proc, const char* event, void* info) {
  extern volatile int cumulative_tick_global;
  klog("[%5d]\t%s\t%d\t%d\tprocess", cumulative_tick_global, event,
       thrd_pid(proc), thrd_priority(proc));
}


