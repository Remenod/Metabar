#include <kernel/fat32.h>

#include <kernel/block.h>
#include <lib/mem.h>

#define DIR_ENTRY_SIZE 32
#define ENTRIES_PER_SECTOR (BLOCK_SECTOR_SIZE / DIR_ENTRY_SIZE)

#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_LFN 0x0F // read-only + hidden + system + volume id at once: never a real file

#define ENTRY_FREE 0xE5 // deleted entry, keep looking
#define ENTRY_END 0x00  // nothing was ever written past here

#define CLUSTER_MASK 0x0FFFFFFF // the top four bits of a FAT32 entry are reserved
#define CLUSTER_LAST 0x0FFFFFF8 // this one and above end a chain

#define LFN_LAST 0x40 // marks the piece that holds the end of the name
#define LFN_CHARS 13  // characters per long name entry
#define LFN_MAX_SEQ 20

#define MBR_PARTITION_TABLE 446
#define PARTITION_TYPE_FAT32 0x0B
#define PARTITION_TYPE_FAT32_LBA 0x0C

static struct
{
    bool_t mounted;
    uint32_t fat_lba;             // first sector of the first FAT
    uint32_t data_lba;            // sector where cluster 2 starts
    uint32_t sectors_per_cluster;
    uint32_t root_cluster;
    uint32_t cluster_count;
} fs;

// one cached sector of the FAT and one of whatever directory is being walked
static uint8_t fat_cache[BLOCK_SECTOR_SIZE];
static uint8_t dir_cache[BLOCK_SECTOR_SIZE];
static uint32_t fat_cache_lba;
static uint32_t dir_cache_lba;
static bool_t fat_cached;
static bool_t dir_cached;

static uint16_t le16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static uint32_t le32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static bool_t read_dir_sector(uint32_t lba)
{
    if (dir_cached && dir_cache_lba == lba)
        return true;

    if (!block_read(lba, 1, dir_cache))
        return false;

    dir_cache_lba = lba;
    dir_cached = true;
    return true;
}

/* The FAT is a plain array of 32-bit entries, one per cluster, holding the number of the next
 * cluster of the same file. Walking a file means following that chain. */
static uint32_t next_cluster(uint32_t cluster)
{
    const uint32_t offset = cluster * 4;
    const uint32_t lba = fs.fat_lba + offset / BLOCK_SECTOR_SIZE;

    if (!fat_cached || fat_cache_lba != lba)
    {
        if (!block_read(lba, 1, fat_cache))
            return CLUSTER_LAST;
        fat_cache_lba = lba;
        fat_cached = true;
    }

    return le32(fat_cache + offset % BLOCK_SECTOR_SIZE) & CLUSTER_MASK;
}

static bool_t cluster_valid(uint32_t cluster)
{
    return cluster >= 2 && cluster < fs.cluster_count + 2;
}

// clusters are numbered from 2, which is why the data area starts there
static uint32_t cluster_lba(uint32_t cluster)
{
    return fs.data_lba + (cluster - 2) * fs.sectors_per_cluster;
}

/* Reads a boot sector and fills fs from its BPB. Everything a FAT32 volume needs is in there:
 * how big a cluster is, how many sectors the FATs take, and where the root directory starts. */
static bool_t read_bpb(uint32_t lba)
{
    if (!block_read(lba, 1, dir_cache))
        return false;
    dir_cached = false; // dir_cache is scratch here, not a directory sector

    if (dir_cache[510] != 0x55 || dir_cache[511] != 0xAA)
        return false;

    const uint32_t bytes_per_sector = le16(dir_cache + 11);
    const uint32_t sectors_per_cluster = dir_cache[13];
    const uint32_t reserved = le16(dir_cache + 14);
    const uint32_t fat_count = dir_cache[16];
    const uint32_t fat_size = le32(dir_cache + 36); // FAT12/16 leave this zero
    const uint32_t root_cluster = le32(dir_cache + 44);

    // small volumes keep their size in the old 16-bit field, bigger ones in the 32-bit one
    uint32_t total_sectors = le16(dir_cache + 19);
    if (total_sectors == 0)
        total_sectors = le32(dir_cache + 32);

    if (bytes_per_sector != BLOCK_SECTOR_SIZE || sectors_per_cluster == 0 || reserved == 0)
        return false;
    if (fat_count == 0 || fat_size == 0 || root_cluster < 2 || total_sectors == 0)
        return false;

    const uint32_t data_start = reserved + fat_count * fat_size;
    if (data_start >= total_sectors)
        return false;

    fs.fat_lba = lba + reserved;
    fs.data_lba = lba + data_start;
    fs.sectors_per_cluster = sectors_per_cluster;
    fs.root_cluster = root_cluster;
    fs.cluster_count = (total_sectors - data_start) / sectors_per_cluster;
    fs.mounted = true;
    fat_cached = false;
    return true;
}

