#include "shell.h"

#include <drivers/screen.h>
#include <drivers/keyboard.h>
#include <kernel/block.h>
#include <kernel/fat32.h>
#include <kernel/memory.h>
#include <lib/string.h>

#include "../atto/atto.h"

#define LINE_MAX 128
#define PATH_MAX 256
#define ARGS_MAX 8
#define TREE_DEPTH_MAX 8  // every level of a recursive delete costs a directory entry on the stack
#define GLOB_ARGS_MAX 32  // one pattern can stand for many names, so the expanded line is longer
#define GLOB_POOL 2048    // and all of them together have to fit in here
#define GLOB_DEPTH_MAX 8

static char cwd[PATH_MAX] = "/";
static char tree_path[PATH_MAX]; // rm -r walks with this one instead of a buffer per level
static bool_t running;

static void out_dec(uint32_t value, uint32_t width)
{
    char buf[12];

    uint_to_str(value, buf);

    for (uint32_t i = strlen(buf); i < width; i++)
        print_char(' ');

    print(buf);
}

static void fail(const char *what, const char *why)
{
    print(what);
    print(": ");
    print(why);
    print_char('\n');
}

static bool_t mounted(const char *what)
{
    if (fat32_mounted())
        return true;

    fail(what, "nothing is mounted");
    return false;
}

/* Takes the slashes, the dots and the double dots out of a path and makes it absolute, so that
 * what is shown in the prompt is the plain name of where the shell stands. */
static void normalize(char *path)
{
    char result[PATH_MAX];
    uint32_t length = 1;
    uint32_t i = 0;

    result[0] = '/';

    while (path[i] != '\0')
    {
        while (path[i] == '/')
            i++;

        const uint32_t start = i;
        while (path[i] != '\0' && path[i] != '/')
            i++;

        const uint32_t piece = i - start;

        if (piece == 0)
            break;

        if (piece == 1 && path[start] == '.')
            continue;

        if (piece == 2 && path[start] == '.' && path[start + 1] == '.')
        {
            while (length > 1 && result[length - 1] != '/')
                length--;
            if (length > 1)
                length--; // and the slash that led to it
            continue;
        }

        if (length > 1)
            result[length++] = '/';

        for (uint32_t k = 0; k < piece && length < PATH_MAX - 2; k++)
            result[length++] = path[start + k];
    }

    result[length] = '\0';
    strcpy(path, result);
}

// an argument is read against the current directory unless it starts at the root itself
static void make_path(char *out_path, const char *argument)
{
    if (argument[0] == '/')
    {
        strncpy(out_path, argument, PATH_MAX - 1);
        out_path[PATH_MAX - 1] = '\0';
    }
    else
    {
        strcpy(out_path, cwd);
        if (strlen(out_path) > 1)
            strcat(out_path, "/");
        strcat(out_path, argument);
    }

    normalize(out_path);
}

/* --- patterns ------------------------------------------------------------------------------- */

static char *glob_argv[GLOB_ARGS_MAX];
static char glob_pool[GLOB_POOL];
static uint32_t glob_count;
static uint32_t glob_used;
static char glob_path[PATH_MAX]; // the path built so far, shaped the way the pattern was written

static char upper(char c)
{
    return c >= 'a' && c <= 'z' ? (char)(c - 'a' + 'A') : c;
}

static bool_t has_star(const char *text, uint32_t length)
{
    for (uint32_t i = 0; i < length && text[i] != '\0'; i++)
        if (text[i] == '*')
            return true;

    return false;
}

/* Matches one name against one piece of a pattern, where a star stands for any run of characters
 * inside that name. Walking back to the last star is what lets "a*b*c" try every way of cutting
 * the name up between its stars instead of giving up at the first one that does not fit. */
static bool_t match_piece(const char *pattern, uint32_t length, const char *name)
{
    uint32_t p = 0;
    uint32_t n = 0;
    uint32_t star = length + 1; // nothing above length is a position, so this means "no star yet"
    uint32_t after_star = 0;

    while (name[n] != '\0')
    {
        if (p < length && pattern[p] == '*')
        {
            star = p++;
            after_star = n;
        }
        else if (p < length && upper(pattern[p]) == upper(name[n]))
        {
            p++;
            n++;
        }
        else if (star <= length)
        {
            p = star + 1;
            n = ++after_star;
        }
        else
        {
            return false;
        }
    }

    while (p < length && pattern[p] == '*')
        p++;

    return p == length;
}

