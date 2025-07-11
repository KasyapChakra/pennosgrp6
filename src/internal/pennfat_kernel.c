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

#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-const-variable"
#pragma clang diagnostic ignored "-Wunused-variable"

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

// stub for unmount to satisfy build yet
static inline void k_unmount(void) { /* no-op stub */ }

/* Cleanup function: call this during application shutdown */
void pennfat_kernel_cleanup(void) {
  LOGGER_CLOSE();

  if (g_mounted) {
    k_unmount();
  }
  printf("PennFAT kernel module cleaned up.\n");
}

#pragma clang diagnostic pop

// ---------------------------------------------------------------------------
// HELPER ROUTINES
// ---------------------------------------------------------------------------

// Helper to get the filename component from a path
static const char* get_filename_from_path(const char* path) {
  if (!path)
    return NULL;
  char* last_slash = strrchr(path, '/');
  return last_slash ? last_slash + 1 : path;
}

// We'll use a simpler approach without a block cache

static inline void perm_to_str(uint8_t perm, char* str) {
  str[0] = (perm & PERM_READ) ? 'r' : '-';
  str[1] = (perm & PERM_WRITE) ? 'w' : '-';
  str[2] = (perm & PERM_EXEC) ? 'x' : '-';
  str[3] = '\0';
}

/*
 * read_block: Reads a block from the FS image using g_fs_fd.
 * Calculates offset = block_index * g_block_size.
 */
static int read_block(void* buf, uint32_t block_index) {
  if (g_fs_fd < 0)
    return -1;

  off_t offset = block_index * g_block_size;
  if (lseek(g_fs_fd, offset, SEEK_SET) < 0)
    return -1;

  ssize_t bytes_read = read(g_fs_fd, buf, g_block_size);
  if (bytes_read != g_block_size)
    return -1;

  return 0;
}

/*
 * write_block: Writes a block to the FS image using g_fs_fd.
 * Computes offset = block_index * g_block_size.
 * Always flushes to disk to ensure data integrity.
 */
static int write_block(const void* buf, uint32_t block_index) {
  if (g_fs_fd < 0)
    return -1;

  off_t offset = block_index * g_block_size;
  if (lseek(g_fs_fd, offset, SEEK_SET) < 0)
    return -1;

  ssize_t bytes_written = write(g_fs_fd, buf, g_block_size);
  if (bytes_written != g_block_size)
    return -1;

  // Always flush to disk immediately to ensure data integrity
  if (fdatasync(g_fs_fd) < 0) {
    LOG_ERR("[write_block] Failed to sync block %u to disk: %s", block_index,
            strerror(errno));
    return -1;
  }

  return 0;
}
// Helper to read the target of a symbolic link
static PennFatErr read_symlink_target(const dir_entry_t* link_entry,
                                      char* target_buf,
                                      size_t buf_size) {
  if (!link_entry || !target_buf || buf_size == 0)
    return PennFatErr_INVAD;
  if (link_entry->type != 4) {
    LOG_ERR("[read_symlink_target] Entry is not a symlink (type=%d)",
            link_entry->type);
    return PennFatErr_INVAD;  // Not a symlink
  }

  LOG_DEBUG(
      "[read_symlink_target] Reading symlink target: first_block=%u, size=%u",
      link_entry->first_block, link_entry->size);

  // Read the block containing the target path
  char* block_buffer = malloc(g_block_size);
  if (!block_buffer) {
    LOG_ERR("[read_symlink_target] Failed to allocate memory for block buffer");
    return PennFatErr_OUTOFMEM;
  }

  if (read_block(block_buffer, link_entry->first_block) != 0) {
    LOG_ERR("[read_symlink_target] Failed to read block %u",
            link_entry->first_block);
    free(block_buffer);
    return PennFatErr_IO;
  }

  // Copy the target path to the buffer, ensuring it's null-terminated
  size_t target_len = link_entry->size;
  if (target_len >= buf_size) {
    LOG_WARN(
        "[read_symlink_target] Target path truncated from %zu to %zu bytes",
        target_len, buf_size - 1);
    target_len = buf_size - 1;
  }

  memcpy(target_buf, block_buffer, target_len);
  target_buf[target_len] = '\0';

  LOG_DEBUG("[read_symlink_target] Read symlink target: '%s'", target_buf);

  free(block_buffer);
  return PennFatErr_OK;
}