bool_t fat32_mount(void)
{
    fs.mounted = false;

    if (block_device() == NULL)
        return false;

    if (read_bpb(0)) // a bare volume, like an image written by mkfs.vfat
        return true;

    if (!block_read(0, 1, dir_cache)) // otherwise look for one in the partition table
        return false;
    dir_cached = false;

    for (uint32_t i = 0; i < 4; i++)
    {
        const uint8_t *entry = dir_cache + MBR_PARTITION_TABLE + i * 16;
        const uint8_t type = entry[4];
        const uint32_t start = le32(entry + 8);

        if (start == 0 || (type != PARTITION_TYPE_FAT32 && type != PARTITION_TYPE_FAT32_LBA))
            continue;

        if (read_bpb(start))
            return true;

        if (!block_read(0, 1, dir_cache)) // read_bpb overwrote the table
            return false;
        dir_cached = false;
    }

    return false;
}

bool_t fat32_mounted(void)
{
    return fs.mounted;
}

// the 8.3 name as stored: padded with spaces and without the dot
static void format_short_name(const uint8_t *entry, char *out)
{
    uint32_t length = 0;

    for (uint32_t i = 0; i < 8 && entry[i] != ' '; i++)
        out[length++] = (char)entry[i];

    if (entry[8] != ' ')
    {
        out[length++] = '.';
        for (uint32_t i = 8; i < 11 && entry[i] != ' '; i++)
            out[length++] = (char)entry[i];
    }

    out[length] = '\0';
}

/* Ties a long name to its short entry: the pieces carry this checksum of the 8.3 name, so a
 * directory edited by a system that did not understand long names is caught instead of trusted. */
static uint8_t short_name_checksum(const uint8_t *entry)
{
    uint8_t sum = 0;

    for (uint32_t i = 0; i < 11; i++)
        sum = (uint8_t)(((sum & 1) << 7) + (sum >> 1) + entry[i]);

    return sum;
}

/* A long name is spread over the entries that come right before the short one, in reverse order:
 * each holds 13 UTF-16 characters at three odd places inside the 32 bytes. */
static void collect_long_name(const uint8_t *entry, char *name, bool_t *collecting, uint8_t *checksum)
{
    static const uint8_t offsets[LFN_CHARS] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

    const uint8_t sequence = entry[0] & 0x1F;
    if (sequence == 0 || sequence > LFN_MAX_SEQ)
    {
        *collecting = false;
        return;
    }

    if (entry[0] & LFN_LAST) // comes first on disk, holds the tail of the name
    {
        memset(name, 0, FAT32_MAX_NAME);
        *checksum = entry[13];
        *collecting = true;
    }
    else if (!*collecting || entry[13] != *checksum)
    {
        *collecting = false;
        return;
    }

    char *piece = name + (sequence - 1) * LFN_CHARS;
    for (uint32_t i = 0; i < LFN_CHARS; i++)
    {
        const uint16_t character = le16(entry + offsets[i]);
        if (character == 0x0000 || character == 0xFFFF)
            break;

        piece[i] = character < 0x80 ? (char)character : '?'; // this kernel only speaks ASCII
    }
}