static void glob_emit(const char *path)
{
    const uint32_t size = strlen(path) + 1;

    if (glob_count == GLOB_ARGS_MAX || glob_used + size > GLOB_POOL)
        return; // more names than one line can carry: the rest are left out rather than half shown

    strcpy(glob_pool + glob_used, path);
    glob_argv[glob_count++] = glob_pool + glob_used;
    glob_used += size;
}

static void glob_append(const char *name, uint32_t length)
{
    uint32_t at = strlen(glob_path);

    if (at + length + 2 >= PATH_MAX)
        return;

    if (at > 0 && glob_path[at - 1] != '/')
        glob_path[at++] = '/';

    for (uint32_t i = 0; i < length; i++)
        glob_path[at++] = name[i];

    glob_path[at] = '\0';
}

/* Walks what is left of the pattern against the directory reached so far. Names are collected in
 * the shape the pattern had, absolute or not, so that what comes out can be typed straight back. */
static void glob_walk(const char *pattern, uint32_t depth)
{
    fat32_entry_t entry;
    fat32_dir_t dir;
    char full[PATH_MAX];

    if (depth > GLOB_DEPTH_MAX)
        return;

    while (*pattern == '/')
        pattern++;

    const uint32_t base = strlen(glob_path);

    if (*pattern == '\0') // the pattern ran out, so whatever it led to is a match
    {
        make_path(full, glob_path);
        if (fat32_stat(full, &entry))
            glob_emit(glob_path);
        return;
    }

    uint32_t length = 0;
    while (pattern[length] != '\0' && pattern[length] != '/')
        length++;

    const char *rest = pattern + length;

    if (!has_star(pattern, length)) // a plain name: step into it and carry on
    {
        glob_append(pattern, length);
        glob_walk(rest, depth + 1);
        glob_path[base] = '\0';
        return;
    }

    const bool_t deep = length == 2 && pattern[0] == '*' && pattern[1] == '*';

    if (deep)
    {
        if (*rest == '\0')
            rest = "/*"; // "**" by itself means everything underneath, which is what "**/*" says

        glob_walk(rest, depth + 1); // a double star stands for no directory at all as well
    }

    make_path(full, base == 0 ? "." : glob_path);

    if (!fat32_open_dir(full, &dir))
        return;

    while (fat32_next_entry(&dir, &entry))
    {
        if (entry.name[0] == '.')
            continue; // "." and ".." would send this in circles, and hidden names stay hidden

        if (deep && !entry.is_dir)
            continue;

        if (!deep && !match_piece(pattern, length, entry.name))
            continue;

        glob_append(entry.name, strlen(entry.name));
        glob_walk(deep ? pattern : rest, depth + 1); // a double star stays, to reach deeper still
        glob_path[base] = '\0';
    }
}

/* Puts every name a pattern stands for in place of the pattern. One that matches nothing is left
 * as it was, so the command can complain about the name that was actually typed. */
static uint32_t expand_patterns(uint32_t argc, char **argv)
{
    glob_count = 0;
    glob_used = 0;

    for (uint32_t i = 0; i < argc; i++)
    {
        const uint32_t before = glob_count;

        if (i == 0 || !fat32_mounted() || !has_star(argv[i], strlen(argv[i])))
        {
            glob_emit(argv[i]);
            continue;
        }

        glob_path[0] = '\0';
        if (argv[i][0] == '/')
            strcpy(glob_path, "/");

        glob_walk(argv[i], 0);

        if (glob_count == before)
            glob_emit(argv[i]);
    }

    return glob_count;
}

/* --- commands ------------------------------------------------------------------------------- */

static bool_t is_flag(const char *argument)
{
    return argument[0] == '-' && argument[1] != '\0';
}

static uint32_t operand_count(uint32_t argc, char **argv)
{
    uint32_t count = 0;

    for (uint32_t i = 1; i < argc; i++)
        if (!is_flag(argv[i]))
            count++;

    return count;
}

