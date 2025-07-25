#define _GNU_SOURCE

#include "shell.h"
#include <signal.h>
#include <pthread.h>
#include "../common/pennos_signal.h"
#include "../util/parser.h"
#include "../util/utils.h"
#include "jobs.h"
#include "../shell/shell.h"  // Include to access current_fg_pid
#include "syscall_kernel.h"  // Include to access s_sleep and other syscalls

#include "../common/pennfat_definitions.h"
#include "../internal/pennfat_kernel.h"

#include <ctype.h>  // isdigit (for sleep)
#include <errno.h>
#include <stdlib.h>  // NULL, atoi
#include <string.h>
#include <unistd.h>  // STDIN_FILENO / read

// Process status macros
#ifndef P_WIFEXITED
#define P_WIFEXITED(status) (((status) & 0x7f) == 0)
#endif
#ifndef P_WIFSTOPPED
#define P_WIFSTOPPED(status) (((status) & 0xff) == 0x7f)
#endif
#ifndef P_WIFSIGNALED
#define P_WIFSIGNALED(status) (((status) & 0x7f) != 0 && ((status) & 0x7f) != 0x7f)
#endif

// File system constants
#ifndef F_SEEK_SET
#define F_SEEK_SET 0
#endif
#ifndef F_SEEK_CUR
#define F_SEEK_CUR 1
#endif
#ifndef F_SEEK_END
#define F_SEEK_END 2
#endif

// Error handling
#ifndef P_ERRNO
extern int P_ERRNO;
#endif

// File system system calls
#ifndef s_open
int s_open(const char *fname, int mode);
int s_read(int fd, int n, char *buf);
int s_write(int fd, const char *str, int n);
int s_close(int fd);
int s_unlink(const char *fname);
int s_lseek(int fd, int offset, int whence);
int s_ls(const char *filename);
#endif

// Process system calls
#ifndef s_spawn
pid_t s_spawn(void* (*func)(void*), char *argv[], int fd0, int fd1);
pid_t s_waitpid(pid_t pid, int* wstatus, bool nohang);
int s_kill(pid_t pid, int signal);
void s_exit(void);
int s_nice(pid_t pid, int priority);
void s_sleep(unsigned int ticks);
void s_printprocess(void);
#endif

// Error handling function
#ifndef u_perror
void u_perror(const char *s);
#endif

/* ---------- forward-declarations for built-ins used below ---------- */
void* jobs_builtin(void*);
void* bg(void*);
void* fg(void*);
void* logout_cmd(void*);

void* touch(void* arg);
void* ls(void* arg);
void* cat(void* arg);
void* chmod(void* arg);
void* cp(void* arg);       /* NEW */
void* mv(void* arg);       /* NEW */
void* rm(void* arg);       /* NEW */
void* kill_cmd(void* arg); /* renamed to avoid clash          */

/*──────────────────────────────────────────────────────────────*/
/*  Dispatch tables                                             */
/*──────────────────────────────────────────────────────────────*/

typedef void* (*thd_func_t)(void*);

typedef struct cmd_func_match_t {
  const char* cmd;
  thd_func_t func;
} cmd_func_match_t;

/* ───────────────────────────────────────────────
 *  built-ins that MUST run inside the shell
 *  (can modify shell state, job control, etc.)
 * ───────────────────────────────────────────────*/
static cmd_func_match_t inline_funcs[] = {{"nice_pid", u_nice_pid},
                                           {"jobs", jobs_builtin},
                                           {"bg", bg},
                                           {"fg", fg},
                                           {"logout", logout_cmd},
                                           {"man", man},
                                           {NULL, NULL}};

/* ───────────────────────────────────────────────
 *  built-ins that MUST run in a *separate* PennOS
 *  process (section 3.1 first table)
 * ───────────────────────────────────────────────*/