static void copy_name(char *dst, const char *src)
{
    uint32_t i = 0;
    while (src[i] != '\0' && i < FAT32_MAX_NAME - 1)
    {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

bool_t fat32_next_entry(fat32_dir_t *dir, fat32_entry_t *entry)
{
    char long_name[FAT32_MAX_NAME];
    bool_t collecting = false;
    uint8_t checksum = 0;

    if (!fs.mounted || dir == NULL || entry == NULL)
        return false;

    while (cluster_valid(dir->cluster))
    {
        if (dir->sector >= fs.sectors_per_cluster) // this cluster is done, follow the chain
        {
            dir->cluster = next_cluster(dir->cluster);
            dir->sector = 0;
            dir->index = 0;
            continue;
        }

        if (!read_dir_sector(cluster_lba(dir->cluster) + dir->sector))
            return false;

        while (dir->index < ENTRIES_PER_SECTOR)
        {
            const uint8_t *raw = dir_cache + dir->index * DIR_ENTRY_SIZE;
            const uint8_t attributes = raw[11];
            dir->index++;

            if (raw[0] == ENTRY_END)
            {
                dir->cluster = 0;
                return false;
            }

            if (raw[0] == ENTRY_FREE)
            {
                collecting = false;
                continue;
            }

            if (attributes == ATTR_LFN)
            {
                collect_long_name(raw, long_name, &collecting, &checksum);
                continue;
            }

            if (attributes & ATTR_VOLUME_ID) // the volume label is not a file
            {
                collecting = false;
                continue;
            }

            if (collecting && checksum == short_name_checksum(raw))
            {
                copy_name(entry->name, long_name);
            }
            else
            {
                char short_name[13];
                format_short_name(raw, short_name);
                copy_name(entry->name, short_name);
            }

            entry->first_cluster = ((uint32_t)le16(raw + 20) << 16) | le16(raw + 26);
            entry->size = le32(raw + 28);
            entry->is_dir = (attributes & ATTR_DIRECTORY) != 0;
            return true;
        }

        dir->index = 0;
        dir->sector++;
    }

    return false;
}

static void open_cluster(fat32_dir_t *dir, uint32_t cluster)
{
    dir->cluster = cluster;
    dir->sector = 0;
    dir->index = 0;
}

static bool_t names_equal(const char *name, const char *component, uint32_t length)
{
    for (uint32_t i = 0; i < length; i++)
        if (name[i] == '\0' || upper(name[i]) != upper(component[i]))
            return false;

    return name[length] == '\0';
}

static bool_t find_in_directory(uint32_t cluster, const char *component, uint32_t length, fat32_entry_t *entry)
{
    fat32_dir_t dir;
    open_cluster(&dir, cluster);

    while (fat32_next_entry(&dir, entry))
        if (names_equal(entry->name, component, length))
            return true;

    return false;
}

// walks the path one component at a time; the entry of the last one comes back in `entry`
static bool_t resolve(const char *path, fat32_entry_t *entry)
{
    if (!fs.mounted || path == NULL || path[0] != '/')
        return false;

    entry->first_cluster = fs.root_cluster;
    entry->size = 0;
    entry->is_dir = true;
    entry->name[0] = '\0';

    uint32_t i = 1;
    while (path[i] != '\0')
    {
        uint32_t length = 0;
        while (path[i + length] != '\0' && path[i + length] != '/')
            length++;

        if (length == 0) // a trailing or doubled slash changes nothing
        {
            i++;
            continue;
        }

        if (!entry->is_dir)
            return false;

        uint32_t cluster = entry->first_cluster;
        if (cluster == 0) // ".." of a top level directory points at the root this way
            cluster = fs.root_cluster;

        if (!find_in_directory(cluster, path + i, length, entry))
            return false;

        i += length;
    }

    return true;
}

bool_t fat32_stat(const char *path, fat32_entry_t *entry)
{
    return entry != NULL && resolve(path, entry);
}

bool_t fat32_open_dir(const char *path, fat32_dir_t *dir)
{
    fat32_entry_t entry;

    if (dir == NULL || !resolve(path, &entry) || !entry.is_dir)
        return false;

    open_cluster(dir, entry.first_cluster == 0 ? fs.root_cluster : entry.first_cluster);
    return true;
}

uint32_t fat32_read_file(const char *path, void *buf, uint32_t max_bytes)
{
    fat32_entry_t entry;

    if (buf == NULL || max_bytes == 0 || !resolve(path, &entry) || entry.is_dir)
        return 0;

    uint32_t left = entry.size < max_bytes ? entry.size : max_bytes;
    uint32_t done = 0;
    uint32_t cluster = entry.first_cluster;

    while (left > 0 && cluster_valid(cluster))
    {
        for (uint32_t sector = 0; sector < fs.sectors_per_cluster && left > 0; sector++)
        {
            if (!read_dir_sector(cluster_lba(cluster) + sector))
                return done;

            const uint32_t chunk = left < BLOCK_SECTOR_SIZE ? left : BLOCK_SECTOR_SIZE;
            memcpy((uint8_t *)buf + done, dir_cache, chunk);
            done += chunk;
            left -= chunk;
        }

        cluster = next_cluster(cluster);
    }

    return done;
}
