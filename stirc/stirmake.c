#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/uio.h>
#include <errno.h>
#include <string.h>
#include <signal.h>
#include <dirent.h>
#include <fcntl.h>
#include <locale.h>
#include <libgen.h>
#include <poll.h>
#include <time.h>
#include <stdarg.h>
#include "stircommon.h"
#include "const.h"
#include "bypid.h"
#include "adddep.h"
#include "bytgt.h"
#include "bydep.h"
#include "rule.h"
#include "mymalloc.h"
#include "stringtab.h"
#include "stirutils.h"
#include "statcache.h"
#include "mygetline.h"
#include "accesscache.h"
#include "abce/abcejmalloc.h"
#include "db.h"
#include "pathmax.h"
#include "stirtrap.h"
#include "myvalgrind.h"
#include "abce/abcetrees.h"
#ifdef __linux__
  #ifdef __GLIBC__
    #if __GLIBC__  > 2 || (__GLIBC__  == 2 && __GLIBC_MINOR__  >= 25)
      #define HAVE_GETRANDOM
      #include <sys/random.h>
    #endif
  #endif
#endif

#undef HAVE_POSIX_SPAWN
#undef USE_VFORK

// System provides getloadavg() function
// Non-POSIX, so we enable only on systems where it is known to work
#undef LOADAVG

#ifdef __CYGWIN__
  // This is mandatory for high performance
  #define HAVE_POSIX_SPAWN
  #undef USE_VFORK // not on Windows
#endif
#ifdef __FreeBSD__
  #include <sys/param.h>
  #define LOADAVG 1
  #if __FreeBSD_version >= 800000
    #define HAVE_POSIX_SPAWN
  #endif
  #define USE_VFORK // from 3BSD or 2.9BSD, apparently all the time in FreeBSD
#endif
#ifdef __DragonFly__
  #include <sys/param.h>
  #define LOADAVG 1
  #if __DragonFly_version >= 200202 // this means 2.3.1 according to some strange logic
    #define HAVE_POSIX_SPAWN
  #endif
  #define USE_VFORK // from 3BSD or 2.9BSD, apparently all the time in FreeBSD and also in DragonFly
#endif
#ifdef __linux__
  #define LOADAVG 1
  #ifdef __GLIBC__
    #if __GLIBC__  > 2 || (__GLIBC__  == 2 && __GLIBC_MINOR__  >= 2)
      #define HAVE_POSIX_SPAWN
    #endif
    #if __GLIBC__  > 2 || (__GLIBC__  == 2 && __GLIBC_MINOR__  >= 1) // actually 2.0.112
      #define USE_VFORK
    #endif
  #endif
#endif
#ifdef __APPLE__
  #define LOADAVG 1
  #ifdef _POSIX_SPAWN // Don't know how to detect MacOS version, or which version contains it
    #define HAVE_POSIX_SPAWN
  #endif
  #undef USE_VFORK // don't know if available
#endif
#ifdef __NetBSD__
  #include <sys/param.h>
  #define LOADAVG 1
  #if __NetBSD_Version__ >= 600000000
    #define HAVE_POSIX_SPAWN
  #endif
  #if __NetBSD_Version__ >= 103000000 // NetBSD 1.3
    #define USE_VFORK
  #endif
#endif
#ifdef __OpenBSD__
  #include <sys/param.h>
  #define LOADAVG 1
  #if OpenBSD >= 201211 // This means OpenBSD 5.2 according to some strange logic
    #define HAVE_POSIX_SPAWN
  #endif
  #undef USE_VFORK // probably normal fork() + block parent here, inefficient
#endif
#if defined(__sun) && defined(__SVR4)
  #include <sys/loadavg.h>
  #define LOADAVG 1
#endif

#ifdef _POSIX_SPAWN // For other systems
#ifndef HAVE_POSIX_SPAWN
#define HAVE_POSIX_SPAWN
#endif
#endif

#undef USE_VFORK // At least on Linux it's slower
// Also highly platform dependent whether close() or dup2() after vfork() is allowed

#ifdef HAVE_POSIX_SPAWN
#include <spawn.h>
#endif

#include <sys/select.h>
#include <sys/mman.h>
#include "yyutils.h"
#include "linkedlist.h"
#include "abce/abcemurmur.h"
#include "abce/abcecontainerof.h"
#include "abce/abcerbtree.h"
#include "incyyutils.h"
#include "dbyy.h"
#include "dbyyutils.h"
#include "stirtrap.h"
#include "syncbuf.h"

#define DEFAULT_CREATE_JOBSERVER_FIFO 0
#ifdef __FreeBSD__
  #undef DEFAULT_CREATE_JOBSERVER_FIFO
  #define DEFAULT_CREATE_JOBSERVER_FIFO 1 // let's assume recent ports user
#endif
#ifdef __NetBSD__
  #undef DEFAULT_CREATE_JOBSERVER_FIFO
  #define DEFAULT_CREATE_JOBSERVER_FIFO 1 // let's assume recent pkgsrc user
#endif
#ifdef __DragonFly__
  #undef DEFAULT_CREATE_JOBSERVER_FIFO
  #define DEFAULT_CREATE_JOBSERVER_FIFO 1 // let's assume recent DPorts user
#endif
// OpenBSD encourages precompiled packages, 7.4 contains gmake-4.4.1
#ifdef __OpenBSD__
  #include <sys/param.h>
  #if OpenBSD >= 202310 // This means OpenBSD 7.4 according to some strange logic
    #undef DEFAULT_CREATE_JOBSERVER_FIFO
    #define DEFAULT_CREATE_JOBSERVER_FIFO 1
  #endif
#endif
#ifdef __APPLE__
  #undef DEFAULT_CREATE_JOBSERVER_FIFO
  #define DEFAULT_CREATE_JOBSERVER_FIFO 1 // let's assume recent homebrew user
#endif
#ifdef __linux__
  #ifdef __GLIBC__
    #undef DEFAULT_CREATE_JOBSERVER_FIFO
    #define DEFAULT_CREATE_JOBSERVER_FIFO 2 // autodetect OS
  #endif
#endif

extern char **environ;
char *jobserver_fifo = NULL;
#if DEFAULT_CREATE_JOBSERVER_FIFO == 0
int create_jobserver_fifo = 0;
#elif DEFAULT_CREATE_JOBSERVER_FIFO == 1
int create_jobserver_fifo = 1;
#else
int create_jobserver_fifo = 0; // OS detection
#endif
int created_jobserver_fifo = 0;

int silent = 0;
int touchmode = 0;
int indentlevel = 0;
int do_trace = 0;
int unsafe = 0;

size_t dbecnt;
extern size_t tsdbecnt;

void print_indent(void)
{
  int i;
  for (i = 0; i < indentlevel; i++)
  {
    putc_unlocked(' ', stdout);
  }
}

enum pretendtype {
	PRETEND_MODIFIED,
	PRETEND_VERY_OLD_NO_REMAKE,
};

struct pretend {
  char *fname;
  struct pretend *next;
  int relative;
  enum pretendtype pretendtype;
};
struct pretend *pretend = NULL;

int ispretend(const char *test, enum pretendtype typ)
{
  struct pretend *iter = pretend;
  while (iter != NULL)
  {
    if (strcmp(iter->fname, test) == 0 && iter->pretendtype == typ)
    {
      return 1;
    }
    iter = iter->next;
  }
  return 0;
}

sig_atomic_t sigterm_atomic;
sig_atomic_t sigint_atomic;
sig_atomic_t sighup_atomic;
sig_atomic_t subproc_sigterm_atomic;
sig_atomic_t subproc_sigint_atomic;
sig_atomic_t subproc_sighup_atomic;

void subproc_sigterm_handler(int x)
{
  subproc_sigterm_atomic = 1;
}
void subproc_sigint_handler(int x)
{
  subproc_sigint_atomic = 1;
}
void subproc_sighup_handler(int x)
{
  subproc_sighup_atomic = 1;
}

struct abce abce;
int abce_inited = 0;

int test = 0;

enum out_sync out_sync = OUT_SYNC_NOT_SET_YET;

void st_compact(void);

void my_abort(void)
{
  fflush(stdout);
  fflush(stderr);
  signal(SIGABRT, SIG_DFL);
  if (abce_inited)
  {
    abce_compact(&abce);
  }
  st_compact();
  abort();
}

enum mode {
  MODE_NONE = 0,
  MODE_THIS = 1,
  MODE_PROJECT = 2,
  MODE_ALL = 3,
};

enum mode mode = MODE_NONE;
int isspecprog = 0;


int debug = 0;
int ignoreerr = 0;
int doasmuchascan = 0;
int cmdfailed = 0;
int unconditional = 0;
int dry_run = 0;

int self_pipe_fd[2];

int jobserver_fd[2];

int children = 0;

int merge_db(void);
int merge_db_v1(void);
int merge_db_v2(void);
void errxit(const char *fmt, ...);

int cmd_equal(struct cmd *cmd1, struct cmd *cmd2)
{
  size_t cnt1, cnt2, i;
  for (cnt1 = 0; cmd1->args[cnt1]; cnt1++);
  for (cnt2 = 0; cmd2->args[cnt2]; cnt2++);
  if (cnt1 != cnt2)
  {
    return 0;
  }
  for (i = 0; i < cnt1; i++)
  {
    size_t argcnt1, argcnt2, j;
    for (argcnt1 = 0; cmd1->args[i][argcnt1]; argcnt1++);
    for (argcnt2 = 0; cmd2->args[i][argcnt2]; argcnt2++);
    if (argcnt1 != argcnt2)
    {
      return 0;
    }
    for (j = 0; j < argcnt1; j++)
    {
      if (strcmp(cmd1->args[i][j], cmd2->args[i][j]) != 0)
      {
        return 0;
      }
    }
  }
  return 1;
}

void update_recursive_pid(int parent)
{
  pid_t pid = parent ? getppid() : getpid();
  char buf[64] = {0};
  snprintf(buf, sizeof(buf), "%d", (int)pid);
  setenv("STIRMAKEPID", buf, 1);
}




char jobserver_chars[1024];
int jobserver_char_cnt = 0;

int write_jobserver(void)
{
  struct itimerval new_value = {.it_interval.tv_sec = 0};
  int ret;
  new_value.it_interval.tv_sec = 0;
  new_value.it_interval.tv_usec = 0;
  new_value.it_value.tv_sec = 0;
  new_value.it_value.tv_usec = 10*1000;
  setitimer(ITIMER_REAL, &new_value, NULL);
  if (jobserver_char_cnt)
  {
    ret = write(jobserver_fd[1], &jobserver_chars[--jobserver_char_cnt], 1);
  }
  else
  {
    ret = write(jobserver_fd[1], ".", 1);
  }
  new_value.it_interval.tv_sec = 0;
  new_value.it_interval.tv_usec = 0;
  new_value.it_value.tv_sec = 0;
  new_value.it_value.tv_usec = 0;
  setitimer(ITIMER_REAL, &new_value, NULL);
  return ret;
}
int read_jobserver(void)
{
  char ch;
  struct pollfd pfd = {.fd = 0};
  int ret;
  pfd.fd = jobserver_fd[0];
  pfd.events = POLLIN;
  ret = poll(&pfd, 1, 0);
  if (ret == 0)
  {
    return 0;
  }
  if (ret < 0)
  {
    if (errno == EINTR)
    {
      return 0;
    }
    printf("111 ret %d, errno %d\n", ret, errno);
    my_abort();
  }
  if (!(pfd.revents & POLLIN))
  {
    my_abort();
  }

#ifdef MSG_DONTWAIT
  ret = recv(jobserver_fd[0], &ch, 1, MSG_DONTWAIT);
#else
  ret = -1;
  errno = ENOTSOCK;
#endif
  if (ret == -1 && errno == ENOTSOCK)
  {
    //fprintf(stderr, "UGH\n");
    // Ugh. GNU make expects the jobserver to be blocking, which doesn't
    // really suit our architecture. Set an interval timer in case of a race
    // that GNU make won.
    struct itimerval new_value = {.it_interval.tv_sec = 0};
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_usec = 0;
    new_value.it_value.tv_sec = 0;
    new_value.it_value.tv_usec = 10*1000;
    setitimer(ITIMER_REAL, &new_value, NULL);
    // TODO to a re-poll?
    ret = read(jobserver_fd[0], &ch, 1);
    new_value.it_interval.tv_sec = 0;
    new_value.it_interval.tv_usec = 0;
    new_value.it_value.tv_sec = 0;
    new_value.it_value.tv_usec = 0;
    setitimer(ITIMER_REAL, &new_value, NULL);

    if (ret > 1)
    {
      printf("222 ret %d, errno %d\n", ret, errno);
      my_abort();
    }
    if (ret == 1 && jobserver_char_cnt < (int)(sizeof(jobserver_chars)/sizeof(*jobserver_chars)))
    {
      jobserver_chars[jobserver_char_cnt++] = ch;
    }
    return (ret == 1);
  }
  if (ret == 1 && jobserver_char_cnt < (int)(sizeof(jobserver_chars)/sizeof(*jobserver_chars)))
  {
    jobserver_chars[jobserver_char_cnt++] = ch;
  }
  return (ret == 1);
}

int cmdequal_db(mysize_t tgtidx, struct cmd *cmd, mysize_t diridx)
{
  struct dbe *dbe;
  dbe = get_dbe(tgtidx);
  if (dbe == NULL)
  {
    if (debug)
    {
      print_indent();
      printf("target %s not found in cmd DB\n", sttable[tgtidx].s);
    }
    return 0;
  }
  if (dbe->diridx != diridx)
  {
    if (debug)
    {
      print_indent();
      printf("target %s has different dir in cmd DB\n", sttable[tgtidx].s);
    }
    return 0;
  }
  if (!cmd_equal(cmd, &dbe->cmds))
  {
    if (debug)
    {
      print_indent();
      printf("target %s has different cmd in cmd DB\n", sttable[tgtidx].s);
    }
    return 0;
  }
  return 1;
}

int get_ruleid_by_tgt(mysize_t tgt);

int ts_cmp(struct timespec ta, struct timespec tb);

int tsszequal_db(mysize_t stringtabidx, struct timespec ts, mysize_t diridx, off_t sz, int istarget)
{
  struct tsdbe *tsdbe;
  const char *tgtsrc = istarget ? "target" : "source";
  tsdbe = get_tsdbe(stringtabidx);
  if (tsdbe == NULL)
  {
    if (debug)
    {
      print_indent();
      printf("%s %s not found in tssz DB\n", tgtsrc, sttable[stringtabidx].s);
    }
    return 0;
  }
  if (!istarget)
  {
    tsdbe->tsnew = ts;
    tsdbe->sznew = sz;
    tsdbe->seen = 1;
  }
  if (tsdbe->sz != sz)
  {
    if (debug)
    {
      print_indent();
      printf("%s %s has different size in tssz DB\n", tgtsrc, sttable[stringtabidx].s);
    }
    return 0;
  }
  if (ts_cmp(tsdbe->ts, ts) != 0)
  {
    if (debug)
    {
      print_indent();
      printf("%s %s has different timestamp in tssz DB\n", tgtsrc, sttable[stringtabidx].s);
    }
    return 0;
  }
  return 1;
}


int ts_cmp(struct timespec ta, struct timespec tb)
{
  if (ta.tv_sec > tb.tv_sec)
  {
    return 1;
  }
  if (ta.tv_sec < tb.tv_sec)
  {
    return -1;
  }
  if (ta.tv_nsec > tb.tv_nsec)
  {
    return 1;
  }
  if (ta.tv_nsec < tb.tv_nsec)
  {
    return -1;
  }
  return 0;
}


extern fd_set globfds;

void drain_pipe(struct rule *rule, int fdit);

void clean_jobserver(void)
{
  if (created_jobserver_fifo && jobserver_fifo)
  {
    unlink(jobserver_fifo);
    jobserver_fifo = NULL;
    created_jobserver_fifo = 0;
  }
}

// FIXME do updates to DB here as well... Now they are not done.
void errxit(const char *fmt, ...)
{
  va_list args;
  const char *prefix = "stirmake: *** ";
  const char *suffix = ". Exiting.\n";
  size_t sz = strlen(prefix) + strlen(fmt) + strlen(suffix) + 1;
  char *fmtdup = malloc(sz);
  int wstatus;
  int fd = -1;
  pid_t pid;
  clean_jobserver();
  snprintf(fmtdup, sz, "%s%s%s", prefix, fmt, suffix);
  va_start(args, fmt);
  vfprintf(stderr, fmtdup, args);
  va_end(args);
  free(fmtdup);
  pid = waitpid(-1, &wstatus, WNOHANG);
  if (pid < 0 && errno == ECHILD)
  {
    if (merge_db())
    {
      fprintf(stderr, "stirmake: Cannot write stirmake database\n");
    }
    exit(2);
  }
  if (pid > 0)
  {
    int ruleid = ruleid_by_pid_erase(pid, &fd);
    if (ruleid < 0)
    {
      printf("31.1\n");
      my_abort();
    }
    if (fd >= 0)
    {
      drain_pipe(rules[ruleid], fd);
      syncbuf_dump(&rules[ruleid]->output, 1);
      close(fd);
      FD_CLR(fd, &globfds);
      fd = -1;
    }
    if (children <= 0)
    {
      printf("27.E\n");
      my_abort();
    }
    children--;
    if (children != 0)
    {
      write_jobserver();
    }
    if (!ignoreerr && wstatus != 0 && pid > 0)
    {
      // FIXME how to find ruleid?
      if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
      {
        struct linked_list_node *node;
        LINKED_LIST_FOR_EACH(node, &rules[ruleid]->tgtlist)
        {
          struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
          unlink(sttable[e->tgtidx].s);
        }
      }
    }
  }
  fprintf(stderr, "stirmake: *** Waiting for child processes to die.\n");
  for (;;)
  {
    int ruleid = -1;
    pid = waitpid(-1, &wstatus, 0);
    if (pid == 0)
    {
      printf("28.E\n");
      my_abort();
    }
    if (pid > 0)
    {
      ruleid = ruleid_by_pid_erase(pid, &fd);
      if (ruleid < 0)
      {
        printf("31.1\n");
        my_abort();
      }
      if (fd >= 0)
      {
        drain_pipe(rules[ruleid], fd);
        syncbuf_dump(&rules[ruleid]->output, 1);
        close(fd);
        FD_CLR(fd, &globfds);
        fd = -1;
      }
    }
    if (!ignoreerr && wstatus != 0 && pid > 0)
    {
      // FIXME how to find ruleid?
      if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
      {
        struct linked_list_node *node;
        LINKED_LIST_FOR_EACH(node, &rules[ruleid]->tgtlist)
        {
          struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
          unlink(sttable[e->tgtidx].s);
        }
      }
    }
    if (children <= 0)
    {
      if (pid < 0 && errno == ECHILD)
      {
        fprintf(stderr, "stirmake: *** No children left. Exiting.\n");
        if (merge_db())
        {
          fprintf(stderr, "stirmake: Cannot write stirmake database\n");
        }
        exit(2);
      }
      printf("29.E\n");
      my_abort();
    }
    if (pid < 0)
    {
      if (errno == EINTR)
      {
        continue;
      }
      printf("30.E\n");
      perror("Error was");
      printf("number of children: %d\n", children);
      my_abort();
    }
#if 0 // Let's not do this, just in case the data is messed up.
    int ruleid = ruleid_by_pid_erase(pid);
    if (ruleid < 0)
    {
      printf("31.E\n");
      my_abort();
    }
#endif
    children--;
    if (children != 0)
    {
      write_jobserver();
    }
  }
}


char **argdup(int ignore, int noecho, int ismake, char **cmdargs);

static mysize_t
cmdsrc_cache_add_str_nul(struct abce *abce, const char *str, int *err)
{
  mysize_t ret;
  ret = abce_cache_add_str_nul(abce, str);
  if (ret == (mysize_t)-1)
  {
    *err = 1;
  }
  return ret;
}

char *st_ignore = "I";
char *st_noignore = "NI";
char *st_noecho = "NE";
char *st_echo = "E";
char *st_make = "M";
char *st_nomake = "NM";


char ***cmdsrc_eval(struct abce *abce, struct rule *rule, int *info_printed)
{
  size_t i, j, k;
  struct cmdsrc *cmdsrc = &rule->cmdsrc;
  char ***result = NULL;
  char ***result2 = NULL;
  char tgtneighbuf[1024];
  size_t resultsz = 0;
  size_t resultcap = 16;
  struct stirtgt *first_tgt =
    ABCE_CONTAINER_OF(rule->tgtlist.node.next, struct stirtgt, llnode);
  char *tgt;
  mysize_t tgtlen;
  struct linked_list_node *node;
  struct abce_mb scope = abce->cachebase[rule->scopeidx]; // no refup!
  struct abce_mb oldscope = abce->dynscope; // no refup, it's in cache anyway
  size_t atidx, plusidx, baridx, hatidx, ltidx, staridx;
  int err = 0;

  *info_printed = 0;

  atidx = cmdsrc_cache_add_str_nul(abce, "@", &err);
  plusidx = cmdsrc_cache_add_str_nul(abce, "+", &err);
  baridx = cmdsrc_cache_add_str_nul(abce, "|", &err);
  hatidx = cmdsrc_cache_add_str_nul(abce, "^", &err);
  ltidx = cmdsrc_cache_add_str_nul(abce, "<", &err);
  staridx = cmdsrc_cache_add_str_nul(abce, "*", &err);

  if (err)
  {
    my_abort();
  }

  if (first_tgt->tgtidxnodir != (mysize_t)-1)
  {
    tgt = sttable[first_tgt->tgtidxnodir].s;
    tgtlen = sttable[first_tgt->tgtidxnodir].len;
  }
  else
  {
    tgt = neighpath_buflen(sttable[rule->diridx].s, sttable[rule->diridx].len,
                           sttable[first_tgt->tgtidx].s,
                           sttable[first_tgt->tgtidx].len,
                           tgtneighbuf, sizeof(tgtneighbuf), &tgtlen);
  }
  result = malloc(resultcap * sizeof(*result));
  for (i = 0; i < cmdsrc->itemsz; i++)
  {
    if (cmdsrc->items[i].iscode || cmdsrc->items[i].isfun)
    {
      unsigned char tmpbuf[64] = {0};
      size_t tmpsiz = 0;
      struct abce_mb mbstruct = ABCE_MB_EMPTY;
      struct abce_mb *mb = NULL;
      //struct abce_mb mbkey = ABCE_MB_EMPTY;
#if 0
      struct abce_mb mbnil = {.typ = ABCE_T_N};
#endif
      struct abce_mb *mbval = NULL;
      int first = 1;

#if 0
      mbkey = abce_mb_create_string(abce, "@", 1);
      if (mbkey.typ == ABCE_T_N)
      {
        return NULL;
      }
#endif
      mbval = abce_mb_cpush_create_string(abce, tgt, tgtlen);
      if (mbval == NULL)
      {
        //abce_mb_refdn(abce, &mbkey);
        return NULL;
      }
      if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[atidx], mbval) != 0)
      {
        //abce_mb_refdn(abce, &mbkey);
        abce_cpop(abce);
        return NULL;
      }
      //abce_mb_refdn(abce, &mbkey);
      //abce_mb_refdn(abce, &mbval);
      abce_cpop(abce);

      if (rule->meatidx != (mysize_t)-1)
      {
        mbval = abce_mb_cpush_create_string(abce, sttable[rule->meatidx].s, sttable[rule->meatidx].len);
        if (mbval == NULL)
        {
          //abce_mb_refdn(abce, &mbkey);
          return NULL;
        }
        if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[staridx], mbval) != 0)
        {
          //abce_mb_refdn(abce, &mbkey);
          abce_cpop(abce);
          return NULL;
        }
        //abce_mb_refdn(abce, &mbkey);
        //abce_mb_refdn(abce, &mbval);
        abce_cpop(abce);
      }

#if 0
      mbkey = abce_mb_create_string(abce, "+", 1);
      if (mbkey.typ == ABCE_T_N)
      {
        return NULL;
      }
#endif
      mbval = abce_mb_cpush_create_array(abce);
      if (mbval == NULL)
      {
        //abce_mb_refdn(abce, &mbkey);
        return NULL;
      }
      if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[plusidx], mbval) != 0)
      {
        //abce_mb_refdn(abce, &mbkey);
        //abce_mb_refdn(abce, &mbval);
        abce_cpop(abce);
        return NULL;
      }
      //abce_mb_refdn(abce, &mbkey);
      LINKED_LIST_FOR_EACH(node, &rule->deplist)
      {
        struct stirdep *dep =
          ABCE_CONTAINER_OF(node, struct stirdep, llnode);
        char *namenodir;
        if (dep->is_orderonly)
        {
          continue;
        }
        if (dep->nameidxnodir != (mysize_t)-1)
        {
          namenodir = sttable[dep->nameidxnodir].s;
          mb = abce_mb_cpush_create_string(abce, namenodir, sttable[dep->nameidxnodir].len);
        }
        else
        {
          char namenodirbuf[1024];
          mysize_t namenodirlen;
          namenodir = neighpath_buflen(sttable[rule->diridx].s,
                                       sttable[rule->diridx].len,
                                       sttable[dep->nameidx].s,
                                       sttable[dep->nameidx].len,
                                       namenodirbuf, sizeof(namenodirbuf),
                                       &namenodirlen);
          mb = abce_mb_cpush_create_string(abce, namenodir, namenodirlen);
          if (namenodir != namenodirbuf)
          {
            free(namenodir);
          }
        }
        if (mb == NULL)
        {
          //abce_mb_refdn(abce, &mbval);
	  abce_cpop(abce);
          return NULL;
        }
        if (abce_mb_array_append(abce, mbval, mb) != 0)
        {
          //abce_mb_refdn(abce, &mbval);
          //abce_mb_refdn(abce, &mb);
	  abce_cpop(abce);
	  abce_cpop(abce);
          return NULL;
        }
	mb = NULL;
        //abce_mb_refdn(abce, &mb);
	abce_cpop(abce);
      }
      abce_cpop(abce);
      mbval = NULL;

#if 0
      mbkey = abce_mb_create_string(abce, "|", 1);
      if (mbkey.typ == ABCE_T_N)
      {
        return NULL;
      }
#endif
      mbval = abce_mb_cpush_create_array(abce);
      if (mbval == NULL)
      {
        //abce_mb_refdn(abce, &mbkey);
        return NULL;
      }
      if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[baridx], mbval) != 0)
      {
        //abce_mb_refdn(abce, &mbkey);
        //abce_mb_refdn(abce, &mbval);
	abce_cpop(abce);
        return NULL;
      }
      //abce_mb_refdn(abce, &mbkey);
      LINKED_LIST_FOR_EACH(node, &rule->deplist)
      {
        struct stirdep *dep =
          ABCE_CONTAINER_OF(node, struct stirdep, llnode);
        char *namenodir;
        if (!dep->is_orderonly)
        {
          continue;
        }
        if (dep->is_dupe)
        {
          continue;
        }
        if (dep->nameidxnodir != (mysize_t)-1)
        {
          namenodir = sttable[dep->nameidxnodir].s;
          mb = abce_mb_cpush_create_string(abce, namenodir, sttable[dep->nameidxnodir].len);
        }
        else
        {
          char namenodirbuf[1024];
          mysize_t namenodirlen;
          namenodir = neighpath_buflen(sttable[rule->diridx].s,
                                       sttable[rule->diridx].len,
                                       sttable[dep->nameidx].s,
                                       sttable[dep->nameidx].len,
                                       namenodirbuf, sizeof(namenodirbuf),
                                       &namenodirlen);
          mb = abce_mb_cpush_create_string(abce, namenodir, namenodirlen);
          if (namenodir != namenodirbuf)
          {
            free(namenodir);
          }
        }
        if (mb == NULL)
        {
          //abce_mb_refdn(abce, &mbval);
	  abce_cpop(abce);
          return NULL;
        }
        if (abce_mb_array_append(abce, mbval, mb) != 0)
        {
          //abce_mb_refdn(abce, &mbval);
          //abce_mb_refdn(abce, &mb);
	  abce_cpop(abce);
	  abce_cpop(abce);
          return NULL;
        }
        //abce_mb_refdn(abce, &mb);
	abce_cpop(abce);
	mb = NULL;
      }
      //abce_mb_refdn(abce, &mbval);
      abce_cpop(abce);

#if 0
      mbkey = abce_mb_create_string(abce, "^", 1);
      if (mbkey.typ == ABCE_T_N)
      {
        return NULL;
      }
