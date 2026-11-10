#ifndef _STIRUTILS_H_
#define _STIRUTILS_H_
#if defined(__sun) && defined(__SVR4)
#define __EXTENSIONS__
#endif
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <stdint.h>

typedef uint32_t mysize_t;

static inline char *stir_strdup(const char *x)
{
  size_t len = strlen(x);
  char *res = malloc(len+1);
  if (!res)
  {
    return NULL;
  }
  memcpy(res, x, len+1);
  return res;
}

char *calc_forward_path(char *storcwd, size_t upcnt);
char *dir_up(char *old);
char *myitoa(int i);
int my_get_nprocs(void);
int utimensat_both_emul(const char *pathname, struct timespec time, int l,
                        int forwards);

static inline struct timespec mtim_from_statbuf(struct stat *sb)
{
#define st_mtim_posix st_mtim
#if defined(__APPLE__)
#undef st_mtim_posix
#define st_mtim_posix st_mtimespec
#ifndef st_mtime
#define st_mtime st_mtimespec.tv_sec
#endif
#endif
#if defined(__NetBSD__)
#if __NetBSD_Version__ < 700000000
#undef st_mtim_posix
#define st_mtim_posix st_mtimespec
#ifndef st_mtime
#define st_mtime st_mtimespec.tv_sec
#endif
#endif
#endif
#ifdef __FreeBSD__
#if __FreeBSD_version < 1000000
#undef st_mtim_posix
#define st_mtim_posix st_mtimespec
#ifndef st_mtime
#define st_mtime st_mtimespec.tv_sec
#endif
#endif
#endif
#ifdef __OpenBSD__
#if OpenBSD < 200905 // This means OpenBSD 4.5 according to some strange logic
#undef st_mtim_posix
#define st_mtim_posix st_mtimespec
#ifndef st_mtime
#define st_mtime st_mtimespec.tv_sec
#endif
#endif
#endif
#ifdef st_mtime
  return sb->st_mtim_posix;
#else
  struct timespec ts;
  ts.tv_nsec = 0;
  ts.tv_sec = sb->st_mtime;
  return ts;
#endif
}

#ifdef _POSIX_C_SOURCE
#if _POSIX_C_SOURCE < 200809L
#undef STIR_NO_MEMPARSE
#define STIR_NO_MEMPARSE 1
#endif
#else
#ifdef _POSIX_VERSION
#if _POSIX_VERSION < 200809L
#undef STIR_NO_MEMPARSE
#define STIR_NO_MEMPARSE 1
#endif
#endif
#endif

#endif
