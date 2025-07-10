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
static PennFatErr mkfs(const char* fs_name,
                       int blocks_in_fat,
                       int block_size_config);
static PennFatErr mount(const char* fs_name);
static PennFatErr unmount();
static PennFatErr mv(const char* oldname, const char* newname);
static PennFatErr chmod(const char** args);
static PennFatErr cp(const char** args);

static void cat(const char** args);
static void rm(const char** args);
static void touch(const char** args);