static cmd_func_match_t independent_funcs[] = {
    {"ps", ps},
    {"echo", echo},
    {"sleep", u_sleep}, /* user types "sleep 10"          */
    {"touch", touch},   /* NEW */
    {"ls", ls},         /* NEW */
    {"cat", cat},       /* NEW */
    {"chmod", chmod},
    {"zombify", zombify},
    {"orphanify", orphanify},
    {"busy", busy},
    {"kill", kill_cmd}, /* renamed                        */
    {"nice", u_nice},   /* moved from inline_funcs        */
    {"cp", cp},
    {"mv", mv},
    {"rm", rm},
    {NULL, NULL}};

/*──────────────────────────────────────────────────────────────*/
/*          MAIN PROGRAM (existing content remains)             */
/*──────────────────────────────────────────────────────────────*/

#define PROMPT "$ "

static bool exit_shell = false;
static pid_t shell_pgid; /* for signal-forwarding */

static pid_t process_one_command(char*** cmdv,
                                 size_t stages,
                                 const char* stdin_file,
                                 const char* stdout_file,
                                 const char* stderr_file,
                                 bool append_out);

static thd_func_t get_func_from_cmd(const char* cmd_name,
                                    cmd_func_match_t* table);
static int get_argc(char** argv);
static bool str_to_int(const char* str, int* ret_val);

#ifdef DEBUG
static void debug_print_argv(char** argv);
static void debug_print_parsed_command(struct parsed_command*);
#endif

static int open_for_read(const char* path) {
  return s_open(path, K_O_RDONLY);
}
static int open_for_write(const char* path, bool append) {
  int flags = K_O_CREATE | (append ? K_O_APPEND : K_O_WRONLY);
  return s_open(path, flags);
}

static pid_t spawn_stage(char** argv, int fd_in, int fd_out);
// static int open_redirect(int* fd, const char* path, int flags);

/*──────────────────────────────────────────────────────────────*/
/*                NEW  BUILT-IN  IMPLEMENTATIONS                */
/*──────────────────────────────────────────────────────────────*/

/* ---------- touch ---------- */
void* touch(void* arg) {
  char** argv = (char**)arg;
  if (!argv || !argv[1]) {
    fprintf(stderr, "touch: missing operand\n");
    return NULL;
  }
  for (int i = 1; argv[i]; ++i) {
    PennFatErr err = s_touch(argv[i]);
    if (err)
      fprintf(stderr, "touch: %s: %s\n", argv[i], PennFatErr_toErrString(err));
  }
  return NULL;
}

/* ---------- ls (no arguments, PennFAT root only) ---------- */
void* ls(void* arg) {
  (void)arg; /* unused */
  PennFatErr err = s_ls(NULL);
  if (err)
    fprintf(stderr, "ls: %s\n", PennFatErr_toErrString(err));
  return NULL;
}

/* ---------- chmod ---------- */
/**
 * Translate a symbolic permission string to a bit-mask.
 * Accepts:  "rwx", "+rw", "-x", etc.  The leading ‘+’/‘-’ is ignored – we
 * presently treat the request as an *absolute* mask because the underlying
 * s_chmod() overwrites the whole permission set.
 * Returns 0xFF on syntax error.
 */
static uint8_t parse_perm_string(const char* s) {
  if (!s)
    return 0xFF;
  if (*s == '+' || *s == '-') /* optional sign */
    ++s;

  uint8_t m = 0;
  for (; *s; ++s) {
    if (*s == 'r')
      m |= PERM_READ;
    else if (*s == 'w')
      m |= PERM_WRITE;
    else if (*s == 'x')
      m |= PERM_EXEC;
    else
      return 0xFF; /* illegal character */
  }
  return (m == 0) ? 0xFF : m;
}