#endif
      mbval = abce_mb_cpush_create_array(abce);
      if (mbval == NULL)
      {
        //abce_mb_refdn(abce, &mbkey);
        return NULL;
      }
      if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[hatidx], mbval) != 0)
      {
        //abce_mb_refdn(abce, &mbkey);
        //abce_mb_refdn(abce, &mbval);
	abce_cpop(abce);
        return NULL;
      }
      //abce_mb_refdn(abce, &mbkey);
      LINKED_LIST_FOR_EACH(node, &rule->deplist)
      {
        struct stirdep *dep =
          ABCE_CONTAINER_OF(node, struct stirdep, llnode);
        char *namenodir;
        if (dep->is_orderonly)
        {
          continue;
        }
        if (dep->is_dupe)
        {
          continue;
        }
        if (dep->nameidxnodir != (mysize_t)-1)
        {
          namenodir = sttable[dep->nameidxnodir].s;
          mb = abce_mb_cpush_create_string(abce, namenodir, sttable[dep->nameidxnodir].len);
        }
        else
        {
          char namenodirbuf[1024];
          mysize_t namenodirlen;
          namenodir = neighpath_buflen(sttable[rule->diridx].s,
                                       sttable[rule->diridx].len,
                                       sttable[dep->nameidx].s,
                                       sttable[dep->nameidx].len,
                                       namenodirbuf, sizeof(namenodirbuf),
                                       &namenodirlen);
          mb = abce_mb_cpush_create_string(abce, namenodir, namenodirlen);
          if (namenodir != namenodirbuf)
          {
            free(namenodir);
          }
        }
        if (mb == NULL)
        {
          //abce_mb_refdn(abce, &mbval);
	  abce_cpop(abce);
          return NULL;
        }
        if (abce_mb_array_append(abce, mbval, mb) != 0)
        {
          //abce_mb_refdn(abce, &mbval);
          //abce_mb_refdn(abce, &mb);
	  abce_cpop(abce);
	  abce_cpop(abce);
          return NULL;
        }
        if (first)
        {
#if 0
          mbkey = abce_mb_create_string(abce, "<", 1);
          if (mbkey.typ == ABCE_T_N)
          {
            abce_mb_refdn(abce, &mbval);
            return NULL;
          }
#endif
          if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[ltidx], mb) != 0)
          {
            //abce_mb_refdn(abce, &mbval);
            //abce_mb_refdn(abce, &mbkey);
            //abce_mb_refdn(abce, &mb);
	    abce_cpop(abce);
	    abce_cpop(abce);
            return NULL;
          }
          //abce_mb_refdn(abce, &mbkey);
          first = 0;
        }
	abce_cpop(abce);
	mb = NULL;
        //abce_mb_refdn(abce, &mb);
      }
      abce_cpop(abce);
      mbval = NULL;
      //abce_mb_refdn(abce, &mbval);
      if (first) // set it to nil if no targets
      {
        mbstruct.typ = ABCE_T_N;
        mbstruct.u.d = 0;
#if 0
        mbkey = abce_mb_create_string(abce, "<", 1);
        if (mbkey.typ == ABCE_T_N)
        {
          abce_mb_refdn(abce, &mbval);
          return NULL;
        }
#endif
        if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[ltidx], &mbstruct) != 0)
        {
          //abce_mb_refdn(abce, &mbval);
          //abce_mb_refdn(abce, &mbkey);
          return NULL;
        }
        //abce_mb_refdn(abce, &mbkey);
        first = 0;
      }

      if (cmdsrc->items[i].iscode)
      {
        abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_PUSH_DBL);
        abce_add_double_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf),
                            cmdsrc->items[i].u.locidx);
        abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_JMP);
      }
      else if (cmdsrc->items[i].isfun)
      {
        abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_PUSH_DBL);
        abce_add_double_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf),
                            cmdsrc->items[i].u.funarg.funidx);
        abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_FUNIFY);
        abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_PUSH_DBL);
        abce_add_double_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf),
                            cmdsrc->items[i].u.funarg.argidx);
        abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf),
                         ABCE_OPCODE_PUSH_FROM_CACHE);
        abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_PUSH_DBL);
        abce_add_double_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), 1); // arg cnt
        abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_CALL);
        abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_EXIT);
      }
      if (abce->sp != 0)
      {
        abort();
      }
      abce->dynscope = scope;
      yy_stored_lineno = -1;
      yy_stored_prefix = NULL;
      if (abce_engine(abce, tmpbuf, tmpsiz) != 0)
      {
        abce->dynscope = oldscope;
        printf("error %s\n", stir_err_to_str(abce->err.code));
        printf("Backtrace:\n");
        for (i = 0; i < abce->btsz; i++)
        {
          if (abce->btbase[i].typ == ABCE_T_S)
          {
            printf("%s\n", abce_mba_str(abce->btbase[i].u.area));
          }
          else
          {
            printf("(-)\n");
          }
        }
        printf("Additional information:\n");
        abce_mb_dump(&abce->err.mb);
        stir_opcode_dump(abce->err.opcode);
        *info_printed = 1;
        return NULL;
      }
      abce->dynscope = oldscope;
      if (abce_getmbptr(&mb, abce, 0) != 0)
      {
        return NULL;
      }
      if (abce->sp != 1)
      {
        abort();
      }
      // Beware. Now only ref is out of stack. Can't alloc abce memory!
      if (mb->typ != ABCE_T_A)
      {
        abce->err.code = ABCE_E_EXPECT_ARRAY;
        abce->err.opcode = ABCE_OPCODE_NOP;
	abce_mb_errreplace_noinline(abce, mb);
        //abce->err.mb = abce_mb_refup(abce, mb);
        //abce_mb_refdn(abce, &mb);
	abce_pop(abce);
        return NULL;
      }
      if (cmdsrc->items[i].merge)
      {
        for (j = 0; j < mb->u.area->u.ar.size; j++)
        {
          if (mb->u.area->u.ar.mbs[j].typ != ABCE_T_A)
          {
            abce->err.code = ABCE_E_EXPECT_ARRAY;
            abce->err.opcode = ABCE_OPCODE_NOP;
            abce->err.mb = abce_mb_refup(abce, &mb->u.area->u.ar.mbs[j]);
            //abce_mb_refdn(abce, &mb);
	    abce_pop(abce);
            return NULL;
          }
          char **cmd = my_argmalloc((mb->u.area->u.ar.mbs[j].u.area->u.ar.size+4)*sizeof(*cmd));
          cmd[0] = cmdsrc->items[i].ignore ? st_ignore : st_noignore;
          cmd[1] = cmdsrc->items[i].noecho ? st_noecho : st_echo;
          cmd[2] = cmdsrc->items[i].ismake ? st_make : st_nomake;
          for (k = 0; k < mb->u.area->u.ar.mbs[j].u.area->u.ar.size; k++)
          {
            if (mb->u.area->u.ar.mbs[j].u.area->u.ar.mbs[k].typ != ABCE_T_S)
            {
              abce->err.code = ABCE_E_EXPECT_STR;
              abce->err.opcode = ABCE_OPCODE_NOP;
              abce->err.mb =
                abce_mb_refup(
                  abce, &mb->u.area->u.ar.mbs[j].u.area->u.ar.mbs[k]);
              //abce_mb_refdn(abce, &mb);
	      abce_pop(abce);
              return NULL;
            }
            cmd[3+k] =
              my_argstrdup(
                abce_mba_str(mb->u.area->u.ar.mbs[j].u.area->u.ar.mbs[k].u.area));
          }
          cmd[3+mb->u.area->u.ar.mbs[j].u.area->u.ar.size] = NULL;
          if (resultsz >= resultcap)
          {
            resultcap = 2*resultsz + 16;
            result = realloc(result, resultcap * sizeof(*result));
          }
          result[resultsz++] = cmd;
        }
      }
      else
      {
        char **cmd = my_argmalloc((mb->u.area->u.ar.size+4)*sizeof(*cmd));
        cmd[0] = cmdsrc->items[i].ignore ? st_ignore : st_noignore;
        cmd[1] = cmdsrc->items[i].noecho ? st_noecho : st_echo;
        cmd[2] = cmdsrc->items[i].ismake ? st_make : st_nomake;
        for (j = 0; j < mb->u.area->u.ar.size; j++)
        {
          if (mb->u.area->u.ar.mbs[j].typ != ABCE_T_S)
          {
            abce->err.code = ABCE_E_EXPECT_STR;
            abce->err.opcode = ABCE_OPCODE_NOP;
            abce->err.mb = abce_mb_refup(abce, &mb->u.area->u.ar.mbs[j]);
            //abce_mb_refdn(abce, &mb);
	    abce_pop(abce);
            return NULL;
          }
          cmd[3+j] = my_argstrdup(abce_mba_str(mb->u.area->u.ar.mbs[j].u.area));
        }
        cmd[3+mb->u.area->u.ar.size] = NULL;
        if (resultsz >= resultcap)
        {
          resultcap = 2*resultsz + 16;
          result = realloc(result, resultcap * sizeof(*result));
        }
        result[resultsz++] = cmd;
      }
      abce_pop(abce);
      if (abce->sp != 0)
      {
        abort();
      }
      abce_sc_remove_val_mb(abce, &scope, &abce->cachebase[atidx]);
      abce_sc_remove_val_mb(abce, &scope, &abce->cachebase[plusidx]);
      abce_sc_remove_val_mb(abce, &scope, &abce->cachebase[ltidx]);
      abce_sc_remove_val_mb(abce, &scope, &abce->cachebase[baridx]);
      abce_sc_remove_val_mb(abce, &scope, &abce->cachebase[hatidx]);
      if (rule->meatidx != (mysize_t)-1)
      {
        abce_sc_remove_val_mb(abce, &scope, &abce->cachebase[staridx]);
      }
#if 0
      if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[atidx], &mbnil) != 0)
      {
        return NULL;
      }
      if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[plusidx], &mbnil) != 0)
      {
        return NULL;
      }
      if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[ltidx], &mbnil) != 0)
      {
        return NULL;
      }
      if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[baridx], &mbnil) != 0)
      {
        return NULL;
      }
      if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[hatidx], &mbnil) != 0)
      {
        return NULL;
      }
      if (rule->meatidx != (mysize_t)-1)
      {
        if (abce_sc_replace_val_mb(abce, &scope, &abce->cachebase[staridx], &mbnil) != 0)
        {
          return NULL;
        }
      }
#endif
      //abce_mb_refdn(abce, &mb);
      continue;
    }
    if (!cmdsrc->items[i].merge)
    {
      if (resultsz >= resultcap)
      {
        resultcap = 2*resultsz + 16;
        result = realloc(result, resultcap * sizeof(*result));
      }
      result[resultsz++] = argdup(cmdsrc->items[i].ignore, cmdsrc->items[i].noecho, cmdsrc->items[i].ismake, cmdsrc->items[i].u.args);
      continue;
    }
    for (j = 0; cmdsrc->items[i].u.cmds[j] != NULL; j++)
    {
      if (resultsz >= resultcap)
      {
        resultcap = 2*resultsz + 16;
        result = realloc(result, resultcap * sizeof(*result));
      }
      result[resultsz++] = argdup(cmdsrc->items[i].ignore, cmdsrc->items[i].noecho, cmdsrc->items[i].ismake, cmdsrc->items[i].u.cmds[j]);
    }
  }
  if (resultsz >= resultcap)
  {
    resultcap = 2*resultsz + 16;
    result = realloc(result, resultcap * sizeof(*result));
  }
  result[resultsz++] = NULL;
  // Replace it with allocation by my_malloc since that is MAP_SHARED
  // Previously we used malloc() since we needed realloc()
  result2 = my_argmalloc(resultsz*sizeof(*result));
  memcpy(result2, result, resultsz*sizeof(*result));
  free(result);
  return result2;
}




//const int limit = 2;

struct rule **rules; // Needs doubly indirect, otherwise pointers messed up
mysize_t rules_capacity;
mysize_t rules_size;

void better_cycle_detect_impl(int cur, unsigned char *no_cycles, unsigned char *parents, int mark_traversed)
{
  struct linked_list_node *node;
  if (no_cycles[cur])
  {
    return;
  }
  if (parents[cur])
  {
    size_t i;
    fprintf(stderr, "stirmake: cycle found\n");
    for (i = 0; i < rules_size; i++)
    {
      if (parents[i])
      {
        fprintf(stderr, " rule in cycle: (");
        LINKED_LIST_FOR_EACH(node, &rules[i]->tgtlist)
        {
          struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
          fprintf(stderr, " %s", sttable[e->tgtidx].s);
        }
        fprintf(stderr, " )\n");
      }
    }
    errxit("cycle found, cannot proceed further");
    exit(2);
  }
  parents[cur] = 1;
  LINKED_LIST_FOR_EACH(node, &rules[cur]->deplist)
  {
    struct stirdep *e = ABCE_CONTAINER_OF(node, struct stirdep, llnode);
    int ruleid = stirdep_ruleid(e);
    if (e->is_dupe)
    {
      continue;
    }
    if (ruleid >= 0)
    {
      better_cycle_detect_impl(ruleid, no_cycles, parents, mark_traversed);
    }
  }
  parents[cur] = 0;
  no_cycles[cur] = 1;
  if (mark_traversed)
  {
    rules[cur]->is_traversed = 1;
  }
}

unsigned char *better_cycle_detect(int cur, int mark_traversed)
{
  unsigned char *no_cycles, *parents;
  no_cycles = malloc(rules_size);
  parents = malloc(rules_size);

  memset(no_cycles, 0, rules_size);
  memset(parents, 0, rules_size);

  better_cycle_detect_impl(cur, no_cycles, parents, mark_traversed);
  free(parents);
  return no_cycles;
}

void add_dep_from_rules(struct tgt *tgts, size_t tgtsz,
                        struct dep *deps, size_t depsz,
                        int phony, const char *prefix)
{
  char canbuf[1024], catbuf[1024];
  char *tmp, *can;
  size_t i, j;
  for (i = 0; i < tgtsz; i++)
  {
    if (tgts[i].namenodir[0] == '/')
    {
      tmp = pathcat1_buf(tgts[i].namenodir, catbuf, sizeof(catbuf));
    }
    else
    {
      tmp = pathcat2_buf(prefix, tgts[i].namenodir, catbuf, sizeof(catbuf));
    }
    can = canon_buf(tmp, canbuf, sizeof(canbuf));
    if (tmp != catbuf)
    {
      free(tmp);
    }
    mysize_t tgtidx = stringtab_add(can);
    if (can != canbuf)
    {
      free(can);
    }
    ins_add_dep(tgtidx, (mysize_t)-1, (mysize_t)-1, 0, !!phony, 0);
    for (j = 0; j < depsz; j++)
    {
      if (deps[j].namenodir[0] == '/')
      {
        tmp = pathcat1_buf(deps[j].namenodir, catbuf, sizeof(catbuf));
      }
      else
      {
        tmp = pathcat2_buf(prefix, deps[j].namenodir, catbuf, sizeof(catbuf));
      }
      can = canon_buf(tmp, canbuf, sizeof(canbuf));
      if (tmp != catbuf)
      {
        free(tmp);
      }
      mysize_t depidx = stringtab_add(can);
      if (can != canbuf)
      {
        free(can);
      }
      ins_add_dep(tgtidx, depidx, (mysize_t)-1, 0, !!phony, 0);
    }
  }
}

void add_dep(char **tgts, size_t tgts_sz,
             char **deps, size_t deps_sz,
             int phony, int auto_phony, int is_inc)
{
  size_t i, j;
  for (i = 0; i < tgts_sz; i++)
  {
    mysize_t tgtidx = stringtab_add(tgts[i]);
    ins_add_dep(tgtidx, (mysize_t)-1, (mysize_t)-1, 0, !!phony, !!is_inc);
    for (j = 0; j < deps_sz; j++)
    {
      mysize_t depidx = stringtab_add(deps[j]);
      ins_add_dep(tgtidx, depidx, (mysize_t)-1, !!auto_phony, !!phony, !!is_inc);
    }
  }
}


char **null_cmds[] = {NULL};

char **argdup(int ignore, int noecho, int ismake, char **cmdargs)
{
  size_t cnt = 0;
  size_t i;
  char **result;
  while (cmdargs[cnt] != NULL)
  {
    cnt++;
  }
  result = my_argmalloc((cnt+4) * sizeof(*result));
  result[0] = ignore ? st_ignore : st_noignore;
  result[1] = noecho ? st_noecho : st_echo;
  result[2] = ismake ? st_make : st_nomake;
  for (i = 0; i < cnt; i++)
  {
    // These are never freed now so copying is unnecessary
    //result[i+3] = my_argstrdup(cmdargs[i]);
    result[i+3] = cmdargs[i];
  }
  result[cnt+3] = NULL;
  return result;
}

char ***argsdupcnt(char ***cmdargs, size_t cnt)
{
  size_t i;
  char ***result;
  result = my_argmalloc((cnt+1) * sizeof(*result));
  for (i = 0; i < cnt; i++)
  {
    result[i] = cmdargs[i] ? argdup(0, 0, 0, cmdargs[i]) : NULL;
  }
  result[cnt] = NULL;
  return result;
}

mysize_t rule_cnt;

int add_dep_after_parsing_stage(char **tgts, size_t tgtsz,
                                char **deps, size_t depsz,
                                char *prefix,
                                int rec, int orderonly, int wait)
{
  size_t i, j;
  for (i = 0; i < tgtsz; i++)
  {
    char *fulltgt;
    char *can;
    char canbuf[1024];
    char catbuf[1024];
    size_t tgtidx;
    int ruleid;
    struct rule *rule;
    fulltgt = pathcat2_buf(prefix, tgts[i], catbuf, sizeof(catbuf));
    can = canon_buf(fulltgt, canbuf, sizeof(canbuf));
    tgtidx = stringtab_add(can);
    if (can != canbuf)
    {
      free(can);
    }
    if (fulltgt != catbuf)
    {
      free(fulltgt);
    }
    ruleid = get_ruleid_by_tgt(tgtidx);
    if (ruleid < 0)
    {
      fprintf(stderr, "stirmake: target %s not found while adding dep\n",
              tgts[i]);
      return -ENOENT;
    }
    rule = rules[ruleid];
    if (rule->is_executed)
    {
      fprintf(stderr, "stirmake: target %s already executed while adding dep\n",
              tgts[i]);
      return -EINVAL;
    }
    if (rule->is_queued)
    {
      fprintf(stderr, "stirmake: target %s already queued while adding dep\n",
              tgts[i]);
      return -EINVAL;
    }
    for (j = 0; j < depsz; j++)
    {
      char *fulldep;
      char catbuf[1024];
      char canbuf[1024];
      mysize_t depidx;
      int otherid;
      int ret;

      fulldep = pathcat2_buf(prefix, deps[j], catbuf, sizeof(catbuf));
      can = canon_buf(fulldep, canbuf, sizeof(canbuf));
      depidx = stringtab_add(can);
      if (can != canbuf)
      {
        free(can);
      }
      if (fulldep != catbuf)
      {
        free(fulldep);
      }

      otherid = get_ruleid_by_tgt(depidx);
      if (otherid < 0)
      {
        fprintf(stderr, "stirmake: dep %s not found while adding dep\n",
                deps[j]);
        return -ENOENT;
      }
      ret = ins_dep(rule, depidx, rule->diridx, (mysize_t)-1, rec, orderonly, wait, 0, 0);
      deps_remain_insert(rule, otherid);
      if (ret == 0)
      {
        ins_ruleid_by_dep2(depidx, ruleid, 1);
      }
    }
  }
  return 0;
}

void process_additional_deps_2(mysize_t global_scopeidx)
{
  struct add_dep_entry_block *blk;
  int last_ruleid = -1;
  mysize_t last_tgtidx = (mysize_t)-1;
  // Ensure all targets are present
  for (blk = add_dep_entry_block_first; blk != NULL; blk = blk->next)
  {
    size_t i;
    for (i = 0; i < blk->cnt; i++)
    {
      struct rule *rule;
      int ruleid;
      if (blk->e[i].depidx != (mysize_t)-1)
      {
        continue;
      }
      ruleid = get_ruleid_by_tgt(blk->e[i].tgtidx);
      if (ruleid >= 0)
      {
        continue;
      }
      if (rules_size >= rules_capacity)
      {
        size_t new_capacity = 2*rules_capacity + 16;
        rules = realloc(rules, new_capacity * sizeof(*rules));
        rules_capacity = new_capacity;
      }
      rule_cnt++;
      rule = my_malloc(rulesz());
      rules[rules_size] = rule;
      zero_rule(rule);
      rule->cmd.args = argsdupcnt(null_cmds, 1);
      rule->is_inc = 1;
      rule->diridx = stringtab_add_len(".", 1); // FIXME any ill side effects?

      rule->scopeidx = global_scopeidx;
      rule->ruleid = (int)rules_size++;
      ins_ruleid_by_tgt(blk->e[i].tgtidx, rule->ruleid, NULL, -1);
      ins_tgt(rule, blk->e[i].tgtidx, (mysize_t)-1, 0, NULL, -1);
      rule->is_phony = 0; // is_inc is enough
      rule->is_rectgt = 0;
      rule->is_detouch = 0;
    }
  }
  // Auto-phony-adder
#if 0
  for (blk = add_dep_entry_block_first; blk != NULL; blk = blk->next)
  {
    size_t i;
    for (i = 0; i < blk->cnt; i++)
    {
      struct rule *rule;
      int ruleid;
      if (1 || !blk->e[i].auto_phony) // now handled via is_inc
      {
        continue;
      }
      ruleid = get_ruleid_by_tgt(blk->e[i].depidx);
      if (ruleid >= 0)
      {
        continue;
      }
      if (rules_size >= rules_capacity)
      {
        size_t new_capacity = 2*rules_capacity + 16;
        rules = realloc(rules, new_capacity * sizeof(*rules));
        rules_capacity = new_capacity;
      }
      rule_cnt++;
      rule = my_malloc(rulesz());
      rules[rules_size] = rule;
      zero_rule(rule);
      rule->cmd.args = argsdupcnt(null_cmds, 1);
      rule->is_inc = 1;
      rule->diridx = stringtab_add_len(".", 1); // FIXME any ill side effects?

      rule->scopeidx = global_scopeidx;
      rule->ruleid = (int)rules_size++;
      ins_ruleid_by_tgt(blk->e[i].depidx, rule->ruleid, NULL, -1);
      ins_tgt(rule, blk->e[i].depidx, (mysize_t)-1, 0, NULL, -1);
      rule->is_phony = 0; // is_inc is enough
      rule->is_rectgt = 0;
      rule->is_detouch = 0;
    }
  }
#endif
  for (blk = add_dep_entry_block_first; blk != NULL; blk = blk->next)
  {
    size_t i;
    for (i = 0; i < blk->cnt; i++)
    {
      mysize_t cur_tgtidx = blk->e[i].tgtidx;
      int ruleid;
      struct rule *rule;
      if (cur_tgtidx == last_tgtidx && last_ruleid >= 0)
      {
        ruleid = last_ruleid;
      }
      else
      {
        ruleid = get_ruleid_by_tgt(cur_tgtidx);
	last_ruleid = ruleid;
      }
      last_tgtidx = cur_tgtidx;
      if (ruleid < 0)
      {
        if (rules_size >= rules_capacity)
        {
          size_t new_capacity = 2*rules_capacity + 16;
          rules = realloc(rules, new_capacity * sizeof(*rules));
          rules_capacity = new_capacity;
        }
        rule_cnt++;
        rule = my_malloc(rulesz());
        rules[rules_size] = rule;
        //rule = &rules[rules_size];
        //printf("adding tgt: %s\n", entry->tgt);
        zero_rule(rule);
        rule->cmd.args = argsdupcnt(null_cmds, 1);
        rule->is_inc = 1;
        rule->diridx = stringtab_add_len(".", 1); // FIXME any ill side effects?
  
        rule->scopeidx = global_scopeidx;
        rule->ruleid = (int)rules_size++;
        ins_ruleid_by_tgt(cur_tgtidx, rule->ruleid, NULL, -1);
        ins_tgt(rule, cur_tgtidx, (mysize_t)-1, 0, NULL, -1);
        if (blk->e[i].depidx != (mysize_t)-1)
        {
          int ret;
          ret = ins_dep(rule, blk->e[i].depidx, rule->diridx, (mysize_t)-1, 0, 0, 0, 0, !!blk->e[i].is_inc);
          if (ret == 0)
          {
            if (get_ruleid_by_tgt(blk->e[i].depidx) < 0)
            {
              if (debug)
              {
                print_indent();
                printf("Omitting-1 ruleid_by_dep for dep %s of rule %s\n", sttable[blk->e[i].depidx].s, sttable[cur_tgtidx].s);
              }
            }
            else
            {
              ins_ruleid_by_dep2(blk->e[i].depidx, rule->ruleid, 1);
            }
          }
        }
        rule->is_phony = !!blk->e[i].tgt_phony;
        rule->is_rectgt = 0;
        rule->is_detouch = 0;
        continue;
      }
      rule = rules[ruleid];
      rule->is_phony |= (!!blk->e[i].tgt_phony);
      if (blk->e[i].depidx != (mysize_t)-1)
      {
        int ret;
        ret = ins_dep(rule, blk->e[i].depidx, rule->diridx, (mysize_t)-1, 0, 0, 0, 0, !!blk->e[i].is_inc);
        if (ret == 0)
        {
          if (ins_ruleid_by_dep2(blk->e[i].depidx, rule->ruleid, 1) != 0)
          {
            if (debug)
            {
              print_indent();
              printf("Omitting-2 ruleid_by_dep for dep %s of rule %s\n", sttable[blk->e[i].depidx].s, sttable[cur_tgtidx].s);
            }
          }
        }
      }
    }
  }
}

void add_rule(struct tgt *tgts, size_t tgtsz,
              struct dep *deps, size_t depsz,
              struct cmdsrc *shells,
              int phony, int rectgt, int detouch, int maybe, int dist,
              int cleanhook, int distcleanhook, int bothcleanhook,
              char *prefix, mysize_t scopeidx, int lineno,
              const char *meat)
{
  char catbuf[1024], canbuf[1024];
  struct rule *rule;
  size_t i;

  if (tgtsz <= 0)
  {
    errxit("Rules must have at least 1 target");
    exit(2);
  }
  if (phony && tgtsz != 1)
  {
    errxit("Phony rules must not have multiple targets");
    exit(2);
  }
  if (debug)
  {
    print_indent();
    printf("Rule %s (%s): add_rule\n", tgts[0].namenodir, prefix);
  }
  if (rules_size >= rules_capacity)
  {
    size_t new_capacity = 2*rules_capacity + 16;
    rules = realloc(rules, new_capacity * sizeof(*rules));
    rules_capacity = new_capacity;
  }
  rule_cnt++;
  rule = my_malloc(rulesz());
  rules[rules_size] = rule;

  zero_rule(rule);
  rule->cmdsrc = *shells; // FIXME duplicate?
  rule->scopeidx = scopeidx;
  rule->ruleid = (int)rules_size++;
  rule->is_phony = !!phony;
  rule->is_maybe = !!maybe;
  rule->is_rectgt = !!rectgt;
  rule->is_detouch = !!detouch;
  rule->is_dist = !!dist;
  rule->is_cleanhook = !!cleanhook;
  rule->is_distcleanhook = !!distcleanhook;
  rule->is_bothcleanhook = !!bothcleanhook;
  rule->diridx = stringtab_add(prefix);
  if (meat)
  {
    rule->meatidx = stringtab_add(meat);
  }
  else
  {
    rule->meatidx = (mysize_t)-1;
  }

  for (i = 0; i < tgtsz; i++)
  {
    char *tmp, *can;
    mysize_t tgtidx, tgtidxnodir;
    if (tgts[i].namenodir[0] == '/')
    {
      tmp = pathcat1_buf(tgts[i].namenodir, catbuf, sizeof(catbuf));
    }
    else
    {
      tmp = pathcat2_buf(prefix, tgts[i].namenodir, catbuf, sizeof(catbuf));
    }
    can = canon_buf(tmp, canbuf, sizeof(canbuf));
    if (cleanhook || distcleanhook || bothcleanhook)
    {
      size_t len = strlen(can);
      char *can2 = malloc(len+4);
      memcpy(can2, can, len);
      can2[len] = '/';
      can2[len+1] = '/';
      can2[len+2] = '/';
      can2[len+3] = '\0';
      if (can != canbuf)
      {
        free(can);
      }
      can = can2;
    }
    if (tmp != catbuf)
    {
      free(tmp);
    }
    tgtidx = stringtab_add(can);
    tgtidxnodir = stringtab_add(tgts[i].namenodir);
    ins_tgt(rule, tgtidx, tgtidxnodir, !!dist, prefix, lineno);
    ins_ruleid_by_tgt(tgtidx, rule->ruleid, prefix, lineno);
    if (can != canbuf)
    {
      free(can);
    }
  }
  for (i = 0; i < depsz; i++)
  {
    char *tmp, *can;
    mysize_t nameidx, nameidxnodir;
    if (deps[i].namenodir[0] == '/')
    {
      tmp = pathcat1_buf(deps[i].namenodir, catbuf, sizeof(catbuf));
    }
    else
    {
      tmp = pathcat2_buf(prefix, deps[i].namenodir, catbuf, sizeof(catbuf));
    }
    can = canon_buf(tmp, canbuf, sizeof(canbuf));
    if (tmp != catbuf)
    {
      free(tmp);
    }
    nameidx = stringtab_add(can);
    nameidxnodir = stringtab_add(deps[i].namenodir);
    if (ins_dep(rule, nameidx, rule->diridx, nameidxnodir, !!deps[i].rec, !!deps[i].orderonly, !!deps[i].wait, 1, 0) == 0)
    {
      ins_ruleid_by_dep2(nameidx, rule->ruleid, 0);
    }
    if (can != canbuf)
    {
      free(can);
    }
  }
}

int *ruleids_to_run;
mysize_t ruleids_to_run_size;
mysize_t ruleids_to_run_capacity;

//std::unordered_map<pid_t, int> ruleid_by_pid;

