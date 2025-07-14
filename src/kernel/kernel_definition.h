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

// ------------ for waitpid() ------------ //
typedef enum {
    K_NOERROR = 0,
    K_EINTR = 1, // waitpid() was interrupted, safe to retry    
    K_ECHILD = 2 // there are no child processes with specified pid or pgid
} k_errno_t;

#define STATUS_EXITED     0x00
#define STATUS_SIGNALED   0x01
#define STATUS_CONTINUED  0x02
#define STATUS_STOPPED    0x7f

#define K_WIFEXITED(wstatus)   (((wstatus) & 0xff) == STATUS_EXITED)
#define K_WIFSIGNALED(wstatus) (((wstatus) & 0xff) == STATUS_SIGNALED)
#define K_WIFCONTINUED(wstatus) (((wstatus) & 0xff) == STATUS_CONTINUED)
#define K_WIFSTOPPED(wstatus)  (((wstatus) & 0xff) == STATUS_STOPPED)

// --------------------------------------- //


#endif  // KERNEL_DEFINITION_H_