void* chmod(void* arg) {
  char** argv = (char**)arg;
  if (!argv || !argv[1] || !argv[2]) {
    fprintf(stderr, "chmod: usage: chmod PERMS FILE …\n");
    return NULL;
  }

  /* parse the +rwx / -wx … string **once** */
  uint8_t perm = parse_perm_string(argv[1]);
  if (perm == 0xFF) {
    fprintf(stderr, "chmod: invalid permission string '%s'\n", argv[1]);
    return NULL;
  }

  /* try to apply it to every remaining operand */
  for (int i = 2; argv[i]; ++i) {
    PennFatErr err = s_chmod(argv[i], perm);
    if (err)
      fprintf(stderr, "chmod: %s: %s\n", argv[i], PennFatErr_toErrString(err));
  }
  return NULL;
}

/* ---------- cat (read files from PennFAT, print to stdout) ---------- */
#define CAT_BUFSZ 4096
void* cat(void* arg) {
  char** argv = (char**)arg;
  /* No file names → echo STDIN until EOF --------------------------- */
  if (!argv || !argv[1]) {
    char buf[CAT_BUFSZ];
    while (1) {
      PennFatErr n = s_read(STDIN_FILENO, CAT_BUFSZ, buf);
      if (n <= 0)
        break;
      fwrite(buf, 1, n, stdout);
    }
    return NULL;
  }
  char buf[CAT_BUFSZ];

  for (int i = 1; argv[i]; ++i) {
    int fd = s_open(argv[i], K_O_RDONLY);
    if (fd < 0) {
      fprintf(stderr, "cat: %s: %s\n", argv[i], PennFatErr_toErrString(fd));
      continue;
    }
    while (1) {
      PennFatErr r = s_read(fd, CAT_BUFSZ, buf);
      if (r < 0) {
        fprintf(stderr, "cat: read error\n");
        break;
      }
      if (r == 0)
        break;
      fwrite(buf, 1, r, stdout);
    }
    s_close(fd);
  }
  return NULL;
}

/*----------- echo -----------------------------------------------------------*/
void* echo(void* arg) {
  char** argv = (char**)arg; /* argv[0] == "echo"              */
  if (!argv)
    return NULL;

  for (int i = 1; argv[i]; ++i) {
    fputs(argv[i], stdout);
    if (argv[i + 1])
      fputc(' ', stdout);
  }
  fputc('\n', stdout);
  return NULL;
}

/*----------- u_sleep   (shell command:  sleep N seconds) --------------------*/
void* u_sleep(void* arg) {
  char** argv = (char**)arg;
  if (!argv || !argv[1]) {
    fprintf(stderr, "sleep: missing <seconds>\n");
    return NULL;
  }

  /* ensure numeric */
  for (const char* p = argv[1]; *p; ++p) {
    if (!isdigit((unsigned char)*p)) {
      fprintf(stderr, "sleep: '%s' is not a positive integer\n", argv[1]);
      return NULL;
    }
  }

  int secs = atoi(argv[1]);
  if (secs <= 0)
    return NULL;

  /* 1 clock-tick = 0.1 s (see CLOCK_TICK_IN_USEC in process_control.c) */
  clock_tick_t ticks = (clock_tick_t)(secs * 10);
  s_sleep(ticks);
  return NULL;
}

/*----------- man  (static help text) ----------------------------------------*/
static const char* help_text =
    "Built-in commands:\n"
    "  echo TEXT …            – print TEXT to stdout\n"
    "  sleep N                – suspend shell for N seconds\n"
    "  ps                     – list processes\n"
    "  kill [-stop|-cont] PID – send signal to PID\n"
    "  nice PRIORITY cmd …    – spawn cmd with priority\n"
    "  nice_pid PRIORITY PID  – change priority of existing PID\n"
    "  man                    – this help text\n";

void* man(void* arg) {
  (void)arg;
  fputs(help_text, stdout);
  return NULL;
}

// static void forward(int signo)
// {
//     job_t *fg = jobs_current_fg();
//     if (fg)
//         s_kill(fg->pid, (signo == SIGINT) ? P_SIGTERM : P_SIGSTOP);  /* fixed
//         */
// }

