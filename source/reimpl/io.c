/*
 * Copyright (C) 2021      Andy Nguyen
 * Copyright (C) 2022      Rinnegatamante
 * Copyright (C) 2022-2024 Volodymyr Atamanenko
 *
 * This software may be modified and distributed under the terms
 * of the MIT license. See the LICENSE file for details.
 */

#include "reimpl/io.h"
#include "debug_log.h"

#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/unistd.h>
#include <stdlib.h>
#include <dirent.h>
#include <stdarg.h>
#include <errno.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/threadmgr.h>

#ifdef USE_SCELIBC_IO
#include <libc_bridge/libc_bridge.h>
#endif

#include "utils/logger.h"
#include "utils/utils.h"

// Includes the following inline utilities:
// int oflags_musl_to_newlib(int flags);
// dirent64_bionic * dirent_newlib_to_bionic(struct dirent* dirent_newlib);
// void stat_newlib_to_bionic(struct stat * src, stat64_bionic * dst);
#include "reimpl/bits/_struct_converters.c"

#define SOLOADER_URANDOM_FD_BASE 0x6F000000

static uint32_t g_soloader_urandom_counter = 0;
static uint64_t g_soloader_urandom_state = 0;

static int is_soloader_urandom_path(const char *path) {
    return path && strcmp(path, "/dev/urandom") == 0;
}

static int is_soloader_urandom_fd(int fd) {
    return fd >= SOLOADER_URANDOM_FD_BASE;
}

static int is_save_related_path(const char *path) {
    return path &&
           (strstr(path, ".cloud") ||
            strstr(path, "SharedPreferences") ||
            strstr(path, "DarkLandsSecurePrefs"));
}

static uint32_t soloader_urandom_next_u32(void) {
    if (g_soloader_urandom_state == 0) {
        uintptr_t stack_addr = (uintptr_t)&stack_addr;
        g_soloader_urandom_state =
            ((uint64_t)(uint32_t)sceKernelGetProcessTimeLow() << 32) ^
            (uint64_t)(uint32_t)sceKernelGetThreadId() ^
            (uint64_t)stack_addr ^
            0x9E3779B97F4A7C15ull;
    }

    // xorshift64*
    g_soloader_urandom_state ^= g_soloader_urandom_state >> 12;
    g_soloader_urandom_state ^= g_soloader_urandom_state << 25;
    g_soloader_urandom_state ^= g_soloader_urandom_state >> 27;
    return (uint32_t)((g_soloader_urandom_state * 0x2545F4914F6CDD1Dull) >> 32);
}

static void soloader_urandom_fill(void *buf, size_t count) {
    uint8_t *out = (uint8_t *)buf;
    while (count >= sizeof(uint32_t)) {
        uint32_t word = soloader_urandom_next_u32();
        memcpy(out, &word, sizeof(word));
        out += sizeof(word);
        count -= sizeof(word);
    }
    if (count > 0) {
        uint32_t tail = soloader_urandom_next_u32();
        memcpy(out, &tail, count);
    }
}

static int soloader_urandom_build_stat(struct stat *st) {
    if (!st)
        return -1;
    memset(st, 0, sizeof(*st));
    st->st_mode = S_IFCHR | 0444;
    st->st_nlink = 1;
    st->st_blksize = 1;
    return 0;
}

FILE *fopen_soloader(const char *filename, const char *mode)
{
    const char *changed = NULL;

    if (strcmp(filename, "/proc/cpuinfo") == 0)
        changed = "app0:/cpuinfo";
    else if (strcmp(filename, "/proc/meminfo") == 0)
        changed = "app0:/meminfo";
    else
    {
        char *p = strstr(filename, "/data/data/");
        if (p)
        {
            static char real_filename[512];
            snprintf(real_filename, sizeof(real_filename), "ux0:/data/%s", p + strlen("/data/data/"));
            changed = real_filename;
        }
    }

    const char *final_path = changed ? changed : filename;
#ifdef USE_SCELIBC_IO
    FILE *ret = sceLibcBridge_fopen(final_path, mode);
#else
    FILE *ret = fopen(final_path, mode);
#endif

    if (DLA_DEBUG_LOGS && is_save_related_path(final_path))
        DLA_DEBUG_PRINTF("[SAVE][IO] fopen path=\"%s\" mode=\"%s\" result=%p\n",
                         final_path, mode ? mode : "<null>", ret);

    if (ret)
        l_debug("fopen(%s, %s): %p", changed ? changed : filename, mode, ret);
    else
        l_warn("fopen(%s, %s): %p", changed ? changed : filename, mode, ret);

    return ret;
}

int open_soloader(const char *path, int oflag, ...)
{
    if (is_soloader_urandom_path(path))
    {
        int fd = SOLOADER_URANDOM_FD_BASE | ((int)(++g_soloader_urandom_counter) & 0x00FFFFFF);
        if (fd == SOLOADER_URANDOM_FD_BASE)
            fd++;
        return fd;
    }

    if (strcmp(path, "/proc/cpuinfo") == 0)
    {
        return open_soloader("app0:/cpuinfo", oflag);
    }
    else if (strcmp(path, "/proc/meminfo") == 0)
    {
        return open_soloader("app0:/meminfo", oflag);
    }
    mode_t mode = 0666;
    if (((oflag & BIONIC_O_CREAT) == BIONIC_O_CREAT) ||
        ((oflag & BIONIC_O_TMPFILE) == BIONIC_O_TMPFILE))
    {
        va_list args;
        va_start(args, oflag);
        mode = (mode_t)(va_arg(args, int));
        va_end(args);
    }

    int original_oflag = oflag;
    oflag = oflags_bionic_to_newlib(oflag);
    int ret = open(path, oflag, mode);
    if (DLA_DEBUG_LOGS && is_save_related_path(path))
        DLA_DEBUG_PRINTF("[SAVE][IO] open path=\"%s\" flags_in=0x%x flags_out=0x%x mode=0%o result=%d\n",
                         path, original_oflag, oflag, (unsigned)mode, ret);
    if (ret >= 0)
        l_debug("open(%s, %x): %i", path, oflag, ret);
    else
        l_warn("open(%s, %x): %i", path, oflag, ret);
    return ret;
}