// the n-th name on the line, flags left out of the count, or NULL when the line is shorter
static const char *operand(uint32_t argc, char **argv, uint32_t index)
{
    for (uint32_t i = 1; i < argc; i++)
        if (!is_flag(argv[i]) && index-- == 0)
            return argv[i];

    return NULL;
}

static void cmd_help(uint32_t argc, char **argv);

static void cmd_clear(uint32_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    clear_screen();
}

static void cmd_exit(uint32_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    running = false;
}

static void cmd_pwd(uint32_t argc, char **argv)
{
    (void)argc;
    (void)argv;
    print(cwd);
    print_char('\n');
}

static void list_directory(const char *path, bool_t all)
{
    fat32_entry_t entry;
    fat32_dir_t dir;
    uint32_t files = 0;
    uint32_t dirs = 0;
    uint32_t bytes = 0;

    if (!fat32_open_dir(path, &dir))
    {
        fail("ls", "no such directory");
        return;
    }

    while (fat32_next_entry(&dir, &entry))
    {
        if (!all && entry.name[0] == '.')
            continue;

        if (entry.is_dir)
        {
            print("     <DIR>  ");
            dirs++;
        }
        else
        {
            out_dec(entry.size, 10);
            print("  ");
            files++;
            bytes += entry.size;
        }

        print(entry.name);
        print_char('\n');
    }

    out_dec(files, 0);
    print(" files, ");
    out_dec(dirs, 0);
    print(" directories, ");
    out_dec(bytes, 0);
    print(" bytes\n");
}

static void cmd_ls(uint32_t argc, char **argv)
{
    char path[PATH_MAX];
    fat32_entry_t entry;
    bool_t all = false;

    if (!mounted("ls"))
        return;

    for (uint32_t i = 1; i < argc; i++)
        if (strcmp(argv[i], "-a") == 0)
            all = true;

    const uint32_t names = operand_count(argc, argv);

    if (names == 0)
    {
        make_path(path, ".");
        list_directory(path, all);
        return;
    }

    for (uint32_t i = 1; i < argc; i++)
    {
        if (is_flag(argv[i]))
            continue;

        make_path(path, argv[i]);

        if (!fat32_stat(path, &entry))
        {
            fail(argv[i], "no such name");
            continue;
        }

        if (!entry.is_dir) // a name that is a file stands for itself, the way a pattern leaves it
        {
            out_dec(entry.size, 10);
            print("  ");
            print(argv[i]);
            print_char('\n');
            continue;
        }

        if (names > 1) // say which directory the lines below belong to
        {
            print(argv[i]);
            print(":\n");
        }

        list_directory(path, all);
    }
}

static void cmd_cd(uint32_t argc, char **argv)
{
    char path[PATH_MAX];
    fat32_entry_t entry;

    if (!mounted("cd"))
        return;

    make_path(path, argc > 1 ? argv[1] : "/");

    if (!fat32_stat(path, &entry) || !entry.is_dir)
    {
        fail("cd", "no such directory");
        return;
    }

    strcpy(cwd, path);
}

static void cat_one(const char *name)
{
    char path[PATH_MAX];
    char buf[512];
    fat32_entry_t entry;
    uint32_t done = 0;

    make_path(path, name);

    if (!fat32_stat(path, &entry) || entry.is_dir)
    {
        fail(name, "no such file");
        return;
    }

    // read in pieces, so that showing a file never asks for as much memory as the file is long
    while (done < entry.size)
    {
        const uint32_t got = fat32_read_at(path, done, buf, sizeof(buf));

        if (got == 0)
            break;

        for (uint32_t i = 0; i < got; i++)
            print_char(buf[i]);

        done += got;
    }

    if (done != 0 && buf[(done - 1) % sizeof(buf)] != '\n')
        print_char('\n');
}

static void cmd_cat(uint32_t argc, char **argv)
{
    if (!mounted("cat"))
        return;

    if (operand_count(argc, argv) == 0)
    {
        fail("cat", "needs a file");
        return;
    }

    for (uint32_t i = 1; i < argc; i++)
        if (!is_flag(argv[i]))
            cat_one(argv[i]);
}