/*======================================================================*/
/*  Data-moving built-ins: cp / mv / rm                                 */
/*======================================================================*/

/* cp  SRC DST  — copy a file inside PennFAT (no host –h support yet) */
void* cp(void* arg) {
  char** argv = (char**)arg;
  if (!argv || !argv[1] || !argv[2]) {
    fprintf(stderr, "cp: usage: cp SRC DST\n");
    return NULL;
  }

  const char* src = argv[1];
  const char* dst = argv[2];

  int src_fd = s_open(src, K_O_RDONLY);
  if (src_fd < 0) {
    fprintf(stderr, "cp: cannot open %s\n", src);
    return NULL;
  }
  int dst_fd = s_open(dst, K_O_CREATE | K_O_WRONLY);
  if (dst_fd < 0) {
    fprintf(stderr, "cp: cannot create %s\n", dst);
    s_close(src_fd);
    return NULL;
  }

  char buf[4096];
  while (1) {
    PennFatErr n = s_read(src_fd, sizeof buf, buf);
    if (n < 0) {
      fprintf(stderr, "cp: read error\n");
      break;
    }
    if (n == 0)
      break; /* EOF */
    if (s_write(dst_fd, buf, n) != n) {
      fprintf(stderr, "cp: write error\n");
      break;
    }
  }

  s_close(src_fd);
  s_close(dst_fd);
  return NULL;
}

/* mv  SRC DST  — rename inside PennFAT */
/* mv SOURCE DEST — rename one file to another */
void* mv(void* arg) {
  char** argv = (char**)arg;
  if (!argv || !argv[1] || !argv[2]) {
    fprintf(stderr, "mv: usage: mv SOURCE DEST\n");
    return NULL;
  }

  PennFatErr err = s_rename(argv[1], argv[2]);
  if (err != PennFatErr_SUCCESS) {
    fprintf(stderr,
            "Error renaming %s to %s: %s\n",
            argv[1], argv[2],
            PennFatErr_toErrString(err));
  }
  return NULL;
}


/* rm  FILE…  — delete one or more files */
void* rm(void* arg) {
  char** argv = (char**)arg;
  if (!argv || !argv[1]) {
    fprintf(stderr, "rm: usage: rm FILE...\n");
    return NULL;
  }

  for (int i = 1; argv[i]; ++i) {
  PennFatErr err = s_unlink(argv[i]);
  if (err != PennFatErr_SUCCESS) {
    fprintf(stderr, "Error removing %s: %s\n",
            argv[i], PennFatErr_toErrString(err));
  }
}

  return NULL;
}

/*──────────────────────────────────────────────────────────────*/
/*      The remainder of shell.c is unchanged (↓ existing)      */
/*──────────────────────────────────────────────────────────────*/