/*
 * locate_block_in_chain: Given a file offset, finds the physical block and the
 * offset within that block, by walking the FAT chain starting at start_block.
 */
static int locate_block_in_chain(uint16_t start_block,
                                 uint32_t file_offset,
                                 uint16_t* block_out,
                                 uint32_t* offset_in_block) {
  if (start_block == FAT_FREE || start_block == FAT_EOC)
    return -1;

  uint32_t block_count = file_offset / g_block_size;
  *offset_in_block = file_offset % g_block_size;
  uint16_t current = start_block;
  for (uint32_t i = 0; i < block_count; i++) {
    if (current == FAT_EOC)
      return -1;

    current = g_fat[current];
  }

  *block_out = current;
  return 0;
}
/*
 * allocate_free_block: Scans the FAT (from data_start_block onward) to find a
 * free block, marks it as allocated (FAT_EOC), and returns its index. Returns
 * -1 if no free block.
 */
static int allocate_free_block(void) {
  uint32_t total_entries =
      (g_superblock.fat_block_count * g_block_size) / sizeof(uint16_t);
  for (uint32_t i = g_superblock.data_start_block; i < total_entries; i++) {
    if (g_fat[i] == FAT_FREE) {
      g_fat[i] = FAT_EOC;
      return i;
    }
  }
  return -1;
}

/*
 * free_block_chain: Frees all blocks in a chain starting from start_block.
 * Sets all FAT entries in the chain to FAT_FREE.
 */
static PennFatErr free_block_chain(uint16_t start_block) {
  if (start_block == FAT_FREE || start_block == FAT_EOC) {
    return PennFatErr_OK;  // Nothing to free
  }

  uint16_t current = start_block;
  uint16_t next;

  while (current != FAT_EOC && current != FAT_FREE) {
    next = g_fat[current];
    g_fat[current] = FAT_FREE;
    current = next;
  }

  return PennFatErr_OK;
}

/*
 * write_dirent: Writes a directory entry to a specific block and index.
 */
static PennFatErr write_dirent(uint16_t block_num,
                               int index,
                               const dir_entry_t* entry) {
  if (!entry)
    return PennFatErr_INVAD;
  if (block_num == FAT_FREE || block_num == FAT_EOC)
    return PennFatErr_INVAD;

  char* block_buffer = malloc(g_block_size);
  if (!block_buffer)
    return PennFatErr_OUTOFMEM;

  if (read_block(block_buffer, block_num) != 0) {
    free(block_buffer);
    return PennFatErr_IO;
  }

  dir_entry_t* dir_entries = (dir_entry_t*)block_buffer;
  uint32_t entries_per_block = g_block_size / sizeof(dir_entry_t);

  if (index < 0 || (uint32_t)index >= entries_per_block) {
    free(block_buffer);
    return PennFatErr_INVAD;
  }

  memcpy(&dir_entries[index], entry, sizeof(dir_entry_t));

  // Log the directory entry being written
  LOG_DEBUG("[write_dirent] Writing directory entry '%s' to block %u index %d",
            entry->name, block_num, index);

  // Force the block to be written to disk immediately for directory blocks
  if (write_block(block_buffer, block_num) != 0) {
    free(block_buffer);
    return PennFatErr_IO;
  }

  // The write_block function already flushes to disk

  free(block_buffer);
  return PennFatErr_OK;
}


PennFatErr k_chmod(const char* path, uint8_t new_perm) {

}

/*
 * lookup_entry:
 *   Searches the global directory (g_root_dir) for an entry with a matching
 * file name. If not found and create==true, it creates a new entry. Returns the
 * directory index on success or a negative PennFatErr code on failure.
 */
/*
 * lookup_entry:
 *   Searches the global directory (g_root_dir) for an entry with a matching
 * file name. If not found and create==true, it creates a new entry. Returns the
 * directory index on success or a negative PennFatErr code on failure.
 */
static int __attribute__((unused)) lookup_entry(const char* fname, int mode) {
  if (!fname || fname[0] == '\0') {
    LOG_ERR("[lookup_entry] Invalid filename.");
    return PennFatErr_INVAD;
  }

  if (!g_mounted) {
    LOG_WARN(
        "[lookup_entry] Failed to lookup file '%s': Filesystem not mounted.",
        fname);
    return PennFatErr_NOT_MOUNTED;
  }

  /* Compute the actual number of directory entries available in the allocated
     block: Since g_root_dir points to one block, num_entries = g_block_size /
     sizeof(dir_entry_t)
  */
  uint32_t num_entries = g_block_size / sizeof(dir_entry_t);

  //TODO
}