static void cmd_touch(uint32_t argc, char **argv)
{
    char path[PATH_MAX];
    fat32_entry_t entry;

    if (!mounted("touch"))
        return;

    if (operand_count(argc, argv) == 0)
    {
        fail("touch", "needs a name");
        return;
    }

    for (uint32_t i = 1; i < argc; i++)
    {
        if (is_flag(argv[i]))
            continue;

        make_path(path, argv[i]);

        if (fat32_stat(path, &entry))
            continue; // it is already there, and there is no clock to bring its date forward

        if (fat32_write_file(path, NULL, 0) != 0 || !fat32_stat(path, &entry))
            fail(argv[i], "could not create it");
    }
}

static void cmd_mkdir(uint32_t argc, char **argv)
{
    char path[PATH_MAX];

    if (!mounted("mkdir"))
        return;

    if (operand_count(argc, argv) == 0)
    {
        fail("mkdir", "needs a name");
        return;
    }

    for (uint32_t i = 1; i < argc; i++)
    {
        if (is_flag(argv[i]))
            continue;

        make_path(path, argv[i]);

        if (!fat32_mkdir(path))
            fail(argv[i], "could not create it");
    }
}

// walks what tree_path names, deleting from the bottom up
static bool_t remove_tree(uint32_t depth)
{
    fat32_entry_t entry;
    fat32_dir_t dir;

    if (depth > TREE_DEPTH_MAX || !fat32_stat(tree_path, &entry))
        return false;

    if (!entry.is_dir)
        return fat32_remove(tree_path);

    if (!fat32_open_dir(tree_path, &dir))
        return false;

    const uint32_t base = strlen(tree_path);

    while (fat32_next_entry(&dir, &entry))
    {
        if (strcmp(entry.name, ".") == 0 || strcmp(entry.name, "..") == 0)
            continue;

        if (base + strlen(entry.name) + 2 >= PATH_MAX)
            return false;

        tree_path[base] = '\0';
        if (base > 1)
            strcat(tree_path, "/");
        strcat(tree_path, entry.name);

        if (!remove_tree(depth + 1))
            return false;
    }

    tree_path[base] = '\0';
    return fat32_remove(tree_path);
}

static void cmd_rm(uint32_t argc, char **argv)
{
    char path[PATH_MAX];
    bool_t recursive = false;

    if (!mounted("rm"))
        return;

    for (uint32_t i = 1; i < argc; i++)
        if (strcmp(argv[i], "-r") == 0)
            recursive = true;

    if (operand_count(argc, argv) == 0)
    {
        fail("rm", "needs a name");
        return;
    }

    for (uint32_t i = 1; i < argc; i++)
    {
        if (is_flag(argv[i]))
            continue;

        make_path(path, argv[i]);

        if (strcmp(path, "/") == 0)
        {
            fail("rm", "not the root");
            continue;
        }

        if (recursive)
        {
            strcpy(tree_path, path);
            if (!remove_tree(0))
                fail(argv[i], "could not delete all of it");
            continue;
        }

        if (!fat32_remove(path))
            fail(argv[i], "could not delete it, a directory has to be empty or given -r");
    }
}

static void cmd_mv(uint32_t argc, char **argv)
{
    char from[PATH_MAX];
    char to[PATH_MAX];

    if (!mounted("mv"))
        return;

    if (operand_count(argc, argv) != 2)
    {
        fail("mv", "takes exactly two names");
        return;
    }

    make_path(from, operand(argc, argv, 0));
    make_path(to, operand(argc, argv, 1));

    if (!fat32_rename(from, to))
        fail("mv", "could not move it");
}

static void cmd_cp(uint32_t argc, char **argv)
{
    char from[PATH_MAX];
    char to[PATH_MAX];
    fat32_entry_t entry;

    if (!mounted("cp"))
        return;

    if (operand_count(argc, argv) != 2)
    {
        fail("cp", "takes exactly two names");
        return;
    }

    make_path(from, operand(argc, argv, 0));
    make_path(to, operand(argc, argv, 1));

    if (!fat32_stat(from, &entry) || entry.is_dir)
    {
        fail("cp", "no such file");
        return;
    }

    // a copy goes through memory, so what can be copied is what the heap can hold at once
    uint8_t *buf = malloc(entry.size != 0 ? entry.size : 1);

    if (buf == NULL)
    {
        fail("cp", "no memory for a file that size");
        return;
    }

    if (fat32_read_file(from, buf, entry.size) != entry.size ||
        fat32_write_file(to, buf, entry.size) != entry.size)
        fail("cp", "could not copy it");

    free(buf);
}

