#pragma once

#include <lib/types.h>
#include <kernel/block.h>

// 255 characters is the longest name FAT32 stores, in 20 pieces of 13
#define FAT32_MAX_NAME 261

typedef struct
{
    char name[FAT32_MAX_NAME]; // the long name when the entry has one, otherwise 8.3
    uint32_t size;             // always 0 for directories
    uint32_t first_cluster;
    bool_t is_dir;
} fat32_entry_t;

// where a directory walk stands; fat32_open_dir sets it up, fat32_next_entry moves it
typedef struct
{
    uint32_t cluster; // 0 once the walk is over
    uint32_t sector;  // sector inside that cluster
    uint32_t index;   // 32-byte entry inside that sector
} fat32_dir_t;

/* Looks for a FAT32 volume on a block device: first the device itself, then every FAT32 partition
 * its MBR lists. fat32_mount takes the first registered device that holds one. */
bool_t fat32_mount(void);
bool_t fat32_mount_device(const block_device_t *device);
void fat32_unmount(void);

bool_t fat32_mounted(void);
const block_device_t *fat32_device(void); // what is mounted, NULL when nothing is

uint32_t fat32_total_clusters(void);
uint32_t fat32_free_clusters(void);
uint32_t fat32_cluster_size(void); // in bytes

// paths are absolute, '/' separated, and compared without regard to case: "/dir/file.txt"
bool_t fat32_open_dir(const char *path, fat32_dir_t *dir);
bool_t fat32_next_entry(fat32_dir_t *dir, fat32_entry_t *entry);

bool_t fat32_stat(const char *path, fat32_entry_t *entry);

// copies at most max_bytes from the start of the file; returns how many bytes were copied
uint32_t fat32_read_file(const char *path, void *buf, uint32_t max_bytes);

// the same, starting `offset` bytes into the file, which is how a file larger than a buffer is read
uint32_t fat32_read_at(const char *path, uint32_t offset, void *buf, uint32_t max_bytes);

/* Writes a whole file at once: creates it when it is not there and replaces what it held when it
 * is. Returns how many bytes landed on the volume, which is either `size` or nothing at all. */
uint32_t fat32_write_file(const char *path, const void *buf, uint32_t size);

// creates one directory; every directory above it has to exist already
bool_t fat32_mkdir(const char *path);

// removes a file or an empty directory
bool_t fat32_remove(const char *path);

/* Gives an entry another name, another directory, or both, without touching what it holds. What
 * stands under the new name is replaced: a file by a file, an empty directory by a directory.
 * Refuses a directory that would end up inside itself. */
bool_t fat32_rename(const char *from, const char *to);