char *print_cmd(const char *tgtname, const char *prefix, char **argiter_orig, int ret)
{
  size_t argcnt = 0;
  char **argiter = argiter_orig;
  size_t i;
  size_t tlen = 0;
  char *tbuf = NULL;
  size_t toff = 0;
  int tret;
  while (*argiter != NULL)
  {
    argiter++;
    argcnt++;
  }
  tlen += 1; // "["
  tlen += strlen(tgtname);
  tlen += 2; // ", "
  tlen += strlen(prefix);
  tlen += 2; // "] "
  for (i = 0; i < argcnt; i++)
  {
    tlen += strlen(argiter_orig[i]);
    tlen += 1;
  }
  tlen += 1; // "\n"
  tlen += 1; // "\0"
  /* Observe how we use standard malloc. A custom allocator using MAP_SHARED
   * would not be safe to use in the child process. */
  tbuf = malloc(tlen);
  tret = snprintf(tbuf+toff, tlen-toff, "[%s, %s]", prefix, tgtname);
  if (tret < 0 || (size_t)tret >= tlen - toff)
  {
    if (ret)
    {
      free(tbuf);
      errxit("Unable to print command");
      exit(2);
      return NULL;
    }
    _exit(1);
  }
  toff += (size_t)tret;
  for (i = 0; i < argcnt; i++)
  {
    tret = snprintf(tbuf+toff, tlen-toff, " %s", argiter_orig[i]);
    if (tret < 0 || (size_t)tret >= tlen - toff)
    {
      if (ret)
      {
        free(tbuf);
        errxit("Unable to print command");
        exit(2);
        return NULL;
      }
      _exit(1);
    }
    toff += (size_t)tret;
  }
  tret = snprintf(tbuf+toff, tlen-toff, "\n");
  if (tret < 0 || (size_t)tret >= tlen - toff)
  {
    if (ret)
    {
      free(tbuf);
      errxit("Unable to print command");
      exit(2);
      return NULL;
    }
    _exit(1);
  }
  toff += (size_t)tret;
  /*
   * Why not writev, you ask. Well, writev is not atomic for terminals, at
   * least on Linux. And, writev requires us to allocate memory for the iovec
   * list too.
   */
  if (ret)
  {
    if (silent)
    {
      free(tbuf);
      return NULL;
    }
    else
    {
      return tbuf;
    }
  }
  if (!silent)
  {
    writeall(1, tbuf, toff);
  }
  free(tbuf); // We could let it leak, too...
  return NULL;
}
char *get_cmd_only(char **argiter_orig)
{
  size_t argcnt = 0;
  char **argiter = argiter_orig;
  size_t i;
  size_t tlen = 0;
  char *tbuf = NULL;
  size_t toff = 0;
  int tret;
  while (*argiter != NULL)
  {
    argiter++;
    argcnt++;
  }
  for (i = 0; i < argcnt; i++)
  {
    tlen += strlen(argiter_orig[i]);
    tlen += 1;
  }
  tlen += 1; // "\0"
  /* Observe how we use standard malloc. A custom allocator using MAP_SHARED
   * would not be safe to use in the child process. */
  tbuf = malloc(tlen);
  if (tbuf == NULL)
  {
    return NULL;
  }
  for (i = 0; i < argcnt; i++)
  {
    tret = snprintf(tbuf+toff, tlen-toff, (i==0) ? "%s" : " %s", argiter_orig[i]);
    if (tret < 0 || (size_t)tret >= tlen - toff)
    {
      return NULL;
    }
    toff += (size_t)tret;
  }
  return tbuf;
}

const char *makecmds[] = {
  "make",
  "gmake",
  "/usr/bin/make",
  "/usr/bin/gmake",
  "/usr/local/bin/make",
  "/usr/local/bin/gmake",
  "/usr/pkg/bin/make",
  "/usr/pkg/bin/gmake",
  "/opt/bin/make",
  "/opt/bin/gmake",
  "/opt/gnu/bin/make",
  "/opt/gnu/bin/gmake",
  "/bin/make",
  "/bin/gmake",
};
mysize_t makecmds_size = sizeof(makecmds)/sizeof(*makecmds);

int is_makecmd(const char *cmd)
{
  size_t i;
  for (i = 0; i < makecmds_size; i++)
  {
    if (strcmp(cmd, makecmds[i]) == 0)
    {
      return 1;
    }
  }
  return 0;
}

void do_makecmd(int ismake, const char *cmd, int create_fd, int create_make_fd, int outpipewr, int just_setenv)
{
  if (ismake || is_makecmd(cmd))
  {
    char env[128] = {0};
    if (create_make_fd)
    {
      if (jobserver_fifo)
      {
        snprintf(env, sizeof(env), " --jobserver-auth=fifo:%s -j -Orecurse",
	         jobserver_fifo);
      }
      else
      {
        snprintf(env, sizeof(env), " --jobserver-fds=%d,%d --jobserver-auth=%d,%d -j -Orecurse",
                 jobserver_fd[0], jobserver_fd[1],
                 jobserver_fd[0], jobserver_fd[1]);
      }
    }
    else if (create_fd)
    {
      if (jobserver_fifo)
      {
        snprintf(env, sizeof(env), " --jobserver-auth=fifo:%s -j -O%s",
	         jobserver_fifo, out_sync == OUT_SYNC_LINE ? "line" : "target");
      }
      else
      {
        snprintf(env, sizeof(env), " --jobserver-fds=%d,%d --jobserver-auth=%d,%d -j -O%s",
                 jobserver_fd[0], jobserver_fd[1],
                 jobserver_fd[0], jobserver_fd[1],
                 out_sync == OUT_SYNC_LINE ? "line" : "target");
      }
    }
    else
    {
      if (jobserver_fifo)
      {
        snprintf(env, sizeof(env), " --jobserver-auth=fifo:%s -j",
	         jobserver_fifo);
      }
      else
      {
        snprintf(env, sizeof(env), " --jobserver-fds=%d,%d --jobserver-auth=%d,%d -j",
                 jobserver_fd[0], jobserver_fd[1],
                 jobserver_fd[0], jobserver_fd[1]);
      }
    }
    setenv("MAKEFLAGS", env, 1);
    if (just_setenv)
    {
      return;
    }
    if (create_make_fd)
    {
      if (dup2(outpipewr, 1) < 0)
      {
        write(2, "DUP2ERR\n", 8);
        _exit(1);
      }
      if (dup2(outpipewr, 2) < 0)
      {
        write(2, "DUP2ERR\n", 8);
        _exit(1);
      }
      close(outpipewr);
    }
    else if (create_fd)
    {
      close(outpipewr);
    }
  }
  else
  {
    if (just_setenv)
    {
      return;
    }
    close(jobserver_fd[0]);
    close(jobserver_fd[1]);
    if (create_fd)
    {
      if (dup2(outpipewr, 1) < 0)
      {
        write(2, "DUP2ERR\n", 8);
        _exit(1);
      }
      if (dup2(outpipewr, 2) < 0)
      {
        write(2, "DUP2ERR\n", 8);
        _exit(1);
      }
      close(outpipewr);
    }
  }
}

void set_nonblock(int fd);

extern FILE *dbf;

int ready_touch(int ruleid)
{
  return (rules[ruleid]->curtgt_touch == NULL);
}
int ready(int ruleid)
{
  return (rules[ruleid]->cmd.args[rules[ruleid]->cmdidx] == NULL);
}

pid_t spawn_child_touch(int ruleid, int create_fd, int create_make_fd, int *fdout)
{
  pid_t pid;
  const char *dir = sttable[rules[ruleid]->diridx].s;
  int outpipe[2] = {-1,-1};
  int outpiperd = -1, outpipewr = -1;
  struct stirtgt *first_tgt =
    ABCE_CONTAINER_OF(rules[ruleid]->tgtlist.node.next, struct stirtgt, llnode);
  int fdcurdir;
  int ret;
  int was_first = 0;
  char *cmdprint = NULL;
  char *args[3] = {"touch", NULL, NULL};
  char *args_true[2] = {"true", NULL};
#ifndef HAVE_POSIX_SPAWN
  int pipecloexec[2];
#endif

#ifdef HAVE_POSIX_SPAWN
  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_t *file_actionsp;
  file_actionsp = NULL;
#endif

  if (rules[ruleid]->curtgt_touch == NULL)
  {
    rules[ruleid]->curtgt_touch = first_tgt;
    was_first = 1;
  }
  if (1)
  {
    char *tgtname = neighpath(sttable[rules[ruleid]->diridx].s, sttable[rules[ruleid]->curtgt_touch->tgtidx].s);
    args[1] = tgtname;
  }
  if (1)
  {
    struct linked_list_node *node = rules[ruleid]->curtgt_touch->llnode.next;
    if (node == &rules[ruleid]->tgtlist.node)
    {
      rules[ruleid]->curtgt_touch = NULL;
    }
    else
    {
      rules[ruleid]->curtgt_touch = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
    }
  }

  //args = cmd.args;

  if (create_fd)
  {
    if (pipe(outpipe) != 0)
    {
      errxit("can't pipe output of command");
      my_abort();
    }
    outpiperd = outpipe[0];
    outpipewr = outpipe[1];
    set_nonblock(outpiperd); // not for outpipewr
    fcntl(outpiperd, F_SETFD, fcntl(outpiperd, F_GETFD) | FD_CLOEXEC);
  }

  fdcurdir = open(".", O_RDONLY);
  if (fdcurdir < 0)
  {
    errxit("can't open current directory");
    exit(2);
  }
  fcntl(fdcurdir, F_SETFD, fcntl(fdcurdir, F_GETFD) | FD_CLOEXEC);

#ifdef HAVE_POSIX_SPAWN
  ret = posix_spawn_file_actions_init(&file_actions);
  if (ret != 0)
  {
    errxit("can't init file actions");
    exit(2);
  }
  ret = posix_spawn_file_actions_addclose(&file_actions, fileno(dbf));
  if (ret != 0)
  {
    errxit("can't add file actions");
    exit(2);
  }
  ret = posix_spawn_file_actions_addclose(&file_actions, self_pipe_fd[0]);
  if (ret != 0)
  {
    errxit("can't add file actions");
    exit(2);
  }
  ret = posix_spawn_file_actions_addclose(&file_actions, self_pipe_fd[1]);
  if (ret != 0)
  {
    errxit("can't add file actions");
    exit(2);
  }
  ret = posix_spawn_file_actions_addclose(&file_actions, jobserver_fd[0]);
  if (ret != 0)
  {
    errxit("can't add file actions");
    exit(2);
  }
  ret = posix_spawn_file_actions_addclose(&file_actions, jobserver_fd[1]);
  if (ret != 0)
  {
    errxit("can't add file actions");
    exit(2);
  }
  if (create_fd)
  {
    ret = posix_spawn_file_actions_adddup2(&file_actions, outpipewr, 1);
    if (ret != 0)
    {
      errxit("can't add file actions");
      exit(2);
    }
    ret = posix_spawn_file_actions_adddup2(&file_actions, outpipewr, 2);
    if (ret != 0)
    {
      errxit("can't add file actions");
      exit(2);
    }
    ret = posix_spawn_file_actions_addclose(&file_actions, outpipewr);
    if (ret != 0)
    {
      errxit("can't add file actions");
      exit(2);
    }
    ret = posix_spawn_file_actions_addclose(&file_actions, outpiperd);
    if (ret != 0)
    {
      errxit("can't add file actions");
      exit(2);
    }
  }
  file_actionsp = &file_actions;
#endif

  if (chdir(dir) != 0)
  {
    errxit("Unable to change directory");
    exit(2);
  }

  do_makecmd(0, "touch", create_fd, create_make_fd, outpipewr, 1);
  cmdprint = print_cmd(sttable[first_tgt->tgtidx].s, dir, args, 1);
  if (cmdprint != NULL && create_fd /* && out_sync != OUT_SYNC_LINE */)
  {
    syncbuf_append(&rules[ruleid]->output, cmdprint, strlen(cmdprint));
  }
  else if (cmdprint != NULL)
  {
    writeall(1, cmdprint, strlen(cmdprint));
  }
  free(cmdprint);

#ifdef HAVE_POSIX_SPAWN
  if (posix_spawnp(&pid, dry_run ? "true" : "touch", file_actionsp, NULL, dry_run ? args_true : args, environ) != 0)
  {
    errxit("Unable to spawn child '%s' for target '%s' in directory '%s'", "touch", sttable[first_tgt->tgtidx].s, sttable[rules[ruleid]->diridx].s);
    exit(2);
  }
#else
  if (pipe(pipecloexec) != 0)
  {
    errxit("Unable to create pipe");
    exit(2);
  }
#ifdef USE_VFORK
  pid = vfork();
#else
  pid = fork();
#endif
  if (pid < 0)
  {
    errxit("Unable to fork child");
    exit(2);
  }
  else if (pid == 0)
  {
    close(fileno(dbf));
    close(self_pipe_fd[0]);
    close(self_pipe_fd[1]);
    close(jobserver_fd[0]);
    close(jobserver_fd[1]);
    close(pipecloexec[0]);
    fcntl(pipecloexec[1], F_SETFD, fcntl(pipecloexec[1], F_GETFD) | FD_CLOEXEC);
    if (create_fd)
    {
      dup2(outpipewr, 1);
      dup2(outpipewr, 2);
      close(outpipewr);
      close(outpiperd);
    }
    if (dry_run)
    {
      _exit(0);
    }
    // TODO create CLOEXEC pipe, write 1 char into it if "touch" fails,
    // read the EOF or the 1 char from it
    execvp("touch", args);
    write(pipecloexec[1], "E", 1);
    _exit(1);
  }
#endif

  if (fchdir(fdcurdir) != 0)
  {
    errxit("can't chdir back");
    exit(2);
  }
  unsetenv("MAKEFLAGS");
  close(fdcurdir);
#ifndef HAVE_POSIX_SPAWN
  close(pipecloexec[1]);
#endif


  if (was_first)
  {
    children++;
  }
  if (create_fd)
  {
    close(outpipewr);
  }
  ruleid_by_pid_insert(ruleid, pid, outpiperd, 0);
  rules[ruleid]->is_forked = 1;
  if (fdout)
  {
    *fdout = outpiperd;
  }
#ifndef HAVE_POSIX_SPAWN
  for (;;)
  {
    char ch;
    ssize_t bytes_read = read(pipecloexec[0], &ch, 1);
    if (bytes_read < 0)
    {
      break;
    }
    if (bytes_read == 0)
    {
      break;
    }
    if (bytes_read < 0 && errno == EINTR)
    {
      continue;
    }
    if (bytes_read == 1)
    {
      errxit("Unable to exec child '%s' for target '%s' in directory '%s'", "touch", sttable[first_tgt->tgtidx].s, sttable[rules[ruleid]->diridx].s);
      fflush(stderr);
      exit(2);
    }
  }
  close(pipecloexec[0]);
#endif
  return pid;
}

pid_t spawn_child(int ruleid, int create_fd, int create_make_fd, int *fdout)
{
  pid_t pid;
  const char *dir = sttable[rules[ruleid]->diridx].s;
  char **oneargiter;
  int outpipe[2] = {-1,-1};
  int outpiperd = -1, outpipewr = -1;
  int fdcurdir = -1;
  int ret;
  char *cmdprint = NULL;
  int was_first = 0;
  char **args;
  char *args_true[2] = {"true", NULL};
  char *cmd;
  int ismake;
#ifndef HAVE_POSIX_SPAWN
  int pipecloexec[2];
#endif
  int fd_in_use = 0;
  struct stirtgt *first_tgt =
    ABCE_CONTAINER_OF(rules[ruleid]->tgtlist.node.next, struct stirtgt, llnode);
#ifdef HAVE_POSIX_SPAWN
  posix_spawn_file_actions_t file_actions;
  posix_spawn_file_actions_t *file_actionsp;

  file_actionsp = NULL;
#endif

  if (rules[ruleid]->cmdidx == 0)
  {
    was_first = 1;
  }
  args = rules[ruleid]->cmd.args[rules[ruleid]->cmdidx];
  rules[ruleid]->cmdidx++;

  if (create_fd)
  {
    if (pipe(outpipe) != 0)
    {
      errxit("can't pipe output of command");
      my_abort();
    }
    outpiperd = outpipe[0];
    outpipewr = outpipe[1];
    set_nonblock(outpiperd); // not for outpipewr
    fcntl(outpiperd, F_SETFD, fcntl(outpiperd, F_GETFD) | FD_CLOEXEC);
  }

  if (args == NULL)
  {
    printf("no arguments\n");
    my_abort();
  }

  if (debug)
  {
    print_indent();
    printf("start args:\n");
  }
  if (1)
  {
    oneargiter = args;
    if (debug)
    {
      print_indent();
      printf(" ");
    }
    while (*oneargiter)
    {
      if (debug)
      {
        printf(" %s", *oneargiter);
      }
      oneargiter++;
    }
    if (debug)
    {
      printf("\n");
    }
  }
  if (debug)
  {
    print_indent();
    printf("end args\n");
  }

  ismake = (strcmp(args[2], st_make) == 0);
  cmd = args[3];

  fdcurdir = open(".", O_RDONLY);
  if (fdcurdir < 0)
  {
    errxit("can't open current directory");
    exit(2);
  }
  fcntl(fdcurdir, F_SETFD, fcntl(fdcurdir, F_GETFD) | FD_CLOEXEC);

#ifdef HAVE_POSIX_SPAWN
  ret = posix_spawn_file_actions_init(&file_actions);
  if (ret != 0)
  {
    errxit("can't init file actions");
    exit(2);
  }
  ret = posix_spawn_file_actions_addclose(&file_actions, fileno(dbf));
  if (ret != 0)
  {
    errxit("can't add file actions");
    exit(2);
  }
  ret = posix_spawn_file_actions_addclose(&file_actions, self_pipe_fd[0]);
  if (ret != 0)
  {
    errxit("can't add file actions");
    exit(2);
  }
  ret = posix_spawn_file_actions_addclose(&file_actions, self_pipe_fd[1]);
  if (ret != 0)
  {
    errxit("can't add file actions");
    exit(2);
  }
  if (!ismake && !is_makecmd(cmd))
  {
    ret = posix_spawn_file_actions_addclose(&file_actions, jobserver_fd[0]);
    if (ret != 0)
    {
      errxit("can't add file actions");
      exit(2);
    }
    ret = posix_spawn_file_actions_addclose(&file_actions, jobserver_fd[1]);
    if (ret != 0)
    {
      errxit("can't add file actions");
      exit(2);
    }
    if (create_fd) // !ismake
    {
      ret = posix_spawn_file_actions_adddup2(&file_actions, outpipewr, 1);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
      ret = posix_spawn_file_actions_adddup2(&file_actions, outpipewr, 2);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
      ret = posix_spawn_file_actions_addclose(&file_actions, outpipewr);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
      ret = posix_spawn_file_actions_addclose(&file_actions, outpiperd);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
      //fd_in_use = 1;
    }
  }
  else // ismake
  {
    if (create_make_fd)
    {
      ret = posix_spawn_file_actions_adddup2(&file_actions, outpipewr, 1);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
      ret = posix_spawn_file_actions_adddup2(&file_actions, outpipewr, 2);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
      ret = posix_spawn_file_actions_addclose(&file_actions, outpipewr);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
      ret = posix_spawn_file_actions_addclose(&file_actions, outpiperd);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
      //fd_in_use = 1;
    }
    else if (create_fd)
    {
      ret = posix_spawn_file_actions_addclose(&file_actions, outpipewr);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
      ret = posix_spawn_file_actions_addclose(&file_actions, outpiperd);
      if (ret != 0)
      {
        errxit("can't add file actions");
        exit(2);
      }
    }
  }
  file_actionsp = &file_actions;
#endif

  if (!ismake && !is_makecmd(cmd))
  {
    if (create_fd) // !ismake
    {
      fd_in_use = 1;
    }
  }
  else
  {
    if (create_make_fd) // ismake
    {
      fd_in_use = 1;
    }
  }

  if (chdir(dir) != 0)
  {
    errxit("Unable to change directory");
    exit(2);
  }

  do_makecmd(ismake, cmd, create_fd, create_make_fd, outpipewr, 1);
  if (strcmp(args[1], st_noecho) != 0 || dry_run)
  {
    cmdprint = print_cmd(sttable[first_tgt->tgtidx].s, dir, &args[3], 1);
  }
  if (cmdprint != NULL && fd_in_use /* && out_sync != OUT_SYNC_LINE*/)
  {
    syncbuf_append(&rules[ruleid]->output, cmdprint, strlen(cmdprint));
  }
  else if (cmdprint != NULL)
  {
    writeall(1, cmdprint, strlen(cmdprint));
  }
  free(cmdprint);

#ifdef HAVE_POSIX_SPAWN
  if (posix_spawnp(&pid, dry_run ? "true" : cmd, file_actionsp, NULL, dry_run ? args_true : &args[3], environ) != 0)
  {
    errxit("Unable to spawn child '%s' for target '%s' in directory '%s'", cmd, sttable[first_tgt->tgtidx].s, sttable[rules[ruleid]->diridx].s);
    exit(2);
  }
#else
  if (pipe(pipecloexec) != 0)
  {
    errxit("Unable to create pipe");
    exit(2);
  }
#ifdef USE_VFORK
  pid = vfork();
#else
  pid = fork();
#endif
  if (pid < 0)
  {
    errxit("Unable to fork child");
    exit(2);
  }
  else if (pid == 0)
  {
    close(fileno(dbf));
    close(self_pipe_fd[0]);
    close(self_pipe_fd[1]);
    close(pipecloexec[0]);
    fcntl(pipecloexec[1], F_SETFD, fcntl(pipecloexec[1], F_GETFD) | FD_CLOEXEC);
    if (!ismake && !is_makecmd(cmd))
    {
      close(jobserver_fd[0]);
      close(jobserver_fd[1]);
      if (create_fd) // !ismake
      {
        dup2(outpipewr, 1);
        dup2(outpipewr, 2);
        close(outpipewr);
        close(outpiperd);
      }
    }
    else // ismake
    {
      if (create_make_fd)
      {
        dup2(outpipewr, 1);
        dup2(outpipewr, 2);
        close(outpipewr);
        close(outpiperd);
      }
      else if (create_fd)
      {
        close(outpipewr);
        close(outpiperd);
      }
    }
    if (dry_run)
    {
      _exit(0);
    }
    // TODO create CLOEXEC pipe, write 1 char into it if "touch" fails,
    // read the EOF or the 1 char from it
    execvp(args[3], &args[3]);
    write(pipecloexec[1], "E", 1);
    _exit(1);
  }
#endif

  if (fchdir(fdcurdir) != 0)
  {
    errxit("can't chdir back");
    exit(2);
  }
  unsetenv("MAKEFLAGS");
  close(fdcurdir);
#ifndef HAVE_POSIX_SPAWN
  close(pipecloexec[1]);
#endif

  if (was_first)
  {
    children++;
  }
  if (outpipewr >= 0)
  {
    close(outpipewr);
  }
  ruleid_by_pid_insert(ruleid, pid, outpiperd, strcmp(args[0], st_ignore) == 0);
  rules[ruleid]->is_forked = 1;
  if (fdout)
  {
    *fdout = outpiperd;
  }
#ifndef HAVE_POSIX_SPAWN
  for (;;)
  {
    char ch;
    ssize_t bytes_read = read(pipecloexec[0], &ch, 1);
    if (bytes_read < 0)
    {
      break;
    }
    if (bytes_read == 0)
    {
      break;
    }
    if (bytes_read < 0 && errno == EINTR)
    {
      continue;
    }
    if (bytes_read == 1)
    {
      errxit("Unable to exec child '%s' for target '%s' in directory '%s'", cmd, sttable[first_tgt->tgtidx].s, sttable[rules[ruleid]->diridx].s);
      fflush(stderr);
      exit(2);
    }
  }
  close(pipecloexec[0]);
#endif
  return pid;
}

void mark_executed(int ruleid, int was_actually_executed);

struct timespec rec_mtim(struct rule *r, const char *name)
{
  struct timespec max;
  struct stat statbuf;
  DIR *dir = opendir(name);
  //printf("Statting %s\n", name);
  if (stat(name, &statbuf) != 0)
  {
    errxit("can't open file %s", name);
    exit(2);
  }
  max = mtim_from_statbuf(&statbuf);
  if (lstat(name, &statbuf) != 0)
  {
    errxit("can't open file %s", name);
    exit(2);
  }
  if (ts_cmp(mtim_from_statbuf(&statbuf), max) > 0)
  {
    max = mtim_from_statbuf(&statbuf);
  }
  if (dir == NULL)
  {
    errxit("can't open dir %s", name);
    exit(2);
  }
  for (;;)
  {
    struct dirent *de = readdir(dir);
    struct timespec cur;
    size_t oldlen;
    size_t namcap;
    char *nam2;
    int found_rectgt = 0;
    if (de == NULL)
    {
      break;
    }
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
    {
      continue;
    }
    namcap = strlen(name) + 1 + strlen(de->d_name) + 1;
    nam2 = malloc(namcap);
    if (snprintf(nam2, namcap, "%s", name) >= (int)namcap)
    {
      errxit("Pathname too long");
      my_abort();
    }
    oldlen = strlen(nam2);
    if (snprintf(nam2+oldlen, namcap-oldlen,
                 "/%s", de->d_name) >= (int)(namcap-oldlen))
    {
      errxit("Pathname too long");
      my_abort();
    }
    //if (de->d_type == DT_DIR)
    if (0)
    {
      cur = rec_mtim(r, nam2);
    }
    else
    {
      if (stat(nam2, &statbuf) != 0)
      {
        errxit("can't open file %s", nam2);
        exit(2);
      }
      cur = mtim_from_statbuf(&statbuf);
      if (lstat(nam2, &statbuf) != 0)
      {
        errxit("can't open file %s", nam2);
        exit(2);
      }
      if (ts_cmp(mtim_from_statbuf(&statbuf), cur) > 0)
      {
        cur = mtim_from_statbuf(&statbuf);
      }
    }
    if (r->is_rectgt)
    {
      mysize_t stidx = stringtab_get(nam2);
      if (stidx != (mysize_t)-1)
      {
        if (rule_get_tgt(r, stidx) != NULL)
        {
          found_rectgt = 1;
        }
      }
    }
    if (ts_cmp(cur, max) > 0 && !found_rectgt)
    {
      //printf("nam2 file new %s\n", nam2);
      max = cur;
    }
    if (S_ISDIR(statbuf.st_mode))
    {
      cur = rec_mtim(r, nam2);
      if (ts_cmp(cur, max) > 0)
      {
        //printf("nam2 dir new %s\n", nam2);
        max = cur;
      }
    }
    free(nam2);
  }
  closedir(dir);
  return max;
}

void reccap_mtim(const char *name, struct timespec cap)
{
  struct stat statbuf;
  DIR *dir = opendir(name);
  //printf("Statting %s\n", name);
  if (dir == NULL)
  {
    errxit("can't open dir %s", name);
    exit(2);
  }
  for (;;)
  {
    struct dirent *de = readdir(dir);
    size_t namcap;
    size_t oldlen;
    char *nam2;
    if (de == NULL)
    {
      break;
    }
    if (strcmp(de->d_name, ".") == 0 || strcmp(de->d_name, "..") == 0)
    {
      continue;
    }
    namcap = strlen(name) + 1 + strlen(de->d_name) + 1;
    nam2 = malloc(namcap);
    if (snprintf(nam2, namcap, "%s", name) >= (int)namcap)
    {
      errxit("Pathname too long");
      my_abort();
    }
    oldlen = strlen(nam2);
    if (snprintf(nam2+oldlen, namcap-oldlen,
                 "/%s", de->d_name) >= (int)(namcap-oldlen))
    {
      errxit("Pathname too long");
      my_abort();
    }
    //if (de->d_type == DT_DIR)
    if (0)
    {
      reccap_mtim(nam2, cap);
    }
    else
    {
      if (stat(nam2, &statbuf) != 0)
      {
        errxit("can't open file %s", nam2);
        exit(2);
      }
      if (ts_cmp(mtim_from_statbuf(&statbuf), cap) > 0)
      {
        utimensat_both_emul(nam2, cap, 0, 0);
      }
      if (lstat(nam2, &statbuf) != 0)
      {
        errxit("can't open file %s", nam2);
        exit(2);
      }
      if (ts_cmp(mtim_from_statbuf(&statbuf), cap) > 0)
      {
        utimensat_both_emul(nam2, cap, 1, 0);
      }
    }
#if 0
    int found_rectgt = 0;
    if (r->is_rectgt)
    {
      mysize_t stidx = stringtab_get(nam2);
      if (stidx != (mysize_t)-1)
      {
        if (rule_get_tgt(r, stidx) != NULL)
        {
          found_rectgt = 1;
        }
      }
    }
#endif
    if (S_ISDIR(statbuf.st_mode))
    {
      reccap_mtim(nam2, cap);
    }
    free(nam2);
  }
  closedir(dir);

  if (stat(name, &statbuf) != 0)
  {
    errxit("can't open file %s", name);
    exit(2);
  }
  if (ts_cmp(mtim_from_statbuf(&statbuf), cap) > 0)
  {
    utimensat_both_emul(name, cap, 0, 0);
  }
  if (lstat(name, &statbuf) != 0)
  {
    errxit("can't open file %s", name);
    exit(2);
  }
  if (ts_cmp(mtim_from_statbuf(&statbuf), cap) > 0)
  {
    utimensat_both_emul(name, cap, 1, 0);
  }
}

void calc_cmd(int ruleid)
{
  struct rule *r = rules[ruleid];
  struct stirtgt *first_tgt =
    ABCE_CONTAINER_OF(r->tgtlist.node.next, struct stirtgt, llnode);
  int info_printed = 0;
  if (r->cmd.args != NULL)
  {
    return;
  }
  r->cmd.args = cmdsrc_eval(&abce, r, &info_printed);
  if (r->cmd.args == NULL)
  {
#if 0
    int i;
    fprintf(stderr, "Backtrace:\n");
    for (i = 0; i < abce.btsz; i++)
    {
      if (abce.btbase[i].typ == ABCE_T_S)
      {
        fprintf(stderr, "%s\n", abce_mba_str(abce.btbase[i].u.area));
      }
      else
      {
        fprintf(stderr, "(-)\n");
      }
    }
#endif
    if (!info_printed)
    {
      fprintf(stderr, "Additional information for error:\n");
      abce_mb_dump(&abce.err.mb);
      stir_opcode_dump(abce.err.opcode);
    }
    errxit("evaluating shell commands for '%s' (directory: '%s') failed, error: %s",
           sttable[first_tgt->tgtidx].s, sttable[r->diridx].s, stir_err_to_str(abce.err.code));
  }
}

void trace_add_vprintf(char **buf, size_t *cap, size_t *sz, const char *fmt, ...)
{
  va_list ap;
  int need = 0;
  int ret;
  va_start(ap, fmt);
  need = vsnprintf(NULL, 0, fmt, ap);
  if (need < 0)
  {
    errxit("trace_add_vprintf failed");
  }
  va_end(ap);
  need++;
  if (*cap - *sz < (size_t)need)
  {
    size_t cap2;
    char *buf2;
    cap2 = *cap + ((size_t)need - (*cap - *sz));
    buf2 = realloc(*buf, cap2);
    if (buf2 == NULL)
    {
      errxit("trace_add_vprintf failed, out of memory");
    }
    *buf = buf2;
    *cap = cap2;
  }
  va_start(ap, fmt);
  ret = vsnprintf(*buf + *sz, *cap - *sz, fmt, ap);
  if (ret < 0)
  {
    errxit("trace_add_vprintf failed");
  }
  va_end(ap);
  *sz += (size_t)ret;
}

