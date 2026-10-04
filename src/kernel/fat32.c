#include <kernel/fat32.h>

#include <kernel/block.h>
#include <lib/mem.h>
#include <lib/string.h>

#define DIR_ENTRY_SIZE 32
#define ENTRIES_PER_SECTOR (BLOCK_SECTOR_SIZE / DIR_ENTRY_SIZE)

#define ATTR_VOLUME_ID 0x08
#define ATTR_DIRECTORY 0x10
#define ATTR_ARCHIVE 0x20 // "changed since the last backup", what a plain new file gets
#define ATTR_LFN 0x0F     // read-only + hidden + system + volume id at once: never a real file

#define ENTRY_FREE 0xE5 // deleted entry, keep looking
#define ENTRY_END 0x00  // nothing was ever written past here

#define CLUSTER_MASK 0x0FFFFFFF // the top four bits of a FAT32 entry are reserved
#define CLUSTER_LAST 0x0FFFFFF8 // this one and above end a chain

#define LFN_LAST 0x40 // marks the piece that holds the end of the name
#define LFN_CHARS 13  // characters per long name entry
#define LFN_MAX_SEQ 20

/* The volume keeps a note of how much of it is free and where to start looking. Both are hints
 * that a driver may ignore, and the signatures are what tells a real one from an empty sector. */
#define FSINFO_LEAD 0x41615252
#define FSINFO_STRUCT 0x61417272
#define FSINFO_FREE_UNKNOWN 0xFFFFFFFF

#define FAT_DATE_UNKNOWN 0x5C21 // 1 January 2026: there is no clock to ask for a real one yet

#define MBR_PARTITION_TABLE 446
#define PARTITION_TYPE_FAT32 0x0B
#define PARTITION_TYPE_FAT32_LBA 0x0C

