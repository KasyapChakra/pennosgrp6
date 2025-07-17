/* ==================================================================
 * CIS_5480 Project 3:  PennOS
 * Author:              
 * Purpose:             Shell related functions
 * File Name:           shell.c
 * File Content:        Implementation of the shell related functions
 * =============================================================== */
#define _GNU_SOURCE

#include "./shell.h"
#include "../user/shell.h"
#include "builtins.h"
#include "../user/syscall_kernel.h"
#include "../kernel/kernel_fn.h"
#include "../util/parser.h"

#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <stddef.h>

/* forward decl for builtins defined in user/shell.c to avoid implicit decl */
extern void* zombify(void*);
extern void* orphanify(void*);


void write_prompt(char* prompt_input) {
  ssize_t num_bytes_write;
  num_bytes_write = write(STDERR_FILENO, prompt_input, strlen(prompt_input));
  if (num_bytes_write == -1) {
    perror("Failed to write to terminal");
    exit(EXIT_FAILURE);
  }
}

void handler_sigint_shell(int signum) {
  if (signum == SIGINT) {
    write_prompt("\n");    // add a newline after ^C printed on the terminal
    write_prompt(PROMPT);  // re-prompt
  }
}

void handler_sigtstp_shell(int signum) {
  if (signum == SIGTSTP) {
    write_prompt("\n");    // add a newline after ^Z printed on the terminal
    write_prompt(PROMPT);  // re-prompt
  }
}


void clear_input_buffer() {
  char input_char;
  while (read(STDIN_FILENO, &input_char, 1) > 0 && input_char != '\n') {
    // keep reading the input buffer
  }
}

ssize_t shell_read_cmd(char* cmd_string) {
  ssize_t num_bytes_read;

  // read user command from user input
  // max input length is (MAX_LINE_LENGTH - 1),
  // so '\0' can be added in the end (required by strtok())
  num_bytes_read = read(STDIN_FILENO, cmd_string, MAX_LINE_LENGTH - 1);
  if (num_bytes_read == -1) {
    perror("Failed to read user command");
    exit(EXIT_FAILURE);
  }

  // check if there is any non-empty (not space or tab or enter) input from user
  bool flag_non_empty_input = false;
  for (int i = 0; i < num_bytes_read; i++) {
    if ((cmd_string[i] != ' ') && (cmd_string[i] != '\t') &&
        (cmd_string[i] != '\n')) {
      flag_non_empty_input = true;
      break;
    }
  }

  // user input starts with ctrl-D ==> write a newline and exit
  if (num_bytes_read == 0) {
    if (write(STDERR_FILENO, "\n", 1) == -1) {
      perror("Failed to write a newline after user exits by ctrl-D");      
      exit(EXIT_FAILURE);
    }
    return -1;
  }

  // empty input ending with ctrl-D ==> write a newline and exit
  if (!flag_non_empty_input && (cmd_string[num_bytes_read - 1] != '\n') &&
      (num_bytes_read < MAX_LINE_LENGTH - 1)) {
    if (write(STDERR_FILENO, "\n", 1) == -1) {
      perror("Failed to write a newline after user exits by ctrl-D");      
      exit(EXIT_FAILURE);
    }
    return -1;
  }

  // empty input exceeding max length ==> write a newline and continue
  if (!flag_non_empty_input && (cmd_string[num_bytes_read - 1] != '\n') &&
      (num_bytes_read == MAX_LINE_LENGTH - 1)) {
    if (write(STDERR_FILENO, "\n", 1) == -1) {
      perror("Failed to write a newline after an empty line with max length");      
      exit(EXIT_FAILURE);
    }
  }

  // only three cases can arrive here:
  // (1) empty input ending with '\n'
  // (2) empty input exceeding max length
  // (3) non-empty line

  // input length reaches max length, clear the remaining input buffer
  if (num_bytes_read == MAX_LINE_LENGTH - 1) {
    clear_input_buffer();
  }

  // add null terminator '\0' at the end of the command string (required by
  // strtok())
  cmd_string[num_bytes_read] = '\0';

  // write a newline if user finishes command by ctrl-D
  if (cmd_string[num_bytes_read - 1] != '\n') {
    if (write(STDERR_FILENO, "\n", 1) == -1) {
      perror("Failed to write a newline after user finishes by ctrl-D");      
      exit(EXIT_FAILURE);
    }
  }

  // command string includes all the terminal input from user,
  // and ends with '\0'
  // num_bytes_read does not include the last null terminator '\0' added
  if (flag_non_empty_input) {
    return num_bytes_read;  // non-empty input ==> return "true" number of bytes
  }

  return 0;  // empty input (end with '\n') ==> return 0
}