int do_exec(int ruleid)
{
  struct rule *r = rules[ruleid];
  struct stirtgt *first_tgt =
    ABCE_CONTAINER_OF(r->tgtlist.node.next, struct stirtgt, llnode);
  int seen_no_remake = 0;
  char *tracebuf = NULL;
  size_t tracebufcap = 0;
  size_t tracebufsz = 0;
  //Rule &r = rules.at(ruleid);
  if (debug)
  {
    print_indent();
    printf("do_exec %s\n", sttable[first_tgt->tgtidx].s);
  }
  if (do_trace)
  {
    trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, "update target '%s' due to:", sttable[first_tgt->tgtidx].s);
  }
  if (!r->is_queued)
  {
    int has_to_exec = 0;
    if (unconditional)
    {
      if (do_trace)
      {
        trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " unconditional build of everything");
      }
      has_to_exec = 1;
    }
    calc_cmd(ruleid);
    if (!r->is_phony && !r->is_inc && !linked_list_is_empty(&r->deplist))
    {
      int seen_nonphony = 0;
      int seen_tgt = 0;
      struct timespec st_mtim = {.tv_sec=0}, st_mtimtgt = {.tv_sec=0};
      struct linked_list_node *node;
      LINKED_LIST_FOR_EACH(node, &r->deplist)
      {
        struct stirdep *e = ABCE_CONTAINER_OF(node, struct stirdep, llnode);
        struct stat statbuf;
        int depid = stirdep_ruleid(e);
        if (e->is_dupe)
        {
          continue;
        }
        if (ispretend(sttable[e->nameidx].s, PRETEND_MODIFIED) || sttable[e->nameidx].is_remade)
        {
          if (do_trace)
          {
            trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " '%s'", sttable[e->nameidx].s);
            if (ispretend(sttable[e->nameidx].s, PRETEND_MODIFIED))
            {
              trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " (pretend)");
            }
          }
          has_to_exec = 1;
        }
        if (depid >= 0)
        {
          if (rules[depid]->is_phony)
          {
            if (debug)
            {
              print_indent();
              printf("rule %d/%s is phony\n", depid, sttable[e->nameidx].s);
            }
            if (do_trace)
            {
              trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " '%s' (phony)", sttable[e->nameidx].s);
            }
            has_to_exec = 1;
            continue;
          }
          if (debug)
          {
            print_indent();
            printf("ruleid %d/%s not phony\n", depid, sttable[e->nameidx].s);
          }
        }
        else
        {
          if (debug)
          {
            print_indent();
            printf("ruleid for tgt %s not found\n", sttable[e->nameidx].s);
          }
        }
        if (e->is_recursive)
        {
          struct timespec st_rectim = rec_mtim(r, sttable[e->nameidx].s);
          if (!seen_nonphony || ts_cmp(st_rectim, st_mtim) > 0)
          {
            st_mtim = st_rectim;
          }
          seen_nonphony = 1;
          continue;
        }
        int recommended = 0;
        struct stathashentry *she;
        she = lstat_cached(e->nameidx);
        if (she->ret != 0)
        {
          if (do_trace)
          {
            trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " '%s' (nonexistent)", sttable[e->nameidx].s);
          }
          has_to_exec = 1;
          // break; // No break, we want to get accurate st_mtim
          continue;
          //perror("can't lstat");
          //fprintf(stderr, "file was: %s\n", it->c_str());
          //my_abort();
        }
        if (S_ISDIR(she->st_mode) && !e->is_orderonly && !recommended)
        {
          char *tgtname = sttable[ABCE_CONTAINER_OF(r->tgtlist.node.next, struct stirtgt, llnode)->tgtidx].s;
          printf("stirmake: Recommend making directory dep %s of %s either @orderonly or @recdep.\n", sttable[e->nameidx].s, tgtname);
          recommended = 1;
        }
        if (!e->is_orderonly)
        {
          if (debug)
          {
            print_indent();
            printf("dep: %llu %llu\n", (unsigned long long)she->st_mtim.tv_sec, (unsigned long long)she->st_mtim.tv_nsec);
          }
          if (!seen_nonphony || ts_cmp(she->st_mtim, st_mtim) > 0)
          {
            if (!ispretend(sttable[e->nameidx].s, PRETEND_VERY_OLD_NO_REMAKE))
            {
              st_mtim = she->st_mtim;
            }
            else
            {
              if (debug)
              {
                print_indent();
                printf("pretend %s is very old\n", sttable[e->nameidx].s);
              }
            }
          }
          if (usetsdb)
          {
            if (!tsszequal_db(e->nameidx, she->st_mtim, r->diridx, she->st_size, 0))
            {
              has_to_exec = 1;
              if (do_trace)
              {
                trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " tsdb mismatch of dependency '%s'", sttable[e->nameidx].s);
              }
            }
          }
          seen_nonphony = 1;
        }
        if (!S_ISLNK(she->st_mode))
        {
          continue;
        }
        if (stat(sttable[e->nameidx].s, &statbuf) != 0)
        {
          if (do_trace)
          {
            trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " '%s' (nonexistent)", sttable[e->nameidx].s);
          }
          has_to_exec = 1;
          // break; // No break, we want to get accurate st_mtim
          continue;
          //perror("can't stat");
          //fprintf(stderr, "file was: %s\n", it->c_str());
          //my_abort();
        }
        if (S_ISDIR(statbuf.st_mode) && !e->is_orderonly && !recommended)
        {
          char *tgtname = sttable[ABCE_CONTAINER_OF(r->tgtlist.node.next, struct stirtgt, llnode)->tgtidx].s;
          printf("stirmake: Recommend making directory dep %s of %s either @orderonly or @recdep.\n", sttable[e->nameidx].s, tgtname);
          recommended = 1;
        }
        if (!e->is_orderonly)
        {
          if (!seen_nonphony || ts_cmp(mtim_from_statbuf(&statbuf), st_mtim) > 0)
          {
            if (!ispretend(sttable[e->nameidx].s, PRETEND_VERY_OLD_NO_REMAKE))
            {
              st_mtim = mtim_from_statbuf(&statbuf);
            }
            else
            {
              if (debug)
              {
                print_indent();
                printf("pretend %s is very old\n", sttable[e->nameidx].s);
              }
            }
          }
          seen_nonphony = 1;
        }
      }
      r->st_mtim_valid = seen_nonphony;
      if (seen_nonphony)
      {
        r->st_mtim = st_mtim;
      }
      LINKED_LIST_FOR_EACH(node, &r->tgtlist)
      {
        struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
        if (ispretend(sttable[e->tgtidx].s, PRETEND_VERY_OLD_NO_REMAKE))
        {
          seen_no_remake = 1;
        }
        if (!cmdequal_db(e->tgtidx, &r->cmd, r->diridx))
        {
          if (r->cmd.args[0] != NULL)
          {
            if (do_trace)
            {
              trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " command database mismatch");
            }
          }
          has_to_exec = 1;
        }
        if (debug)
        {
          print_indent();
          printf("statting %s\n", sttable[e->tgtidx].s);
        }
        struct stathashentry *she;
        she = lstat_cached(e->tgtidx);
        if (she->ret != 0)
        {
          if (debug)
          {
            print_indent();
            printf("immediate has_to_exec\n");
          }
          if (do_trace)
          {
            trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " being nonexistent");
          }
          has_to_exec = 1;
          //break; // can't break, has to compare all commands from DB
          continue;
        }
        if (usetsdb && !S_ISDIR(she->st_mode))
        {
          if (!tsszequal_db(e->tgtidx, she->st_mtim, r->diridx, she->st_size, 1))
          {
            if (do_trace)
            {
              trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " tsdb mismatch");
            }
            has_to_exec = 1;
            continue;
          }
        }
        if (debug)
        {
          print_indent();
          printf("tgt: %llu %llu\n", (unsigned long long)she->st_mtim.tv_sec, (unsigned long long)she->st_mtim.tv_nsec);
        }
        if (!seen_tgt || ts_cmp(she->st_mtim, st_mtimtgt) < 0)
        {
          st_mtimtgt = she->st_mtim;
        }
        seen_tgt = 1;
      }
      if (!has_to_exec)
      {
        if (!seen_tgt)
        {
          printf("15\n");
          my_abort(); // shouldn't happen if there's at least 1 tgt
        }
        if (seen_nonphony && ts_cmp(st_mtimtgt, st_mtim) < 0)
        {
          if (debug)
          {
            print_indent();
            printf("delayed has_to_exec\n");
          }
          if (do_trace)
          {
            LINKED_LIST_FOR_EACH(node, &r->deplist)
            {
              struct stirdep *e = ABCE_CONTAINER_OF(node, struct stirdep, llnode);
              struct stathashentry *she;
              struct stat statbuf;
              if (e->is_dupe)
              {
                continue;
              }
              if (e->is_recursive)
              {
                struct timespec st_rectim = rec_mtim(r, sttable[e->nameidx].s);
                if (ts_cmp(st_mtimtgt, st_rectim) < 0)
                {
                  trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " '%s'", sttable[e->nameidx].s);
                }
                continue;
              }
              she = lstat_cached(e->nameidx);
              if (she->ret != 0)
              {
                continue;
              }
              if (e->is_orderonly)
              {
                continue;
              }
              if (usetsdb)
              {
                if (!tsszequal_db(e->nameidx, she->st_mtim, r->diridx, she->st_size, 0))
                {
                  if (do_trace)
                  {
                    trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " tsdb mismatch of dependency '%s'", sttable[e->nameidx].s);
                  }
                  has_to_exec = 1;
                  continue;
                }
              }
              if (ts_cmp(st_mtimtgt, she->st_mtim) < 0)
              {
                if (!ispretend(sttable[e->nameidx].s, PRETEND_VERY_OLD_NO_REMAKE))
                {
                  trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " '%s'", sttable[e->nameidx].s);
                  continue;
                }
              }
              if (!S_ISLNK(she->st_mode))
              {
                continue;
              }
              if (stat(sttable[e->nameidx].s, &statbuf) != 0)
              {
                continue;
              }
              if (ts_cmp(st_mtimtgt, mtim_from_statbuf(&statbuf)) < 0)
              {
                if (!ispretend(sttable[e->nameidx].s, PRETEND_VERY_OLD_NO_REMAKE))
                {
                  trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " '%s'", sttable[e->nameidx].s);
                  continue;
                }
              }
            }
          }
          has_to_exec = 1;
        }
      }
    }
    else if (r->is_phony || r->is_inc)
    {
      if (do_trace)
      {
        trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " being phony");
      }
      has_to_exec = 1;
    }
    else // no deps, check that all targets exist
    {
      struct linked_list_node *node;
      LINKED_LIST_FOR_EACH(node, &r->tgtlist)
      {
        struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
        struct stat statbuf;
        if (lstat(sttable[e->tgtidx].s, &statbuf) != 0)
        {
          if (do_trace)
          {
            trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " '%s' being nonexistent", sttable[e->tgtidx].s);
          }
          has_to_exec = 1;
          break;
        }
        if (!cmdequal_db(e->tgtidx, &r->cmd, r->diridx))
        {
          if (r->cmd.args[0] != NULL)
          {
            if (do_trace)
            {
              trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " command database mismatch");
            }
          }
          has_to_exec = 1;
          break;
        }
        if (usetsdb && !S_ISDIR(statbuf.st_mode))
        {
          if (!tsszequal_db(e->tgtidx, mtim_from_statbuf(&statbuf), r->diridx, statbuf.st_size, 1))
          {
            if (do_trace)
            {
              trace_add_vprintf(&tracebuf, &tracebufcap, &tracebufsz, " tsdb mismatch");
            }
            has_to_exec = 1;
            continue;
          }
        }
      }
    }
    if (has_to_exec && (r->cmd.args[0] != NULL && !seen_no_remake) && do_trace)
    {
      printf("%s\n", tracebuf);
    }
    free(tracebuf);
    tracebuf = NULL;
    if (has_to_exec && r->cmd.args[0] != NULL && !seen_no_remake && !(r->is_phony && touchmode))
    {
      if (debug)
      {
        print_indent();
        printf("do_exec: has_to_exec %d\n", ruleid);
      }
      if (ruleids_to_run_size >= ruleids_to_run_capacity)
      {
        size_t new_capacity = 2*ruleids_to_run_capacity + 16;
        ruleids_to_run = realloc(ruleids_to_run, new_capacity*sizeof(*ruleids_to_run));
        ruleids_to_run_capacity = new_capacity;
      }
      ruleids_to_run[ruleids_to_run_size++] = ruleid;
      r->is_queued = 1;
    }
    else if (has_to_exec && seen_no_remake)
    {
      if (debug)
      {
        print_indent();
        printf("do_exec: has_to_exec but seen no remake, setting !has_to_exec %d\n", ruleid);
      }
      r->is_queued = 1;
      indentlevel++;
      mark_executed(ruleid, 0);
      indentlevel--;
      return 1;
    }
    else
    {
      if (debug)
      {
        print_indent();
        printf("do_exec: mark_executed %s has_to_exec %d\n",
               sttable[first_tgt->tgtidx].s, has_to_exec);
      }
      r->is_queued = 1;
      indentlevel++;
      mark_executed(ruleid, 0);
      indentlevel--;
      return 1;
    }
  }
  else
  {
    if (debug)
    {
      print_indent();
      printf("do_exec: is queued already\n");
    }
  }
  return 0;
}

int consider(int ruleid)
{
  struct rule *r = rules[ruleid];
  struct stirtgt *first_tgt =
    ABCE_CONTAINER_OF(r->tgtlist.node.next, struct stirtgt, llnode);
  struct linked_list_node *node;
  int toexecute = 0;
  int execed_some = 0;
  if (debug)
  {
    print_indent();
    printf("considering %s\n", sttable[first_tgt->tgtidx].s);
  }
  if (r->is_executed)
  {
    if (debug)
    {
      print_indent();
      printf("already execed %s\n", sttable[first_tgt->tgtidx].s);
    }
    return 0;
  }
  if (r->is_executing)
  {
    if (debug)
    {
      print_indent();
      printf("already execing %s\n", sttable[first_tgt->tgtidx].s);
    }
    return 0;
  }
  r->is_executing = 1;
  r->is_under_consideration = 1;
  LINKED_LIST_FOR_EACH(node, &r->deplist)
  {
    struct stirdep *e = ABCE_CONTAINER_OF(node, struct stirdep, llnode);
    int idbytgt = stirdep_ruleid(e);
    if (e->is_dupe)
    {
      continue;
    }
    r->waitloc = e;
    if (r->wait_remain_cnt > 0 && e->is_wait)
    {
      break;
    }
    if (idbytgt >= 0)
    {
      indentlevel++;
      if (consider(idbytgt))
      {
        execed_some = 1;
      }
      indentlevel--;
      //execed_some = execed_some || consider(idbytgt); // BAD! DON'T DO THIS!
      if (!rules[idbytgt]->is_executed)
      {
        if (debug)
        {
          print_indent();
          printf("rule %d not executed, executing rule %d\n", idbytgt, ruleid);
          //std::cout << "rule " << ruleid_by_tgt[it->name] << " not executed, executing rule " << ruleid << std::endl;
        }
        toexecute = 1;
        deps_remain_forwait(r, idbytgt);
        r->wait_remain_cnt++;
      }
    }
    else
    {
      if (debug)
      {
        print_indent();
        printf("ruleid by target %s not found\n", sttable[e->nameidx].s);
      }
      if (!e->is_inc && access_cached(e->nameidx) == -1)
      {
        errxit("No %s and rule not found, required by target %s", sttable[e->nameidx].s, sttable[first_tgt->tgtidx].s);
        exit(2);
      }
    }
  }
/*
  if (r.phony)
  {
    r.executed = true;
    break;
  }
*/
  if (!toexecute && !r->is_queued)
  {
    indentlevel++;
    int ret = do_exec(ruleid);
    indentlevel--;
    r->is_under_consideration = 0;
    return ret;
    //ruleids_to_run.push_back(ruleid);
    //r.queued = true;
  }
  r->is_under_consideration = 0;
  return execed_some;
/*
  ruleids_to_run.push_back(ruleid);
  r.executed = true;
*/
}

void reconsider(int ruleid, int ruleid_executed)
{
  struct rule *r = rules[ruleid];
  struct stirtgt *first_tgt =
    ABCE_CONTAINER_OF(r->tgtlist.node.next, struct stirtgt, llnode);
  int toexecute = 0;
  struct linked_list_node *node;
  if (debug)
  {
    print_indent();
    printf("reconsidering %s\n", sttable[first_tgt->tgtidx].s);
  }
  if (r->is_executed)
  {
    if (debug)
    {
      print_indent();
      printf("already execed %s\n", sttable[first_tgt->tgtidx].s);
    }
    return;
  }
  if (!r->is_executing || r->is_under_consideration)
  {
    if (debug)
    {
      print_indent();
      printf("rule not executing or is under consideration %s\n", sttable[first_tgt->tgtidx].s);
    }
    // Must do this always in case the rule is to be executed in future.
    deps_remain_erase(r, ruleid_executed);
    return;
  }
  deps_remain_erase(r, ruleid_executed);
  //if (!linked_list_is_empty(&r->depremainlist))
  if (r->deps_remain_cnt > 0)
  {
    toexecute = 1;
    if (debug)
    {
      print_indent();
      printf("deps remain: %zu\n", (size_t)r->deps_remain_cnt);
      LINKED_LIST_FOR_EACH(node, &r->depremainlist)
      {
        struct dep_remain *rem =
          ABCE_CONTAINER_OF(node, struct dep_remain, llnode[0]);
        struct stirtgt *first_tgt =
          ABCE_CONTAINER_OF(rules[rem->ruleid]->tgtlist.node.next, struct stirtgt, llnode);
        print_indent();
        printf("  dep_remain: %d / %s\n", rem->ruleid, sttable[first_tgt->tgtidx].s);
      }
    }
  }
  //int toexecute2 = 0;
  r->is_under_consideration = 1;
  for (node = &r->waitloc->llnode; node != &r->deplist.node; node = node->next)
  {
    struct stirdep *e = ABCE_CONTAINER_OF(node, struct stirdep, llnode);
    int idbytgt = stirdep_ruleid(e);
    r->waitloc = e;
    if (r->wait_remain_cnt > 0 && e->is_wait)
    {
      break;
    }
    if (e->is_dupe)
    {
      continue; // RFE correct?
    }
    if (idbytgt >= 0)
    {
      indentlevel++;
      if (consider(idbytgt))
      {
        //execed_some = 1;
      }
      indentlevel--;
      //execed_some = execed_some || consider(idbytgt); // BAD! DON'T DO THIS!
      if (!rules[idbytgt]->is_executed)
      {
        if (debug)
        {
          print_indent();
          printf("rule %d not executed, executing rule %d\n", idbytgt, ruleid);
          //std::cout << "rule " << ruleid_by_tgt[it->name] << " not executed, executing rule " << ruleid << std::endl;
        }
        //toexecute2 = 1;
        deps_remain_forwait(r, idbytgt);
        r->wait_remain_cnt++;
      }
    }
    else
    {
      if (debug)
      {
        print_indent();
        printf("ruleid by target %s not found\n", sttable[e->nameidx].s);
      }
      if (!e->is_inc && access_cached(e->nameidx) == -1)
      {
        errxit("No %s and rule not found, required by target %s", sttable[e->nameidx].s, sttable[first_tgt->tgtidx].s);
        exit(2);
      }
    }
  }
  if (!toexecute && !r->is_queued)
  {
    indentlevel++;
    do_exec(ruleid);
    indentlevel--;
    //ruleids_to_run.push_back(ruleid);
    //r.queued = true;
  }
  r->is_under_consideration = 0;
}

void mark_executed(int ruleid, int was_actually_executed)
{
  struct rule *r = rules[ruleid];
  struct linked_list_node *node;
  if (r->is_executed)
  {
    printf("16\n");
    my_abort();
  }
  if (!r->is_executing)
  {
    printf("17\n");
    my_abort();
  }
  ruleremain_rm(r);
  r->is_executed = 1;
  r->is_actually_executed = was_actually_executed;
#if 0
  if (ruleid == 0) // FIXME should we return??? This is totally incorrect.
  {
    return;
  }
#endif
  if (r->is_detouch && !dry_run)
  {
    struct timespec cap;
    int cap_valid = 0;
    LINKED_LIST_FOR_EACH(node, &r->tgtlist)
    {
      struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
      struct stat statbuf;
      if (stat(sttable[e->tgtidx].s, &statbuf) != 0)
      {
        errxit("Can't stat %s", sttable[e->tgtidx].s);
      }
      if (!cap_valid || ts_cmp(mtim_from_statbuf(&statbuf), cap) < 0)
      {
        cap = mtim_from_statbuf(&statbuf);
        cap_valid = 1;
      }
      if (lstat(sttable[e->tgtidx].s, &statbuf) != 0)
      {
        errxit("Can't lstat %s", sttable[e->tgtidx].s);
      }
      if (!cap_valid || ts_cmp(mtim_from_statbuf(&statbuf), cap) < 0)
      {
        cap = mtim_from_statbuf(&statbuf);
        cap_valid = 1;
      }
      tsszstoretarget(&tsdb, e->tgtidx, mtim_from_statbuf(&statbuf), statbuf.st_size);
    }
    if (!cap_valid)
    {
      my_abort();
    }
    LINKED_LIST_FOR_EACH(node, &r->deplist)
    {
      struct stirdep *e = ABCE_CONTAINER_OF(node, struct stirdep, llnode);
      if (e->is_dupe)
      {
        continue;
      }
      if (e->is_recursive)
      {
        reccap_mtim(sttable[e->nameidx].s, cap);
      }
    }
  }
  else if (r->is_rectgt && r->st_mtim_valid && !dry_run)
  {
    LINKED_LIST_FOR_EACH(node, &r->deplist)
    {
      struct stirdep *e = ABCE_CONTAINER_OF(node, struct stirdep, llnode);
      if (e->is_dupe)
      {
        continue;
      }
      if (e->is_recursive)
      {
        struct timespec st_mtim2 = rec_mtim(r, sttable[e->nameidx].s);
        if (ts_cmp(st_mtim2, r->st_mtim) > 0)
        {
          r->st_mtim = st_mtim2;
        }
      }
    }
    LINKED_LIST_FOR_EACH(node, &r->tgtlist)
    {
      struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
      int utimeret;
      struct stat statbuf;
      if (stat(sttable[e->tgtidx].s, &statbuf) != 0)
      {
        errxit("Can't stat %s", sttable[e->tgtidx].s);
      }
      if (ts_cmp(r->st_mtim, mtim_from_statbuf(&statbuf)) <= 0)
      {
        if (debug)
        {
          print_indent();
          printf("utime %s won't move clock backwards!\n", sttable[e->tgtidx].s);
        }
        tsszstoretarget(&tsdb, e->tgtidx, mtim_from_statbuf(&statbuf), statbuf.st_size);
        continue;
      }
      utimeret = utimensat_both_emul(sttable[e->tgtidx].s, r->st_mtim, 0, 1);
      if (debug)
      {
        print_indent();
        printf("utime %s succeeded? %d\n", sttable[e->tgtidx].s, (utimeret == 0));
      }
      if (stat(sttable[e->tgtidx].s, &statbuf) != 0)
      {
        errxit("Can't stat %s", sttable[e->tgtidx].s);
      }
      tsszstoretarget(&tsdb, e->tgtidx, mtim_from_statbuf(&statbuf), statbuf.st_size);
    }
  }
  else if (!r->is_phony && !r->is_inc && !dry_run)
  {
    struct stat statbuf;
    struct timespec st_mtim;
    off_t st_size = 0;
    int statret;
    int seen_pretend = 0;
    LINKED_LIST_FOR_EACH(node, &r->tgtlist)
    {
      struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
      if (ispretend(sttable[e->tgtidx].s, PRETEND_VERY_OLD_NO_REMAKE))
      {
        seen_pretend = 1;
      }
    }
    LINKED_LIST_FOR_EACH(node, &r->tgtlist)
    {
      struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
      if (was_actually_executed)
      {
        statret = lstat(sttable[e->tgtidx].s, &statbuf);
        if (statret == 0)
        {
          st_mtim = mtim_from_statbuf(&statbuf);
          st_size = statbuf.st_size;
        }
      }
      else
      {
        struct stathashentry *she = lstat_cached(e->tgtidx);
        statret = she->ret;
        st_mtim = she->st_mtim;
        st_size = she->st_size;
      }
      if (statret != 0 && !seen_pretend && !r->is_maybe)
      {
        fprintf(stderr, "stirmake: *** Target '%s' was not created by rule (directory: '%s').\n",
               sttable[e->tgtidx].s, sttable[r->diridx].s);
        fprintf(stderr, "stirmake: *** Hint: use @phonyrule for phony rules.\n");
        fprintf(stderr, "stirmake: *** Hint: use @mayberule for rules that may or may not update target.\n");
        fprintf(stderr, "stirmake: *** Hint: use @rectgtrule for rules that have targets inside @recdep.\n");
        errxit("Target '%s' was not created by rule", sttable[e->tgtidx].s);
      }
      if (!seen_pretend && !r->is_maybe && r->st_mtim_valid && ts_cmp(st_mtim, r->st_mtim) < 0)
      {
        fprintf(stderr, "stirmake: *** Target '%s' was not updated by rule (directory: '%s').\n",
               sttable[e->tgtidx].s, sttable[r->diridx].s);
        fprintf(stderr, "stirmake: *** Hint: use @phonyrule for phony rules.\n");
        fprintf(stderr, "stirmake: *** Hint: use @mayberule for rules that may or may not update target.\n");
        fprintf(stderr, "stirmake: *** Hint: use @rectgtrule for rules that have targets inside @recdep.\n");
        errxit("Target '%s' was not updated by rule", sttable[e->tgtidx].s);
      }
      if (statret == 0)
      {
        tsszstoretarget(&tsdb, e->tgtidx, st_mtim, st_size);
      }
    }
  }
  if (!r->is_inc && !r->is_phony)
  LINKED_LIST_FOR_EACH(node, &r->deplist)
  {
    struct stirdep *e = ABCE_CONTAINER_OF(node, struct stirdep, llnode);
    struct stathashentry *she;
    if (e->is_dupe)
    {
      continue;
    }
    she = lstat_cached(e->nameidx);
    if (she->ret != 0)
    {
      continue;
    }
    tsszstoresource(&tsdb, e->nameidx, she->st_mtim, she->st_size);
  }
  /* FIXME we need to handle:
   * a: b
   * b: c
   * c:
   * ...and then somebody types "smka b" and we don't want to build a
   */
  LINKED_LIST_FOR_EACH(node, &r->tgtlist)
  {
    struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
    if (was_actually_executed)
    {
      lstat_evict_named(e->tgtidx);
      accesshash_evict_named(e->tgtidx);
    }
    if (dry_run && was_actually_executed)
    {
      sttable[e->tgtidx].is_remade = 1;
    }
    if (!dry_run && !touchmode && was_actually_executed && sttable[e->tgtidx].is_cdepwatch)
    {
      size_t j, k;
      char *tgt = (e->tgtidxnodir != (mysize_t)-1) ? sttable[e->tgtidxnodir].s : neighpath(sttable[r->diridx].s, sttable[e->tgtidx].s); // RFE is only the second condition after ':' enough?
      FILE *f;
      struct incyy incyy = {
        .prefix = sttable[r->diridx].s,
        .auto_target = 0,
        .fnamenodir = tgt,
      };
      incyy.prefixlen = strlen(incyy.prefix);
      incyy.fnamenodirlen = strlen(incyy.fnamenodir);
      //printf("Re-read %s\n", sttable[e->tgtidx].s);
      f = fopen(sttable[e->tgtidx].s, "r");
      if (f)
      {
        if (incyymineparse(f, &incyy, NULL, NULL, NULL, NULL))
        {
          errxit("Invalid cdepincludes format in %s", sttable[e->tgtidx].s);
          my_abort();
        }
        for (j = 0; j < incyy.rulesz; j++)
        {
          for (k = 0; k < incyy.rules[j].depsz; k++)
          {
            mysize_t stidx = stringtab_add(incyy.rules[j].deps[k]);
            struct stathashentry *she;
            she = lstat_cached(stidx);
            if (she->ret != 0)
            {
              continue;
            }
            tsszstoresource(&tsdb, stidx, she->st_mtim, she->st_size);
            //printf("Re-read dep: %s size %zu\n", incyy.rules[j].deps[k], (size_t)she->st_size);
          }
        }
        fclose(f);
        incyy_free(&incyy);
      }
    }
  }
  //LINKED_LIST_FOR_EACH(node, &r->tgtlist)
  {
    //struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
    //int tgt_ruleid = get_ruleid_by_tgt(e->tgtidx);
    //struct ruleid_by_dep_entry_block *blk = rules[tgt_ruleid]->firstdepblock;
    struct ruleid_by_dep_entry_block *blk = rules[ruleid]->firstdepblock;
    while (blk != NULL)
    {
      size_t i;
      for (i = 0; i < blk->cnt; i++)
      {
        int one_ruleid = blk->ruleid[i];
        indentlevel++;
        reconsider(one_ruleid, ruleid);
        indentlevel--;
      }
      blk = blk->next;
    }
  }
}

/*
.PHONY: all

all: l1g.txt

l1g.txt: l2a.txt l2b.txt
	./touchs1 l1g.txt

l2a.txt l2b.txt: l3c.txt l3d.txt l3e.txt
	./touchs1 l2a.txt l2b.txt

l3c.txt: l4f.txt
	./touchs1 l3c.txt

l3d.txt: l4f.txt
	./touchs1 l3d.txt

l3e.txt: l4f.txt
	./touchs1 l3e.txt
 */

void set_nonblock(int fd)
{
  int flags = fcntl(fd, F_GETFL);
  if (flags < 0)
  {
    printf("18\n");
    my_abort();
  }
  flags |= O_NONBLOCK;
  if (fcntl(fd, F_SETFL, flags) < 0)
  {
    printf("19\n");
    my_abort();
  }
}