int shell_main(struct parsed_command* cmd) {

  /* ─────────────────────────────────────────────────────────────
    *  STEP ➊ : built-ins that must run *inside* the shell (nice,
    *           bg, fg, jobs, logout, man, …).
    *           If the first word of the command matches one of
    *           those, execute it synchronously and go back to the
    *           prompt – no pipes, no redirections, no child proc.     *
    * ────────────────────────────────────────────────────────────*/
  char** argv0 = cmd->commands[0]; /* words of 1st stage */
  
  thd_func_t inl = get_func_from_cmd(argv0[0], inline_funcs);
  if (inl) {    /* found a shell-local built-in */
    inl(argv0); /* run it right here            */
    return 0;   /* prompt user again            */
  }

  /* ────────────────────────────────────────────────────────────
    *  From here on we know the command is *not* shell-local, so
    *  we treat it like an external pipeline: handle < > >> and
    *  possibly create one or many child processes.
    * ────────────────────────────────────────────────────────────*/

  /*----------------------------------------------*
    *  Handle <  >  >>  from the parsed structure  *
    *----------------------------------------------*/
  int redir_in = STDIN_FILENO; /* default: inherit from shell */
  int redir_out = STDOUT_FILENO;
  bool close_in = false; /* whether we must k_close later */
  bool close_out = false;

  /* open input redirection, if any */
  if (cmd->stdin_file) {
    int fd = open_for_read(cmd->stdin_file);
    if (fd < 0) {
      dprintf(STDERR_FILENO, "shell: cannot open %s for reading\n", cmd->stdin_file);
      goto AFTER_LOOP_CLEANUP;
    }
    redir_in = fd;
    close_in = true;
  }

  /* open output redirection, if any */
  if (cmd->stdout_file) {
    int fd = open_for_write(cmd->stdout_file, cmd->is_file_append);
    if (fd < 0) {
      dprintf(STDERR_FILENO, "shell: cannot open %s (%s)\n", cmd->stdout_file,
              PennFatErr_toErrString(fd));
      if (close_in)
        s_close(redir_in);
      goto AFTER_LOOP_CLEANUP;
    }
    redir_out = fd;
    close_out = true;
  }

  pid_t child_pid = process_one_command(
      cmd->commands, cmd->num_commands, cmd->stdin_file, cmd->stdout_file,
      /* stderr */ NULL, cmd->is_file_append);

  if (child_pid <= 0)
    return -1;

  if (!cmd->is_background) { /* foreground job           */
    // Wait for foreground process to complete
    current_fg_pid = child_pid; // Track foreground process for Ctrl+C
    // s_tcsetpid(child_pid);  
    int status;
    pid_t wait_result = s_waitpid(child_pid, &status, false); // Enable waitpid for foreground processes
    current_fg_pid = -1; // Reset after process completes
    
    // Check if the process was terminated by signal
    if (wait_result > 0) {
      // Process was successfully reaped
      // No additional handling needed for normal termination or signal termination
    } else {
      // waitpid failed - this shouldn't normally happen for valid child processes
      dprintf(STDERR_FILENO, "shell: waitpid failed for PID %d\n", child_pid);
    }
    // s_tcsetpid(shell_pgid);
  } else {
    /* Background job - add to jobs table and don't wait */
    // TODO: Add to jobs table when jobs system is fully implemented
    // For now, just let it run in background without waiting
  }

  if (close_in)
    s_close(redir_in);
  if (close_out)
    s_close(redir_out);

  AFTER_LOOP_CLEANUP:; /* empty statement so the label isn’t alone */

  return 0; /* prompt user again */
}



/* Existing built-ins (busy / kill / ps / testing helpers) remain unchanged */
/* … (the rest of the original file’s content is intentionally left intact) */

/******************************************
 *     INDEPENDENT BUILT-INS              *
 ******************************************/

void* ps(void* arg) {
  s_printprocess();
  return NULL;
}

void* busy(void* arg) {
  // Simple but CPU-intensive loop that allows scheduler preemption
  volatile unsigned long counter = 0;
  volatile unsigned long result = 1;
  
  while (true) {
    // Do CPU-intensive mathematical operations
    for (int i = 0; i < 50000; i++) {
      counter++;
      result *= counter;
      result ^= counter;
      result += counter * counter;
      
      // Add some memory operations
      if (counter % 1000 == 0) {
        volatile int temp_data[50];
        for (int j = 0; j < 50; j++) {
          temp_data[j] = counter + j;
          result += temp_data[j];
        }
      }
    }
    
    // Reset to prevent overflow and keep working
    if (counter > 1000000) {
      counter = 0;
      result = 1;
    }
  }
  
  return NULL;
}