void* thrd_shell_fn([[maybe_unused]] void* arg) {

    // install handler for SIGINT (ctrl-C) and SIGTSTP (ctrl-Z)
    sigset_t sig_set_shell;    
    sigfillset(&sig_set_shell);

    struct sigaction sigaction_st_sigint = (struct sigaction){
        .sa_handler = handler_sigint_shell,
        .sa_mask = sig_set_shell, 
        .sa_flags = SA_RESTART,
    };              
    sigaction(SIGINT, &sigaction_st_sigint, NULL);

    struct sigaction sigaction_st_sigtstp = (struct sigaction){
        .sa_handler = handler_sigtstp_shell,
        .sa_mask = sig_set_shell, 
        .sa_flags = SA_RESTART,
    };              
    sigaction(SIGTSTP, &sigaction_st_sigtstp, NULL);

    // block SIGALRM for shell thread 
    sigemptyset(&sig_set_shell);
    sigaddset(&sig_set_shell, SIGALRM);
    pthread_sigmask(SIG_BLOCK, &sig_set_shell, NULL); 

    // unblock SIGINT and SIGTSTP
    sigemptyset(&sig_set_shell);
    sigaddset(&sig_set_shell, SIGINT);
    sigaddset(&sig_set_shell, SIGTSTP);
    pthread_sigmask(SIG_UNBLOCK, &sig_set_shell, NULL);

    dprintf(STDERR_FILENO, "########### Shell thread started ###########\n");

    // // Call the main shell function from user/shell.c
    // shell_main(NULL);

    while (true) {
        write_prompt(PROMPT);

        char cmd_string[MAX_LINE_LENGTH];
        ssize_t num_bytes_read = shell_read_cmd(cmd_string);        

        // user press ctrl-D or empty line ending with ctrl-D ==> exit shell
        if (num_bytes_read == -1) {
            spthread_disable_interrupts_self();
            pennos_done = true;
            spthread_enable_interrupts_self();               
            break;
        }

        // if empty input from user (only ' ' or '\t' or '\n')
        // nothing to parse and execute, continue the while-loop
        if (num_bytes_read == 0) {          
            continue;
        }   

        // parse the command into the parsed_command structure
        struct parsed_command* pcmd_ptr = NULL;
        int parse_ret = parse_command(cmd_string, &pcmd_ptr);
        if (parse_ret != 0) {
          /* invalid command */
          dprintf(STDERR_FILENO, "ERR: invalid user command\n");
          free(pcmd_ptr);
          continue;
        }

        // support experimental commands. TODO eventually add them to the user/shell.c
        bool is_experimental_cmd = false;
        if (strcmp(pcmd_ptr->commands[0][0], "ps") == 0) {
            //s_spawn(ps_builtin, NULL, -1, -1);
            pid_t temp_pid = s_spawn(ps_builtin, NULL, -1, -1);
            s_waitpid(temp_pid, NULL, false);
            is_experimental_cmd = true;
        } else if (strcmp(pcmd_ptr->commands[0][0], "pcbvec") == 0) {
            print_pcb_vec_info(&all_unreaped_pcb_vector);
            is_experimental_cmd = true;
        } else if (strcmp(pcmd_ptr->commands[0][0], "ps1") == 0) {
            ps_print_pcb_vec_info(&all_unreaped_pcb_vector);
            is_experimental_cmd = true;
        }

        if (pcmd_ptr->num_commands == 0) {
          free(pcmd_ptr);
          continue;
        }
        
        // run the shell main function from user/shell.c
        // this function is modifed to run in this loop but take advantage of the already made command
        if (!is_experimental_cmd) { // TODO eventually remove this 
          if (shell_main(pcmd_ptr) == -1) {
            free(pcmd_ptr);
            pcmd_ptr = NULL;
            continue;
          }
        }

        // wait on any finished children before next prompt
        //while (s_waitpid(-1, NULL, true) > 0) {
        //    ;
        // }

        free(pcmd_ptr);  // free the parsed command structure
        pcmd_ptr = NULL;
        
    }// end of shell-loop

    dprintf(STDERR_FILENO, "~~~~~~~~~~ Shell thread exit ~~~~~~~~~~\n");

    spthread_exit(NULL);
    return NULL;   
}