void sigsegv_handler(int x)
{
  my_abort();
}
void sigabrt_handler(int x)
{
  my_abort();
}
void sigfpe_handler(int x)
{
  my_abort();
}
void sigill_handler(int x)
{
  my_abort();
}
void sigquit_handler(int x)
{
  my_abort();
}
void sigsys_handler(int x)
{
  my_abort();
}
void sigxcpu_handler(int x)
{
  my_abort();
}
void sigxfsz_handler(int x)
{
  my_abort();
}
void sigbus_handler(int x)
{
  my_abort();
}
void sigchld_handler(int x)
{
  write(self_pipe_fd[1], ".", 1);
}

void sigterm_handler(int x)
{
  sigterm_atomic = 1;
  write(self_pipe_fd[1], ".", 1);
}
void sigint_handler(int x)
{
  sigint_atomic = 1;
  write(self_pipe_fd[1], ".", 1);
}
void sighup_handler(int x)
{
  sighup_atomic = 1;
  write(self_pipe_fd[1], ".", 1);
}
void sigalrm_handler(int x)
{
}
void handle_signal(int signum);

#if 0
void pathological_test(void)
{
  int rule;
  char *v_rules[3000];
  size_t v_rules_sz = 0;
  struct timeval tv1, tv2;
  char *rulestr;
  for (rule = 0; rule < 3000; rule++)
  {
    rulestr = myitoa(rule);
    add_dep(&rulestr, 1, v_rules, v_rules_sz, 0);
    v_rules[v_rules_sz++] = rulestr;
  }
  process_additional_deps_2(0);
  printf("starting DFS2\n");
  gettimeofday(&tv1, NULL);
  free(better_cycle_detect(get_ruleid_by_tgt(stringtab_add(rulestr))));
  gettimeofday(&tv2, NULL);
  double ms = (tv2.tv_usec - tv1.tv_usec + 1e6*(tv2.tv_sec - tv1.tv_sec)) / 1e3;
  printf("ending DFS2 in %g ms\n", ms);
  exit(0);
}
#endif

void stack_conf(void)
{
  const rlim_t stackSize = 16 * 1024 * 1024;
  struct rlimit rl;
  int result;
  result = getrlimit(RLIMIT_STACK, &rl);
  if (result != 0)
  {
    printf("20\n");
    my_abort();
  }
  if (rl.rlim_cur < stackSize)
  {
    rl.rlim_cur = stackSize;
    result = setrlimit(RLIMIT_STACK, &rl);
    if (result != 0)
    {
      printf("21\n");
      my_abort();
    }
  }
}


void version(char *argv0)
{
  fprintf(stderr, "Stirmake %s, %s\n", gitversion_get(), gitversions_head());
  fprintf(stderr, "Copyright (C) 2017-19 Aalto University, 2018, 2020-2026 Juha-Matti Tilli\n");
  fprintf(stderr, "Logo (C) 2019 Juha-Matti Tilli, All right reserved, license not applicable\n");
  fprintf(stderr, "\n");
  fprintf(stderr, "Authors:\n");
  fprintf(stderr, "- Juha-Matti Tilli (copyright transfer to Aalto on 19.4.2018 for some code)\n");
  fprintf(stderr, "\n");
  fprintf(stderr,
"Permission is hereby granted, free of charge, to any person obtaining a copy\n"
"of this software and associated documentation files (the \"Software\"), to\n"
"deal in the Software without restriction, including without limitation the\n"
"rights to use, copy, modify, merge, publish, distribute, sublicense, and/or\n"
"sell copies of the Software, and to permit persons to whom the Software is\n"
"furnished to do so, subject to the following conditions:\n"
"\n"
"The above copyright notice and this permission notice shall be included in\n"
"all copies or substantial portions of the Software.\n"
"\n"
"THE SOFTWARE IS PROVIDED \"AS IS\", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR\n"
"IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,\n"
"FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE\n"
"AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER\n"
"LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING\n"
"FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS\n"
"IN THE SOFTWARE.\n");
  exit(0);
}

void usage(char *argv0)
{
  fprintf(stderr, "Usage:\n");
  if (isspecprog)
  {
    fprintf(stderr, "%s [-vGdHhcbqikBnsTReEFPu] [-j jobcnt] [-l loadavg] [-W|-X|-o|-r file] [-O n|t|r] [-C dir] [target...]\n", argv0);
    fprintf(stderr, "  You can start %s as smka, smkt or smkp or use main command stirmake\n", argv0);
    fprintf(stderr, "  smka, smkt and smkp do not take -t | -p | -a whereas stirmake takes\n");
    fprintf(stderr, "  smka, smkt and smkp do not take -f Stirfile whereas stirmake takes\n");
    fprintf(stderr, "  -b and -c options are incompatible with -q\n");
  }
  else
  {
    fprintf(stderr, "%s [-vGdHhcbqikBnsTReEFPu] [-j jobcnt] [-l loadavg] [-W|-X|-o|-r file] [-O n|t|r] [-C dir] -f Stirfile | -t | -p | -a [target...]\n", argv0);
    fprintf(stderr, "  You can start %s as smka, smkt or smkp or use main command %s\n", argv0, argv0);
    fprintf(stderr, "  smka, smkt and smkp do not take -t | -p | -a whereas %s takes\n", argv0);
    fprintf(stderr, "  smka, smkt and smkp do not take -f Stirfile whereas %s takes\n", argv0);
    fprintf(stderr, "  -b and -c options are incompatible with -q\n");
  }
  exit(2);
}

void do_narration(void)
{
  wchar_t aumlautch = L'\xe4';
  wchar_t oumlautch = L'\xf6';
  char *aumlaut = malloc(MB_CUR_MAX+1);
  char *oumlaut = malloc(MB_CUR_MAX+1);
  int ret;
  ret = wctomb(aumlaut, aumlautch);
  if (ret < 0)
  {
    aumlaut[0] = 'a';
    aumlaut[1] = '\0';
  }
  else
  {
    aumlaut[ret] = '\0';
  }
  ret = wctomb(oumlaut, oumlautch);
  if (ret < 0)
  {
    oumlaut[0] = 'o';
    oumlaut[1] = '\0';
  }
  else
  {
    oumlaut[ret] = '\0';
  }
  printf("Hellurei, hellurei, k%s%snt%s on hurjaa!\n", aumlaut, aumlaut, oumlaut);
  fflush(stdout);
  free(aumlaut);
  free(oumlaut);
}

void recursion_misuse_prevention(void)
{
  char *pidstr = getenv("STIRMAKEPID");
  if (pidstr != NULL)
  {
    pid_t pid = (int)atoi(pidstr);
    was_recursive = 1;
    if (getppid() == pid)
    {
      fprintf(stderr, "stirmake: Seven Processes for the Programmers waiting to type,\n");
      fprintf(stderr, "stirmake: Three for the Operators in their halls of racks,\n");
      fprintf(stderr, "stirmake: Three for the End-users believing all hype.\n");
      fprintf(stderr, "stirmake: One for the Evil Cracker for his dark hacks\n");
      fprintf(stderr, "stirmake: In the land of Recursion where the Shadows lie.\n");
      fprintf(stderr, "stirmake: One Build Tool to rule them all, one Build Tool to find them,\n");
      fprintf(stderr, "stirmake: One Build Tool to bring them all, and in the darkness bind them,\n");
      fprintf(stderr, "stirmake: In the land of Recursion where the Shadows lie.\n");
      fprintf(stderr, "\n");
      fprintf(stderr, "stirmake: *** Recursion misuse detected.\n");
      fprintf(stderr, "stirmake: *** Stirmake is designed to be used non-recursively.\n");
      fprintf(stderr, "stirmake: *** Exiting.\n");
      exit(2);
    }
  }
  update_recursive_pid(0);
}

void set_mode(enum mode newmode, int newspecprog, char *argv0)
{
  if (mode != MODE_NONE && mode != newmode)
  {
    fprintf(stderr, "Ambiguous mode\n");
    usage(argv0);
  }
  mode = newmode;
  if (newspecprog)
  {
    isspecprog = 1;
  }
}

void process_mflags(char **fds, char **auth, char **outputsync)
{
  char *iter = getenv("MAKEFLAGS");
  if (fds)
  {
    *fds = NULL;
  }
  if (auth)
  {
    *auth = NULL;
  }
  if (outputsync)
  {
    *outputsync = NULL;
  }
  while (iter && *iter == ' ')
  {
    iter++;
  }
  // RFE is this 100% accurate?
  while (iter && *iter != '\0')
  {
    if (strncmp(iter, "-O", strlen("-O")) == 0)
    {
      iter += strlen("-O");
      while (iter && *iter == ' ')
      {
        iter++;
      }
      if (outputsync)
      {
        *outputsync = iter;
      }
    }
    if (strncmp(iter, "--jobserver-fds=", strlen("--jobserver-fds=")) == 0)
    {
      iter += strlen("--jobserver-fds=");
      if (fds)
      {
        *fds = iter;
      }
    }
    if (strncmp(iter, "--jobserver-auth=", strlen("--jobserver-auth=")) == 0)
    {
      iter += strlen("--jobserver-auth=");
      if (auth)
      {
        *auth = iter;
      }
    }
    iter = strchr(iter, ' ');
    while (iter && *iter == ' ')
    {
      iter++;
    }
  }
  unsetenv("MAKEFLAGS");
}

int process_jobserver(int fds[2])
{
  char *fdstr;
  char *authstr;
  process_mflags(&fdstr, &authstr, NULL);
  if (fdstr == NULL && authstr != NULL)
  {
    if (strncmp(authstr, "fifo:", strlen("fifo:")) == 0)
    {
      char *fifostr = authstr+strlen("fifo:");
      char *sp = strchr(fifostr, ' ');
      if (sp)
      {
        char *fifostr2;
        fifostr2 = malloc((size_t)(sp-fifostr)+1);
        if (fifostr2 == NULL)
        {
          return -ENOENT;
        }
        memcpy(fifostr2, fifostr, (size_t)(sp-fifostr));
        fifostr2[sp-fifostr] = '\0';
        fifostr = fifostr2;
      }
      fds[0] = open(fifostr, O_RDWR);
      if (fds[0] < 0)
      {
        if (sp)
        {
          free(fifostr);
        }
        return -ENOENT;
      }
      fds[1] = open(fifostr, O_RDWR);
      //fds[1] = dup(fds[0]); // Not sure if necessary, to make it distinct
      if (fds[1] < 0)
      {
        if (sp)
        {
          free(fifostr);
        }
        close(fds[0]);
        return -ENOENT;
      }
      set_nonblock(fds[1]);
      set_nonblock(fds[0]); // We can only do this if we pass FIFO name to child
      jobserver_fifo = fifostr;
#if 0
      if (sp)
      {
        free(fifostr);
      }
#endif
      return 0;
    }
    else if (sscanf(authstr, "%d,%d", &fds[0], &fds[1]) == 2)
    {
      fdstr = authstr;
    }
    else
    {
      return -ENOENT;
    }
  }
  if (fdstr == NULL)
  {
    return -ENOENT;
  }
  if (sscanf(fdstr, "%d,%d", &fds[0], &fds[1]) != 2)
  {
    fprintf(stderr, "stirmake: Jobserver unavailable\n");
    return -EINVAL;
  }
  if (fcntl(fds[0], F_GETFD) == -1)
  {
    fprintf(stderr, "stirmake: Jobserver unavailable\n");
    return -EBADF;
  }
  if (fcntl(fds[1], F_GETFD) == -1)
  {
    fprintf(stderr, "stirmake: Jobserver unavailable\n");
    return -EBADF;
  }
  fprintf(stderr, "stirmake: Jobserver available\n");
  return 0;
}

struct linked_list_head cleanlist = STIR_LINKED_LIST_HEAD_INITER(cleanlist);

void run_loop(int printnothing);

int deps_remain_calculated = 0;

void do_clean(char *fwd_path, int objs, int bins)
{
  size_t i, fp_len;
  char canbuf[1024];
  int all;
  char *cleanstr = NULL;
  struct linked_list_node *node, *node2, *node3, *nodetmp;
  all = (strcmp(fwd_path, ".") == 0);
  fp_len = strlen(fwd_path);
  if (bins && objs)
  {
    cleanstr = "BOTHCLEAN";
  }
  else if (bins)
  {
    cleanstr = "DISTCLEAN";
  }
  else if (objs)
  {
    cleanstr = "CLEAN";
  }
  if (!cleanstr)
  {
    my_abort();
  }
  for (i = 0; i < rules_size; i++)
  {
    int doit = all;
    char *prefix = sttable[rules[i]->diridx].s;
    if (strncmp(prefix, fwd_path, fp_len) == 0)
    {
      if (prefix[fp_len] == '/' || prefix[fp_len] == '\0')
      {
        doit = 1;
      }
    }
    if (!doit)
    {
      continue;
    }
    doit = 0;
    if (objs && !bins)
    {
      if (rules[i]->is_cleanhook)
      {
        doit = 1;
      }
    }
    else if (bins && !objs)
    {
      if (rules[i]->is_distcleanhook)
      {
        doit = 1;
      }
    }
    else if (objs && bins)
    {
      if (rules[i]->is_bothcleanhook)
      {
        doit = 1;
      }
    }
    if (!doit)
    {
      continue;
    }

    size_t parentsz = strlen(prefix) + strlen(cleanstr) + 5;
    char *parent = malloc(parentsz);
    size_t cleanslashsz = strlen(cleanstr) + 4;
    char *cleanslash = malloc(cleanslashsz);
    if (snprintf(cleanslash, cleanslashsz, "%s///", cleanstr) >= (int)cleanslashsz)
    {
      my_abort();
    }
    if (snprintf(parent, parentsz, "%s/../%s", prefix, cleanstr) >= (int)parentsz)
    {
      my_abort();
    }
    char *cparent = canon_buf(parent, canbuf, sizeof(canbuf));
    free(parent);
    parentsz = strlen(cparent) + 4;
    parent = malloc(parentsz);
    if (snprintf(parent, parentsz, "%s///", cparent) >= (int)parentsz)
    {
      my_abort();
    }
    if (cparent != canbuf)
    {
      free(cparent);
    }

    if (strncmp(parent, "../", 3) != 0)
    {
      mysize_t tgtidx = ABCE_CONTAINER_OF(rules[i]->tgtlist.node.next, struct stirtgt, llnode)->tgtidx;
      // FIXME!!! The path to child is incorrect!
      add_dep(&parent, 1, &sttable[tgtidx].s, 1, /*&cleanslash,*/ 0, 0, 0);
    }

    free(parent);
    free(cleanslash);

    ruleremain_add(rules[i]);
  }

  // XXX: this is bad, the same calc_deps_remain code is in two places
  if (!deps_remain_calculated)
  {
    process_additional_deps_2(abce.dynscope.u.area->u.sc.locidx);
    for (i = 0; i < rules_size; i++)
    {
      calc_deps_remain(rules[i]);
    }
    deps_remain_calculated = 1;
  }

  ins_ruleid_by_dep_later();

  run_loop(0);
  for (i = 0; i < rules_size; i++)
  {
    int doit = all;
    char *prefix = sttable[rules[i]->diridx].s;
    if (strncmp(prefix, fwd_path, fp_len) == 0)
    {
      if (prefix[fp_len] == '/' || prefix[fp_len] == '\0')
      {
        doit = 1;
      }
    }
    if (!doit)
    {
      continue; // optimization
    }
    if (rules[i]->is_phony)
    {
      continue;
    }
    if (rules[i]->is_inc)
    {
      continue;
    }
    if (rules[i]->is_cleanqueued)
    {
      continue;
    }
    LINKED_LIST_FOR_EACH(node2, &rules[i]->tgtlist)
    {
      struct stirtgt *tgt = ABCE_CONTAINER_OF(node2, struct stirtgt, llnode);
      char *oldname = stir_strdup(sttable[tgt->tgtidx].s);
      char *name;
      mysize_t stidx;
      int ruleid;
      struct linked_list_head tmplist; // extra list for reversing
      int prefixok;
      char *rprefix;
      linked_list_head_init(&tmplist);
      if (debug)
      {
        print_indent();
        printf("itering tgt %s\n", oldname);
      }
      for (;;)
      {
        if (debug)
        {
          print_indent();
          printf("itering dir %s\n", oldname);
        }
        name = dir_up(oldname);
        if (debug)
        {
          print_indent();
          printf("dir-up %s\n", name);
        }
        free(oldname);
        oldname = name;
        stidx = stringtab_get(name);
        if (stidx == (mysize_t)-1)
        {
          break;
        }
        ruleid = get_ruleid_by_tgt(stidx);
        if (ruleid < 0)
        {
          break;
        }
        if (rules[ruleid]->is_phony)
        {
          break;
        }
        if (rules[ruleid]->is_inc)
        {
          break;
        }
        if (rules[ruleid]->is_cleanqueued)
        {
          break;
        }
        prefixok = all;
        rprefix = sttable[rules[ruleid]->diridx].s;
        if (strncmp(rprefix, fwd_path, fp_len) == 0)
        {
          if (rprefix[fp_len] == '/' || rprefix[fp_len] == '\0')
          {
            prefixok = 1;
          }
        }
        if (!prefixok)
        {
          break;
        }
        rules[ruleid]->is_cleanqueued = 1;
        if (debug)
        {
          print_indent();
          printf("adding rule %d to rm list\n", ruleid);
        }
        linked_list_add_head(&rules[ruleid]->cleanllnode, &tmplist);
      }
      free(oldname);
      LINKED_LIST_FOR_EACH_SAFE(node3, nodetmp, &tmplist)
      {
        struct rule *rule = ABCE_CONTAINER_OF(node3, struct rule, cleanllnode);
        linked_list_delete(&rule->cleanllnode);
        linked_list_add_head(&rule->cleanllnode, &cleanlist);
        if (!rule->is_cleanqueued)
        {
          printf("rule %d not cleanqueued\n", rule->ruleid);
          my_abort();
        }
      }
    }
    rules[i]->is_cleanqueued = 1;
    linked_list_add_head(&rules[i]->cleanllnode, &cleanlist);
  }
  LINKED_LIST_FOR_EACH(node, &cleanlist)
  {
    struct rule *rule = ABCE_CONTAINER_OF(node, struct rule, cleanllnode);
    int alldist = rule->is_dist;
    LINKED_LIST_FOR_EACH(node2, &rule->tgtlist)
    {
      struct stirtgt *tgt = ABCE_CONTAINER_OF(node2, struct stirtgt, llnode);
      int dist = tgt->is_dist || alldist;
      if ((objs && !dist) || (bins && dist))
      {
        char *name = sttable[tgt->tgtidx].s;
        struct stat statbuf;
        int ret = 0;
        ret = lstat(name, &statbuf);
        if (ret != 0 && errno == ENOENT)
        {
          continue;
        }
        if (ret != 0)
        {
          perror("stirmake: *** Can't stat file");
          errxit("Can't stat file %s", name);
        }
        ret = 0;
        if (S_ISREG(statbuf.st_mode))
        {
          if (!silent) printf("unlink regular: %s\n", name);
          ret = unlink(name);
        }
        else if (S_ISLNK(statbuf.st_mode))
        {
          if (!silent) printf("unlink symlink: %s\n", name);
          ret = unlink(name);
        }
        else if (S_ISSOCK(statbuf.st_mode))
        {
          if (!silent) printf("unlink socket: %s\n", name);
          ret = unlink(name);
        }
        else if (S_ISFIFO(statbuf.st_mode))
        {
          if (!silent) printf("unlink fifo: %s\n", name);
          ret = unlink(name);
        }
        else if (S_ISDIR(statbuf.st_mode))
        {
          if (!silent) printf("rmdir: %s\n", name);
          ret = rmdir(name);
          if (ret != 0)
          {
            if (!silent) perror("stirmake: (ignoring) can't rmdir");
            ret = 0;
          }
        }
        if (ret != 0)
        {
          perror("stirmake: *** Can't unlink file");
          errxit("Can't unlink file %s", name);
        }
      }
    }
  }
  stathashentry_evict_all();
  accesshashentry_evict_all();
}

struct cmd dbyycmd_add(struct dbyycmd *cmds, size_t cmdssz)
{
  struct cmd ret = CMD_EMPTY;
  size_t i, j;
  char ***result = my_argmalloc((cmdssz+1) * sizeof(*result));
  for (i = 0; i < cmdssz; i++)
  {
    result[i] = my_argmalloc((cmds[i].argssz+1) * sizeof(*(result[i])));
    for (j = 0; j < cmds[i].argssz; j++)
    {
      //result[i][j] = my_argstrdup(cmds[i].args[j]);
      result[i][j] = cmds[i].args[j]; // These are single-use
    }
    result[i][cmds[i].argssz] = NULL;
  }
  result[cmdssz] = NULL;
  ret.args = result;
  return ret;
}

FILE *dbf = NULL;
int dbf_did_exist = 0;
int part2_done = 0;

void load_db_part1(void)
{
  struct flock fl = {.l_type=0};
  //int ret;
  int dbfd;
  linked_list_head_init(&db.ll);
  if (access(".stir.db", F_OK) == 0)
  {
    dbf_did_exist = 1;
  }
  //dbyynameparse(".stir.db", &dbyy, 0);
  dbf = fopen(".stir.db", "a+");
  if (dbf == NULL)
  {
    fprintf(stderr, "stirmake: *** Can't open DB. Exiting.\n");
    exit(2);
  }
  fseek(dbf, 0, SEEK_SET); // Cygwin/BSD/MacOS requires this
  dbfd = fileno(dbf);
  if (dbfd < 0)
  {
    fprintf(stderr, "stirmake: *** Can't get DB fileno. Exiting.\n");
    exit(2);
  }
  fl.l_type = F_WRLCK;
  fl.l_whence = SEEK_SET;
  fl.l_start = 0;
  fl.l_len = 0; // XXX or 1?
  if (fcntl(dbfd, F_SETLK, &fl) != 0)
  {
    fprintf(stderr, "stirmake: *** Can't lock DB. Other stirmake running? Exiting.\n");
    exit(2);
  }
}
void dbyy_cmdfn(void *userdata, struct dbyycmd *cmdsbuf, size_t cmdssz,
                struct dbyyrule *rule)
{
  struct dbe *dbe = my_malloc(sizeof(struct dbe));
  dbecnt++;
  dbe->tgtidx = stringtab_add_len(rule->tgt, rule->tgtlen);
  dbe->diridx = stringtab_add_len(rule->dir, rule->dirlen);
  dbe->cmds = dbyycmd_add(cmdsbuf, cmdssz);
  ins_dbe(&db, dbe);
}
void dbyy_tsfn(void *userdata, struct tsdbentry *e)
{
  struct tsdbe *tsdbe = my_malloc(sizeof(struct tsdbe));
  tsdbecnt++;
  tsdbe->stringtabidx = stringtab_add_len(e->tgt, e->tgtlen);
  tsdbe->seen = 0;
  tsdbe->sz = e->filesz;
  tsdbe->ts = e->ts;
  ins_tsdbe(&tsdb, tsdbe);
}

void load_db_part2(void)
{
  struct dbyy dbyy = DBYY_EMPTY;
  //size_t i;
  int ret;
  int dbfd;
  part2_done = 1;
  dbfd = fileno(dbf);
  db_init(&db);
  tsdb_init(&tsdb);
  //ret = dbyydoparse(dbf, &dbyy);
  ret = dbyymineparse(dbf, &dbyy, dbyy_cmdfn, dbyy_tsfn, NULL);
  if (!test && ftruncate(dbfd, 0) != 0)
  {
    fprintf(stderr, "stirmake: *** Can't truncate DB. Exiting.\n");
    exit(2);
  }
  if (ret)
  {
    fprintf(stderr, "stirmake: *** Incompatible DB version. Truncating.\n");
    zap_dbs(&db, &tsdb);
    fseek(dbf, 0, SEEK_END); // switching from read to write must call positioning function
    return;
  }
#if 0
  for (i = 0; i < dbyy.rulesz; i++)
  {
    struct dbe *dbe = my_malloc(sizeof(struct dbe));
    dbecnt++;
    dbe->tgtidx = stringtab_add(dbyy.rules[i].tgt);
    dbe->diridx = stringtab_add(dbyy.rules[i].dir);
    dbe->cmds = dbyycmd_add(dbyy.rules[i].cmds, dbyy.rules[i].cmdssz);
    ins_dbe(&db, dbe);
  }
#endif
#if 0
  for (i = 0; i < dbyy.tssz; i++)
  {
    struct tsdbe *tsdbe = my_malloc(sizeof(struct tsdbe));
    tsdbecnt++;
    tsdbe->stringtabidx = stringtab_add(dbyy.tsdb[i].tgt);
    tsdbe->seen = 0;
    tsdbe->sz = dbyy.tsdb[i].filesz;
    tsdbe->ts = dbyy.tsdb[i].ts;
    ins_tsdbe(&tsdb, tsdbe);
  }
#endif
  free(dbyy.rules);
  free(dbyy.tsdb);
  fseek(dbf, 0, SEEK_END); // switching from read to write must call positioning function
  // This is necessitated by standard for a+ mode
}

int merge_db_v1(void)
{
  size_t i;
  struct linked_list_node *node;
  FILE *f;
  int firstrule = 1;
  int ret;
  if (test)
  {
    return 0;
  }
  for (i = 0; i < rules_size; i++)
  {
    struct rule *rule = rules[i];
    if (!rule->is_actually_executed)
    {
      if (rule->is_forked)
      {
        LINKED_LIST_FOR_EACH(node, &rule->tgtlist)
        {
          struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
          if (debug)
          {
            print_indent();
            printf("removing %s from DB\n", sttable[e->tgtidx].s);
          }
          maybe_del_dbe(&db, e->tgtidx);
        }
      }
      continue;
    }
    LINKED_LIST_FOR_EACH(node, &rule->tgtlist)
    {
      struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
      struct dbe *dbe = my_malloc(sizeof(struct dbe));
      dbecnt++;
      dbe->tgtidx = e->tgtidx;
      dbe->diridx = rule->diridx;
      dbe->cmds = rule->cmd;
      ins_dbe(&db, dbe);
    }
  }
  if (linked_list_is_empty(&db.ll) && !dbf_did_exist)
  {
    fclose(dbf);
    f = NULL;
    dbf = NULL;
    unlink(".stir.db");
    return 0;
  }
  /*
  f = fopen(".stir.db", "w");
  if (f == NULL)
  {
    fprintf(stderr, "Can't open .stir.db"); // can't use errxit
    exit(2);
  }
  */
  f = dbf;
  fprintf(f, "@v1@\n\n");
  LINKED_LIST_FOR_EACH(node, &db.ll)
  {
    struct dbe *dbe = ABCE_CONTAINER_OF(node, struct dbe, llnode);
    char ***argiter = dbe->cmds.args;
    char **oneargiter;
    // dir tgt:
    if (firstrule)
    {
      firstrule = 0;
    }
    else
    {
      putc_unlocked('\n', f);
    }
    putc_unlocked('"', f);
    file_escape_string(f, sttable[dbe->diridx].s);
    fputs("\" \"", f);
    file_escape_string(f, sttable[dbe->tgtidx].s);
    fputs("\":\n", f);
    while (*argiter)
    {
      int first = 1;
      putc_unlocked('\t', f);
      oneargiter = *argiter++;
      while (*oneargiter)
      {
        if (first)
        {
          first = 0;
        }
        else
        {
          putc_unlocked(' ', f);
        }
        putc_unlocked('"', f);
        file_escape_string(f, *oneargiter);
        putc_unlocked('"', f);
        oneargiter++;
      }
      putc_unlocked('\n', f);
    }
  }
  if (ferror(f))
  {
    fclose(f);
    ret = -1;
  }
  else
  {
    ret = fclose(f);
  }
  f = NULL;
  dbf = NULL;
  return ret;
}
int merge_db_v2(void)
{
  size_t i;
  struct linked_list_node *node;
  FILE *f;
  int firstrule = 1;
  int ret;
  if (test)
  {
    return 0;
  }
  for (i = 0; i < rules_size; i++)
  {
    struct rule *rule = rules[i];
    if (!rule->is_actually_executed)
    {
      if (rule->is_forked)
      {
        LINKED_LIST_FOR_EACH(node, &rule->tgtlist)
        {
          struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
          if (debug)
          {
            print_indent();
            printf("removing %s from DB\n", sttable[e->tgtidx].s);
          }
          maybe_del_dbe(&db, e->tgtidx);
        }
      }
      continue;
    }
    LINKED_LIST_FOR_EACH(node, &rule->tgtlist)
    {
      struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
      struct dbe *dbe = my_malloc(sizeof(struct dbe));
      dbecnt++;
      dbe->tgtidx = e->tgtidx;
      dbe->diridx = rule->diridx;
      dbe->cmds = rule->cmd;
      ins_dbe(&db, dbe);
    }
  }
  if (linked_list_is_empty(&db.ll) && linked_list_is_empty(&tsdb.ll) &&
      !dbf_did_exist)
  {
    fclose(dbf);
    f = NULL;
    dbf = NULL;
    unlink(".stir.db");
    return 0;
  }
  /*
  f = fopen(".stir.db", "w");
  if (f == NULL)
  {
    fprintf(stderr, "Can't open .stir.db"); // can't use errxit
    exit(2);
  }
  */
  f = dbf;
  fprintf(f, "@v2@\n\n");
  LINKED_LIST_FOR_EACH(node, &db.ll)
  {
    struct dbe *dbe = ABCE_CONTAINER_OF(node, struct dbe, llnode);
    char ***argiter = dbe->cmds.args;
    char **oneargiter;
    // dir tgt:
    if (firstrule)
    {
      firstrule = 0;
    }
    else
    {
      putc_unlocked('\n', f);
    }
    putc_unlocked('"', f);
    file_escape_string(f, sttable[dbe->diridx].s);
    fputs("\" \"", f);
    file_escape_string(f, sttable[dbe->tgtidx].s);
    fputs("\":\n", f);
    while (*argiter)
    {
      int first = 1;
      putc_unlocked('\t', f);
      oneargiter = *argiter++;
      while (*oneargiter)
      {
        if (first)
        {
          first = 0;
        }
        else
        {
          putc_unlocked(' ', f);
        }
        putc_unlocked('"', f);
        file_escape_string(f, *oneargiter);
        putc_unlocked('"', f);
        oneargiter++;
      }
      putc_unlocked('\n', f);
    }
  }
  LINKED_LIST_FOR_EACH(node, &tsdb.ll)
  {
    struct tsdbe *tsdbe = ABCE_CONTAINER_OF(node, struct tsdbe, llnode);
    if (!tsdbe->seen)
    {
      // dir tgt:
      if (firstrule)
      {
        firstrule = 0;
      }
      else
      {
        putc_unlocked('\n', f);
      }
      putc_unlocked('"', f);
      file_escape_string(f, sttable[tsdbe->stringtabidx].s);
      //fputs("\" = ", f);
      fprintf(f, "\" = %lld %lld %lld\n", (long long)tsdbe->sz, (long long)tsdbe->ts.tv_sec, (long long)tsdbe->ts.tv_nsec);
      //putc_unlocked(' ', f);
      //fprintf(f, "%lld", (long long)tsdbe->ts.tv_sec);
      //putc_unlocked(' ', f);
      //fprintf(f, "%lld", (long long)tsdbe->ts.tv_nsec);
      //putc_unlocked('\n', f);
      continue;
    }
    // dir tgt:
    if (firstrule)
    {
      firstrule = 0;
    }
    else
    {
      putc_unlocked('\n', f);
    }
    putc_unlocked('"', f);
    file_escape_string(f, sttable[tsdbe->stringtabidx].s);
    //fputs("\" = ", f);
    fprintf(f, "\" = %lld %lld %lld\n", (long long)tsdbe->sznew, (long long)tsdbe->tsnew.tv_sec, (long long)tsdbe->tsnew.tv_nsec);
    //putc_unlocked(' ', f);
    //fprintf(f, "%lld", (long long)tsdbe->tsnew.tv_sec);
    //putc_unlocked(' ', f);
    //fprintf(f, "%lld", (long long)tsdbe->tsnew.tv_nsec);
    //putc_unlocked('\n', f);
  }
  if (ferror(f))
  {
    fclose(f);
    ret = -1;
  }
  else
  {
    ret = fclose(f);
  }
  f = NULL;
  dbf = NULL;
  return ret;
}
int merge_db(void)
{
  if (!part2_done)
  {
    if (linked_list_is_empty(&db.ll) && linked_list_is_empty(&tsdb.ll) &&
        !dbf_did_exist)
    {
      fclose(dbf);
      dbf = NULL;
      unlink(".stir.db");
      return 0;
    }
    return 0;
  }
  if (usetsdb)
  {
    return merge_db_v2();
  }
  else
  {
    return merge_db_v1();
  }
}

