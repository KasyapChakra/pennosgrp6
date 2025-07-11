#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common/pennfat_definitions.h"
#include "common/pennfat_errors.h"
#include "internal/pennfat_kernel.h"
#include "util/utils.h"

// Definition

#define MAX_CMD_LENGTH 1024
#define BUFSIZE 4096

// function declarations for special routines
static __attribute__((unused)) PennFatErr mkfs(const char* fs_name,
                       int blocks_in_fat,
                       int block_size_config);
static __attribute__((unused)) PennFatErr mount(const char* fs_name);
static __attribute__((unused)) PennFatErr unmount();
static __attribute__((unused)) PennFatErr mv(const char* oldname, const char* newname);
static __attribute__((unused)) PennFatErr chmod(const char** args);
static __attribute__((unused)) PennFatErr cp(const char** args);

static __attribute__((unused)) void cat(const char** args);
static __attribute__((unused)) void rm(const char** args);
static __attribute__((unused)) void touch(const char** args);