void* kill_cmd(void* arg) { /* ➋ renamed implementation   */
  // TODO: not completely finished

  if (!arg) {
    return NULL;
  }

  char** argv = (char**)arg;

  // VALIDATE ARGUMENTS
  if (!argv || argv[0] == NULL) {
    fprintf(stderr, "Error: Invalid arg.\n");
    return NULL;
  }

  int argc = get_argc(argv);
  // need at least 2 arguments
  if (argc < 2) {
    fprintf(stderr, "%s Error: Incorrect number of args.\n", argv[0]);
    return NULL;
  }

  int pid_start_index = 1;
  int signal = P_SIGTERM;
  if (argv[1] && argv[1][0] == '-') {
    ++pid_start_index;

    if (strcmp(argv[1], "-cont") == 0) {
      signal = P_SIGCONT;
    } else if (strcmp(argv[1], "-stop") == 0) {
      signal = P_SIGSTOP;
    } else if (strcmp(argv[1], "-term") == 0) {
      signal = P_SIGTERM;
    } else {
      fprintf(stderr, "%s Error: Invalid arg: %s.\n", argv[0], argv[1]);
      return NULL;
    }
  }

  // TODO: error checking and multiple processes

  int pid;
  if (!str_to_int(argv[pid_start_index], &pid) || pid <= 0) {
    fprintf(
        stderr,
        "%s Error: Invalid arg: %s. PID number should be a positive integer.\n",
        argv[0], argv[pid_start_index]);
    return NULL;
  }

  if (s_kill(pid, signal) == 0) {
    fprintf(stderr, "Signal <%d> sent to PID [%d].\n", signal, pid);
  } else {
    // TODO: errno checking, more verbose error explanation
    fprintf(stderr, "Error sending signal to PID [%d].\n", pid);
  }

  return NULL;
}

// TODO: add other command functions

/******************************************
 *            SUB-ROUTINES                *
 ******************************************/

// TODO: add other command functions

void* u_nice_pid(void* arg) {
  char** argv = (char**)arg;

  // VALIDATE ARGUMENTS
  if (!argv || argv[0] == NULL) {
    fprintf(stderr, "Error: Invalid arg.\n");
    return NULL;
  }

  int argc = get_argc(argv);
  // need exactly 3 arguments
  if (argc != 3) {
    fprintf(stderr, "%s Error: Incorrect number of args.\n", argv[0]);
    return NULL;
  }

  int priority, pid;
  if (!str_to_int(argv[2], &pid) || pid <= 0) {
    fprintf(
        stderr,
        "%s Error: Invalid args. PID number should be a positive integer.\n",
        argv[0]);
    return NULL;
  }

  if (!str_to_int(argv[1], &priority) || priority < 0 || priority >= 3) {
    fprintf(stderr,
            "%s Error: Invalid args. Priority should be an integer between 0 "
            "and 2.\n",
            argv[0]);
    return NULL;
  }

  // SYSTEM CALL TO UPDATE NICE VALUE
  if (s_nice(pid, priority) == 0) {
    fprintf(stderr, "Successfully set PID[%d] to priority %d.\n", pid,
            priority);
  } else {
    // TODO: more verbose response with errno checking
    fprintf(stderr, "%s failed\n", argv[0]);
  }

  return NULL;
}

void* u_nice(void* arg) {
  char** argv = (char**)arg;
  if (!argv || argv[0] == NULL) {
    fprintf(stderr, "Error: Invalid arg.\n");
    return NULL;
  }

  // VALIDATE ARGUMENTS
  int argc = get_argc(argv);
  // need at least 3 arguments
  if (argc < 3) {
    fprintf(stderr, "%s Error: Incorrect number of args.\n", argv[0]);
    return NULL;
  }

  int priority;
  if (!str_to_int(argv[1], &priority) || priority < 0 || priority >= 3) {
    fprintf(stderr,
            "%s Error: Invalid args. Priority should be an integer between 0 "
            "and 2.\n",
            argv[0]);
    return NULL;
  }

  thd_func_t func = get_func_from_cmd(argv[2], independent_funcs);
  if (func != NULL) {
    // FOUND INDEPENDENT FUNC COMMAND
    // spawn new process to run the command
    pid_t child_pid = s_spawn(func, argv + 2, 0, 1);
    if (child_pid < 0) {
      // spawn failed somehow
      fprintf(stderr, "%s Failed to spawn process for command: %s\n", argv[0],
              argv[2]);
      return NULL;
    } 
    
    if (s_nice(child_pid, priority) == 0) {  // SYSTEM CALL TO UPDATE NICE VALUE
      // update nice value successful
      fprintf(stderr, "Command run as PID[%d] and set to priority %d: %s\n",
              child_pid, priority, argv[2]);
    } else {
      // TODO: more verbose response with errno checking
      fprintf(stderr, "Command run as PID[%d] but set priority failed: %s\n",
              child_pid, argv[2]);
    }
    
    // Don't wait for the process - let it run independently
    return NULL;
  } else {
    fprintf(stderr, "Invalid command: %s\n", argv[2]);
    return NULL;
  }
}

