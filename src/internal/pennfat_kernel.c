#include <errno.h>  // IWYU pragma: keep [errno]
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/mman.h>
#include <sys/types.h>

#include "../common/pennfat_definitions.h"
#include "../common/pennfat_errors.h"
#include "../util/logger.h"
#include "pennfat_kernel.h"


/* FAT entry definitions */
#define FAT_FREE 0x0000
#define FAT_EOC 0xFFFF  // End-Of-Chain

/* Table sizes */
#define MAX_SYSTEM_FILES \
  64  // Subject to chanage; maximum number of system-wide file entries
#define MAX_FD 32  // Subject to change; max number of open file descriptors
#define MAX_DIR_ENTRIES \
  128  // Subject to change; maximum number of entries in the root directory

/* Allowed block sizes mapping */
static const int block_sizes[] = {256, 512, 1024, 2048, 4096};

// ---------------------------------------------------------------------------
// 2) GLOBAL DATA STRUCTURES
// ---------------------------------------------------------------------------

// static int g_mounted = 0;            // 1 if a filesystem is mounted; 0
// otherwise
static int g_fs_fd = -1;             // File descriptor for the FS image
static uint32_t g_block_size = 512;  // Actual block size (set during mount)
static uint16_t* g_fat = NULL;       // Pointer to the mapped FAT region
static dir_entry_t* g_root_dir =
    NULL;  // Pointer to the root directory block (1 block)

/* The superblock info is embedded in FAT[0]:
 * MSB = number of FAT blocks; LSB = block_size_config.
 * For helper routines we store parsed info here:
 */
typedef struct {
  uint32_t fat_block_count;  /* number of FAT blocks (from FAT[0]'s MSB) */
  uint32_t data_start_block; /* computed: FAT region size in blocks */
} superblock_t;
static superblock_t g_superblock;

/* Global arrays for our system-wide file table and FD table */
static system_file_t g_sysfile_table[MAX_SYSTEM_FILES];
static fd_entry_t g_fd_table[MAX_FD];

/* Current working directory block - starts at root (block 1) */
static uint16_t g_cwd_block = 1;

/* Maximum path depth for directory traversal */
#define MAX_DEPTH 32
#define PATH_MAX 256

/* We'll use a simpler approach without a block cache */

/* Path resolution result structure */
typedef struct {
  bool found;                 // Whether the path was found
  bool is_root;               // Whether this is the root directory
  dir_entry_t entry;          // The directory entry if found
  uint16_t entry_block;       // Block containing the entry
  int entry_index_in_block;   // Index of entry within the block
  uint16_t parent_dir_block;  // Block of parent directory
} resolved_path_t;

static int g_mounted = 0;

static Logger* logger = NULL;

/* Initialization function: call this from your main application */
void pennfat_kernel_init(void) {
  LOGGER_INIT("pennfat_kernel", LOG_LEVEL_INFO);
}

/* Cleanup function: call this during application shutdown */
void pennfat_kernel_cleanup(void) {
  LOGGER_CLOSE();

  if (g_mounted) {
    k_unmount();
  }
  printf("PennFAT kernel module cleaned up.\n");
}
