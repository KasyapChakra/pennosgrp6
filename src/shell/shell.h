/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Shell related functions
 * File Name:           shell.h
 * File Content:        Header file for the shell related functions
 * =============================================================== */

#ifndef SHELL_H_
#define SHELL_H_

#include <string.h>
#include <sys/types.h>


#ifndef PROMPT
#define PROMPT "$ "
#endif

#ifndef MAX_LINE_LENGTH
#define MAX_LINE_LENGTH 4096
#endif

// ============================ Functions ============================ //
void write_prompt(char* prompt_input);
void handler_sigint_shell(int signum);
void handler_sigtstp_shell(int signum);
void clear_input_buffer();
ssize_t shell_read_cmd(char* cmd_string);
void* thrd_shell_fn([[maybe_unused]] void* arg);

// Global variable for tracking foreground process
extern volatile pid_t current_fg_pid;


#endif  // SHELL_H_