/******************************************
 *            TEST HELPERS                *
 ******************************************/

void* zombie_child(void* arg) {
  // do nothing and exit right away intentionally
  return NULL;
}

void* zombify(void* arg) {
  char* args[] = {"zombie_child", NULL};
  s_spawn(zombie_child, args, 0, 1);
  
  // Make the infinite loop more responsive to cancellation
  volatile long counter = 0;
  while (1) {
    counter++;
    // Every 100000 iterations, check for cancellation
    if (counter % 100000 == 0) {
      pthread_testcancel();  // This is a cancellation point
      counter = 0;  // Reset to prevent overflow
    }
  }
  return NULL;
}

void* orphan_child(void* arg) {
  // spinning intentionally
  while (1)
    ;
  return NULL;
}

void* orphan_child_autodie(void* arg) {
  s_sleep(20);
  return NULL;
}

void* orphanify(void* arg) {
  char* args[] = {"orphan_child", NULL};
  s_spawn(orphan_child, args, 0, 1);
  char* args2[] = {"orphan_child_autodie", NULL};
  s_spawn(orphan_child_autodie, args2, 0, 1);
  return NULL;
}

/* open <file> for shell redirection, return FD or negative error */
/* — already defined once above — */

/******************************************
 *       internal help functions          *
 ******************************************/

/*  NEW built-ins: jobs / bg / fg / logout – step 7               */
void* jobs_builtin(void* arg) {
  jobs_list();
  return NULL;
}

void* bg(void* arg) {
  char** argv = (char**)arg;
  int jid = argv[1] ? atoi(argv[1] + 1) : -1; /* %N form          */
  job_t* j = (jid > 0) ? jobs_by_jid(jid) : NULL;
  if (!j) {
    fprintf(stderr, "bg: job not found\n");
    return NULL;
  }
  s_kill(j->pid, P_SIGCONT);
  j->state = JOB_RUNNING;
  fprintf(stderr, "[%d] %s &\n", j->jid, j->cmdline);
  return NULL;
}

void* fg(void* arg) {
  char** argv = (char**)arg;
  int jid = argv[1] ? atoi(argv[1] + 1) : -1;
  job_t* j = (jid > 0) ? jobs_by_jid(jid) : jobs_current_fg();
  if (!j) {
    fprintf(stderr, "fg: job not found\n");
    return NULL;
  }

  s_kill(j->pid, P_SIGCONT);
  j->state = JOB_RUNNING;
  s_tcsetpid(j->pid);
  s_waitpid(j->pid, NULL, true);
  s_tcsetpid(shell_pgid);
  jobs_remove(j->pid);
  return NULL;
}

void* logout_cmd(void* arg) {
  if (jobs_have_stopped()) {
    fprintf(stderr, "logout: there are stopped jobs\n");
    return NULL;
  }
  exit_shell = true;
  return NULL;
}

/*------------------------------------------------------------------*/
/*  piping helpers / process_one_command – unchanged                */
/*------------------------------------------------------------------*/

static int open_redirect(int* fd, const char* path, int flags) {
  int newfd = s_open(path, flags);
  if (newfd < 0) {
    fprintf(stderr, "shell: cannot open %s\n", path);
    return -1;
  }
  *fd = newfd;
  return 0;
}