int fstat_soloader(int fd, stat64_bionic *buf)
{
    if (is_soloader_urandom_fd(fd))
    {
        if (!buf) {
            errno = EINVAL;
            return -1;
        }
        struct stat st;
        if (soloader_urandom_build_stat(&st) < 0) {
            errno = EINVAL;
            return -1;
        }
        stat_newlib_to_bionic(&st, buf);
        return 0;
    }

    struct stat st;
    int res = fstat(fd, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    l_debug("fstat(%i): %i", fd, res);
    return res;
}

int stat_soloader(const char *path, stat64_bionic *buf)
{
    if (is_soloader_urandom_path(path))
    {
        if (!buf) {
            errno = EINVAL;
            return -1;
        }
        struct stat st;
        if (soloader_urandom_build_stat(&st) < 0) {
            errno = EINVAL;
            return -1;
        }
        stat_newlib_to_bionic(&st, buf);
        return 0;
    }

    struct stat st;
    const char *changed = NULL;
    char *p = strstr(path, "/data/data/");
    if (p)
    {
        static char real_filename[512];
        snprintf(real_filename, sizeof(real_filename), "ux0:/data/%s", p + strlen("/data/data/"));
        changed = real_filename;
    }
    const char *final_path = changed ? changed : path;
    int res = stat(final_path, &st);

    if (res == 0)
        stat_newlib_to_bionic(&st, buf);

    if (DLA_DEBUG_LOGS && is_save_related_path(final_path))
        DLA_DEBUG_PRINTF("[SAVE][IO] stat path=\"%s\" result=%d size=%u\n",
                         final_path, res, res == 0 ? (unsigned)st.st_size : 0u);

    l_debug("stat(%s): %i", final_path, res);
    return res;
}

int fclose_soloader(FILE *f)
{
#ifdef USE_SCELIBC_IO
    int ret = sceLibcBridge_fclose(f);
#else
    int ret = fclose(f);
#endif

    l_debug("fclose(%p): %i", f, ret);
    return ret;
}

int close_soloader(int fd)
{
    if (is_soloader_urandom_fd(fd))
        return 0;

    int ret = close(fd);
    l_debug("close(%i): %i", fd, ret);
    return ret;
}

ssize_t read_soloader(int fd, void *buf, size_t count)
{
    if (is_soloader_urandom_fd(fd))
    {
        if (!buf) {
            errno = EFAULT;
            return -1;
        }
        soloader_urandom_fill(buf, count);
        return (ssize_t)count;
    }

    return read(fd, buf, count);
}

DIR *opendir_soloader(char *_pathname)
{
    DIR *ret = opendir(_pathname);
    l_debug("opendir(\"%s\"): %p", _pathname, ret);
    return ret;
}

struct dirent64_bionic *readdir_soloader(DIR *dir)
{
    static struct dirent64_bionic dirent_tmp;

    struct dirent *ret = readdir(dir);
    l_debug("readdir(%p): %p", dir, ret);

    if (ret)
    {
        dirent64_bionic *entry_tmp = dirent_newlib_to_bionic(ret);
        memcpy(&dirent_tmp, entry_tmp, sizeof(dirent64_bionic));
        free(entry_tmp);
        return &dirent_tmp;
    }

    return NULL;
}

int readdir_r_soloader(DIR *dirp, dirent64_bionic *entry,
                       dirent64_bionic **result)
{
    struct dirent dirent_tmp;
    struct dirent *pdirent_tmp;

    int ret = readdir_r(dirp, &dirent_tmp, &pdirent_tmp);

    if (ret == 0)
    {
        dirent64_bionic *entry_tmp = dirent_newlib_to_bionic(&dirent_tmp);
        memcpy(entry, entry_tmp, sizeof(dirent64_bionic));
        *result = (pdirent_tmp != NULL) ? entry : NULL;
        free(entry_tmp);
    }

    l_debug("readdir_r(%p, %p, %p): %i", dirp, entry, result, ret);
    return ret;
}

int closedir_soloader(DIR *dir)
{
    int ret = closedir(dir);
    l_debug("closedir(%p): %i", dir, ret);
    return ret;
}

int fcntl_soloader(int fd, int cmd, ...)
{
    l_warn("fcntl(%i, %i, ...): not implemented", fd, cmd);
    return 0;
}

int ioctl_soloader(int fd, int request, ...)
{
    l_warn("ioctl(%i, %i, ...): not implemented", fd, request);
    return 0;
}

int fsync_soloader(int fd)
{
    int ret = fsync(fd);
    l_debug("fsync(%i): %i", fd, ret);
    return ret;
}

int access_soloader(const char *path, int amode)
{
    if (is_soloader_urandom_path(path))
    {
        if (amode & W_OK) {
            errno = EACCES;
            return -1;
        }
        return 0;
    }

    return access(path, amode);
}

int lstat_soloader(const char *path, struct stat *buf)
{
    if (is_soloader_urandom_path(path))
    {
        if (!buf) {
            errno = EINVAL;
            return -1;
        }
        return soloader_urandom_build_stat(buf);
    }

    return lstat(path, buf);
}