const char base62[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";

void create_pipe(int jobcnt)
{
  int err = 0;
  int buf;
  int i;
  if (create_jobserver_fifo)
  {
    const char *tmpdir = "/tmp";
    char *tmp;
    size_t tmpcap;
    int fd;
    if (getenv("TMPDIR") && *getenv("TMPDIR"))
    {
      tmpdir = getenv("TMPDIR");
    }
    tmpcap = strlen(tmpdir)+strlen("/SMfifoXXXXXX")+6+1;
    tmp = malloc(tmpcap);
    if (tmpdir[strlen(tmpdir)-1] == '/')
    {
      snprintf(tmp, tmpcap, "%sSMfifoXXXXXX", tmpdir);
    }
    else
    {
      snprintf(tmp, tmpcap, "%s/SMfifoXXXXXX", tmpdir);
    }
    fd = mkstemp(tmp);
    if (fd < 0)
    {
      printf("stirmake: FIFO didn't work, trying something else instead\n");
      fflush(stdout);
      goto fallback;
    }
    close(fd);
    unlink(tmp);
    // Add more randomness since an attacker may have seen the file name
    snprintf(tmp+strlen(tmp), tmpcap-strlen(tmp), "%c", base62[(unsigned)rand()%(sizeof(base62)-1)]);
    snprintf(tmp+strlen(tmp), tmpcap-strlen(tmp), "%c", base62[(unsigned)rand()%(sizeof(base62)-1)]);
    snprintf(tmp+strlen(tmp), tmpcap-strlen(tmp), "%c", base62[(unsigned)rand()%(sizeof(base62)-1)]);
    snprintf(tmp+strlen(tmp), tmpcap-strlen(tmp), "%c", base62[(unsigned)rand()%(sizeof(base62)-1)]);
    snprintf(tmp+strlen(tmp), tmpcap-strlen(tmp), "%c", base62[(unsigned)rand()%(sizeof(base62)-1)]);
    snprintf(tmp+strlen(tmp), tmpcap-strlen(tmp), "%c", base62[(unsigned)rand()%(sizeof(base62)-1)]);
    jobserver_fifo = stir_strdup(tmp);
    free(tmp);
    if (mkfifo(jobserver_fifo, 0600) != 0)
    {
      printf("stirmake: FIFO didn't work, trying something else instead\n");
      fflush(stdout);
      goto fallback;
    }
    jobserver_fd[0] = open(jobserver_fifo, O_RDWR);
    if (jobserver_fd[0] < 0)
    {
      unlink(jobserver_fifo);
      printf("stirmake: FIFO didn't work, trying something else instead\n");
      fflush(stdout);
      goto fallback;
    }
    jobserver_fd[1] = open(jobserver_fifo, O_RDWR);
    if (jobserver_fd[1] < 0)
    {
      close(jobserver_fd[0]);
      unlink(jobserver_fifo);
      printf("stirmake: FIFO didn't work, trying something else instead\n");
      fflush(stdout);
      goto fallback;
    }
    set_nonblock(jobserver_fd[1]);
    set_nonblock(jobserver_fd[0]);
    for (i = 0; i < jobcnt - 1; i++)
    {
      struct itimerval new_value = {.it_interval.tv_sec=0};
      int ret;
      new_value.it_interval.tv_sec = 0;
      new_value.it_interval.tv_usec = 0;
      new_value.it_value.tv_sec = 0;
      new_value.it_value.tv_usec = 10*1000;
      setitimer(ITIMER_REAL, &new_value, NULL);
      ret = write(jobserver_fd[1], ".", 1);
      new_value.it_interval.tv_sec = 0;
      new_value.it_interval.tv_usec = 0;
      new_value.it_value.tv_sec = 0;
      new_value.it_value.tv_usec = 0;
      setitimer(ITIMER_REAL, &new_value, NULL);
      if (ret < 0)
      {
        close(jobserver_fd[1]);
        close(jobserver_fd[0]);
        unlink(jobserver_fifo);
        printf("stirmake: FIFO didn't work, trying something else instead\n");
        fflush(stdout);
        goto fallback;
      }
    }
    created_jobserver_fifo = 1;
    return;
  }
fallback:
  jobserver_fifo = NULL; // reset it if we tried to create it
  if (socketpair(AF_UNIX, SOCK_STREAM, 0, jobserver_fd) != 0)
  {
    printf("28\n");
    my_abort();
  }
  //set_nonblock(jobserver_fd[0]); // Blocking on purpose (because of GNU make)
  set_nonblock(jobserver_fd[1]);

  buf = 512*1024;
  setsockopt(jobserver_fd[0], SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
  buf = 512*1024;
  setsockopt(jobserver_fd[1], SOL_SOCKET, SO_RCVBUF, &buf, sizeof(buf));
  buf = 512*1024;
  setsockopt(jobserver_fd[0], SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));
  buf = 512*1024;
  setsockopt(jobserver_fd[1], SOL_SOCKET, SO_SNDBUF, &buf, sizeof(buf));

  for (i = 0; i < jobcnt - 1; i++)
  {
    if (write(jobserver_fd[1], ".", 1) == -1)
    {
      err = 1;
      break;
    }
  }
  if (!err)
  {
    return;
  }
  printf("stirmake: Socket pair didn't work, using pipe instead\n");
  fflush(stdout);
  close(jobserver_fd[0]);
  close(jobserver_fd[1]);
  if (pipe(jobserver_fd) != 0)
  {
    printf("28.2\n");
    my_abort();
  }
  //set_nonblock(jobserver_fd[0]); // Blocking on purpose (because of GNU make)
  set_nonblock(jobserver_fd[1]);
  err = 0;
  for (i = 0; i < jobcnt - 1; i++)
  {
    if (write(jobserver_fd[1], ".", 1) == -1)
    {
      err = 1;
      break;
    }
  }
  if (!err)
  {
    return;
  }
  printf("stirmake: Could not write all tokens to jobserver (only %d)\n", i);
  fflush(stdout);
}

void do_setrlimit(void)
{
  struct rlimit corelimit;
  if (getrlimit(RLIMIT_CORE, &corelimit))
  {
    perror("can't getrlimit");
    exit(2);
  }
  corelimit.rlim_cur = 256*1024*1024;
  if (   corelimit.rlim_max != RLIM_INFINITY
      && corelimit.rlim_max < corelimit.rlim_cur)
  {
    corelimit.rlim_cur = corelimit.rlim_max;
  }
  if (setrlimit(RLIMIT_CORE, &corelimit))
  {
    perror("can't setrlimit");
    exit(2);
  }
}

void handle_signal(int signum)
{
  kill_all_children(signum);
}

uint32_t forkedchildcnt = 0;
int narration = 0;

fd_set globfds;
int globmaxfd = -1;

static void *mymemrchr(const void *s, int c, size_t n)
{
  unsigned char uc = (unsigned char)c;
  const unsigned char *us = s;
  size_t x;
  for (x = n; x > 0; x--)
  {
    if (us[x-1] == uc)
    {
      return (void*)&us[x-1];
    }
  }
  return NULL;
}

void drain_pipe(struct rule *rule, int fdit)
{
  for (;;)
  {
    char buf[10240];
    ssize_t bytes_read;
    char *end = NULL;
    bytes_read = read(fdit, buf, sizeof(buf));
    if (bytes_read < 0 && (errno == EWOULDBLOCK || errno == EAGAIN))
    {
      break;
    }
    if (bytes_read < 0 && (errno == EINTR))
    {
      continue;
    }
    if (bytes_read < 0)
    {
      my_abort();
    }
    if (bytes_read == 0)
    {
      FD_CLR(fdit, &globfds); // don't close yet, FIXME close later!
      break;
    }
    if (0 && out_sync == OUT_SYNC_LINE) // line means a command, not a line!
    {
      end = mymemrchr(buf, '\n', (size_t)bytes_read);
    }
    if (end)
    {
      syncbuf_append(&rule->output, buf, end-buf+1);
      syncbuf_dump(&rule->output, 1);
      syncbuf_append(&rule->output, end+1, bytes_read-(end-buf+1));
    }
    else
    {
      syncbuf_append(&rule->output, buf, (size_t)bytes_read);
    }
  }
}

#ifdef LOADAVG
double loadavg_limit = 1e100;

int loadavg_check(void)
{
  double loadavg = 0;
  if (loadavg_limit >= 1e100)
  {
    return 1;
  }
  if (getloadavg(&loadavg, 1) != 1 || loadavg > loadavg_limit)
  {
    return 0;
  }
  return 1;
}
#endif

void run_loop(int printnothing)
{
  struct linked_list_node *node;
back:
  LINKED_LIST_FOR_EACH(node, &rules_remain_list)
  {
    int ruleid = ABCE_CONTAINER_OF(node, struct rule, remainllnode)->ruleid;
    if (consider(ruleid))
    {
      if (debug)
      {
        print_indent();
        printf("goto back\n");
      }
      goto back; // this can edit the list, need to re-start iteration
    }
  }

  if (ruleids_to_run_size == 0)
  {
    if (test)
    {
      clean_jobserver();
      // FIXME should we do this: merge_db()
      exit(0);
    }
    if (!silent)
    {
      if (printnothing)
      {
        fprintf(stderr, "stirmake: Nothing to be done.\n");
      }
    }
    goto out;
  }
  else if (test)
  {
    exit(1);
  }

  while (ruleids_to_run_size > 0)
  {
    if (children)
    {
#ifdef LOADAVG
      if (!loadavg_check())
      {
        break;
      }
#endif
      if (!read_jobserver())
      {
        break;
      }
    }
    if (debug)
    {
      print_indent();
      printf("forking1 child\n");
    }
    int pipefd = -1;
    if (touchmode)
    {
      spawn_child_touch(ruleids_to_run[ruleids_to_run_size-1], out_sync != OUT_SYNC_NONE, out_sync == OUT_SYNC_RECURSE, &pipefd);
    }
    else
    {
      spawn_child(ruleids_to_run[ruleids_to_run_size-1], out_sync != OUT_SYNC_NONE, out_sync == OUT_SYNC_RECURSE, &pipefd);
    }
    if (pipefd >= 0)
    {
      FD_SET(pipefd, &globfds);
      if (pipefd > globmaxfd)
      {
        globmaxfd = pipefd;
      }
    }
    ruleids_to_run_size--;
  }

  int locmaxfd = 0;
  if (self_pipe_fd[0] > locmaxfd)
  {
    locmaxfd = self_pipe_fd[0];
  }
  if (jobserver_fd[0] > locmaxfd)
  {
    locmaxfd = jobserver_fd[0];
  }
  char chbuf[100];
  while (children > 0)
  {
    int wstatus = 0;
    struct timeval tv = {.tv_sec = 1, .tv_usec = 0}; // needed for load avg
    fd_set readfds = globfds;
    FD_SET(self_pipe_fd[0], &readfds);
    if (ruleids_to_run_size > 0)
    {
#ifdef LOADAVG
      if (loadavg_check())
      {
        FD_SET(jobserver_fd[0], &readfds);
      }
#endif
    }
    select((locmaxfd > globmaxfd) ? (locmaxfd+1) : (globmaxfd+1),
           &readfds, NULL, NULL, &tv);
    if (debug)
    {
      print_indent();
      printf("select returned\n");
    }
    if (sigterm_atomic)
    {
      handle_signal(SIGTERM);
      errxit("Got SIGTERM");
      exit(2);
    }
    if (sigint_atomic)
    {
      handle_signal(SIGINT);
      errxit("Got SIGINT");
      exit(2);
    }
    if (sighup_atomic)
    {
      handle_signal(SIGHUP);
      errxit("Got SIGHUP");
      exit(2);
    }
    int fdit;
    for (fdit = 0; fdit <= globmaxfd; fdit++)
    {
      if (fdit == self_pipe_fd[0] || fdit == jobserver_fd[0])
      {
        continue;
      }
      if (!FD_ISSET(fdit, &readfds))
      {
        continue;
      }
      int ruleid = ruleid_by_fd(fdit);
      if (ruleid < 0)
      {
        abort();
      }
      drain_pipe(rules[ruleid], fdit);
    }
    if (read(self_pipe_fd[0], chbuf, 100))
    {
      for (;;)
      {
        pid_t pid;
        pid = waitpid(-1, &wstatus, WNOHANG);
        if (pid == 0)
        {
          break;
        }
        if (!ignoreerr && wstatus != 0 && pid > 0 && ignore_by_pid(pid) <= 0)
        {
          if (!WIFEXITED(wstatus) || WEXITSTATUS(wstatus) != 0)
          {
            int fd = -1;
            int ruleid = ruleid_by_pid_erase(pid, &fd);
            if (ruleid < 0)
            {
              printf("31.1\n");
              my_abort();
            }
            if (fd >= 0)
            {
              drain_pipe(rules[ruleid], fd);
              syncbuf_dump(&rules[ruleid]->output, 1);
              close(fd);
              FD_CLR(fd, &globfds);
            }
            children--;
            if (!touchmode)
            {
              char *cmd;
              cmd = get_cmd_only(&rules[ruleid]->cmd.args[rules[ruleid]->cmdidx-1][3]);
              fprintf(stderr, "stirmake: recipe for target '%s' in directory '%s' failed\nstirmake: failed command was: %s\n", sttable[ABCE_CONTAINER_OF(rules[ruleid]->tgtlist.node.next, struct stirtgt, llnode)->tgtidx].s, sttable[rules[ruleid]->diridx].s, cmd);
              free(cmd);
            }
            else
            {
              fprintf(stderr, "stirmake: recipe for target '%s' in directory '%s' failed\n", sttable[ABCE_CONTAINER_OF(rules[ruleid]->tgtlist.node.next, struct stirtgt, llnode)->tgtidx].s, sttable[rules[ruleid]->diridx].s);
            }
            LINKED_LIST_FOR_EACH(node, &rules[ruleid]->tgtlist)
            {
              struct stirtgt *e = ABCE_CONTAINER_OF(node, struct stirtgt, llnode);
	      unlink(sttable[e->tgtidx].s);
	    }
            if (WIFSIGNALED(wstatus))
            {
              if (doasmuchascan)
              {
                fprintf(stderr, "Error: signaled\n");
                cmdfailed = 1;
              }
              else
              {
                errxit("Error: signaled");
              }
            }
            else if (WIFEXITED(wstatus))
            {
              if (doasmuchascan)
              {
                fprintf(stderr, "Error %d\n", (int)WEXITSTATUS(wstatus));
                cmdfailed = 1;
              }
              else
              {
                errxit("Error %d", (int)WEXITSTATUS(wstatus));
              }
            }
            else
            {
              errxit("Unknown error");
            }
            if (doasmuchascan)
            {
              continue;
            }
            my_abort();
          }
        }
        if (children <= 0 && ruleids_to_run_size == 0)
        {
          if (pid < 0 && errno == ECHILD)
          {
            goto out;
          }
          printf("29\n");
          my_abort();
        }
        if (pid < 0)
        {
          if (errno == ECHILD)
          {
            break;
          }
          printf("30\n");
          my_abort();
        }
        int fd = -1;
        int ruleid = ruleid_by_pid_erase(pid, &fd);
        if (ruleid < 0)
        {
          printf("31\n");
          my_abort();
        }
        // FIXME do something with fd
        if (fd >= 0)
        {
          drain_pipe(rules[ruleid], fd);
          if (out_sync == OUT_SYNC_LINE)
          {
            syncbuf_dump(&rules[ruleid]->output, 1);
          }
          else if (touchmode && ready_touch(ruleid))
          {
            syncbuf_dump(&rules[ruleid]->output, 1);
          }
          else if (!touchmode && ready(ruleid))
          {
            syncbuf_dump(&rules[ruleid]->output, 1);
          }
          close(fd);
          FD_CLR(fd, &globfds);
        }
        if (touchmode && !ready_touch(ruleid))
        {
          int pipefd = -1;
          spawn_child_touch(ruleid, out_sync != OUT_SYNC_NONE, out_sync == OUT_SYNC_RECURSE, &pipefd);
          if (pipefd >= 0)
          {
            FD_SET(pipefd, &globfds);
            if (pipefd > globmaxfd)
            {
              globmaxfd = pipefd;
            }
          }
        }
        else if (!touchmode && !ready(ruleid))
        {
          int pipefd = -1;
          spawn_child(ruleid, out_sync != OUT_SYNC_NONE, out_sync == OUT_SYNC_RECURSE, &pipefd);
          if (pipefd >= 0)
          {
            FD_SET(pipefd, &globfds);
            if (pipefd > globmaxfd)
            {
              globmaxfd = pipefd;
            }
          }
        }
        else
        {
          //ruleremain_rm(rules[ruleid]);
          children--;
          if (children != 0)
          {
            write_jobserver();
          }
          mark_executed(ruleid, 1);
        }
        forkedchildcnt++;
        if (((forkedchildcnt % 10) == 0) && narration)
        {
          do_narration();
        }
      }
    }
    while (ruleids_to_run_size > 0)
    {
      if (children)
      {
#ifdef LOADAVG
        if (!loadavg_check())
        {
          break;
        }
#endif
        if (!read_jobserver())
        {
          break;
        }
      }
      if (debug)
      {
        print_indent();
        printf("forking child\n");
      }
      //std::cout << "forking child" << std::endl;
      int pipefd = -1;
      if (touchmode)
      {
        spawn_child_touch(ruleids_to_run[ruleids_to_run_size-1], out_sync != OUT_SYNC_NONE, out_sync == OUT_SYNC_RECURSE, &pipefd);
      }
      else
      {
        spawn_child(ruleids_to_run[ruleids_to_run_size-1], out_sync != OUT_SYNC_NONE, out_sync == OUT_SYNC_RECURSE, &pipefd);
      }
      if (pipefd >= 0)
      {
        FD_SET(pipefd, &globfds);
        if (pipefd > globmaxfd)
        {
          globmaxfd = pipefd;
        }
      }
      ruleids_to_run_size--;
      //ruleids_to_run.pop_back();
    }
  }
out:
  if (linked_list_is_empty(&rules_remain_list))
  {
    return;
  }
  else
  {
    if (doasmuchascan && cmdfailed)
    {
      errxit("Some of the commands failed");
    }
    fprintf(stderr, "stirmake: *** Out of children, yet not all targets made.\n");
    LINKED_LIST_FOR_EACH(node, &rules_remain_list)
    {
      int ruleid = ABCE_CONTAINER_OF(node, struct rule, remainllnode)->ruleid;
      free(better_cycle_detect(ruleid, 0));
    }
    my_abort();
  }

}

void process_orders(struct stiryy_main *stirmain)
{
  size_t i;

  if (!deps_remain_calculated)
  {
    abort();
  }

  for (i = 0; i < stirmain->ordersz; i++)
  {
    struct rule *rule;
    int ret;
    if (stirmain->orders[i].rulecnt != 2)
    {
      my_abort();
    }
    mysize_t first = stringtab_get(stirmain->orders[i].rules[0]);
    mysize_t second = stringtab_get(stirmain->orders[i].rules[1]);
    if (first == (mysize_t)-1)
    {
      errxit("@order rule '%s' not found", stirmain->orders[i].rules[0]);
    }
    if (second == (mysize_t)-1)
    {
      errxit("@order rule '%s' not found", stirmain->orders[i].rules[1]);
    }
    int firstrule = get_ruleid_by_tgt(first);
    int secondrule = get_ruleid_by_tgt(second);
    if (firstrule < 0)
    {
      errxit("@order rule '%s' not found", stirmain->orders[i].rules[0]);
    }
    if (secondrule < 0)
    {
      errxit("@order rule '%s' not found", stirmain->orders[i].rules[1]);
    }
    if (!rules[firstrule]->is_traversed || !rules[secondrule]->is_traversed)
    {
      continue;
    }
    rule = rules[secondrule];
    ret = ins_dep(rule, first, rule->diridx, (mysize_t)-1, 0, 0, 0, 0, 0);
    deps_remain_insert(rule, firstrule);
    if (ret == 0)
    {
      ins_ruleid_by_dep2(first, secondrule, 1);
    }
    if (debug)
    {
      print_indent();
      printf("added order %s %s\n", stirmain->orders[i].rules[0], stirmain->orders[i].rules[1]);
    }
  }
}

void do_srand(void)
{
  uint32_t randseed = 0;
#ifndef HAVE_GETRANDOM
  ssize_t read_ret;
  int fd;
#endif
  srand(time(NULL) ^ getpid());
#ifdef HAVE_GETRANDOM
  if (getrandom(&randseed, sizeof(randseed), 0) == sizeof(randseed))
  {
    srand(randseed);
  }
#else
  fd = open("/dev/urandom", O_RDONLY);
  if (fd < 0)
  {
    return;
  }
  while (((read_ret = read(fd, &randseed, sizeof(randseed)) < 0) || read_ret != sizeof(randseed)) && errno == EINTR)
  {
    // re-do
  }
  srand(randseed);
  close(fd);
#endif
}
int yy_stored_lineno = -1;
const char *yy_stored_prefix = NULL;

void chomp(char *x)
{
  if (x && *x && x[strlen(x)-1] == '\n')
  {
    x[strlen(x)-1] = '\0';
  }
}

#ifdef WITH_LUA
int lua_dirup(lua_State *lua)
{
  char *prefix;
  char *backpath;
  struct abce *abce;
  struct stiryy_main *stirmain;
  lua_getglobal(lua, "__abcelua_abce");
  abce = lua_touserdata(lua, -1);
  stirmain = abce->trap_baton;
  lua_pop(lua, 1);
  if (abce_scope_get_userdata(&abce->dynscope))
  {
    prefix =
      ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prjprefix;
  }
  else
  {
    prefix = ".";
  }
  if (prefix == NULL)
  {
    prefix = ".";
  }
  backpath = construct_backpath(prefix);
  lua_pushstring(lua, backpath);
  free(backpath);
  return 1;
}
int lua_dirupall(lua_State *lua)
{
  char *prefix;
  char *backpath;
  struct abce *abce;
  struct stiryy_main *stirmain;
  lua_getglobal(lua, "__abcelua_abce");
  abce = lua_touserdata(lua, -1);
  stirmain = abce->trap_baton;
  lua_pop(lua, 1);
  if (abce_scope_get_userdata(&abce->dynscope))
  {
    prefix =
      ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prefix;
  }
  else
  {
    prefix = ".";
  }
  if (prefix == NULL)
  {
    prefix = ".";
  }
  backpath = construct_backpath(prefix);
  lua_pushstring(lua, backpath);
  free(backpath);
  return 1;
}
int lua_dirdown(lua_State *lua)
{
  char *prefix;
  struct abce *abce;
  struct stiryy_main *stirmain;
  lua_getglobal(lua, "__abcelua_abce");
  abce = lua_touserdata(lua, -1);
  stirmain = abce->trap_baton;
  lua_pop(lua, 1);
  if (abce_scope_get_userdata(&abce->dynscope))
  {
    prefix =
      ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prjprefix;
  }
  else
  {
    prefix = ".";
  }
  if (prefix == NULL)
  {
    prefix = ".";
  }
  lua_pushstring(lua, prefix);
  return 1;
}
int lua_dirdownall(lua_State *lua)
{
  char *prefix;
  struct abce *abce;
  struct stiryy_main *stirmain;
  lua_getglobal(lua, "__abcelua_abce");
  abce = lua_touserdata(lua, -1);
  stirmain = abce->trap_baton;
  lua_pop(lua, 1);
  if (abce_scope_get_userdata(&abce->dynscope))
  {
    prefix =
      ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prefix;
  }
  else
  {
    prefix = ".";
  }
  if (prefix == NULL)
  {
    prefix = ".";
  }
  lua_pushstring(lua, prefix);
  return 1;
}
int lua_addrule(lua_State *lua)
{
  struct abce *abce;
  if (lua_gettop(lua) == 0)
  {
    return luaL_error(lua, "Stir.addrule requires an argument");
  }
  const char *btsym = "(Stir.addrule)";
  struct abce_mb *mb = NULL;
  char *prefix;
  struct stiryy_main *stirmain;
  lua_getglobal(lua, "__abcelua_abce");
  abce = lua_touserdata(lua, -1);
  stirmain = abce->trap_baton;
  lua_pop(lua, 1);
  mb_from_lua(lua, abce, 1);
  if (abce_getmbptr(&mb, abce, -1) != 0)
  {
    abce_pop(abce);
    return luaL_error(lua, "Stir.addrule encountered error");
  }
  // Convert empty array into empty tree
  if (mb->typ == ABCE_T_A && mb->u.area->u.ar.size == 0)
  {
    struct abce_mb *mbt;
    mbt = abce_mb_cpush_create_tree(abce);
    if (mbt != NULL)
    {
      abce_pop(abce);
      abce_push_mb(abce, mbt);
      abce_cpop(abce);
    }
  }
  if (mb->typ == ABCE_T_T)
  {
    struct abce_mb *res;
    int64_t attrs;
    attrs = abce_cache_add_str_nul(abce, "attrs");
    if (attrs >= 0 && abce_tree_get_str(abce, &res, mb, &abce->cachebase[attrs]) == 0)
    {
      if (res->typ == ABCE_T_A && res->u.area->u.ar.size == 0)
      {
        struct abce_mb *mbval = abce_mb_cpush_create_tree(abce);
	if (mbval)
	{
          abce_tree_set_str(abce, mb, &abce->cachebase[attrs], mbval);
	  abce_cpop(abce);
	}
      }
    }
  }
  if (!stirmain->parsing)
  {
    abce->err.code = STIR_E_RULECHANGE_NOT_PERMITTED;
    abce->err.mb.typ = ABCE_T_N;
    return luaL_error(lua, "Stir.addrule encountered error, rule change not permitted in non-parsing state");
  }
  if (abce_scope_get_userdata(&abce->dynscope))
  {
    prefix =
      ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prefix;
  }
  else
  {
    prefix = ".";
  }
  if (stir_trap_ruleadd(stirmain, abce, prefix) != 0)
  {
    //abce_pop(abce); // stir_trap_ruleadd does this
    return luaL_error(lua, "Stir.addrule encountered error");
  }
  //abce_pop(abce); // No result for this operation
  lua_pushnil(lua);
  return 1;
}
int lua_adddep(lua_State *lua)
{
  struct abce *abce;
  if (lua_gettop(lua) < 3)
  {
    return luaL_error(lua, "Stir.adddep requires three arguments");
  }
  const char *btsym = "(Stir.adddep)";
  struct abce_mb *mb = NULL;
  char *prefix;
  struct stiryy_main *stirmain;
  lua_getglobal(lua, "__abcelua_abce");
  abce = lua_touserdata(lua, -1);
  stirmain = abce->trap_baton;
  lua_pop(lua, 1);
  mb_from_lua(lua, abce, 1);
  mb_from_lua(lua, abce, 2);
  mb_from_lua(lua, abce, 3);
  if (abce_getmbptr(&mb, abce, -1) != 0)
  {
    abce_pop(abce);
    abce_pop(abce);
    abce_pop(abce);
    return luaL_error(lua, "Stir.adddep encountered error");
  }
  // Convert empty array into empty tree
  if (mb->typ == ABCE_T_A && mb->u.area->u.ar.size == 0)
  {
    struct abce_mb *mbt;
    mbt = abce_mb_cpush_create_tree(abce);
    if (mbt != NULL)
    {
      abce_pop(abce);
      abce_push_mb(abce, mbt);
      abce_cpop(abce);
    }
  }
  if (abce_scope_get_userdata(&abce->dynscope))
  {
    prefix =
      ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prefix;
  }
  else
  {
    prefix = ".";
  }
  if (stir_trap_depadd(stirmain, abce, prefix) != 0)
  {
    //abce_pop(abce); // stir_trap_depadd does this
    //abce_pop(abce); // stir_trap_depadd does this
    //abce_pop(abce); // stir_trap_depadd does this
    return luaL_error(lua, "Stir.adddep encountered error");
  }
  //abce_pop(abce); // No result for this operation
  lua_pushnil(lua);
  return 1;
}
int luaopen_stir1(lua_State *lua)
{
  static const luaL_Reg stir_lib[] = {
    {"addrule", lua_addrule},
    {"adddep", lua_adddep},
    {"dirup", lua_dirup},
    {"dirupall", lua_dirupall},
    {"dirdown", lua_dirdown},
    {"dirdownall", lua_dirdownall},
    {NULL, NULL}
  };
  luaL_newlib(lua, stir_lib);
  return 1;
}
void luaopen_stir(lua_State *lua, struct abce *abce, struct abce_mb_area *scope)
{
  const char *luapathorig;
  size_t sz;
  char *luapathnew;
  char *dirdownall;
  lua_pushcfunction(lua, luaopen_stir1);
  lua_pushstring(lua, "Stir");
  lua_call(lua, 1, 1);
  lua_pushvalue(lua, -1);
  lua_setglobal(lua, "Stir");
  lua_pop(lua, 1);
  //
  if (abce_scope_get_userdata(&abce->dynscope))
  {
    dirdownall =
      ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prefix;
  }
  else
  {
    dirdownall = ".";
  }
  if (dirdownall == NULL)
  {
    dirdownall = ".";
  }
  //
  lua_getglobal(lua, "package");
  lua_getfield(lua, -1, "path");
  luapathorig = lua_tostring(lua, -1);
  sz = strlen("./")+strlen(dirdownall)+strlen("/?.lua;")+strlen(luapathorig)+1;
  luapathnew = malloc(sz);
  if (snprintf(luapathnew, sz, "./%s/?.lua;%s", dirdownall, luapathorig) >= (int)sz)
  {
    abort();
  }
  lua_pop(lua, 1);
  lua_pushstring(lua, luapathnew);
  lua_setfield(lua, -2, "path");
  lua_pop(lua, 1);
  free(luapathnew);
}
#endif

struct incyyfnstruct {
  int auto_phony;
  mysize_t *tgtstringidxs;
  mysize_t tgtstringidxsz;
  mysize_t tgtstringidxcap;
};

void incyy_add_rule(void *userdata, const char *str, mysize_t len)
{
  struct incyyfnstruct *dat = userdata;
  dat->tgtstringidxsz = 0;
}
void incyy_add_tgt(void *userdata, const char *str, mysize_t len)
{
  struct incyyfnstruct *dat = userdata;
  mysize_t tgtidx;
  if (dat->tgtstringidxsz >= dat->tgtstringidxcap)
  {
    mysize_t newcap = 2*dat->tgtstringidxcap + 4;
    mysize_t *newbuf = realloc(dat->tgtstringidxs, newcap*sizeof(*dat->tgtstringidxs));
    if (!newbuf)
    {
      errxit("Not enough memory");
      exit(2);
    }
    dat->tgtstringidxs = newbuf;
    dat->tgtstringidxcap = newcap;
  }
  tgtidx = stringtab_add_len(str, len);
  dat->tgtstringidxs[dat->tgtstringidxsz++] = tgtidx;
  ins_add_dep(tgtidx, (mysize_t)-1, (mysize_t)-1, 0, 0, !!dat->auto_phony);
}
void incyy_add_dep(void *userdata, const char *str, mysize_t len)
{
  struct incyyfnstruct *dat = userdata;
  mysize_t i;
  mysize_t depidx;
  depidx = stringtab_add_len(str, len);
  for (i = 0; i < dat->tgtstringidxsz; i++)
  {
    mysize_t tgtidx = dat->tgtstringidxs[i];
    ins_add_dep(tgtidx, depidx, (mysize_t)-1, !!dat->auto_phony, 0, !!dat->auto_phony);
  }
}

int isapipe(int fd)
{
  if (lseek(fd, 0, SEEK_CUR) == (off_t)-1 && errno == ESPIPE && !isatty(fd))
  {
    return 1;
  }
  return 0;
}

int main(int argc, char **argv)
{
#if 0
  pathological_test();
#endif
  FILE *f;
  struct stiryy_main stirmain = {.abce = &abce};
  struct stiryy stiryy = STIRYY_EMPTY;
  size_t i;
  int opt;
  int filename_set = 0;
  const char *filename = "Stirfile";
  int jobcnt = 1;
  int cleanbinaries = 0;
  int clean = 0;
  size_t cwdcap = PATH_MAX+1;
  size_t cwd_sameprojcap = PATH_MAX+1;
  size_t storcwdcap = PATH_MAX+1;
  size_t curcwdcap = PATH_MAX+1;
  char *cwd;
  char *cwd_sameproj;
  char *storcwd;
  char *curcwd;
  size_t upcnt = 0;
  size_t upcnt_sameproj = 0;
  char *fwd_path = ".";
  char *this_path = ".";
  int ruleid_first = 0;
  int ruleid_first_set = 0;
  char *outsyncmflag = NULL;
  struct linked_list_node *node;

  char *dupargv0 = stir_strdup(argv[0]);
  char *basenm = basename(dupargv0);

  acceptchars_init();

  cwd = malloc(cwdcap);
  cwd_sameproj = malloc(cwd_sameprojcap);
  storcwd = malloc(storcwdcap);
  curcwd = malloc(curcwdcap);
  cwd[0] = '\0';
  cwd_sameproj[0] = '\0';
  storcwd[0] = '\0';
  curcwd[0] = '\0';

  struct sigaction saseg;
  sigemptyset(&saseg.sa_mask);
#ifdef SA_RESTART
  saseg.sa_flags = SA_RESTART;
#else
  saseg.sa_flags = 0;
#endif
  saseg.sa_handler = sigsegv_handler;
  sigaction(SIGSEGV, &saseg, NULL);

  struct sigaction safpe;
  sigemptyset(&safpe.sa_mask);
#ifdef SA_RESTART
  safpe.sa_flags = SA_RESTART;
#else
  safpe.sa_flags = 0;
#endif
  safpe.sa_handler = sigfpe_handler;
  sigaction(SIGFPE, &safpe, NULL);

  struct sigaction saill;
  sigemptyset(&saill.sa_mask);
#ifdef SA_RESTART
  saill.sa_flags = SA_RESTART;
#else
  saill.sa_flags = 0;
#endif
  saill.sa_handler = sigill_handler;
  sigaction(SIGILL, &saill, NULL);

  struct sigaction saquit;
  sigemptyset(&saquit.sa_mask);
#ifdef SA_RESTART
  saquit.sa_flags = SA_RESTART;
#else
  saquit.sa_flags = 0;
#endif
  saquit.sa_handler = sigquit_handler;
  sigaction(SIGQUIT, &saquit, NULL);

  struct sigaction sasys;
  sigemptyset(&sasys.sa_mask);
#ifdef SA_RESTART
  sasys.sa_flags = SA_RESTART;
#else
  sasys.sa_flags = 0;
#endif
  sasys.sa_handler = sigsys_handler;
  sigaction(SIGSYS, &sasys, NULL);

  struct sigaction saxcpu;
  sigemptyset(&saxcpu.sa_mask);
#ifdef SA_RESTART
  saxcpu.sa_flags = SA_RESTART;
#else
  saxcpu.sa_flags = 0;
#endif
  saxcpu.sa_handler = sigxcpu_handler;
  sigaction(SIGXCPU, &saxcpu, NULL);

  struct sigaction saxfsz;
  sigemptyset(&saxfsz.sa_mask);
#ifdef SA_RESTART
  saxfsz.sa_flags = SA_RESTART;
#else
  saxfsz.sa_flags = 0;
#endif
  saxfsz.sa_handler = sigxfsz_handler;
  sigaction(SIGXFSZ, &saxfsz, NULL);

  struct sigaction saabrt;
  sigemptyset(&saabrt.sa_mask);
#ifdef SA_RESTART
  saabrt.sa_flags = SA_RESTART;
#else
  saabrt.sa_flags = 0;
#endif
  saabrt.sa_handler = sigabrt_handler;
  sigaction(SIGABRT, &saabrt, NULL);

  struct sigaction sabus;
  sigemptyset(&sabus.sa_mask);
#ifdef SA_RESTART
  sabus.sa_flags = SA_RESTART;
#else
  sabus.sa_flags = 0;
#endif
  sabus.sa_handler = sigbus_handler;
  sigaction(SIGBUS, &sabus, NULL);

  do_srand();

  do_setrlimit();

  if (my_malloc_init())
  {
    errxit("Can't mmap arena");
    exit(2);
  }

  if (strcmp(basenm, "smka") == 0)
  {
    set_mode(MODE_ALL, 1, argv[0]);
  }
  else if (strcmp(basenm, "smkt") == 0)
  {
    set_mode(MODE_THIS, 1, argv[0]);
  }
  else if (strcmp(basenm, "smkp") == 0)
  {
    set_mode(MODE_PROJECT, 1, argv[0]);
  }

  process_mflags(NULL, NULL, &outsyncmflag);
  if (outsyncmflag)
  {
    if (outsyncmflag[0] == 'n')
    {
      out_sync = OUT_SYNC_NONE;
    }
    else if (outsyncmflag[0] == 'l')
    {
      was_recursive_outsync = 1;
      out_sync = OUT_SYNC_LINE;
    }
    else if (outsyncmflag[0] == 't')
    {
      was_recursive_outsync = 1;
      out_sync = OUT_SYNC_TARGET;
    }
    else if (outsyncmflag[0] == 'r')
    {
      was_recursive_outsync = 1;
      out_sync = OUT_SYNC_RECURSE;
    }
    else
    {
      fprintf(stderr, "stirmake: invalid output sync mode in MAKEFLAGS, using default\n");
      out_sync = OUT_SYNC_NOT_SET_YET;
    }
  }
  if (isapipe(1))
  {
    output_is_pipe = 1;
  }

#if DEFAULT_CREATE_JOBSERVER_FIFO == 2
  f = fopen("/etc/os-release", "r");
  if (f)
  {
    char *line = NULL;
    size_t n = 0;
    int is_ubuntu = 0;
    int is_debian = 0;
    int is_fedora = 0;
    int is_mint = 0;
    int is_rhel_or_almalinux = 0;
    //int is_arch = 0;
    int is_sles = 0;
    for (;;)
    {
      if (mygetline(&line, &n, f) <= 0)
      {
        break;
      }
      chomp(line);
      if (strcmp(line, "ID=sles") == 0 ||
          strcmp(line, "ID=\"sles\"") == 0 ||
          strcmp(line, "ID='sles'") == 0)
      {
        is_sles = 1;
      }
      if (strcmp(line, "ID=ubuntu") == 0 ||
          strcmp(line, "ID=\"ubuntu\"") == 0 ||
          strcmp(line, "ID='ubuntu'") == 0)
      {
        is_ubuntu = 1;
      }
      if (strcmp(line, "ID=linuxmint") == 0 ||
          strcmp(line, "ID=\"linuxmint\"") == 0 ||
          strcmp(line, "ID='linuxmint'") == 0)
      {
        is_mint = 1;
      }
      if (strcmp(line, "ID=fedora") == 0 ||
          strcmp(line, "ID=\"fedora\"") == 0 ||
          strcmp(line, "ID='fedora'") == 0)
      {
        is_fedora = 1;
      }
      if (strcmp(line, "ID=arch") == 0 ||
          strcmp(line, "ID=\"arch\"") == 0 ||
          strcmp(line, "ID='arch'") == 0)
      {
        // rolling release, assume up-to-date
        //is_arch = 1;
        create_jobserver_fifo = 1;
      }
      if (strcmp(line, "ID=debian") == 0 ||
          strcmp(line, "ID=\"debian\"") == 0 ||
          strcmp(line, "ID='debian'") == 0 ||
          strcmp(line, "ID=raspbian") == 0 ||
          strcmp(line, "ID=\"raspbian\"") == 0 ||
          strcmp(line, "ID='raspbian'") == 0)
      {
        is_debian = 1;
      }
      if (strcmp(line, "ID=rhel") == 0 ||
          strcmp(line, "ID=\"rhel\"") == 0 ||
          strcmp(line, "ID='rhel'") == 0 ||
          strcmp(line, "ID=almalinux") == 0 ||
          strcmp(line, "ID=\"almalinux\"") == 0 ||
          strcmp(line, "ID='almalinux'") == 0 ||
          strcmp(line, "ID=rocky") == 0 ||
          strcmp(line, "ID=\"rocky\"") == 0 ||
          strcmp(line, "ID='rocky'") == 0)
      {
        is_rhel_or_almalinux = 1;
      }
    }
    rewind(f);
    for (;;)
    {
      if (mygetline(&line, &n, f) <= 0)
      {
        break;
      }
      chomp(line);
      if (strncmp(line, "VERSION_ID=\"",
                  strlen("VERSION_ID=\"")) == 0 ||
          strncmp(line, "VERSION_ID='",
                  strlen("VERSION_ID='")) == 0)
      {
        if (is_ubuntu)
        {
          if (strcmp(line+strlen("VERSION_ID=\""), "25.04") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_mint)
        {
          if (strcmp(line+strlen("VERSION_ID="), "23") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_debian)
        {
          if (strcmp(line+strlen("VERSION_ID=\""), "13") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_rhel_or_almalinux)
        {
          if (strcmp(line+strlen("VERSION_ID="), "10") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_fedora)
        {
          if (strcmp(line+strlen("VERSION_ID="), "39") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_sles)
        {
          if (strcmp(line+strlen("VERSION_ID="), "16") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
      }
      else if (strncmp(line, "VERSION_ID=",
                       strlen("VERSION_ID=")) == 0)
      {
        if (is_ubuntu)
        {
          if (strcmp(line+strlen("VERSION_ID="), "25.04") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_mint)
        {
          if (strcmp(line+strlen("VERSION_ID="), "23") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_debian)
        {
          if (strcmp(line+strlen("VERSION_ID="), "13") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_rhel_or_almalinux)
        {
          if (strcmp(line+strlen("VERSION_ID="), "10") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_fedora)
        {
          if (strcmp(line+strlen("VERSION_ID="), "39") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
        if (is_sles)
        {
          if (strcmp(line+strlen("VERSION_ID="), "16") >= 0)
          {
            create_jobserver_fifo = 1;
          }
        }
      }
    }
    fclose(f);
    free(line);
  }
#endif

  debug = 0;
  while ((opt = getopt(argc, argv, "vGdf:Htpaj:hcbO:qC:ikBW:X:no:r:sl:TReEFPu")) != -1)
  {
    switch (opt)
    {
    case 'e':
      usetsdb = 0;
      break;
    case 'E':
      usetsdb = 1;
      break;
    case 'C':
      if (chdir(optarg) != 0)
      {
        fprintf(stderr, "stirmake: failed to chdir to %s\n", optarg);
	exit(2);
      }
      break;
    case 'v':
      version(argv[0]);
      break;
    case 'G':
      gitversions(argv[0]);
      break;
    case 'T':
      touchmode = 1;
      break;
    case 's':
      silent = 1;
      break;
    case 'l':
#ifdef LOADAVG
      if (optarg[0] == 'a') // a for auto
      {
        loadavg_limit = my_get_nprocs();
        if (loadavg_limit < 1)
        {
          fprintf(stderr, "stirmake: Processor count %d insane, using 1\n",
                  (int)loadavg_limit);
          loadavg_limit = 1;
        }
      }
      else
      {
        char *endptr = NULL;
        loadavg_limit = strtod(optarg, &endptr);
        if (*endptr == '\0' && *optarg != '\0')
        {
          // ok
        }
        else
        {
          fprintf(stderr, "stirmake: invalid load average limit: %s\n", optarg);
          exit(2);
        }
      }
#else
      fprintf(stderr, "System does not support load average.\n");
      exit(2);
#endif
      break;
    case 'W':
    {
      struct pretend *oldpretend = pretend;
      pretend = malloc(sizeof(*pretend));
      pretend->fname = canon(optarg);
      pretend->next = oldpretend;
      pretend->relative = 0;
      pretend->pretendtype = PRETEND_MODIFIED;
      break;
    }
    case 'X':
    {
      struct pretend *oldpretend = pretend;
      pretend = malloc(sizeof(*pretend));
      pretend->fname = canon(optarg);
      pretend->next = oldpretend;
      pretend->relative = 1;
      pretend->pretendtype = PRETEND_MODIFIED;
      break;
    }
    case 'o': // don't remake and still pretend being very old
    {
      struct pretend *oldpretend = pretend;
      pretend = malloc(sizeof(*pretend));
      pretend->fname = canon(optarg);
      pretend->next = oldpretend;
      pretend->relative = 0;
      pretend->pretendtype = PRETEND_VERY_OLD_NO_REMAKE;
      break;
    }
    case 'R': // trace
      do_trace = 1;
      break;
    case 'r': // -o but relative
    {
      struct pretend *oldpretend = pretend;
      pretend = malloc(sizeof(*pretend));
      pretend->fname = canon(optarg);
      pretend->next = oldpretend;
      pretend->relative = 1;
      pretend->pretendtype = PRETEND_VERY_OLD_NO_REMAKE;
      break;
    }
    case 'i':
      ignoreerr = 1;
      break;
    case 'k':
      doasmuchascan = 1;
      break;
    case 'B':
      unconditional = 1;
      break;
    case 'd':
      debug = 1;
      break;
    case 'q':
      test = 1;
      break;
    case 'n':
      dry_run = 1;
      break;
    case 'O':
      if (optarg[0] == 'n')
      {
        out_sync = OUT_SYNC_NONE;
      }
      else if (optarg[0] == 'l')
      {
        out_sync = OUT_SYNC_LINE;
      }
      else if (optarg[0] == 't')
      {
        out_sync = OUT_SYNC_TARGET;
      }
      else if (optarg[0] == 'r')
      {
        out_sync = OUT_SYNC_RECURSE;
      }
      else
      {
        usage(argv[0]);
      }
      break;
    case 'H':
      narration = 1;
      setlocale(LC_CTYPE, "");
      break;
    case 'F':
      create_jobserver_fifo = 1;
      break;
    case 'P':
      create_jobserver_fifo = 0; // use a pipe
      break;
    case 'f': // FIXME what if optarg contains directories?
      filename_set = 1;
      filename = optarg;
      break;
    case 'c': // clean
      clean = 1;
      break;
    case 'b': // clean with binaries
      cleanbinaries = 1;
      break;
    case 'j':
      if (optarg[0] == 'a') // a for auto
      {
        jobcnt = my_get_nprocs();
        if (jobcnt < 1)
        {
          fprintf(stderr, "stirmake: Processor count %d insane, using 1\n",
                  jobcnt);
          jobcnt = 1;
        }
        if (jobcnt > MAX_JOBCNT)
        {
          jobcnt = MAX_JOBCNT;
        }
      }
      else
      {
        jobcnt = atoi(optarg);
        if (jobcnt < 1)
        {
          usage(argv[0]);
        }
        if (jobcnt > MAX_JOBCNT)
        {
          usage(argv[0]);
        }
      }
      break;
    case 't':
      set_mode(MODE_THIS, 0, argv[0]);
      break;
    case 'p':
      set_mode(MODE_PROJECT, 0, argv[0]);
      break;
    case 'a':
      set_mode(MODE_ALL, 0, argv[0]);
      break;
    case 'u':
      unsafe = 1;
      break;
    default:
    case '?':
    case 'h':
      usage(argv[0]);
    }
  }

  if (test && (clean || cleanbinaries))
  {
    usage(argv[0]);
  }

  if (mode == MODE_NONE && !filename_set)
  {
    usage(argv[0]);
  }
  if (mode != MODE_NONE && filename_set)
  {
    usage(argv[0]);
  }

  max_syncbufs_cached = jobcnt * 2 + 8;

  recursion_misuse_prevention();

  if (!filename_set)
  {
    size_t curupcnt = 0;
    int last_was_toplevel = 1; // default
    char *ptr;
    while ((ptr = getcwd(cwd, cwdcap)) == NULL && errno == ERANGE)
    {
      cwdcap *= 2;
      cwd = realloc(cwd, cwdcap);
    }
    if (ptr == NULL)
    {
      my_abort();
    }
    while ((ptr = getcwd(storcwd, storcwdcap)) == NULL && errno == ERANGE)
    {
      storcwdcap *= 2;
      storcwd = realloc(storcwd, storcwdcap);
    }
    if (ptr == NULL)
    {
      my_abort();
    }
    while ((ptr = getcwd(curcwd, curcwdcap)) == NULL && errno == ERANGE)
    {
      curcwdcap *= 2;
      curcwd = realloc(curcwd, curcwdcap);
    }
    if (ptr == NULL)
    {
      my_abort();
    }
    while ((ptr = getcwd(cwd_sameproj, cwd_sameprojcap)) == NULL && errno == ERANGE)
    {
      cwd_sameprojcap *= 2;
      cwd_sameproj = realloc(cwd_sameproj, cwd_sameprojcap);
    }
    if (ptr == NULL)
    {
      my_abort();
    }
    f = fopen("Stirfile", "r");
    if (f)
    {
      abce_init_opts(&abce, !my_is_running_on_valgrind());
#ifdef WITH_LUA
      abce_set_luaopen_caller(&abce, luaopen_stir);
#endif
      abce_inited = 1;
      abce.trap = stir_trap;
      abce.trap_baton = &stirmain;
      init_main_for_realpath(&stirmain, storcwd);
      stirmain.abce = &abce;
      stirmain.parsing = 1;
      stirmain.trial = 1;
      stirmain.freeform_token_seen = 1;
      stiryy_init(&stiryy, &stirmain, ".", ".", abce.dynscope, curcwd, "Stirfile", 1);
      stiryy.abort_early = 1; // for performance, just get toplevel indicator
      stiryydoparse(f, &stiryy);
      if (stiryy.indicator_seen)
      {
        last_was_toplevel = stiryy.was_toplevel;
      }
      fclose(f);
      stiryy_free(&stiryy);
      stiryy_main_free(&stirmain);
      abce_free(&abce);
      abce_inited = 0;
      yy_stored_lineno = -1;
      yy_stored_prefix = NULL;
    }
    for (;;)
    {
      struct stat sb;
      char *ptr;
      if (strcmp(curcwd, "/") == 0)
      {
        break;
      }
      if (chdir("..") != 0)
      {
        printf("stirmake: Can't change current directory to .. at %s\n", curcwd);
        break;
      }
      while ((ptr = getcwd(curcwd, curcwdcap)) == NULL && errno == ERANGE)
      {
        curcwdcap *= 2;
        curcwd = realloc(curcwd, curcwdcap);
      }
      if (ptr == NULL)
      {
        printf("stirmake: Can't get name of current directory\n");
        my_abort();
      }
      if (stat(".", &sb) != 0)
      {
        printf("stirmake: Can't stat current directory at %s\n", curcwd);
        break;
      }
      if ((sb.st_mode & S_IWOTH) && !unsafe && last_was_toplevel)
      {
#if 0
        if ((sb.st_mode & S_ISVTX) && access("Stirfile", R_OK) == 0)
        {
          if (stat("Stirfile", &sb) != 0 || sb.st_uid != getuid())
          {
            printf("stirmake: Encountered world-writable directory at %s, not considering it\n", curcwd);
            printf("stirmake: Hint: execute stirmake with -u to allow unsafe operation\n");
            break;
          }
        }
        else
#endif
        {
          printf("stirmake: Encountered world-writable directory at %s, not considering it\n", curcwd);
          printf("stirmake: Hint: execute stirmake with -u to allow unsafe operation\n");
          break;
        }
      }
      curupcnt++;
      f = fopen("Stirfile", "r");
      if (f)
      {
        int ret;
        last_was_toplevel = 1; // unless otherwise proven
        abce_init_opts(&abce, !my_is_running_on_valgrind());
#ifdef WITH_LUA
        abce_set_luaopen_caller(&abce, luaopen_stir);
#endif
        abce_inited = 1;
        abce.trap = stir_trap;
        abce.trap_baton = &stirmain;
        init_main_for_realpath(&stirmain, storcwd);
        stirmain.abce = &abce;
        stirmain.parsing = 1;
        stirmain.trial = 1;
        stirmain.freeform_token_seen = 1;
        stiryy_init(&stiryy, &stirmain, ".", ".", abce.dynscope, curcwd, "Stirfile", 1);
        ret = stiryydoparse(f, &stiryy);
        if (stiryy.indicator_seen)
        {
          last_was_toplevel = stiryy.was_toplevel;
        }
        fclose(f);
        if (ret == 0 && stirmain.subdirseen)
        {
          upcnt = curupcnt;
          if (curcwdcap > cwdcap)
          {
            cwdcap = curcwdcap;
            cwd = realloc(cwd, cwdcap);
          }
          if (snprintf(cwd, cwdcap, "%s", curcwd) >= (int)cwdcap)
          {
            printf("can't snprintf\n");
            my_abort();
          }
        }
        if (ret == 0 && stirmain.subdirseen_sameproject)
        {
          upcnt_sameproj = curupcnt;
          if (curcwdcap > cwd_sameprojcap)
          {
            cwd_sameprojcap = curcwdcap;
            cwd_sameproj = realloc(cwd_sameproj, cwd_sameprojcap);
          }
          if (snprintf(cwd_sameproj, cwd_sameprojcap, "%s", curcwd)
              >= (int)cwd_sameprojcap)
          {
            printf("can't snprintf\n");
            my_abort();
          }
        }
        stiryy_free(&stiryy);
        stiryy_main_free(&stirmain);
        abce_free(&abce);
        abce_inited = 0;
        yy_stored_lineno = -1;
        yy_stored_prefix = NULL;
      }
    }
    if (!silent)
    {
      printf("stirmake: Using directory %s\n", cwd);
      fflush(stdout);
    }
    if (chdir(cwd) != 0)
    {
      my_abort();
    }
  }

  linked_list_head_init(&tsdb.ll);
  load_db_part1();
  abce_init_opts(&abce, !my_is_running_on_valgrind());
#ifdef WITH_LUA
  abce_set_luaopen_caller(&abce, luaopen_stir);
#endif
  abce_inited = 1;
  abce.trap = stir_trap;
  abce.trap_baton = &stirmain;
  init_main_for_realpath(&stirmain, ".");
  stirmain.abce = &abce;
  stirmain.parsing = 1;
  stirmain.trial = 0;
  stirmain.freeform_token_seen = 0;
  stiryy_init(&stiryy, &stirmain, ".", ".", abce.dynscope, NULL, filename, 1);

  f = fopen(filename, "r");
  if (!f)
  {
    errxit("Stirfile not found");
    my_abort();
  }
  if (stiryydoparse(f, &stiryy) != 0)
  {
    errxit("Parsing failed");
  }
  stirmain.parsing = 0;
  fclose(f);
  yy_stored_lineno = -1;
  yy_stored_prefix = NULL;

  if (stirmain.expected_deps > 0)
  {
    expdeps = stirmain.expected_deps;
    //printf("Expdeps: %d\n", (int)expdeps);
  }
  if (stirmain.expected_total_targets > 0)
  {
    ruleid_by_tgt_size = stirmain.expected_total_targets;
  }
  if (stirmain.expected_strings > 0)
  {
    expstrings = stirmain.expected_strings;
  }
  if (stirmain.statcache_size > 0)
  {
    statcache_size = stirmain.statcache_size;
    if (statcache_size < STATHASH_SIZE_INIT)
    {
      statcache_size = STATHASH_SIZE_INIT;
    }
  }
  if (stirmain.accesscache_size > 0)
  {
    accesscache_size = stirmain.accesscache_size;
    if (accesscache_size < ACCESSHASH_SIZE_INIT)
    {
      accesscache_size = ACCESSHASH_SIZE_INIT;
    }
  }
  if (stirmain.stathash_size > 0)
  {
    stathash_size = stirmain.stathash_size;
  }
  if (stirmain.accesshash_size > 0)
  {
    accesshash_size = stirmain.accesshash_size;
  }
  if (stirmain.dbhash_size > 0)
  {
    db.sz = stirmain.dbhash_size;
  }
  if (stirmain.tsdbhash_size > 0)
  {
    tsdb.sz = stirmain.tsdbhash_size;
  }

  statcache_init();
  accesscache_init();

  if (out_sync == OUT_SYNC_NOT_SET_YET)
  {
    out_sync = OUT_SYNC_NONE;
  }
  ruleid_by_tgt_init();
  st_init();
  sttable = stir_do_mmap_madvise(st_cap*sizeof(*sttable));
  if (sttable == NULL)
  {
    errxit("Can't mmap sttable");
    exit(2);
  }
  load_db_part2();

  stack_conf();

  this_path = calc_forward_path(storcwd, upcnt);
  if (mode == MODE_ALL || mode == MODE_NONE)
  {
    fwd_path = ".";
  }
  else if (mode == MODE_THIS)
  {
    fwd_path = calc_forward_path(storcwd, upcnt);
    if (!silent)
    {
      printf("stirmake: Forward path: %s\n", fwd_path);
      fflush(stdout);
    }
  }
  else if (mode == MODE_PROJECT)
  {
    fwd_path = calc_forward_path(cwd_sameproj, upcnt - upcnt_sameproj);
    if (!silent)
    {
      printf("stirmake: Forward path: %s\n", fwd_path);
      fflush(stdout);
    }
  }
  else
  {
    my_abort();
  }
  if (pretend != NULL)
  {
    struct pretend *iter = pretend;
    size_t pathcap;
    char *pathbuf;
    while (iter != NULL)
    {
      if (iter->relative)
      {
        pathcap = strlen(this_path)+1+strlen(iter->fname)+1;
        pathbuf = malloc(pathcap);
        if (snprintf(pathbuf, pathcap, "%s/%s", this_path, iter->fname) >= (int)pathcap)
        {
          errxit("too long pathname to pretend: %s", iter->fname);
        }
        iter->fname = canon(pathbuf);
        free(pathbuf);
        iter->relative = 0;
      }
      else
      {
        pathcap = strlen(fwd_path)+1+strlen(iter->fname)+1;
        pathbuf = malloc(pathcap);
        if (snprintf(pathbuf, pathcap, "%s/%s", fwd_path, iter->fname) >= (int)pathcap)
        {
          errxit("too long pathname to pretend: %s", iter->fname);
        }
        iter->fname = canon(pathbuf);
        free(pathbuf);
      }
      iter = iter->next;
    }
  }
  for (i = 0; i < stirmain.rulesz; i++)
  {
    if (stirmain.rules[i].targetsz > 0) // FIXME chg to if (1)
    {
      if (stirmain.rules[i].deponly)
      {
        continue;
      }
      if (debug)
      {
        print_indent();
        printf("ADDING RULE\n");
      }
      if (stirmain.rules[i].ispat)
      {
        //char *prefix = stirmain.rules[i].prefix;
        size_t j, k;
        if (stirmain.rules[i].targetsz < 1)
        {
          my_abort();
        }
        for (j = 0; j < stirmain.rules[i].basesz; j++)
        {
          //char *base = stirmain.rules[i].bases[j].name;
          char *basenodir = stirmain.rules[i].bases[j].namenodir;
          char *tgt = stirmain.rules[i].targets[0].namenodir;
          char *suffix = stirmain.rules[i].targets[0].suffix;
          struct tgt *tgts;
          struct dep *deps;
          char *loc, *locp1;
          size_t meatsz;
          char *meat;
          size_t locp1sz;
          tgts = malloc(sizeof(*tgts)*stirmain.rules[i].targetsz);
          deps = malloc(sizeof(*deps)*stirmain.rules[i].depsz);
          if (suffix == NULL && (strcnt(tgt, '%') != 1 || !stirmain.rules[i].targets[0].percent_special))
          {
            errxit("Target %s must have exactly one %% sign", tgt);
            exit(2);
          }
          if (stirmain.rules[i].targets[0].percent_special && strcnt(tgt, '%') == 1)
          {
            loc = strchr(tgt, '%');
            locp1 = loc+1;
            locp1sz = strlen(locp1);
            if (memcmp(basenodir, tgt, (size_t)(loc-tgt)) != 0)
            {
              errxit("Target %s didn't match base %s", tgt, basenodir);
              exit(2);
            }
            if (memcmp(basenodir+strlen(basenodir)-locp1sz, locp1, locp1sz) != 0)
            {
              errxit("Target %s didn't match base %s", tgt, basenodir);
              exit(2);
            }
            meatsz = strlen(basenodir)-strlen(tgt)+1;
            meat = malloc(meatsz+1);
            memcpy(meat, basenodir+(loc-tgt), meatsz);
            meat[meatsz] = '\0';
            //tgts[0].name = base;
            tgts[0].namenodir = basenodir;
          }
          else if (suffix)
          {
            locp1 = suffix;
            locp1sz = strlen(locp1);
            if (memcmp(basenodir, tgt, strlen(tgt)) != 0)
            {
              errxit("Target %s%%%s didn't match base %s", tgt, suffix, basenodir);
              exit(2);
            }
            if (memcmp(basenodir+strlen(basenodir)-locp1sz, locp1, locp1sz) != 0)
            {
              errxit("Target %s%%%s didn't match base %s", tgt, suffix, basenodir);
              exit(2);
            }
            meatsz = strlen(basenodir)-strlen(tgt)-strlen(suffix);
            meat = malloc(meatsz+1);
            memcpy(meat, basenodir+strlen(tgt), meatsz);
            meat[meatsz] = '\0';
            //tgts[0].name = base;
            tgts[0].namenodir = basenodir;
          }
          else
          {
            errxit("Target %s must have exactly one %% sign", tgt);
            exit(2);
          }
          for (k = 1; k < stirmain.rules[i].targetsz; k++)
          {
            char *tgt = stirmain.rules[i].targets[k].namenodir;
            char *suffix = stirmain.rules[i].targets[k].suffix;
            size_t exptgtsz; // expanded target size
            char *exptgt;
            //char catbuf[1024];
            //char *namedir;
            if (stirmain.rules[i].targets[k].percent_special && strcnt(tgt, '%') == 1)
            {
              loc = strchr(tgt, '%');
              locp1 = loc+1;
              locp1sz = strlen(locp1);
              exptgtsz = strlen(tgt) - 1 + meatsz;
              exptgt = my_malloc_align(exptgtsz + 1, 1);
              memcpy(exptgt, tgt, (size_t)(loc-tgt));
              memcpy(&exptgt[(size_t)(loc-tgt)], meat, meatsz);
              memcpy(&exptgt[(size_t)(loc-tgt)+meatsz], locp1, locp1sz);
              exptgt[(size_t)(loc-tgt)+meatsz+locp1sz] = '\0';
              if ((size_t)(loc-tgt)+meatsz+locp1sz != exptgtsz)
              {
                my_abort();
              }
              //namedir = pathcat2_buf(prefix, exptgt, catbuf, sizeof(catbuf));
              //tgts[k].name = canon(namedir);
              tgts[k].namenodir = exptgt;
#if 0
              if (namedir != catbuf)
              {
                free(namedir);
              }
#endif
            }
            else if (suffix)
            {
              size_t strlen_tgt = strlen(tgt);
              locp1 = suffix;
              locp1sz = strlen(locp1);
              exptgtsz = strlen_tgt + strlen(suffix) + meatsz;
              exptgt = my_malloc_align(exptgtsz + 1, 1);
              memcpy(exptgt, tgt, strlen_tgt);
              memcpy(&exptgt[strlen_tgt], meat, meatsz);
              memcpy(&exptgt[strlen_tgt+meatsz], locp1, locp1sz);
              exptgt[strlen_tgt+meatsz+locp1sz] = '\0';
              if (strlen_tgt+meatsz+locp1sz != exptgtsz)
              {
                my_abort();
              }
              //namedir = pathcat2_buf(prefix, exptgt, catbuf, sizeof(catbuf));
              //tgts[k].name = canon(namedir);
              tgts[k].namenodir = exptgt;
#if 0
              if (namedir != catbuf)
              {
                free(namedir);
              }
#endif
            }
            else
            {
              errxit("Target %s must have exactly one %% sign", tgt);
              exit(2);
            }
          }
          for (k = 0; k < stirmain.rules[i].depsz; k++)
          {
            //char catbuf[1024];
            char *dep = stirmain.rules[i].deps[k].namenodir;
            char *suffix = stirmain.rules[i].deps[k].suffix;
            //char *dep = stirmain.rules[i].deps[k].name;
            size_t expdepsz; // expanded target size
            char *expdep;
            //char *namedir;
            if (suffix)
            {
              size_t strlen_dep = strlen(dep);
              locp1 = suffix;
              locp1sz = strlen(locp1);
              expdepsz = strlen_dep + locp1sz + meatsz;
              expdep = my_malloc_align(expdepsz + 1, 1);
              memcpy(expdep, dep, strlen_dep);
              memcpy(&expdep[strlen_dep], meat, meatsz);
              memcpy(&expdep[strlen_dep+meatsz], locp1, locp1sz);
              expdep[strlen_dep+meatsz+locp1sz] = '\0';
              if (strlen_dep+meatsz+locp1sz != expdepsz)
              {
                my_abort();
              }
              //namedir = pathcat2_buf(prefix, expdep, catbuf, sizeof(catbuf));
              //deps[k].name = canon(namedir);
              deps[k].namenodir = expdep;
              deps[k].rec = stirmain.rules[i].deps[k].rec;
              deps[k].orderonly = stirmain.rules[i].deps[k].orderonly;
              deps[k].wait = stirmain.rules[i].deps[k].wait;
#if 0
              if (namedir != catbuf)
              {
                free(namedir);
              }
#endif
            }
            else
            {
              loc = NULL;
              if (stirmain.rules[i].deps[k].percent_special)
              {
                if (strcnt(dep, '%') > 1)
                {
                  errxit("Dep %s must have exactly zero or one %% signs", dep);
                  exit(2);
                }
                loc = strchr(dep, '%');
              }
              if (loc == NULL)
              {
                //char *prefixdep;
#if 0
                prefixdep = pathcat2_buf(prefix, dep, catbuf, sizeof(catbuf));
                //deps[k].name = canon(prefixdep);
                if (prefixdep != catbuf)
                {
                  free(prefixdep);
                }
#endif
                deps[k].namenodir = stirmain.rules[i].deps[k].namenodir;
                deps[k].rec = stirmain.rules[i].deps[k].rec;
                deps[k].orderonly = stirmain.rules[i].deps[k].orderonly;
                deps[k].wait = stirmain.rules[i].deps[k].wait;
                continue;
              }
              locp1 = loc+1;
              locp1sz = strlen(locp1);
              expdepsz = strlen(dep) - 1 + meatsz;
              expdep = my_malloc_align(expdepsz + 1, 1);
              memcpy(expdep, dep, (size_t)(loc-dep));
              memcpy(&expdep[loc-dep], meat, meatsz);
              memcpy(&expdep[(size_t)(loc-dep)+meatsz], locp1, locp1sz);
              expdep[(size_t)(loc-dep)+meatsz+locp1sz] = '\0';
              if ((size_t)(loc-dep)+meatsz+locp1sz != expdepsz)
              {
                my_abort();
              }
              //namedir = pathcat2_buf(prefix, expdep, catbuf, sizeof(catbuf));
              //deps[k].name = canon(namedir);
              deps[k].namenodir = expdep;
              deps[k].rec = stirmain.rules[i].deps[k].rec;
              deps[k].orderonly = stirmain.rules[i].deps[k].orderonly;
              deps[k].wait = stirmain.rules[i].deps[k].wait;
#if 0
              if (namedir != catbuf)
              {
                free(namedir);
              }
#endif
            }
          }
          if (   stirmain.rules[i].iscleanhook
              || stirmain.rules[i].isdistcleanhook
              || stirmain.rules[i].isbothcleanhook)
          {
            my_abort();
          }
          add_rule(tgts, stirmain.rules[i].targetsz,
                   deps, stirmain.rules[i].depsz,
                   &stirmain.rules[i].shells, //stirmain.rules[i].shellsz,
                   stirmain.rules[i].phony, stirmain.rules[i].rectgt,
                   stirmain.rules[i].detouch, stirmain.rules[i].maybe,
                   stirmain.rules[i].dist,
                   stirmain.rules[i].iscleanhook, stirmain.rules[i].isdistcleanhook,
                   stirmain.rules[i].isbothcleanhook,
                   stirmain.rules[i].prefix, stirmain.rules[i].scopeidx,
                   stirmain.rules[i].lineno,
                   meat);
          free(tgts);
          free(deps);
          free(meat);
        }
        continue;
      }
      add_rule(stirmain.rules[i].targets, stirmain.rules[i].targetsz,
               stirmain.rules[i].deps, stirmain.rules[i].depsz,
               &stirmain.rules[i].shells, //stirmain.rules[i].shellsz,
               stirmain.rules[i].phony, stirmain.rules[i].rectgt,
               stirmain.rules[i].detouch, stirmain.rules[i].maybe,
               stirmain.rules[i].dist,
               stirmain.rules[i].iscleanhook, stirmain.rules[i].isdistcleanhook,
               stirmain.rules[i].isbothcleanhook,
               stirmain.rules[i].prefix, stirmain.rules[i].scopeidx,
               stirmain.rules[i].lineno,
               NULL /*meat*/);
      if (   (!ruleid_first_set)
          && (/* strcmp(fwd_path, ".") == 0 || */
              strcmp(fwd_path, stirmain.rules[i].prefix) == 0))
      {
        if (rules_size == 0)
        {
          my_abort();
        }
        if (stirmain.rules[i].iscleanhook)
        {
          continue;
        }
        if (stirmain.rules[i].isdistcleanhook)
        {
          continue;
        }
        if (stirmain.rules[i].isbothcleanhook)
        {
          continue;
        }
        ruleid_first = (int)rules_size - 1;
        ruleid_first_set = 1;
      }
    }
  }
  for (i = 0; i < stirmain.rulesz; i++)
  {
    if (stirmain.rules[i].targetsz > 0) // FIXME chg to if (1)
    {
      if (!stirmain.rules[i].deponly)
      {
        continue;
      }
      if (debug)
      {
        print_indent();
        printf("ADDING DEP\n");
      }
      add_dep_from_rules(stirmain.rules[i].targets, stirmain.rules[i].targetsz,
                         stirmain.rules[i].deps, stirmain.rules[i].depsz, 0,
                         stirmain.rules[i].prefix);
    }
  }
  if (!ruleid_first_set && optind == argc)
  {
    errxit("no applicable rules");
  }
  // cdepincludes is going to take a lot of memory, so free some
  // excess memory here
  stiryy_main_free_rules(&stirmain);
  for (i = 0; i < stiryy.main->cdepincludesz; i++)
  {
    struct incyyfnstruct fnstruct = {
      .auto_phony = !!stiryy.main->cdepincludes[i].auto_phony,
    };
    struct incyy incyy = {
      .prefix = stiryy.main->cdepincludes[i].prefix,
      .auto_target = !!stiryy.main->cdepincludes[i].auto_target,
      .fnamenodir = stiryy.main->cdepincludes[i].name,
    };
    size_t j;
    char canbuf[1024];
    size_t fnamesz =
      strlen(incyy.prefix) + strlen(stiryy.main->cdepincludes[i].name) + 2;
    char *fname = malloc(fnamesz);
    char *fnamecanon;
    incyy.prefixlen = strlen(incyy.prefix);
    incyy.fnamenodirlen = strlen(incyy.fnamenodir);
    mysize_t stidx;
    if (snprintf(fname, fnamesz, "%s/%s", incyy.prefix, stiryy.main->cdepincludes[i].name) >= (int)fnamesz)
    {
      printf("24.5\n");
      my_abort();
    }
    if (debug)
    {
      print_indent();
      printf("reading cdepincludes from %s\n", fname);
    }
    fnamecanon = canon_buf(fname, canbuf, sizeof(canbuf));
    stidx = stringtab_add(fnamecanon);
    if (fnamecanon != canbuf)
    {
      free(fnamecanon);
    }
    sttable[stidx].is_cdepwatch = 1;
    //printf("Is_cdepwatch %s\n", sttable[stidx].s);
    f = fopen(fname, "r");
    if (!f)
    {
      if (stiryy.main->cdepincludes[i].ignore)
      {
        free(fname);
        continue;
      }
      errxit("Can't read cdepincludes from %s", fname);
      my_abort();
    }
    if (incyymineparse(f, &incyy, incyy_add_rule, incyy_add_tgt, incyy_add_dep, &fnstruct))
    {
      errxit("Invalid cdepincludes format in %s", fname);
      my_abort();
    }
    free(fname);
    //for (auto it = incyy.rules; it != incyy.rules + incyy.rulesz; it++)
    for (j = 0; j < incyy.rulesz; j++)
    {
      size_t k;
      //std::vector<std::string> tgt;
      //std::vector<std::string> dep;
      //std::copy(it->deps, it->deps+it->depsz, std::back_inserter(dep));
      //std::copy(it->targets, it->targets+it->targetsz, std::back_inserter(tgt));
      if (debug)
      {
        print_indent();
        printf("Adding dep\n");
        for (k = 0; k < incyy.rules[j].targetsz; k++)
        {
          printf("  target: %s\n", incyy.rules[j].targets[k]);
        }
        for (k = 0; k < incyy.rules[j].depsz; k++)
        {
          printf("  dep: %s\n", incyy.rules[j].deps[k]);
        }
      }
      add_dep(incyy.rules[j].targets, incyy.rules[j].targetsz,
              incyy.rules[j].deps, incyy.rules[j].depsz,
              0, !!stiryy.main->cdepincludes[i].auto_phony,
	      !!stiryy.main->cdepincludes[i].auto_phony);
      //add_dep(tgt, dep, 0);
    }
    fclose(f);
    incyy_free(&incyy);
  }
  stiryy_free(&stiryy);

  //add_dep(v_l3e, v_l1g, 0); // offending rule

#if 0
  unsigned char *no_cycles = better_cycle_detect(0); // FIXME 0 incorrect!
#endif

  // Delete unreachable rules from ruleids_by_dep
#if 0
  struct linked_list_node *node, *tmp;
  LINKED_LIST_FOR_EACH_SAFE(node, tmp, &ruleids_by_dep_list)
  {
    struct ruleid_by_dep_entry *entry =
      ABCE_CONTAINER_OF(node, struct ruleid_by_dep_entry, llnode);
    int bytgt = get_ruleid_by_tgt(entry->depidx);
    if (bytgt < 0)
    {
      continue;
      printf("26\n");
      my_abort();
    }
    if (no_cycles[bytgt])
    {
      struct linked_list_node *node2, *tmp2;
      LINKED_LIST_FOR_EACH_SAFE(node2, tmp2, &entry->one_ruleid_by_deplist)
      {
        struct one_ruleid_by_dep_entry *one =
          ABCE_CONTAINER_OF(node2, struct one_ruleid_by_dep_entry, llnode);
        if (no_cycles[one->ruleid])
        {
          continue;
        }
        uint32_t hashval;
        size_t hashloc;
        hashval = abce_nonmurmur32(one->ruleid);
        hashloc = hashval % (sizeof(entry->one_ruleid_by_dep)/sizeof(*entry->one_ruleid_by_dep));
        abce_rb_tree_nocmp_delete(&entry->one_ruleid_by_dep[hashloc], &one->node);
        linked_list_delete(&one->llnode);
        my_free(one);
      }
    }
    else
    {
      uint32_t hashval;
      size_t hashloc;
      hashval = abce_nonmurmur32(entry->depidx);
      hashloc = hashval % (sizeof(ruleids_by_dep)/sizeof(*ruleids_by_dep));
      abce_rb_tree_nocmp_delete(&ruleids_by_dep[hashloc], &entry->node);
      linked_list_delete(&entry->llnode);
      my_free(entry);
    }
  }
  LINKED_LIST_FOR_EACH_SAFE(node, tmp, &ruleid_by_tgt_list)
  {
    struct ruleid_by_tgt_entry *entry =
      ABCE_CONTAINER_OF(node, struct ruleid_by_tgt_entry, llnode);
    if (no_cycles[entry->ruleid])
    {
      continue;
    }
    uint32_t hashval;
    size_t hashloc;
    hashval = abce_nonmurmur32(entry->tgtidx);
    hashloc = hashval % (sizeof(ruleid_by_tgt)/sizeof(*ruleid_by_tgt));
    abce_rb_tree_nocmp_delete(&ruleid_by_tgt[hashloc], &entry->node);
    linked_list_delete(&entry->llnode);
    my_free(entry);
  }
#endif

  if (pipe(self_pipe_fd) != 0)
  {
    printf("27\n");
    my_abort();
  }
  set_nonblock(self_pipe_fd[0]);
  set_nonblock(self_pipe_fd[1]);

  if (process_jobserver(jobserver_fd) != 0)
  {
    create_pipe(jobcnt);
  }

  struct sigaction sa;
  sigemptyset(&sa.sa_mask);
#ifdef SA_RESTART
  sa.sa_flags = SA_RESTART;
#else
  sa.sa_flags = 0;
#endif
  sa.sa_handler = sigchld_handler;
  sigaction(SIGCHLD, &sa, NULL);

  struct sigaction salrm;
  sigemptyset(&salrm.sa_mask);
#ifdef SA_RESTART
  salrm.sa_flags = SA_RESTART;
#else
  salrm.sa_flags = 0;
#endif
  salrm.sa_handler = sigalrm_handler;
  sigaction(SIGALRM, &salrm, NULL);

  struct sigaction saint, saterm, sahup;
  sigemptyset(&saint.sa_mask);
#ifdef SA_RESTART
  saint.sa_flags = SA_RESTART;
#else
  saint.sa_flags = 0;
#endif
  saint.sa_handler = sigint_handler;
  sigaction(SIGINT, &saint, NULL);
  sigemptyset(&saterm.sa_mask);
#ifdef SA_RESTART
  saterm.sa_flags = SA_RESTART;
#else
  saterm.sa_flags = 0;
#endif
  saterm.sa_handler = sigterm_handler;
  sigaction(SIGTERM, &saterm, NULL);
  sigemptyset(&sahup.sa_mask);
#ifdef SA_RESTART
  sahup.sa_flags = SA_RESTART;
#else
  sahup.sa_flags = 0;
#endif
  sahup.sa_handler = sighup_handler;
  sigaction(SIGHUP, &sahup, NULL);

  //consider(0);

/*
  while (children < limit && !ruleids_to_run.empty())
  {
    std::cout << "forking1 child" << std::endl;
    fork_child(ruleids_to_run.back());
    ruleids_to_run.pop_back();
  }
*/

  if (optind == argc)
  {
    //free(better_cycle_detect(ruleid_first, 1));
    if (clean || cleanbinaries)
    {
      do_clean(fwd_path, clean, cleanbinaries);
      clean_jobserver();
      if (merge_db())
      {
        fprintf(stderr, "stirmake: Cannot write stirmake database\n");
        exit(2);
      }
      exit(0); // don't process first rule
    }
    ruleremain_add(rules[ruleid_first]);
  }
  else if (mode == MODE_ALL || mode == MODE_NONE)
  {
    int i;
    for (i = optind; i < argc; i++)
    {
      mysize_t stidx = stringtab_add(argv[i]);
      int ruleid = get_ruleid_by_tgt(stidx);
      if (ruleid < 0)
      {
        errxit("rule '%s' not found", argv[i]);
      }
      //free(better_cycle_detect(ruleid, 1));
      //ruleremain_add(rules[ruleid]); // later!
    }
    if (clean || cleanbinaries)
    {
      do_clean(".", clean, cleanbinaries);
    }
    for (i = optind; i < argc; i++)
    {
      mysize_t stidx = stringtab_add(argv[i]);
      int ruleid = get_ruleid_by_tgt(stidx);
      if (ruleid < 0)
      {
        errxit("rule '%s' not found", argv[i]);
      }
      //free(better_cycle_detect(ruleid, 0));
      ruleremain_add(rules[ruleid]);
    }
  }
  else if (mode == MODE_THIS)
  {
    int i;
    for (i = optind; i < argc; i++)
    {
      size_t bufsz = strlen(fwd_path) + strlen(argv[i]) + 2;
      char *buf = malloc(bufsz);
      char *can;
      mysize_t stidx;
      int ruleid;
      snprintf(buf, bufsz, "%s/%s", fwd_path, argv[i]);
      can = canon(buf);
      free(buf);
      stidx = stringtab_add(can);
      ruleid = get_ruleid_by_tgt(stidx);
      if (ruleid < 0)
      {
        errxit("rule '%s' not found", argv[i]);
      }
      //free(better_cycle_detect(ruleid, 1));
      //ruleremain_add(rules[ruleid]); // later!
      free(can);
    }
    if (clean || cleanbinaries)
    {
      do_clean(fwd_path, clean, cleanbinaries);
    }
    for (i = optind; i < argc; i++)
    {
      size_t bufsz = strlen(fwd_path) + strlen(argv[i]) + 2;
      char *buf = malloc(bufsz);
      char *can;
      mysize_t stidx;
      int ruleid;
      snprintf(buf, bufsz, "%s/%s", fwd_path, argv[i]);
      can = canon(buf);
      free(buf);
      stidx = stringtab_add(can);
      ruleid = get_ruleid_by_tgt(stidx);
      if (ruleid < 0)
      {
        errxit("rule '%s' not found", argv[i]);
      }
      //free(better_cycle_detect(ruleid, 0));
      ruleremain_add(rules[ruleid]);
      free(can);
    }
  }
  else
  {
    int i;
    for (i = optind; i < argc; i++)
    {
      size_t bufsz = strlen(fwd_path) + strlen(argv[i]) + 2;
      char *buf = malloc(bufsz);
      char *can;
      mysize_t stidx;
      int ruleid;
      snprintf(buf, bufsz, "%s/%s", fwd_path, argv[i]);
      can = canon(buf);
      free(buf);
      stidx = stringtab_add(can);
      ruleid = get_ruleid_by_tgt(stidx);
      if (ruleid < 0)
      {
        errxit("rule '%s' not found", argv[i]);
      }
      //free(better_cycle_detect(ruleid, 1));
      //ruleremain_add(rules[ruleid]); // later!
      free(can);
    }
    if (clean || cleanbinaries)
    {
      do_clean(fwd_path, clean, cleanbinaries);
    }
    for (i = optind; i < argc; i++)
    {
      size_t bufsz = strlen(fwd_path) + strlen(argv[i]) + 2;
      char *buf = malloc(bufsz);
      char *can;
      mysize_t stidx;
      int ruleid;
      snprintf(buf, bufsz, "%s/%s", fwd_path, argv[i]);
      can = canon(buf);
      free(buf);
      stidx = stringtab_add(can);
      ruleid = get_ruleid_by_tgt(stidx);
      if (ruleid < 0)
      {
        errxit("rule '%s' not found", argv[i]);
      }
      //free(better_cycle_detect(ruleid, 0));
      ruleremain_add(rules[ruleid]);
      free(can);
    }
  }

  if (!deps_remain_calculated)
  {
    process_additional_deps_2(abce.dynscope.u.area->u.sc.locidx);
    for (i = 0; i < rules_size; i++)
    {
      calc_deps_remain(rules[i]);
    }
    deps_remain_calculated = 1;
  }

  LINKED_LIST_FOR_EACH(node, &rules_remain_list)
  {
    int ruleid = ABCE_CONTAINER_OF(node, struct rule, remainllnode)->ruleid;
    free(better_cycle_detect(ruleid, 1));
  }

  process_orders(&stirmain);

  if (stirmain.ordersz > 0)
  {
    LINKED_LIST_FOR_EACH(node, &rules_remain_list)
    {
      int ruleid = ABCE_CONTAINER_OF(node, struct rule, remainllnode)->ruleid;
      free(better_cycle_detect(ruleid, 0));
    }
  }

  ins_ruleid_by_dep_later();

  run_loop(1);

  if (debug)
  {
    printf("\n");
    printf("Memory use statistics:\n");
    printf("  accesscache: %zu %zu\n", (size_t)accesshashentriescnt, (size_t)accesshashentriescnt*sizeof(struct accesshashentry));
    printf("  statcache: %zu %zu\n", (size_t)stathashentriescnt, (size_t)stathashentriescnt*sizeof(struct stathashentry));
    printf("  my_malloc all: %zu %zu (+ wasted %zu)\n", (size_t)mymemcnt, (size_t)mymem, (size_t)mymemwaste);
    printf("  abce arena all: %zu \n", (size_t)abce_jmgetarenabytes());
    printf("  abce bytecode all: %zu \n", (size_t)abce.bytecodecap);
    printf("  abce object cache all: %zu \n", (size_t)abce.cachecap*sizeof(*abce.cachebase));
    printf("  abce GC block all: %zu \n", (size_t)abce.gcblockcap*sizeof(*abce.gcblockbase));
    printf("  dbe: %zu %zu\n", (size_t)dbecnt, dbecnt*(size_t)sizeof(struct dbe));
    printf("  tsdbe: %zu %zu\n", (size_t)tsdbecnt, tsdbecnt*(size_t)sizeof(struct tsdbe));
    printf("  argmem: %zu %zu\n", (size_t)argmemcnt, (size_t)argmem);
    printf("  stringtab: %zu %zu + %zu + %zu + %zu\n", (size_t)stringtab_cnt, (size_t)(stringtab_cnt*sizeof(struct stringtabentry)), (size_t)stringtab_bytes, (size_t)(st_cap*sizeof(*sttable)), (size_t)(expstrings*sizeof(*st)));
    printf("  ruleid_by_tgt_entry: %zu %zu\n", (size_t)ruleid_by_tgt_entry_cnt, (size_t)(ruleid_by_tgt_entry_cnt*sizeof(struct ruleid_by_tgt_entry)));
    printf("  tgt: %zu %zu\n", (size_t)tgt_cnt, (size_t)(tgt_cnt*sizeof(struct tgt)));
    printf("  stirdep: %zu %zu\n", (size_t)stirdep_cnt, (size_t)(stirdep_cnt*sizeof(struct stirdep)));
    printf("  dep_remain: %zu %zu\n", (size_t)dep_remain_cnt, (size_t)(dep_remain_cnt*(sizeof(struct dep_remain) + (debug?sizeof(struct linked_list_node):0))));
    printf("  ruleid_by_dep_entry_block: %zu %zu\n", (size_t)ruleid_by_dep_entry_block_cnt, (size_t)(ruleid_by_dep_entry_block_cnt*sizeof(struct ruleid_by_dep_entry_block)));
    printf("  ruleid_by_dep_entry_block_later: %zu %zu\n", (size_t)ruleid_by_dep_entry_block_later_cnt, (size_t)(ruleid_by_dep_entry_block_later_cnt*sizeof(struct ruleid_by_dep_entry_block_later)));
    printf("  add_dep_entry_block: %zu %zu\n", (size_t)add_dep_entry_block_cnt, (size_t)(add_dep_entry_block_cnt*sizeof(struct add_dep_entry_block)));
    printf("  rule: %zu %zu\n", (size_t)rule_cnt, (size_t)rule_cnt*rulesz());
    printf("  ruleid_by_pid: %zu %zu\n", (size_t)ruleid_by_pid_cnt, (size_t)(ruleid_by_pid_cnt*sizeof(struct ruleid_by_pid)));
    printf("  syncbuf cache: %zu %zu\n", (size_t)cur_syncbufs_cached, (size_t)(cur_syncbufs_cached*sizeof(struct syncbufpage)));
  }
  if (merge_db())
  {
    fprintf(stderr, "stirmake: Cannot write stirmake database\n");
    exit(2);
  }
  clean_jobserver();
#if 0
  free(dupargv0);
  stiryy_main_free(&stirmain);
  abce_free(&abce);
  free(rules);
  rules = NULL;
#endif
  return 0;
}
