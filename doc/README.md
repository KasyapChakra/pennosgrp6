# 25su-cis5480-pennos-6
# PennOS – CIS 5480 Project 6

## Author  
Name: Yuuna Wang, Sean Zhang, Jake Donnini,
Tunga Bayrak, Kasyap Chakravadhanula
PennKey: yuunaw

## Submitted Files
- **Makefile**
- **.gitignore**  
- **bin/** (binaries directory include: `pennos`, `pennfat`, `.gitkeep`)  
- **doc/** (documentation directlory)
  - 'README.md'(this file)
- **log/** (runtime logs directory)  
- **tests/**(directlory)   
  - `sched-demo.c` (scheduler demo)  
  - `test.c` (unit tests)  
- **src/**(directlory)  
  - **common/**  
    - `pennos_types.h`
    - `pennos_signal.h`  
    - `pennfat_definitions.h` 
    - `pennfat_errors.h`  
  - **internal/**  
    - `pennfat_kernel.c`, `pennfat_kernel.h`  
  - **kernel/**  
    - `kernel_definition.h`  
    - `kernel_fn.c`, `kernel_fn.h`  
    - `kernel_syscall.c`  
    - `klogger.c`, `klogger.h`  
    - `PCB.c`, `PCB.h`  
    - `pcb_queue.c`, `pcb_queue.h`  
    - `pcb_vec.c`, `pcb_vec.h`  
    - `scheduler.c`, `scheduler.h`  
    - `spthread.c`, `spthread.h`  
  - **shell/**  
    - `shell.c`, `shell.h`
    (for testing purpose not main shell)  
    - `builtins.c`, `builtins.h`  
  - **user/**  
    - `syscall_kernel.c`, `syscall_kernel.h`  
    - `jobs.c`, `jobs.h`  
    - `shell.c`, `shell.h` (main shell.c)  
  - **util/**  
    - `parser.c`, `parser.h`  
    - `Vec.c`, `Vec.h`  
    - `panic.c`, `panic.h`  
    - `logger.c`, `logger.h`  
    - `utils.c`, `utils.h`
  - **pennos.c**  
  - **pennfat.c**  

## Overview  
PennOS is a user-level simulation of a UNIX-like operating system, all running in a single host process. It unifies three subsystems—(1) a preemptive priority scheduler over “spthreads,”(PENNOS) (2) a standalone FAT‐style file system (PennFAT), and (3) an interactive shell—into one coherent framework. PennOS exposes a thin “system‐call” API that models process creation, waiting, signaling, file I/O, and more. Detailed logging, signal handling, job control, pipelines, and I/O redirection mirror the behaviors of a real OS.

1. **Kernel** (`pennos`)  
   – A round-robin scheduler, process control blocks (PCBs), user-level threads, and a trap-based syscall interface.  
2. **File System** (`pennfat`)  
   – A custom FAT-style on-disk format withmkfs, mount, unmount, file I/O, directory listings, and permission bits.  
3. **Shell & User Utilities**  
   – An interactive shell with job control, pipelines, I/O redirection, built-in commands, and user-level wrappers for syscalls.  

All components build together under a single Makefile, producing the `pennos` kernel, the `pennfat` CLI, and test binaries.

## Description
### 1. Kernel (`pennos.c` & `src/kernel/`)  
- **PCB Management** (`PCB.c/h`, `pcb_queue.*`, `pcb_vec.*`)  
  – Allocates & tracks process control blocks.  
- **Scheduler** (`scheduler.*`)  
  – Implements round-robin scheduling and context switching.  
- **System Calls** (`kernel_syscall.c`, `kernel_fn.*`)  
  – Dispatches traps for `spawn`, `exit`, `sleep`, I/O, file system operations, and signals.  
- **Threads** (`spthread.*`)  
  – Provides lightweight user threads within a single address space.  
- **Logging** (`klogger.*`)  
  – Kernel‐mode logging for debugging and error reporting.

### 2. File System (`pennfat.c` & `src/internal/`, `src/common/`)  
**PennFAT Kernel Module (src/internal/pennfat_kernel.c)**
The core of PENNFAT file system exports both low-level block operations and the high-level file APIs (k_open, k_close, k_read, k_write, k_mkfs, k_mount, k_unmount, etc.) used by both the shell’s built-ins (ls, touch, rm, etc.) and the PennFAT CLI.
- On-disk layout: a superblock in FAT[0], followed by FAT blocks, then data blocks.
- Block I/O: read_block()/write_block() use lseek() + read()/write() plus fdatasync() for durability.
- FAT management: allocates/free chains, traverses file data via locate_block_in_chain().
- Directory handling: reads/writes dir_entry_t in fixed-size root directory blocks, handles creation, deletion, and lookup.
- System file table & FD table: global arrays for open files, ref-counting, and flushing metadata on close. 
**pennfat.c - file system CLI Main Function**
pennfat.c bypasses the shell and calls the PennFAT API (k_open, k_read, k_write, etc.) directly in pennfat_kernel.c.
- User program for PennFAT operations: mkfs, mount, unmount, ls, touch, mv, rm, chmod, cat, and cp.
- Parses simple one-command inputs, calls into the kernel API (the k_* functions) exposed by pennfat_kernel.

### 3.Shell (`src/user/shell`)
uses wrappers (in syscall_kernel.c) to invoke kernel syscalls implemented in kernel_syscall.c, which dispatch to the scheduler or file system as appropriate.
- Interactive loop: reads commands via read_command(), parses them, and dispatches to either shell-local built-ins (nice, jobs, bg, fg, logout) or spawns helper processes.
- Pipelines & Redirection: uses pipe(), fork(), and dup2() to wire up multi-stage commands.
- Job control: tracks background jobs in a dynamic vector (Vec.c/.h), handles SIGCHLD, and restores terminal control via tcsetpgrp().

## Comments
- Current Fat implementation failing Test Cleanup and Unmount (need fix)