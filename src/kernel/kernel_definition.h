/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Define kernel definition
 * File Name:           kernel_definiton.h
 * File Content:        Header file for kernel definition
 * =============================================================== */

#ifndef KERNEL_DEFINITION_H_
#define KERNEL_DEFINITION_H_

#include <stdint.h>
#include <sys/types.h>

typedef unsigned int clock_tick_t;

typedef enum {
    P_SIGSTOP = 1,
    P_SIGCONT = 2, 
    P_SIGTERM = 3 
} k_signal_t;

// ------------ for waitpid() ------------ //
typedef enum {
    P_NOERROR = 0,
    P_EINTR = 1, // waitpid() was interrupted, safe to retry    
    P_ECHILD = 2 // there are no child processes with specified pid or pgid
} k_errno_t;


// exit normally
#define P_WIFEXITED(wstatus)    (((wstatus) & 0x7F) == 0) // returns true if exited normally (bit 0-6 all ZEROs)
#define P_WEXITSTATUS(wstatus)  (((wstatus) >> 8) & 0xFF) // returns exit code for a thread exited normally (bit 8-15)
// terminated by signal
#define P_WIFSIGNALED(wstatus)  (((wstatus) & 0x7F) != 0 && ((wstatus) & 0x7F) != 0x7F) // returns true if terminated by a signal (bit 0-6 neither all ZEROs nor all ONEs)
#define P_WTERMSIG(wstatus)     ((wstatus) & 0x7F) // returns signal number that caused the termination (bit 0-6)
// stopped
#define P_WIFSTOPPED(wstatus)   (((wstatus) & 0xFF) == 0x7F) // returns true if the thread was stopped (bit 0-6 all ONEs)
#define P_WSTOPSIG(wstatus)     (((wstatus) >> 8) & 0xFF) // returns the signal number that causes the stop (bit 8-15)
// continued
#define P_WIFCONTINUED(wstatus) ((wstatus) == 0xFFFF) // returns true if the thread was continued (bit 0-15 all ONEs)

// --------------------------------------- //




#endif  // KERNEL_DEFINITION_H_