
#ifndef _SYS_STAT_H
#define _SYS_STAT_H

#include <bits/posix/stat.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef __MLIBC_ABI_ONLY

int chmod(const char *, mode_t);
int fchmod(int, mode_t);
int fchmodat(int, const char *, mode_t, int);
int fstat(int fd, struct stat *result);
int fstatat(int, const char *__restrict, struct stat *__restrict, int);
int futimens(int fd, const struct timespec times[2]);
int lstat(const char *__restrict, struct stat *__restrict);
int mkdir(const char *, mode_t);
int mkdirat(int, const char *, mode_t);
int mkfifo(const char *, mode_t);
int mkfifoat(int, const char *, mode_t);
int mknod(const char *, mode_t, dev_t);
int mknodat(int, const char *, mode_t, dev_t);
int stat(const char *__restrict, struct stat *__restrict);
mode_t umask(mode_t);
int utimensat(int, const char *, const struct timespec times[2], int);

// These macros are taken from musl
#define stat64 stat
#define fstat64 fstat
#define lstat64 lstat
#define fstatat64 fstatat
#define blkcnt64_t blkcnt_t
#define fsblkcnt64_t fsblkcnt_t
#define fsfilcnt64_t fsfilcnt_t
#define ino64_t ino_t
#define off64_t off_t

#endif /* !__MLIBC_ABI_ONLY */

#ifdef __cplusplus
}
#endif

#endif // _SYS_STAT_H

