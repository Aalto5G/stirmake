#include "mymalloc.h"
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stddef.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include "myvalgrind.h"

#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 201112L
#define ALIGN (_Alignof(max_align_t))
#else
struct align_check {
  unsigned char uch;
  union {
    void *p;
    void (*fp)(void);
    ptrdiff_t pd;
    off_t o;
    size_t sz;
    ssize_t ssz;
    unsigned char uch;
    char ch;
    unsigned short us;
    short s;
    unsigned int ui;
    int i;
    unsigned long ul;
    long l;
    unsigned long long ull;
    long long ll;
    float flt;
    double dbl;
    long double ldbl;
  } u;
};
static inline size_t get_max_align_compatibility(void)
{
  size_t res;
  res = offsetof(struct align_check, u);
  res += (res == 0);
  res--;
  res |= res >> 1;
  res |= res >> 2;
  res |= res >> 4;
  res |= res >> 8;
  res |= res >> 16;
  res |= res >> 32;
  res++;
  return res;
}
#define ALIGN (get_max_align_compatibility())
#endif

char *my_arena;
size_t my_arena_off;
size_t sizeof_my_arena;
size_t argmem;
size_t argmemcnt;
size_t mymem;
size_t mymemcnt;
size_t mymemwaste;

void errxit(const char *fmt, ...);
void my_abort(void);

void *my_malloc_align(size_t sz, size_t align)
{
  void *result;
  if (my_is_running_on_valgrind())
  {
    return malloc(sz);
  }
  if ((align & (align-1)) != 0 || align == 0)
  {
    my_abort();
  }
  if (sz > sizeof_my_arena)
  {
#if 0
    if (debug)
    {
      print_indent();
      printf("allocating outside of arena, big allocation, %zu bytes\n", sz);
    }
#endif
    result = stir_do_mmap_madvise(sz); // assume alignment is ok
    if (result == NULL)
    {
      fprintf(stderr, "too large alloc, out of memory: %zu bytes\n", sz);
      my_abort();
    }
    mymem += sz;
    mymemcnt += 1;
    return result;
  }
  if (my_arena_off & (align-1))
  {
    size_t move = align - (my_arena_off & (align-1));
    my_arena_off += move;
    mymemwaste += move;
    if (my_arena_off > sizeof_my_arena)
    {
      size_t moveback = my_arena_off - sizeof_my_arena;
      my_arena_off -= moveback;
      mymemwaste -= moveback;
    }
  }
  result = my_arena + my_arena_off;
  if (my_arena_off + sz > sizeof_my_arena)
  {
    mymemwaste += (size_t)(sizeof_my_arena - my_arena_off);
#if 0
    if (debug)
    {
      print_indent();
      printf("allocating new arena\n");
    }
#endif
    my_arena = stir_do_mmap_madvise(sizeof_my_arena); // assume alignment is ok
    if (my_arena == NULL)
    {
      errxit("Can't mmap new arena");
      exit(2);
    }
    my_arena_off = 0;
    result = my_arena + my_arena_off;
    my_arena_off += sz;
    if (my_arena_off > sizeof_my_arena)
    {
      //fprintf(stderr, "out of memory\n"); // nondescriptive
      my_abort();
    }
  }
  else
  {
    my_arena_off += sz;
  }
  mymem += sz;
  mymemcnt += 1;
  return result;
}
void *my_malloc(size_t sz)
{
  return my_malloc_align(sz, ALIGN);
}
void *my_argmalloc(size_t sz)
{
  void *res;
  res = my_malloc(sz);
  if (res)
  {
    argmem += sz;
    argmemcnt += 1;
  }
  return res;
}
void *my_argmalloc_align(size_t sz, size_t align)
{
  void *res;
  res = my_malloc_align(sz, align);
  if (res)
  {
    argmem += sz;
    argmemcnt += 1;
  }
  return res;
}
void my_free(void *ptr)
{
  // nop
}
void *my_strdup_len(const char *str, size_t sz)
{
  char *result = my_malloc_align(sz + 1, 1);
  memcpy(result, str, sz);
  result[sz] = '\0';
  return result;
}
void *my_strdup(const char *str)
{
  size_t sz = strlen(str);
  void *result = my_malloc_align(sz + 1, 1);
  memcpy(result, str, sz + 1);
  return result;
}
void *my_argstrdup_len(const char *str, size_t sz)
{
  char *result = my_argmalloc_align(sz + 1, 1);
  memcpy(result, str, sz);
  result[sz] = '\0';
  return result;
}
void *my_argstrdup(const char *str)
{
  size_t sz = strlen(str);
  void *result = my_argmalloc_align(sz + 1, 1);
  memcpy(result, str, sz + 1);
  return result;
}

int my_malloc_init(void)
{
  sizeof_my_arena = 128*1024;
  my_arena = stir_do_mmap_madvise(sizeof_my_arena);
  if (my_arena == NULL)
  {
    return -1;
  }
  my_arena_off = 0;
  return 0;
}

void *stir_do_mmap_madvise(size_t bytes)
{
  void *ptr;
#if STIR_NO_MMAP
  return malloc(bytes);
#else
  bytes = stir_topages(bytes);
  // Ugh. I wish all systems had simple and compatible interface.
#ifdef MAP_ANON
  #ifdef MAP_NORESERVE
  ptr = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON|MAP_NORESERVE, -1, 0);
  #else
  ptr = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANON, -1, 0);
  #endif
#else
  #ifdef MAP_ANONYMOUS
    #ifdef MAP_NORESERVE
  ptr = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS|MAP_NORESERVE, -1, 0);
    #else
  ptr = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_ANONYMOUS, -1, 0);
    #endif
  #else
  {
    int fd;
    fd = open("/dev/zero", O_RDWR);
    if (fd < 0)
    {
      abort();
    }
    #ifdef MAP_FILE
    ptr = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FILE, fd, 0);
    #else
    ptr = mmap(NULL, bytes, PROT_READ|PROT_WRITE, MAP_SHARED, fd, 0);
    #endif
    close(fd);
  }
  #endif
#endif
#ifdef MADV_DONTNEED
  #ifdef __linux__
  if (ptr && ptr != MAP_FAILED)
  {
    madvise(ptr, bytes, MADV_DONTNEED); // Linux-ism
  }
  #endif
#endif
#ifdef MADV_FREE
  #if defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__DragonFly__)
  if (ptr && ptr != MAP_FAILED)
  {
    madvise(ptr, bytes, MADV_FREE); // *BSD-ism
    // on Linux, MADV_FREE works only on private anonymous pages
    // TODO: not sure about FreeBSD
  }
  #endif
#endif
  if (ptr == MAP_FAILED)
  {
    return NULL;
  }
#endif
  return ptr;
}

void stir_do_munmap(void *ptr, size_t bytes)
{
#if STIR_NO_MMAP
  free(ptr);
#else
  munmap(ptr, stir_topages(bytes));
#endif
}