static pid_t spawn_stage(char** argv, int fd_in, int fd_out) {
  if (!argv || !argv[0])
    return -1;

  thd_func_t func = get_func_from_cmd(argv[0], independent_funcs);
  if (!func) {
    fprintf(stderr, "command not found: %s\n", argv[0]);
    return -1;
  }
  return s_spawn(func, argv, fd_in, fd_out);
}

static pid_t process_one_command(char** cmdv[],
                                 size_t stages,
                                 const char* stdin_file,
                                 const char* stdout_file,
                                 const char* stderr_file,
                                 bool append_out) {
  /* … body unchanged … */
  int prev_rd = STDIN_FILENO;
  int first_pid = -1;

  /* optional <  redirection for very first stage */
  if (stdin_file && open_redirect(&prev_rd, stdin_file, K_O_RDONLY) < 0)
    return -1;

  for (size_t s = 0; s < stages; ++s) {
    int pipefds[2] = {-1, -1};
    int this_out = STDOUT_FILENO;

    /* if NOT last stage, create a pipe */
    if (s + 1 < stages) {
      if (s_pipe(pipefds) < 0) {
        perror("pipe");
        return -1;
      }
      this_out = pipefds[1]; /* writer for this stage        */
    } else {
      /* last stage may have > or >>   */
      if (stdout_file) {
        int flags = K_O_CREATE | (append_out ? K_O_APPEND : K_O_WRONLY);
        if (open_redirect(&this_out, stdout_file, flags) < 0)
          return -1;
      }
    }

    /* stderr redirection only applies to *last* stage (bash semantics) */
    if (s + 1 == stages && stderr_file) {
      int fd;
      if (open_redirect(&fd, stderr_file, K_O_CREATE | K_O_WRONLY) < 0)
        return -1;
      /* (child duplicates fd on FD 2 inside spawn)                   */
    }

    pid_t pid = spawn_stage(cmdv[s], prev_rd, this_out);
    if (pid < 0)
      return -1;
    if (first_pid == -1)
      first_pid = pid;

    /* parent closes ends it no longer needs */
    if (prev_rd != STDIN_FILENO)
      s_close(prev_rd);
    if (this_out != STDOUT_FILENO)
      s_close(this_out);

    prev_rd = pipefds[0]; /* read end for next iteration (or dangling) */
  }
  return first_pid;
}

static thd_func_t get_func_from_cmd(const char* cmd_name,
                                    cmd_func_match_t* func_match) {
  for (size_t i = 0; func_match[i].cmd != NULL; ++i) {
    if (strcmp(func_match[i].cmd, cmd_name) == 0) {
      return func_match[i].func;
    }
  }
  return NULL;
}

static int get_argc(char** argv) {
  int i = 0;
  while (argv[i] != NULL) {
    ++i;
  }
  return i;
}

/**
 * Helper function to convert string to int, and return whether the conversion
 * is successful
 */
static bool str_to_int(const char* str, int* ret_val) {
  if (!str) {
    return false;
  }

  char* end;
  int res = strtol(str, &end, 10);

  if (*end != '\0') {
    return false;
  }

  if (ret_val) {
    *ret_val = res;
  }

  return true;
}

#ifdef DEBUG
static void debug_print_argv(char** argv) {
  if (!argv) {
    return;
  }

  for (size_t i = 0; argv[i] != NULL; ++i) {
    fprintf(stderr, "argv[%zu]: %s\n", i, argv[i]);
  }
}

static void debug_print_parsed_command(struct parsed_command* cmd) {
  if (!cmd) {
    return;
  }

  size_t command_num = cmd->num_commands;

  for (size_t c = 0; c < command_num; ++c) {
    fprintf(stderr, "command %zu:\n", c);
    debug_print_argv(cmd->commands[c]);
  }
}
#endif /* DEBUG */