static struct
{
    bool_t mounted;
    uint32_t fat_lba;  // first sector of the first FAT
    uint32_t data_lba; // sector where cluster 2 starts
    uint32_t sectors_per_cluster;
    uint32_t root_cluster;
    uint32_t cluster_count;
    uint32_t fat_count;  // every copy of the FAT has to be written, not just the first
    uint32_t fat_size;   // sectors in one of them
    uint32_t fsinfo_lba; // 0 when the volume has no FSInfo sector
    uint32_t next_free;  // where the search for a free cluster starts
    uint32_t free_count; // clusters still free, kept so the volume can be left with a true one
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

static void put16(uint8_t *p, uint16_t value)
{
    p[0] = (uint8_t)value;
    p[1] = (uint8_t)(value >> 8);
}

static void put32(uint8_t *p, uint32_t value)
{
    put16(p, (uint16_t)value);
    put16(p + 2, (uint16_t)(value >> 16));
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
static bool_t load_fat_sector(uint32_t lba)
{
    if (fat_cached && fat_cache_lba == lba)
        return true;

    if (!block_read(lba, 1, fat_cache))
        return false;

    fat_cache_lba = lba;
    fat_cached = true;
    return true;
}

static uint32_t next_cluster(uint32_t cluster)
{
    const uint32_t offset = cluster * 4;

    if (!load_fat_sector(fs.fat_lba + offset / BLOCK_SECTOR_SIZE))
        return CLUSTER_LAST;

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

// walking the whole FAT, which is what the FSInfo sector exists to spare a driver
static uint32_t count_free_clusters(void)
{
    uint32_t free = 0;

    for (uint32_t cluster = 2; cluster < fs.cluster_count + 2; cluster++)
        if (next_cluster(cluster) == 0)
            free++;

    return free;
}

/* The volume keeps a count of its free clusters and a hint of where they start, both of which a
 * driver may find stale: they are only as good as the last system that wrote them left them. */
static void read_fsinfo(void)
{
    fs.next_free = 2;
    fs.free_count = FSINFO_FREE_UNKNOWN;

    if (fs.fsinfo_lba != 0 && block_read(fs.fsinfo_lba, 1, dir_cache))
    {
        dir_cached = false; // dir_cache is scratch here, not a directory sector

        if (le32(dir_cache) == FSINFO_LEAD && le32(dir_cache + 484) == FSINFO_STRUCT)
        {
            const uint32_t free = le32(dir_cache + 488);
            const uint32_t next = le32(dir_cache + 492);

            if (free <= fs.cluster_count)
                fs.free_count = free;
            if (cluster_valid(next))
                fs.next_free = next;
        }
    }

    if (fs.free_count == FSINFO_FREE_UNKNOWN)
        fs.free_count = count_free_clusters();
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
    const uint32_t fsinfo_sector = le16(dir_cache + 48);

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
    fs.fat_count = fat_count;
    fs.fat_size = fat_size;
    fs.fsinfo_lba = fsinfo_sector != 0 && fsinfo_sector < reserved ? lba + fsinfo_sector : 0;
    fs.mounted = true;
    fat_cached = false;
    read_fsinfo();
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

// where the thirteen characters of a long name piece sit inside its 32 bytes
static const uint8_t lfn_offsets[LFN_CHARS] = {1, 3, 5, 7, 9, 14, 16, 18, 20, 22, 24, 28, 30};

/* A long name is spread over the entries that come right before the short one, in reverse order:
 * each holds 13 UTF-16 characters at three odd places inside the 32 bytes. */
static void collect_long_name(const uint8_t *entry, char *name, bool_t *collecting, uint8_t *checksum)
{
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
        const uint16_t character = le16(entry + lfn_offsets[i]);
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

/* Where the records of the entry fat32_next_entry returned last sit: the first of them, a long
 * name piece when the entry has one, and the short entry itself. Only the calls that change a
 * directory look at these, and only right after the walk that set them. */
static fat32_dir_t found_first;
static fat32_dir_t found_short;

bool_t fat32_next_entry(fat32_dir_t *dir, fat32_entry_t *entry)
{
    char long_name[FAT32_MAX_NAME];
    fat32_dir_t group = {0, 0, 0};
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
            const fat32_dir_t here = {dir->cluster, dir->sector, dir->index};
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
                if (raw[0] & LFN_LAST)
                    group = here;
                collect_long_name(raw, long_name, &collecting, &checksum);
                continue;
            }

            if (attributes & ATTR_VOLUME_ID) // the volume label is not a file
            {
                collecting = false;
                continue;
            }

            const bool_t long_name_fits = collecting && checksum == short_name_checksum(raw);
            if (long_name_fits)
            {
                copy_name(entry->name, long_name);
            }
            else
            {
                char short_name[13];
                format_short_name(raw, short_name);
                copy_name(entry->name, short_name);
            }

            found_first = long_name_fits ? group : here;
            found_short = here;

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

/* Walks the path one component at a time; the entry of the last one comes back in `entry`. Only
 * the first `limit` characters count, which lets a caller resolve the directory part of a path
 * without copying it out first. */
static bool_t resolve_within(const char *path, uint32_t limit, fat32_entry_t *entry)
{
    if (!fs.mounted || path == NULL || path[0] != '/')
        return false;

    entry->first_cluster = fs.root_cluster;
    entry->size = 0;
    entry->is_dir = true;
    entry->name[0] = '\0';

    uint32_t i = 1;
    while (i < limit && path[i] != '\0')
    {
        uint32_t length = 0;
        while (i + length < limit && path[i + length] != '\0' && path[i + length] != '/')
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

        // root . n .. stuff
        const bool_t here = names_equal(".", path + i, length);
        const bool_t up = names_equal("..", path + i, length);

        if (here || (up && cluster == fs.root_cluster))
        {
            i += length;
            continue;
        }

        if (!find_in_directory(cluster, path + i, length, entry))
            return false;

        i += length;
    }

    return true;
}

static bool_t resolve(const char *path, fat32_entry_t *entry)
{
    return resolve_within(path, 0xFFFFFFFF, entry);
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

uint32_t fat32_read_at(const char *path, uint32_t offset, void *buf, uint32_t max_bytes)
{
    fat32_entry_t entry;

    if (buf == NULL || max_bytes == 0 || !resolve(path, &entry) || entry.is_dir)
        return 0;

    if (offset >= entry.size)
        return 0;

    uint32_t left = entry.size - offset;
    if (left > max_bytes)
        left = max_bytes;

    const uint32_t cluster_bytes = fs.sectors_per_cluster * BLOCK_SECTOR_SIZE;
    uint32_t cluster = entry.first_cluster;

    for (uint32_t skip = offset / cluster_bytes; skip > 0; skip--)
    {
        if (!cluster_valid(cluster))
            return 0;
        cluster = next_cluster(cluster);
    }

    uint32_t sector = offset % cluster_bytes / BLOCK_SECTOR_SIZE;
    uint32_t at = offset % BLOCK_SECTOR_SIZE; // only the first sector of a read starts part way in
    uint32_t done = 0;

    while (left > 0 && cluster_valid(cluster))
    {
        while (sector < fs.sectors_per_cluster && left > 0)
        {
            uint32_t chunk = BLOCK_SECTOR_SIZE - at;
            if (chunk > left)
                chunk = left;

            if (!read_dir_sector(cluster_lba(cluster) + sector))
                return done;

            memcpy((uint8_t *)buf + done, dir_cache + at, chunk);
            done += chunk;
            left -= chunk;
            at = 0;
            sector++;
        }

        sector = 0;
        cluster = next_cluster(cluster);
    }

    return done;
}

uint32_t fat32_read_file(const char *path, void *buf, uint32_t max_bytes)
{
    return fat32_read_at(path, 0, buf, max_bytes);
}

/* --- writing -------------------------------------------------------------------------------- */

/* Changes one link of the chain in every copy of the FAT. A volume whose copies disagree is one
 * that another system may read differently, or quietly repair. */
static bool_t fat_set(uint32_t cluster, uint32_t value)
{
    const uint32_t offset = cluster * 4;
    const uint32_t lba = fs.fat_lba + offset / BLOCK_SECTOR_SIZE;

    if (!cluster_valid(cluster) || !load_fat_sector(lba))
        return false;

    uint8_t *slot = fat_cache + offset % BLOCK_SECTOR_SIZE;
    put32(slot, (le32(slot) & ~CLUSTER_MASK) | (value & CLUSTER_MASK)); // the top four bits are not ours

    for (uint32_t copy = 0; copy < fs.fat_count; copy++)
        if (!block_write(lba + copy * fs.fat_size, 1, fat_cache))
            return false;

    return true;
}

// leaves the count and the hint as this driver now has them
static void update_fsinfo(void)
{
    if (fs.fsinfo_lba == 0 || !block_read(fs.fsinfo_lba, 1, dir_cache))
        return;

    dir_cached = false; // dir_cache is scratch here, not a directory sector

    if (le32(dir_cache) != FSINFO_LEAD || le32(dir_cache + 484) != FSINFO_STRUCT)
        return;

    put32(dir_cache + 488, fs.free_count);
    put32(dir_cache + 492, fs.next_free);
    block_write(fs.fsinfo_lba, 1, dir_cache);
}

static bool_t zero_cluster(uint32_t cluster)
{
    memset(dir_cache, 0, BLOCK_SECTOR_SIZE);
    dir_cached = false;

    for (uint32_t sector = 0; sector < fs.sectors_per_cluster; sector++)
        if (!block_write(cluster_lba(cluster) + sector, 1, dir_cache))
            return false;

    return true;
}

/* Takes the first cluster the FAT still lists as free and marks it as the end of a chain, so that
 * a half finished chain never looks free to the next call. */
static uint32_t alloc_cluster(void)
{
    uint32_t cluster = fs.next_free < 2 ? 2 : fs.next_free;

    for (uint32_t tried = 0; tried < fs.cluster_count; tried++, cluster++)
    {
        if (!cluster_valid(cluster))
            cluster = 2; // the search wraps, because clusters are freed behind it as well

        if (next_cluster(cluster) == 0 && fat_set(cluster, CLUSTER_LAST))
        {
            fs.next_free = cluster + 1;
            if (fs.free_count > 0)
                fs.free_count--;
            return cluster;
        }
    }

    return 0;
}

static void free_chain(uint32_t cluster)
{
    while (cluster_valid(cluster))
    {
        const uint32_t next = next_cluster(cluster);

        if (!fat_set(cluster, 0))
            return;

        if (cluster < fs.next_free)
            fs.next_free = cluster;
        fs.free_count++;

        cluster = next;
    }
}

// moves a cursor one record on; past the last record of the chain it goes invalid
static void advance_slot(fat32_dir_t *slot)
{
    if (++slot->index < ENTRIES_PER_SECTOR)
        return;

    slot->index = 0;
    if (++slot->sector < fs.sectors_per_cluster)
        return;

    slot->sector = 0;
    slot->cluster = next_cluster(slot->cluster);
}

// writes one 32-byte record where the cursor stands, and moves the cursor past it
static bool_t write_record(fat32_dir_t *slot, const uint8_t *record)
{
    if (!cluster_valid(slot->cluster) || !read_dir_sector(cluster_lba(slot->cluster) + slot->sector))
        return false;

    memcpy(dir_cache + slot->index * DIR_ENTRY_SIZE, record, DIR_ENTRY_SIZE);
    if (!block_write(dir_cache_lba, 1, dir_cache))
        return false;

    advance_slot(slot);
    return true;
}

/* Directories grow one cluster at a time, and a fresh one has to be all zeroes: a zero first byte
 * is what tells a reader that the directory ends here. */
static uint32_t grow_directory(uint32_t last_cluster)
{
    const uint32_t cluster = alloc_cluster();

    if (cluster == 0 || !zero_cluster(cluster) || !fat_set(last_cluster, cluster))
        return 0;

    return cluster;
}

/* Finds a run of `needed` records nobody uses, growing the directory when it has no such run.
 * Deleted records count as free, which is how the space of removed files comes back. */
static bool_t find_free_slots(uint32_t dir_cluster, uint32_t needed, fat32_dir_t *slot)
{
    fat32_dir_t cursor = {dir_cluster, 0, 0};
    fat32_dir_t start = cursor;
    uint32_t run = 0;

    while (cluster_valid(cursor.cluster))
    {
        if (cursor.sector >= fs.sectors_per_cluster)
        {
            uint32_t next = next_cluster(cursor.cluster);
            if (!cluster_valid(next))
                next = grow_directory(cursor.cluster); // 0 when the volume is full

            cursor.cluster = next;
            cursor.sector = 0;
            cursor.index = 0;
            continue;
        }

        if (!read_dir_sector(cluster_lba(cursor.cluster) + cursor.sector))
            return false;

        while (cursor.index < ENTRIES_PER_SECTOR)
        {
            const uint8_t first = dir_cache[cursor.index * DIR_ENTRY_SIZE];

            if (first != ENTRY_FREE && first != ENTRY_END)
                run = 0;
            else if (run++ == 0)
                start = cursor;

            cursor.index++;

            if (run == needed)
            {
                *slot = start;
                return true;
            }
        }

        cursor.index = 0;
        cursor.sector++;
    }

    return false;
}

// the characters a short name may hold, on top of the digits and the capital letters
static bool_t short_char_ok(char c)
{
    const char *extra = "_-~!#$%&@^(){}'";

    if ((c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'))
        return true;

    for (uint32_t i = 0; extra[i] != '\0'; i++)
        if (c == extra[i])
            return true;

    return false;
}

// index of the last dot, or the length of the name when it has none
static uint32_t last_dot(const char *name)
{
    uint32_t dot = strlen(name);

    for (uint32_t i = 0; name[i] != '\0'; i++)
        if (name[i] == '.')
            dot = i;

    return dot;
}

/* Fills the eleven bytes a short entry stores: eight for the name and three for the extension,
 * padded with spaces and without the dot between them. Returns false when the name cannot be kept
 * this way - lower case included, since the short entry has nowhere to remember it - and long name
 * records have to carry it instead. */
static bool_t to_short_name(const char *name, uint8_t *out)
{
    const uint32_t dot = last_dot(name);
    uint32_t ext = 0;

    memset(out, ' ', 11);

    if (dot == 0 || dot > 8)
        return false;

    for (uint32_t i = 0; i < dot; i++)
    {
        if (!short_char_ok(name[i]))
            return false;
        out[i] = (uint8_t)name[i];
    }

    if (name[dot] == '\0')
        return true;

    for (uint32_t i = dot + 1; name[i] != '\0'; i++)
    {
        if (ext == 3 || !short_char_ok(name[i]))
            return false;
        out[8 + ext++] = (uint8_t)name[i];
    }

    return ext > 0; // a name ending in a dot is not one of these either
}

static bool_t short_name_taken(uint32_t dir_cluster, const uint8_t *name)
{
    fat32_dir_t slot = {dir_cluster, 0, 0};

    while (cluster_valid(slot.cluster))
    {
        if (!read_dir_sector(cluster_lba(slot.cluster) + slot.sector))
            return true; // better to give up on the name than to hand out one that may be in use

        const uint8_t *raw = dir_cache + slot.index * DIR_ENTRY_SIZE;

        if (raw[0] == ENTRY_END)
            return false;

        if (raw[0] != ENTRY_FREE && raw[11] != ATTR_LFN)
        {
            uint32_t i = 0;
            while (i < 11 && raw[i] == name[i])
                i++;
            if (i == 11)
                return true;
        }

        advance_slot(&slot);
    }

    return false;
}

/* A long name still needs a short one beside it: that is what a system which does not read long
 * names sees, and what the checksum inside the long records is taken over. The shape of it is a
 * stump of the real name, a tilde and a number that makes it unique in this directory. */
static bool_t make_unique_short_name(uint32_t dir_cluster, const char *name, uint8_t *out)
{
    const uint32_t dot = last_dot(name);
    uint32_t base = 0;
    uint32_t ext = 0;

    memset(out, ' ', 11);

    for (uint32_t i = 0; i < dot && base < 6; i++)
        if (short_char_ok(upper(name[i])))
            out[base++] = (uint8_t)upper(name[i]);

    if (base == 0)
        out[base++] = '_';

    if (name[dot] != '\0')
        for (uint32_t i = dot + 1; name[i] != '\0' && ext < 3; i++)
            if (short_char_ok(upper(name[i])))
                out[8 + ext++] = (uint8_t)upper(name[i]);

    for (uint32_t number = 1; number <= 99; number++)
    {
        const uint32_t digits = number < 10 ? 1 : 2;
        const uint32_t at = base + 1 + digits <= 8 ? base : 8 - 1 - digits;

        out[at] = '~';
        if (digits == 2)
            out[at + 1] = (uint8_t)('0' + number / 10);
        out[at + digits] = (uint8_t)('0' + number % 10);

        if (!short_name_taken(dir_cluster, out))
            return true;
    }

    return false;
}

/* One piece of a long name: thirteen of its characters, the number saying which thirteen, and the
 * checksum that ties the piece to the short entry it belongs to. */
static void build_long_record(uint8_t *record, const char *name, uint32_t length, uint32_t piece,
                              bool_t last, uint8_t checksum)
{
    uint32_t at = (piece - 1) * LFN_CHARS;

    memset(record, 0, DIR_ENTRY_SIZE);
    record[0] = (uint8_t)(piece | (last ? LFN_LAST : 0));
    record[11] = ATTR_LFN;
    record[13] = checksum;

    for (uint32_t i = 0; i < LFN_CHARS; i++, at++)
    {
        uint16_t character = 0xFFFF; // the padding past the end of the name

        if (at < length)
            character = (uint8_t)name[at];
        else if (at == length)
            character = 0x0000; // which starts with a terminator

        put16(record + lfn_offsets[i], character);
    }
}

static void build_short_record(uint8_t *record, const uint8_t *short_name, uint8_t attributes,
                               uint32_t cluster, uint32_t size)
{
    memset(record, 0, DIR_ENTRY_SIZE);
    memcpy(record, short_name, 11);

    record[11] = attributes;
    put16(record + 16, FAT_DATE_UNKNOWN); // created
    put16(record + 18, FAT_DATE_UNKNOWN); // last read
    put16(record + 24, FAT_DATE_UNKNOWN); // last written
    put16(record + 20, (uint16_t)(cluster >> 16));
    put16(record + 26, (uint16_t)cluster);
    put32(record + 28, size);
}

/* Writes the records of a new entry: the pieces of the long name when the name needs them, and
 * then the short entry, which is the order a reader meets them in. */
static bool_t create_entry(uint32_t dir_cluster, const char *name, uint8_t attributes,
                           uint32_t cluster, uint32_t size)
{
    const uint32_t length = strlen(name);
    uint8_t short_name[11];
    uint8_t record[DIR_ENTRY_SIZE];
    uint32_t pieces = 0;
    fat32_dir_t slot;

    if (length == 0 || length > LFN_MAX_SEQ * LFN_CHARS - 5)
        return false;

    if (!to_short_name(name, short_name))
    {
        if (!make_unique_short_name(dir_cluster, name, short_name))
            return false;
        pieces = (length + LFN_CHARS - 1) / LFN_CHARS;
    }

    if (!find_free_slots(dir_cluster, pieces + 1, &slot))
        return false;

    const uint8_t checksum = short_name_checksum(short_name);

    for (uint32_t piece = pieces; piece >= 1; piece--)
    {
        build_long_record(record, name, length, piece, piece == pieces, checksum);
        if (!write_record(&slot, record))
            return false;
    }

    build_short_record(record, short_name, attributes, cluster, size);
    return write_record(&slot, record);
}

// points an entry that is already there at other contents
static bool_t update_short_record(const fat32_dir_t *slot, uint32_t cluster, uint32_t size)
{
    if (!read_dir_sector(cluster_lba(slot->cluster) + slot->sector))
        return false;

    uint8_t *raw = dir_cache + slot->index * DIR_ENTRY_SIZE;
    put16(raw + 20, (uint16_t)(cluster >> 16));
    put16(raw + 26, (uint16_t)cluster);
    put32(raw + 28, size);
    put16(raw + 24, FAT_DATE_UNKNOWN);

    return block_write(dir_cache_lba, 1, dir_cache);
}

/* Marks every record of an entry free, from the first piece of its long name to the short entry.
 * Nothing else about them changes: a deleted entry keeps its name bar the first character. */
static bool_t free_records(fat32_dir_t first, fat32_dir_t last)
{
    for (;;)
    {
        if (!cluster_valid(first.cluster) || !read_dir_sector(cluster_lba(first.cluster) + first.sector))
            return false;

        dir_cache[first.index * DIR_ENTRY_SIZE] = ENTRY_FREE;
        if (!block_write(dir_cache_lba, 1, dir_cache))
            return false;

        if (first.cluster == last.cluster && first.sector == last.sector && first.index == last.index)
            return true;

        advance_slot(&first);
    }
}

/* Puts the bytes into a chain of fresh clusters and reports the first of them, which is zero for a
 * file with nothing in it. The chain is built before anything points at it, so a failure in the
 * middle costs clusters rather than the file that used to be there. */
static bool_t write_chain(const uint8_t *data, uint32_t size, uint32_t *first_cluster)
{
    uint32_t first = 0;
    uint32_t last = 0;
    uint32_t done = 0;

    while (done < size)
    {
        const uint32_t cluster = alloc_cluster();

        if (cluster == 0 || (last != 0 && !fat_set(last, cluster)))
        {
            free_chain(first);
            return false;
        }

        if (first == 0)
            first = cluster;
        last = cluster;

        for (uint32_t sector = 0; sector < fs.sectors_per_cluster && done < size; sector++)
        {
            const uint32_t left = size - done;
            const uint8_t *source = data + done;
            uint32_t chunk = BLOCK_SECTOR_SIZE;

            if (left < BLOCK_SECTOR_SIZE) // the tail of the file shares its sector with padding
            {
                memset(dir_cache, 0, BLOCK_SECTOR_SIZE);
                memcpy(dir_cache, source, left);
                dir_cached = false;
                source = dir_cache;
                chunk = left;
            }

            if (!block_write(cluster_lba(cluster) + sector, 1, source))
            {
                free_chain(first);
                return false;
            }

            done += chunk;
        }
    }

    *first_cluster = first;
    return true;
}

/* Splits a path into the directory that holds the last component and the component itself, which
 * is the pair every call that changes something works with. */
static bool_t split_path(const char *path, uint32_t *dir_cluster, const char **name)
{
    fat32_entry_t parent;
    uint32_t last = 0;

    if (!fs.mounted || path == NULL || path[0] != '/')
        return false;

    for (uint32_t i = 0; path[i] != '\0'; i++)
        if (path[i] == '/')
            last = i;

    *name = path + last + 1;

    if ((*name)[0] == '\0' || strlen(*name) > FAT32_MAX_NAME - 1)
        return false; // a path ending in a slash names no entry to work on

    if (names_equal(*name, ".", 1) || names_equal(*name, "..", 2))
        return false; // neither of these is an entry a caller may touch

    if (!resolve_within(path, last + 1, &parent) || !parent.is_dir)
        return false;

    *dir_cluster = parent.first_cluster == 0 ? fs.root_cluster : parent.first_cluster;
    return true;
}

// a directory is empty when it holds nothing but the two entries every directory starts with
static bool_t directory_is_empty(uint32_t cluster)
{
    fat32_entry_t entry;
    fat32_dir_t dir;

    open_cluster(&dir, cluster);

    while (fat32_next_entry(&dir, &entry))
        if (!names_equal(entry.name, ".", 1) && !names_equal(entry.name, "..", 2))
            return false;

    return true;
}

uint32_t fat32_write_file(const char *path, const void *buf, uint32_t size)
{
    fat32_entry_t entry;
    const char *name;
    uint32_t dir_cluster;
    uint32_t first = 0;
    bool_t written;

    if ((buf == NULL && size != 0) || !split_path(path, &dir_cluster, &name))
        return 0;

    const bool_t exists = find_in_directory(dir_cluster, name, strlen(name), &entry);
    const fat32_dir_t slot = found_short; // only means anything right after the search above

    if (exists && entry.is_dir)
        return 0;

    if (size != 0 && !write_chain(buf, size, &first))
        return 0;

    /* The entry starts pointing at the new chain before the old one is let go, so that a failure
     * in between leaves clusters behind rather than a file pointing at free space. */
    if (exists)
    {
        written = update_short_record(&slot, first, size);
        if (written)
            free_chain(entry.first_cluster);
    }
    else
    {
        written = create_entry(dir_cluster, name, ATTR_ARCHIVE, first, size);
    }

    if (!written)
    {
        free_chain(first);
        return 0;
    }

    update_fsinfo();
    return size;
}

bool_t fat32_mkdir(const char *path)
{
    fat32_entry_t entry;
    const char *name;
    uint32_t parent;
    uint8_t record[DIR_ENTRY_SIZE];
    uint8_t dot_name[11];

    if (!split_path(path, &parent, &name))
        return false;

    if (find_in_directory(parent, name, strlen(name), &entry))
        return false;

    const uint32_t cluster = alloc_cluster();
    if (cluster == 0 || !zero_cluster(cluster))
        return false;

    /* Every directory but the root opens with two entries of its own: "." for itself and ".." for
     * the directory above, where the root is written as cluster zero rather than its real number. */
    fat32_dir_t slot = {cluster, 0, 0};

    memset(dot_name, ' ', 11);
    dot_name[0] = '.';
    build_short_record(record, dot_name, ATTR_DIRECTORY, cluster, 0);
    if (!write_record(&slot, record))
        return false;

    dot_name[1] = '.';
    build_short_record(record, dot_name, ATTR_DIRECTORY, parent == fs.root_cluster ? 0 : parent, 0);
    if (!write_record(&slot, record))
        return false;

    if (!create_entry(parent, name, ATTR_DIRECTORY, cluster, 0))
    {
        free_chain(cluster);
        return false;
    }

    update_fsinfo();
    return true;
}

bool_t fat32_remove(const char *path)
{
    fat32_entry_t entry;
    const char *name;
    uint32_t dir_cluster;

    if (!split_path(path, &dir_cluster, &name))
        return false;

    if (!find_in_directory(dir_cluster, name, strlen(name), &entry))
        return false;

    const fat32_dir_t first = found_first; // both only mean anything right after the search above
    const fat32_dir_t last = found_short;

    if (entry.is_dir && !directory_is_empty(entry.first_cluster))
        return false;

    if (!free_records(first, last))
        return false;

    free_chain(entry.first_cluster);
    update_fsinfo();
    return true;
}

/* Tells a directory which one is above it now. The pair every directory starts with is written by
 * whoever created it, and ".." is the only part of it that a move has to correct. */
static bool_t set_parent(uint32_t dir_cluster, uint32_t parent)
{
    if (!read_dir_sector(cluster_lba(dir_cluster)))
        return false;

    for (uint32_t index = 0; index < ENTRIES_PER_SECTOR; index++)
    {
        uint8_t *raw = dir_cache + index * DIR_ENTRY_SIZE;

        if (raw[0] != '.' || raw[1] != '.')
            continue;

        const uint32_t cluster = parent == fs.root_cluster ? 0 : parent; // the root is written as zero
        put16(raw + 20, (uint16_t)(cluster >> 16));
        put16(raw + 26, (uint16_t)cluster);
        return block_write(dir_cache_lba, 1, dir_cache);
    }

    return false;
}

/* Walks from a directory up to the root looking for one particular cluster on the way. A directory
 * moved inside its own subtree would hang off nothing and take the subtree with it. */
static bool_t outside_subtree(uint32_t cluster, uint32_t forbidden)
{
    fat32_entry_t up;

    for (uint32_t depth = 0; depth < 64; depth++)
    {
        if (cluster == forbidden)
            return false;

        if (cluster == fs.root_cluster)
            return true;

        if (!find_in_directory(cluster, "..", 2, &up))
            return false;

        cluster = up.first_cluster == 0 ? fs.root_cluster : up.first_cluster;
    }

    return false; // a chain this deep is a volume that lies about its own shape
}

bool_t fat32_rename(const char *from, const char *to)
{
    fat32_entry_t entry;
    fat32_entry_t target;
    const char *from_name;
    const char *to_name;
    uint32_t from_dir;
    uint32_t to_dir;

    if (!split_path(from, &from_dir, &from_name) || !split_path(to, &to_dir, &to_name))
        return false;

    if (!find_in_directory(from_dir, from_name, strlen(from_name), &entry))
        return false;

    const fat32_dir_t first = found_first; // both only mean anything right after the search above
    const fat32_dir_t last = found_short;

    bool_t replacing = find_in_directory(to_dir, to_name, strlen(to_name), &target);
    const fat32_dir_t target_first = found_first;
    const fat32_dir_t target_last = found_short;

    if (replacing && target_last.cluster == last.cluster && target_last.sector == last.sector &&
        target_last.index == last.index)
        replacing = false;

    if (replacing)
    {
        // what takes the place of what still has to make sense
        if (target.is_dir != entry.is_dir)
            return false;

        if (target.is_dir && !directory_is_empty(target.first_cluster))
            return false;
    }

    if (entry.is_dir && !outside_subtree(to_dir, entry.first_cluster))
        return false;

    if (replacing)
    {
        if (!free_records(target_first, target_last))
            return false;

        free_chain(target.first_cluster);
    }

    if (!create_entry(to_dir, to_name, entry.is_dir ? ATTR_DIRECTORY : ATTR_ARCHIVE, entry.first_cluster,
                      entry.size))
        return false;

    if (!free_records(first, last))
        return false;

    if (entry.is_dir && from_dir != to_dir && !set_parent(entry.first_cluster, to_dir))
        return false;

    update_fsinfo();
    return true;
}