static uint32_t to_kib(uint32_t clusters, uint32_t cluster_size)
{
    return cluster_size >= 1024 ? clusters * (cluster_size / 1024) : clusters / (1024 / cluster_size);
}

static void cmd_df(uint32_t argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (!mounted("df"))
        return;

    const uint32_t size = fat32_cluster_size();
    const uint32_t total = fat32_total_clusters();
    const uint32_t free_clusters = fat32_free_clusters();

    print("volume on ");
    print(fat32_device()->name);
    print("\n");
    out_dec(to_kib(total, size), 10);
    print(" KiB total\n");
    out_dec(to_kib(total - free_clusters, size), 10);
    print(" KiB used\n");
    out_dec(to_kib(free_clusters, size), 10);
    print(" KiB free, in clusters of ");
    out_dec(size, 0);
    print(" bytes\n");
}

static void cmd_lsblk(uint32_t argc, char **argv)
{
    (void)argc;
    (void)argv;

    print("NAME         SECTORS    KiB  MOUNTED\n");

    for (uint32_t i = 0; i < block_count(); i++)
    {
        const block_device_t *device = block_at(i);

        print(device->name);
        for (uint32_t pad = strlen(device->name); pad < 12; pad++)
            print_char(' ');

        out_dec(device->sectors, 8);
        out_dec(device->sectors / 2, 7);
        print("  ");
        print(device == fat32_device() ? "yes" : "no");
        print(device->write == NULL ? "  (read only)\n" : "\n");
    }

    if (block_count() == 0)
        print("no block devices\n");
}

static void cmd_mount(uint32_t argc, char **argv)
{
    if (argc < 2)
    {
        if (fat32_device() == NULL)
            print("nothing is mounted\n");
        else
        {
            print(fat32_device()->name);
            print(" on /\n");
        }
        return;
    }

    const block_device_t *device = block_find(argv[1]);

    if (device == NULL)
    {
        fail("mount", "no such device, try lsblk");
        return;
    }

    if (!fat32_mount_device(device))
    {
        fail("mount", "no FAT32 volume on it");
        return;
    }

    strcpy(cwd, "/");
}

static void cmd_umount(uint32_t argc, char **argv)
{
    (void)argc;
    (void)argv;

    if (fat32_device() == NULL)
    {
        fail("umount", "nothing is mounted");
        return;
    }

    fat32_unmount();
    strcpy(cwd, "/");
}

static void cmd_atto(uint32_t argc, char **argv)
{
    char path[PATH_MAX];

    if (!mounted("atto"))
        return;

    if (argc < 2)
    {
        fail("atto", "needs a file");
        return;
    }

    make_path(path, argv[1]);
    atto_main(path);
}

typedef struct
{
    const char *name;
    void (*run)(uint32_t argc, char **argv);
    const char *usage;
} command_t;

static const command_t commands[] = {
    {"help", cmd_help, "help                  this list"},
    {"ls", cmd_ls, "ls [-a] [path]        what is in a directory"},
    {"cd", cmd_cd, "cd [path]             go to a directory"},
    {"pwd", cmd_pwd, "pwd                   where you are"},
    {"cat", cmd_cat, "cat <file>            show a file"},
    {"atto", cmd_atto, "atto <file>           edit a file"},
    {"touch", cmd_touch, "touch <file>          make an empty file"},
    {"mkdir", cmd_mkdir, "mkdir <name>          make a directory"},
    {"rm", cmd_rm, "rm [-r] <path>        delete"},
    {"mv", cmd_mv, "mv <from> <to>        move or rename"},
    {"cp", cmd_cp, "cp <from> <to>        copy a file"},
    {"df", cmd_df, "df                    space on the volume"},
    {"lsblk", cmd_lsblk, "lsblk                 block devices"},
    {"mount", cmd_mount, "mount [device]        mount one of them"},
    {"umount", cmd_umount, "umount                let go of it"},
    {"clear", cmd_clear, "clear                 wipe the screen"},
    {"exit", cmd_exit, "exit                  back to the selector"},
};

