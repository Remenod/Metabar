#pragma once

#include <lib/types.h>

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

/* Looks for a FAT32 volume on the registered block device: first the device itself, then every
 * FAT32 partition its MBR lists. Returns false if neither holds one. */
bool_t fat32_mount(void);
bool_t fat32_mounted(void);

// paths are absolute, '/' separated, and compared without regard to case: "/dir/file.txt"
bool_t fat32_open_dir(const char *path, fat32_dir_t *dir);
bool_t fat32_next_entry(fat32_dir_t *dir, fat32_entry_t *entry);

bool_t fat32_stat(const char *path, fat32_entry_t *entry);

// copies at most max_bytes from the start of the file; returns how many bytes were copied
uint32_t fat32_read_file(const char *path, void *buf, uint32_t max_bytes);