//------------------------------------------------
/* --- Mount/Unmount Functions --- */

/*
 * mount: Mounts the PennFAT filesystem.
 * The FAT region (of predetermined size) is mapped starting at offset 0.
 * The superblock functionality is implemented by reading FAT[0]:
 *   - The least-significant byte (LSB) is the block_size_config.
 *   - The most-significant byte (MSB) is the number of FAT blocks.
 * Based on that, we compute g_block_size and the FAT region size, and then
 * read the root directory from the first data block.
 */
PennFatErr k_lseek(int fd, int offset, int whence) {
  if (!g_mounted) {
    LOG_WARN(
        "[k_lseek] Failed to seek in file descriptor %d: Filesystem not "
        "mounted.",
        fd);
    return PennFatErr_NOT_MOUNTED;
  }

  if (fd < 0 || fd >= MAX_FD || !g_fd_table[fd].in_use) {
    LOG_ERR(
        "[k_lseek] Failed to seek in file descriptor %d: Invalid file "
        "descriptor or not in use.",
        fd);
    return PennFatErr_INTERNAL;
  }

  fd_entry_t* fdesc = &g_fd_table[fd];
  int sys_idx = fdesc->sysfile_index;
  system_file_t* sf = &g_sysfile_table[sys_idx];

  LOG_DEBUG(
      "[k_lseek] Attempting to seek in file descriptor %d (sysfile index %d) "
      "to offset %d from whence %d.",
      fd, sys_idx, offset, whence);

  int new_offset = 0;
  switch (whence) {
    case F_SEEK_SET:
      new_offset = offset;
      break;
    case F_SEEK_CUR:
      new_offset = (int)fdesc->offset + offset;
      break;
    case F_SEEK_END:
      new_offset = (int)sf->size + offset;
      break;
    default:
      LOG_ERR(
          "[k_lseek] Failed to seek in file descriptor %d: Unknown whence "
          "value %d.",
          fd, whence);
      return PennFatErr_INVAD;
  }

  if (new_offset < 0) {
    LOG_ERR(
        "[k_lseek] Failed to seek in file descriptor %d: New offset %d is "
        "negative.",
        fd, new_offset);
    return PennFatErr_INVAD; /* Cannot seek to a negative offset */
  }

  fdesc->offset = (uint32_t)new_offset;
  LOG_INFO(
      "[k_lseek] Successfully sought in file descriptor %d (sysfile index %d) "
      "to new offset %u.",
      fd, sys_idx, fdesc->offset);

  return fdesc->offset;
}
PennFatErr k_ls(const char* path) {
}
PennFatErr k_touch(const char* path) {
}
PennFatErr k_mount(const char* fs_name) {
  if (g_mounted) {
    LOG_WARN("[k_mount] Failed to mount filesystem '%s': Already mounted.",
             fs_name);
    return PennFatErr_UNEXPCMD;
  }

  /* Open the filesystem file using open(2) for read/write */
  int fd = open(fs_name, O_RDWR);
  if (fd < 0) {
    LOG_CRIT("[k_mount] Failed to open filesystem file '%s': %s", fs_name,
             strerror(errno));
    return PennFatErr_INTERNAL;
  }
  g_fs_fd = fd;

  /* Read the first 2 bytes from the file to get FAT[0] (the superblock info) */
  uint16_t super_entry;
  ssize_t rd = read(fd, &super_entry, sizeof(super_entry));
  if (rd != sizeof(super_entry)) {
    LOG_CRIT(
        "[k_mount] Failed to read superblock from filesystem file '%s': %s",
        fs_name, strerror(errno));
    close(fd);
    return PennFatErr_INTERNAL;
  }

  /* Interpret FAT[0] in little-endian format:
     - LSB (lower 8 bits) is block_size_config (0–4).
     - MSB (upper 8 bits) is the number of FAT blocks.
  */
  uint8_t block_size_config = super_entry & 0xFF;
  uint8_t fat_blocks = (super_entry >> 8) & 0xFF;

  if (block_size_config > 4) {
    LOG_ERR("[k_mount] Invalid block size config: %u", block_size_config);
    close(fd);
    return PennFatErr_INVAD;
  }
  if (fat_blocks < 1 || fat_blocks > 32) {
    LOG_ERR("[k_mount] Invalid number of FAT blocks: %u", fat_blocks);
    close(fd);
    return PennFatErr_INVAD;
  }

  /* Compute the actual block size */
  g_block_size = block_sizes[block_size_config];

  /* Compute the FAT region size (in bytes) */
  uint32_t fat_region_size = fat_blocks * g_block_size;

  LOG_DEBUG(
      "[k_mount] Mounting filesystem '%s' with block size %u bytes and %u FAT "
      "blocks.",
      fs_name, g_block_size, fat_blocks);

  /* Map the FAT region into memory using mmap(2).
     The FAT region is stored at offset 0.
  */
  g_fat =
      mmap(NULL, fat_region_size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
  if (g_fat == MAP_FAILED) {
    LOG_CRIT("[k_mount] Failed to map FAT region from filesystem file '%s': %s",
             fs_name, strerror(errno));
    close(fd);
    return PennFatErr_INTERNAL;
  }

  /* Optionally verify that the mapped FAT[0] matches the super_entry we read */
  if (g_fat[0] != super_entry) {
    LOG_CRIT("[k_mount] FAT[0] mismatch: expected 0x%04x, got 0x%04x",
             super_entry, g_fat[0]);
    munmap(g_fat, fat_region_size);
    close(fd);
    return PennFatErr_INTERNAL;
  }

  /* Set up the superblock info from FAT[0]:
     - g_superblock.fat_block_count is set from fat_blocks.
     - Data blocks start at index 2: index 0 holds formatting info and index 1
     is reserved for the root directory.
  */
  g_superblock.fat_block_count = fat_blocks;
  g_superblock.data_start_block = 2;

  /* Read the root directory region.
     According to our mkfs, the root directory is stored in the first data
     block, which immediately follows the FAT region. Its size is one block
     (g_block_size bytes). Compute the offset for the root directory:
         root_offset = fat_region_size
  */
  off_t root_offset = fat_region_size;
  g_root_dir = malloc(g_block_size);
  if (!g_root_dir) {
    LOG_CRIT("[k_mount] Failed to allocate memory for root directory: %s",
             strerror(errno));
    munmap(g_fat, fat_region_size);
    close(fd);
    return PennFatErr_OUTOFMEM;
  }

  if (lseek(fd, root_offset, SEEK_SET) < 0) {
    LOG_CRIT(
        "[k_mount] Failed to seek to root directory in filesystem file '%s': "
        "%s",
        fs_name, strerror(errno));
    free(g_root_dir);
    munmap(g_fat, fat_region_size);
    close(fd);
    return PennFatErr_INTERNAL;
  }

  if (read(fd, g_root_dir, g_block_size) != (ssize_t)g_block_size) {
    LOG_CRIT(
        "[k_mount] Failed to read root directory from filesystem file '%s': %s",
        fs_name, strerror(errno));
    free(g_root_dir);
    munmap(g_fat, fat_region_size);
    close(fd);
    return PennFatErr_INTERNAL;
  }

  LOG_DEBUG(
      "[k_mount] Successfully read root directory from offset %u in filesystem "
      "file '%s'.",
      root_offset, fs_name);

  /* Clear system-wide and FD tables (if necessary) */
  memset(g_sysfile_table, 0, sizeof(g_sysfile_table));
  memset(g_fd_table, 0, sizeof(g_fd_table));

  /* No block cache initialization needed */

  LOG_INFO(
      "[k_mount] Successfully mounted filesystem '%s' with block size %u "
      "bytes.",
      fs_name, g_block_size);

  g_mounted = 1;
  return PennFatErr_SUCCESS;
}

/* unmount: Writes back the FAT and root directory to disk, then unmaps and
 * closes the FS */
PennFatErr k_unmount(void) {
  if (!g_mounted) {
    LOG_WARN("[k_unmount] Failed to unmount filesystem: Not mounted.");
    return PennFatErr_NOT_MOUNTED;
  }

  /* Recompute FAT region size:
     FAT[0] contains the formatting info:
       MSB = number of FAT blocks
     Calculate:
       fat_blocks = (g_fat[0] >> 8) & 0xff
       fat_region_size = fat_blocks * g_block_size
  */
  uint8_t fat_blocks = (g_fat[0] >> 8) & 0xff;
  uint32_t fat_region_size = fat_blocks * g_block_size;

  LOG_DEBUG(
      "[k_unmount] Unmounting filesystem with %u FAT blocks, block size %u "
      "bytes.",
      fat_blocks, g_block_size);

  /* Close all open file descriptors to ensure metadata is written back */
  for (int fd = 0; fd < MAX_FD; fd++) {
    if (g_fd_table[fd].in_use) {
      LOG_INFO("[k_unmount] Auto-closing open file descriptor %d", fd);
      k_close(fd);
    }
  }

  /* No block cache to flush */

  /* Write the root directory directly to disk */
  LOG_INFO("[k_unmount] Writing root directory to disk...");
  off_t root_offset = fat_region_size;
  if (lseek(g_fs_fd, root_offset, SEEK_SET) < 0) {
    LOG_CRIT(
        "[k_unmount] Failed to seek to root directory in filesystem file: %s",
        strerror(errno));
    return PennFatErr_INTERNAL;
  }
  if (write(g_fs_fd, g_root_dir, g_block_size) != (ssize_t)g_block_size) {
    LOG_CRIT(
        "[k_unmount] Failed to write root directory to filesystem file: %s",
        strerror(errno));
    return PennFatErr_INTERNAL;
  }

  /* Synchronize the mapped FAT region to disk */
  if (msync(g_fat, fat_region_size, MS_SYNC) < 0) {
    LOG_CRIT("[k_unmount] Failed to synchronize FAT region to disk: %s",
             strerror(errno));
    return PennFatErr_INTERNAL;
  }

  /* Unmap the FAT region */
  if (munmap(g_fat, fat_region_size) < 0) {
    LOG_CRIT("[k_unmount] Failed to unmap FAT region: %s", strerror(errno));
    return PennFatErr_INTERNAL;
  }
  g_fat = NULL;

  /* Free the allocated root directory buffer */
  free(g_root_dir);
  g_root_dir = NULL;

  /* Ensure all written data is flushed to the disk */
  LOG_INFO("[k_unmount] Syncing all filesystem data to disk...");
  if (fsync(g_fs_fd) < 0) {
    LOG_CRIT("[k_unmount] Failed to sync filesystem data to disk: %s",
             strerror(errno));
    // Even if fsync fails, try to close the file descriptor
    close(g_fs_fd);
    g_fs_fd = -1;  // Mark as closed
    return PennFatErr_INTERNAL;
  }
  LOG_INFO("[k_unmount] All filesystem data successfully synced to disk.");

  /* Close the filesystem file */
  if (close(g_fs_fd) < 0) {
    LOG_ERR("[k_unmount] Failed to close filesystem file: %s", strerror(errno));
    return PennFatErr_INTERNAL;
  }
  g_fs_fd = -1;

  LOG_INFO("[k_unmount] Successfully unmounted filesystem.");

  g_mounted = 0;
  return 0;
}

/**
 * mkfs: Creates a new PennFAT filesystem.
 * Usage: mkfs FS_NAME BLOCKS_IN_FAT BLOCK_SIZE_CONFIG
 *
 * BLOCKS_IN_FAT must be between 1 and 32.
 * BLOCK_SIZE_CONFIG must be between 0 and 4, which maps to:
 *   0: 256 bytes, 1: 512 bytes, 2: 1024 bytes, 3: 2048 bytes, 4: 4096 bytes.
 *
 * The FAT is placed at the very beginning of the filesystem image.
 * The first FAT entry (FAT[0]) stores formatting info:
 *     MSB = blocks_in_fat, LSB = block_size_config.
 * FAT[1] is set to FAT_EOC, designating that the first data block (Block 1,
 * which is the root directory file's first block) is allocated. The data region
 * size is: block_size * (number of FAT entries - 1).
 */
PennFatErr k_mkfs(const char* fs_name,
                  int blocks_in_fat,
                  int block_size_config) {
  /* Check if a filesystem is already mounted */
  if (g_mounted) {
    LOG_WARN(
        "[k_mkfs] Cannot create a new filesystem while one is already "
        "mounted.");
    return PennFatErr_UNEXPCMD;
  }

  /* Validate parameters */
  if (blocks_in_fat < 1 || blocks_in_fat > 32) {
    LOG_ERR(
        "[k_mkfs] Invalid number of blocks in FAT. Must be between 1 and 32.");
    return PennFatErr_INVAD;
  }
  if (block_size_config < 0 || block_size_config > 4) {
    LOG_ERR(
        "[k_mkfs] Invalid block size configuration. Must be between 0 and 4.");
    return PennFatErr_INVAD;
  }
  uint32_t block_size = block_sizes[block_size_config];

  /* Calculate region sizes:
   * FAT region size = blocks_in_fat * block_size.
   * Number of FAT entries = FAT region size / 2.
   * Data region size = block_size * (number of FAT entries - 1).
   * Total FS size = FAT region size + Data region size.
   */
  uint32_t fat_region_size = blocks_in_fat * block_size;
  uint32_t fat_entries =
      fat_region_size / sizeof(uint16_t);  // each entry is 2 bytes
  uint32_t data_blocks =
      (fat_entries - 1) -
      ((fat_entries - 1) == 0xFFFF);  // subtract one for FAT[0]
                                      // subtract one for xFFFF
  uint32_t data_region_size = data_blocks * block_size;
  uint32_t total_fs_size = fat_region_size + data_region_size;

  LOG_DEBUG(
      "[k_mkfs] Creating filesystem with %d blocks in FAT, block size %u "
      "bytes, total size %u bytes.",
      blocks_in_fat, block_size, total_fs_size);

  /* Open (or create) the filesystem file */
  int fd = open(fs_name, O_RDWR | O_CREAT | O_TRUNC, 0666);
  if (fd < 0) {
    LOG_CRIT("[k_mkfs] Failed to open/create filesystem file '%s': %s", fs_name,
             strerror(errno));
    return PennFatErr_INTERNAL;
  }

  /* Set the file size to total_fs_size bytes */
  if (ftruncate(fd, total_fs_size) < 0) {
    perror("mkfs: ftruncate");
    close(fd);
    return PennFatErr_INTERNAL;
  }

  /* Allocate and initialize the FAT array */
  uint16_t* fat_array = malloc(fat_region_size);
  if (!fat_array) {
    perror("mkfs: malloc (fat_array)");
    close(fd);
    return PennFatErr_OUTOFMEM;
  }
  uint32_t num_entries = fat_region_size / sizeof(uint16_t);
  for (uint32_t i = 0; i < num_entries; i++) {
    fat_array[i] = FAT_FREE;
  }

  /* Set formatting info in FAT[0]:
     MSB = blocks_in_fat, LSB = block_size_config.
     For example, if blocks_in_fat = 32 and block_size_config = 4, FAT[0] =
     0x2004.
  */
  fat_array[0] = ((uint16_t)blocks_in_fat << 8) | (uint16_t)block_size_config;

  /* Set FAT[1] to FAT_EOC so that the root directory's first block is allocated
   * and marked as the end of chain */
  fat_array[1] = FAT_EOC;

  /* Write the FAT region at offset 0 */
  if (lseek(fd, 0, SEEK_SET) < 0) {
    perror("mkfs: lseek (FAT region)");
    free(fat_array);
    close(fd);
    return PennFatErr_INTERNAL;
  }
  if (write(fd, fat_array, fat_region_size) != fat_region_size) {
    perror("mkfs: write (FAT region)");
    free(fat_array);
    close(fd);
    return PennFatErr_INTERNAL;
  }
  free(fat_array);

  /* Initialize the root directory region.
     The root directory is stored in the first data block (Block 1).
     We'll zero out one block (block_size bytes) at offset = fat_region_size.
     (If the entire FS image is already zeroed by ftruncate, this might be
     optional, but it's good to explicitly set the root directory.)
  */
  char* zero_buf = calloc(1, block_size);
  if (!zero_buf) {
    LOG_CRIT("[k_mkfs] Failed to allocate memory for zero buffer.");
    close(fd);
    return PennFatErr_INTERNAL;
  }
  if (lseek(fd, fat_region_size, SEEK_SET) < 0) {
    LOG_CRIT("[k_mkfs] Failed to seek to root directory region.");
    free(zero_buf);
    close(fd);
    return PennFatErr_INTERNAL;
  }
  if (write(fd, zero_buf, block_size) != block_size) {
    LOG_CRIT("[k_mkfs] Failed to write root directory region.");
    free(zero_buf);
    close(fd);
    return PennFatErr_INTERNAL;
  }
  free(zero_buf);

  /* The data region can be left uninitialized or zeroed as needed. */

  LOG_INFO(
      "[k_mkfs] Created filesystem '%s' with %d blocks in FAT and block size "
      "%u bytes.",
      fs_name, blocks_in_fat, block_size, block_size_config);

  close(fd);
  return PennFatErr_SUCCESS;
}