#define COMMAND_COUNT (sizeof(commands) / sizeof(command_t))

static void cmd_help(uint32_t argc, char **argv)
{
    (void)argc;
    (void)argv;

    print("names with spaces go in quotes: cat \"Loader Notes.txt\"\n");
    print("* stands for any part of a name, ** for any run of directories: rm **/*.tmp\n");
    print("Up and Down walk back through what was typed\n\n");

    for (uint32_t i = 0; i < COMMAND_COUNT; i++)
    {
        print("  ");
        print(commands[i].usage);
        print_char('\n');
    }
}

/* --- the line being typed --------------------------------------------------------------------- */

#define HISTORY_MAX 16

static char history[HISTORY_MAX][LINE_MAX];
static uint32_t history_count;

static void history_add(const char *line)
{
    if (line[0] == '\0')
        return;

    if (history_count > 0 && strcmp(history[history_count - 1], line) == 0)
        return; // the same line twice over is worth keeping once

    if (history_count == HISTORY_MAX) // the oldest one falls off the end
    {
        for (uint32_t i = 1; i < HISTORY_MAX; i++)
            strcpy(history[i - 1], history[i]);

        history_count--;
    }

    strcpy(history[history_count++], line);
}

// rubs out what stands on the line and puts something else in its place
static void replace_line(char *line, uint32_t *length, const char *with)
{
    while (*length > 0)
    {
        print_char('\b');
        (*length)--;
    }

    strcpy(line, with);
    *length = strlen(line);
    print(line);
}

static void read_line(char *line)
{
    uint32_t length = 0;
    uint32_t browse = history_count; // where the arrows stand, the far end being the new line

    for (;;)
    {
        char c = 0;
        while (!(c = get_keyboard_char()))
            asm volatile("hlt");

        if (c == KEY_ESC)
        {
            running = false;
            line[0] = '\0';
            return;
        }

        if (c == '\n')
        {
            print_char('\n');
            line[length] = '\0';
            history_add(line);
            return;
        }

        if (c == KEY_UP && browse > 0)
        {
            browse--;
            replace_line(line, &length, history[browse]);
            continue;
        }

        if (c == KEY_DOWN && browse < history_count)
        {
            browse++;
            replace_line(line, &length, browse == history_count ? "" : history[browse]);
            continue;
        }

        if (c == '\b')
        {
            if (length > 0)
            {
                length--;
                print_char('\b');
            }
            continue;
        }

        if (c >= ' ' && length + 1 < LINE_MAX)
        {
            line[length++] = c;
            print_char(c);
        }
    }
}

/* Cuts a line into words. A name that holds spaces, which is most of what a long name is for, is
 * written inside quotes and taken as one. */
static uint32_t split(char *line, char **argv)
{
    uint32_t argc = 0;
    uint32_t i = 0;

    while (argc < ARGS_MAX)
    {
        while (line[i] == ' ')
            i++;

        if (line[i] == '\0')
            break;

        char ends_at = ' ';
        if (line[i] == '"')
        {
            ends_at = '"';
            i++;
        }

        argv[argc++] = line + i;

        while (line[i] != '\0' && line[i] != ends_at)
            i++;

        if (line[i] != '\0')
            line[i++] = '\0';
    }

    return argc;
}

void shell_main(void)
{
    char line[LINE_MAX];
    char *argv[ARGS_MAX];

    running = true;
    strcpy(cwd, "/");

    set_vga_cursor_visibility(true);
    clear_screen();
    print("Metabar shell. help lists what it knows, Esc leaves.\n\n");
    history_count = 0;

    while (running)
    {
        print(cwd);
        print("> ");

        read_line(line);

        uint32_t argc = split(line, argv);
        if (argc == 0)
            continue;

        argc = expand_patterns(argc, argv); // from here on the arguments are the expanded ones

        uint32_t i = 0;
        while (i < COMMAND_COUNT && strcmp(commands[i].name, glob_argv[0]) != 0)
            i++;

        if (i == COMMAND_COUNT)
            fail(glob_argv[0], "no such command, try help");
        else
            commands[i].run(argc, glob_argv);
    }
}
