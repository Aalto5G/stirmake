#if defined(__sun) && defined(__SVR4)
#define __EXTENSIONS__
#endif
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <libgen.h>
#include <errno.h>
#include <unistd.h>
#include "stiryy.h"
#include "yyutils.h"
#include "stirutils.h"
#include "pathmax.h"
#include "git.h"
#include "mymalloc.h"

typedef void *yyscan_t;
extern int stiryyparse(yyscan_t scanner, struct stiryy *stiryy);
extern int stiryylex_init(yyscan_t *scanner);
extern void stiryyset_in(FILE *in_str, yyscan_t yyscanner);
extern void stiryyset_extra (unsigned int user_defined, yyscan_t yyscanner);
extern int stiryylex_destroy(yyscan_t yyscanner);

static inline int hexdig(const char ch)
{
  if (ch >= '0' && ch <= '9')
  {
    return ch-'0';
  }
  if (ch == 'A' || ch == 'a') return 10;
  if (ch == 'B' || ch == 'b') return 11;
  if (ch == 'C' || ch == 'c') return 12;
  if (ch == 'D' || ch == 'd') return 13;
  if (ch == 'E' || ch == 'e') return 14;
  if (ch == 'F' || ch == 'f') return 15;
  my_abort();
  return -1;
}

int gitshas_has(const char *needle, size_t needle_len)
{
  size_t i;
  char rawbuf[20];
  if (strlen(needle) != needle_len)
  {
    return 0;
  }
  if (needle_len != 40)
  {
    return 0;
  }
  if (strspn(needle, "0123456789ABCDEFabcdef") != 40)
  {
    return 0;
  }
  for (i = 0; i < 20; i++)
  {
    rawbuf[i] = (hexdig(needle[2*i+0])<<4);
    rawbuf[i] |= hexdig(needle[2*i+1]);
  }
  for (i = 0; i < sizeof(gitshas)/sizeof(*gitshas); i++)
  {
    if (memcmp(rawbuf, gitshas[i], 20) == 0)
    {
      return 1;
    }
  }
  return 0;
}

char headbuf[41];

const char *gitversions_head(void)
{
  snprintf(headbuf, sizeof(headbuf),
    "%.2x%.2x%.2x%.2x%.2x"
    "%.2x%.2x%.2x%.2x%.2x"
    "%.2x%.2x%.2x%.2x%.2x"
    "%.2x%.2x%.2x%.2x%.2x",
    (uint8_t)gitshas[0][0],
    (uint8_t)gitshas[0][1],
    (uint8_t)gitshas[0][2],
    (uint8_t)gitshas[0][3],
    (uint8_t)gitshas[0][4],
    (uint8_t)gitshas[0][5],
    (uint8_t)gitshas[0][6],
    (uint8_t)gitshas[0][7],
    (uint8_t)gitshas[0][8],
    (uint8_t)gitshas[0][9],
    (uint8_t)gitshas[0][10],
    (uint8_t)gitshas[0][11],
    (uint8_t)gitshas[0][12],
    (uint8_t)gitshas[0][13],
    (uint8_t)gitshas[0][14],
    (uint8_t)gitshas[0][15],
    (uint8_t)gitshas[0][16],
    (uint8_t)gitshas[0][17],
    (uint8_t)gitshas[0][18],
    (uint8_t)gitshas[0][19]);
  return headbuf;
}
const char *gitversion_get(void)
{
  return gitversion;
}

void gitversions(char *argv0)
{
  char locbuf[41];
  size_t i;
  for (i = 0; i < sizeof(gitshas)/sizeof(*gitshas); i++)
  {
    snprintf(locbuf, sizeof(locbuf),
      "%.2x%.2x%.2x%.2x%.2x"
      "%.2x%.2x%.2x%.2x%.2x"
      "%.2x%.2x%.2x%.2x%.2x"
      "%.2x%.2x%.2x%.2x%.2x",
      (uint8_t)gitshas[i][0],
      (uint8_t)gitshas[i][1],
      (uint8_t)gitshas[i][2],
      (uint8_t)gitshas[i][3],
      (uint8_t)gitshas[i][4],
      (uint8_t)gitshas[i][5],
      (uint8_t)gitshas[i][6],
      (uint8_t)gitshas[i][7],
      (uint8_t)gitshas[i][8],
      (uint8_t)gitshas[i][9],
      (uint8_t)gitshas[i][10],
      (uint8_t)gitshas[i][11],
      (uint8_t)gitshas[i][12],
      (uint8_t)gitshas[i][13],
      (uint8_t)gitshas[i][14],
      (uint8_t)gitshas[i][15],
      (uint8_t)gitshas[i][16],
      (uint8_t)gitshas[i][17],
      (uint8_t)gitshas[i][18],
      (uint8_t)gitshas[i][19]);
    printf("%s\n", locbuf);
  }
  exit(0);
}

int stiryydoparse(FILE *filein, struct stiryy *stiryy)
{
  yyscan_t scanner;
  stiryylex_init(&scanner);
  // 1 == in environment where shell commands can occur
  // 2 == last newline was line continuation or no newline seen yet
  stiryyset_extra(2, scanner);
  stiryyset_in(filein, scanner);
  if (stiryyparse(scanner, stiryy) != 0)
  {
    stiryylex_destroy(scanner);
    return -EBADMSG;
  }
  stiryylex_destroy(scanner);
  if (!feof(filein))
  {
    fprintf(stderr, "stirmake: Additional data at end of Stirfile.\n");
    return -EBADMSG;
  }
  return 0;
}

#if !STIR_NO_MEMPARSE
void stiryydomemparse(char *filedata, size_t filesize, struct stiryy *stiryy)
{
  FILE *myfile;
  myfile = fmemopen(filedata, filesize, "r");
  if (myfile == NULL)
  {
    fprintf(stderr, "can't open memory file\n");
    exit(2);
  }
  stiryydoparse(myfile, stiryy);
  if (fclose(myfile) != 0)
  {
    fprintf(stderr, "can't close memory file\n");
    exit(2);
  }
}
#endif

static void *memdup(const void *mem, size_t sz)
{
  void *result;
  result = malloc(sz);
  if (result == NULL)
  {
    return result;
  }
  memcpy(result, mem, sz);
  return result;
}

struct escaped_string yy_escape_string(char *orig, char **strendptr)
{
  char *buf = NULL;
  char *result = NULL;
  struct escaped_string resultstruct;
  size_t j = 0;
  size_t capacity = 0;
  size_t i = 1;
  if (strendptr)
  {
    *strendptr = NULL;
  }
  while (orig[i] != '"' && orig[i])
  {
    //if (j+2 >= capacity)
    if (j+7 >= capacity)
    {
      char *buf2;
      capacity = 2*capacity+10;
      if (capacity < 32)
      {
        capacity = 32; // initial size
      }
      buf2 = realloc(buf, capacity);
      if (buf2 == NULL)
      {
        free(buf);
        resultstruct.str = NULL;
        return resultstruct;
      }
      buf = buf2;
    }
    if (orig[i] != '\\')
    {
      buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '"') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '"') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '"') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '"') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '"') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '"') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '"') buf[j++] = orig[i++];
    }
    else if (orig[i+1] == 'x')
    {
      char hexbuf[3] = {0};
      char *endptr;
      hexbuf[0] = orig[i+2];
      hexbuf[1] = orig[i+3];
      buf[j++] = strtol(hexbuf, &endptr, 16);
      if (strlen(hexbuf) != 2 || *endptr != '\0')
      {
        fprintf(stderr, "Invalid string unicode escape: \\x%s\n", hexbuf);
        exit(2);
      }
      i += 4;
    }
    else if (orig[i+1] == 'u')
    {
      char hexbuf[5] = {0};
      uint16_t unicode;
      char *endptr;
      hexbuf[0] = orig[i+2];
      hexbuf[1] = orig[i+3];
      hexbuf[2] = orig[i+4];
      hexbuf[3] = orig[i+5];
      unicode = strtol(hexbuf, &endptr, 16);
      if (strlen(hexbuf) != 4 || *endptr != '\0')
      {
        fprintf(stderr, "Invalid string unicode escape: \\u%s\n", hexbuf);
        exit(2);
      }
      if (unicode <= 0x7F)
      {
        buf[j++] = (char)(uint8_t)unicode;
        i += 6;
        continue;
      }
      if (unicode <= 0x7FF)
      {
        buf[j++] = (char)(uint8_t)(0xc0|(unicode>>6));
        buf[j++] = (char)(uint8_t)(0x80|(unicode&0x3f));
        i += 6;
        continue;
      }
      if (unicode <= 0xFFFF)
      {
        buf[j++] = (char)(uint8_t)(0xe0|(unicode>>12));
        buf[j++] = (char)(uint8_t)(0x80|((unicode>>6)&0x3f));
        buf[j++] = (char)(uint8_t)(0x80|(unicode&0x3f));
        i += 6;
        continue;
      }
      abort();
    }
    else if (orig[i+1] == 't')
    {
      buf[j++] = '\t';
      i += 2;
    }
    else if (orig[i+1] == 'r')
    {
      buf[j++] = '\r';
      i += 2;
    }
    else if (orig[i+1] == 'n')
    {
      buf[j++] = '\n';
      i += 2;
    }
    else
    {
      buf[j++] = orig[i+1];
      i += 2;
    }
  }
  if (j >= capacity)
  {
    char *buf2;
    capacity = 2*capacity+10;
    buf2 = realloc(buf, capacity);
    if (buf2 == NULL)
    {
      free(buf);
      resultstruct.str = NULL;
      return resultstruct;
    }
    buf = buf2;
  }
  if (!orig[i])
  {
    free(buf);
    resultstruct.str = NULL;
    return resultstruct;
  }
  resultstruct.sz = j;
  buf[j++] = '\0';
  result = memdup(buf, j);
  resultstruct.str = result;
  free(buf);
  if (strendptr)
  {
    *strendptr = &orig[i+1];
  }
  return resultstruct;
}

struct escaped_string yy_escape_string_single(char *orig, char **strendptr)
{
  char *buf = NULL;
  char *result = NULL;
  struct escaped_string resultstruct;
  size_t j = 0;
  size_t capacity = 0;
  size_t i = 1;
  if (strendptr)
  {
    *strendptr = NULL;
  }
  while (orig[i] != '\'' && orig[i])
  {
    //if (j+2 >= capacity)
    if (j+7 >= capacity)
    {
      char *buf2;
      capacity = 2*capacity+10;
      if (capacity < 32)
      {
        capacity = 32; // initial size
      }
      buf2 = realloc(buf, capacity);
      if (buf2 == NULL)
      {
        free(buf);
        resultstruct.str = NULL;
        return resultstruct;
      }
      buf = buf2;
    }
    if (orig[i] != '\\')
    {
      buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '\'') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '\'') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '\'') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '\'') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '\'') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '\'') buf[j++] = orig[i++];
      if (orig[i] != '\\' && orig[i] != '\'') buf[j++] = orig[i++];
    }
    else if (orig[i+1] == 'x')
    {
      char hexbuf[3] = {0};
      char *endptr;
      hexbuf[0] = orig[i+2];
      hexbuf[1] = orig[i+3];
      buf[j++] = strtol(hexbuf, &endptr, 16);
      if (strlen(hexbuf) != 2 || *endptr != '\0')
      {
        fprintf(stderr, "Invalid string unicode escape: \\x%s\n", hexbuf);
        exit(2);
      }
      i += 4;
    }
    else if (orig[i+1] == 'u')
    {
      char hexbuf[5] = {0};
      uint16_t unicode;
      char *endptr;
      hexbuf[0] = orig[i+2];
      hexbuf[1] = orig[i+3];
      hexbuf[2] = orig[i+4];
      hexbuf[3] = orig[i+5];
      unicode = strtol(hexbuf, &endptr, 16);
      if (strlen(hexbuf) != 4 || *endptr != '\0')
      {
        fprintf(stderr, "Invalid string unicode escape: \\u%s\n", hexbuf);
        exit(2);
      }
      if (unicode <= 0x7F)
      {
        buf[j++] = (char)(uint8_t)unicode;
        i += 6;
        continue;
      }
      if (unicode <= 0x7FF)
      {
        buf[j++] = (char)(uint8_t)(0xc0|(unicode>>6));
        buf[j++] = (char)(uint8_t)(0x80|(unicode&0x3f));
        i += 6;
        continue;
      }
      if (unicode <= 0xFFFF)
      {
        buf[j++] = (char)(uint8_t)(0xe0|(unicode>>12));
        buf[j++] = (char)(uint8_t)(0x80|((unicode>>6)&0x3f));
        buf[j++] = (char)(uint8_t)(0x80|(unicode&0x3f));
        i += 6;
        continue;
      }
      abort();
    }
    else if (orig[i+1] == 't')
    {
      buf[j++] = '\t';
      i += 2;
    }
    else if (orig[i+1] == 'r')
    {
      buf[j++] = '\r';
      i += 2;
    }
    else if (orig[i+1] == 'n')
    {
      buf[j++] = '\n';
      i += 2;
    }
    else
    {
      buf[j++] = orig[i+1];
      i += 2;
    }
  }
  if (j >= capacity)
  {
    char *buf2;
    capacity = 2*capacity+10;
    buf2 = realloc(buf, capacity);
    if (buf2 == NULL)
    {
      free(buf);
      resultstruct.str = NULL;
      return resultstruct;
    }
    buf = buf2;
  }
  if (!orig[i])
  {
    free(buf);
    resultstruct.str = NULL;
    return resultstruct;
  }
  resultstruct.sz = j;
  buf[j++] = '\0';
  result = memdup(buf, j);
  resultstruct.str = result;
  free(buf);
  if (strendptr)
  {
    *strendptr = &orig[i+1];
  }
  return resultstruct;
}

int stiryynameparse(const char *fname, struct stiryy *stiryy, int require)
{
  FILE *stiryyfile;
  int ret;
  stiryyfile = fopen(fname, "r");
  if (stiryyfile == NULL)
  {
    if (require)
    {
      fprintf(stderr, "File %s cannot be opened\n", fname);
      exit(2);
    }
#if 0
    if (stiryy_postprocess(stiryy) != 0)
    {
      exit(2);
    }
#endif
    return -ENOENT;
  }
  ret = stiryydoparse(stiryyfile, stiryy);
#if 0
  if (stiryy_postprocess(stiryy) != 0)
  {
    exit(2);
  }
#endif
  fclose(stiryyfile);
  return ret;
}

int stiryydirparse(
  const char *argv0, const char *fname, struct stiryy *stiryy, int require)
{
  const char *dir;
  char *copy = stir_strdup(argv0);
  char *pathbuf;
  size_t pathcap;
  int res;
  dir = dirname(copy); // NB: not for multi-threaded operation!
  pathcap = strlen(dir)+strlen(fname)+2;
  pathbuf = malloc(pathcap);
  snprintf(pathbuf, pathcap, "%s/%s", dir, fname);
  free(copy);
  res = stiryynameparse(pathbuf, stiryy, require);
  free(pathbuf);
  return res;
}

int do_dirinclude(struct stiryy *stiryy, int noproj, const char *fname, const char *scopevarname)
{
  struct stiryy stiryy2 = STIRYY_EMPTY;
  size_t fsz = strlen(stiryy->curprefix) + strlen(fname) + 8 + 3;
  size_t psz = strlen(stiryy->curprefix) + strlen(fname) + 2;
  size_t ppsz = strlen(stiryy->curprojprefix) + strlen(fname) + 2;
  char *prefix = malloc(psz);
  char *projprefix = malloc(ppsz);
  char *filename = malloc(fsz);
  char realpathname[PATH_MAX];
  char *prefix2, *projprefix2;
  int ret;
  int do_free = 1;
  char *rp;
  FILE *f;
  struct abce_mb oldscope;
  struct abce_mb *mbs = NULL, *mbsc = NULL;
  size_t oldscopeidx;
  if (snprintf(prefix, psz, "%s/%s", stiryy->curprefix, fname) >= (int)psz)
  {
    my_abort();
  }
  if (snprintf(projprefix, ppsz, "%s/%s", stiryy->curprojprefix, fname) >= (int)ppsz)
  {
    my_abort();
  }
  if (snprintf(filename, fsz, "%s/%s/%s", stiryy->curprefix, fname, "Stirfile") >= (int)fsz)
  {
    my_abort();
  }
  errno = EINVAL + 1;
#if _POSIX_VERSION >= 200809
  rp = realpath(filename, NULL);
#else
  rp = NULL;
  errno = EINVAL;
#endif
  if (rp == NULL && errno == EINVAL)
  {
    rp = realpath(filename, realpathname);
    do_free = 0;
  }
  if (rp == NULL)
  {
    printf("path %s does not exist\n", filename);
    return -ENOENT;
  }
  if (strcmp(rp, stiryy->main->realpathname) == 0)
  {
    stiryy->main->subdirseen = 1;
    if (noproj && stiryy->sameproject)
    {
      stiryy->main->subdirseen_sameproject = 1;
    }
  }
  if (do_free)
  {
    free(rp);
  }
  prefix2 = canon(prefix);
  projprefix2 = canon(projprefix);
  if (!noproj)
  {
    // replace projprefix2
    free(projprefix2);
    projprefix2 = stir_strdup(".");
  }
  oldscope = stiryy->main->abce->dynscope;
  oldscopeidx = oldscope.u.area->u.sc.locidx;
  stiryy->main->abce->dynscope = abce_mb_create_scope(stiryy->main->abce, ABCE_DEFAULT_SCOPE_SIZE, &oldscope, 0);
  abce_mb_refdn(stiryy->main->abce, &oldscope);
  //printf("projprefix2: %s\n", projprefix2);
  // This needs to be valid until the end of the program:
  struct scope_ud *ud = my_malloc(sizeof(*ud));
  ud->prefix = prefix2;
  ud->prjprefix = projprefix2;
  abce_scope_set_userdata(&stiryy->main->abce->dynscope, ud);
  if (stiryy->main->abce->dynscope.typ == ABCE_T_N)
  {
    my_abort();
  }
  stiryy_init(&stiryy2, stiryy->main, prefix2, projprefix2, stiryy->main->abce->dynscope, stiryy->dirname, filename, !noproj);
  stiryy2.sameproject = stiryy->sameproject && noproj;

  f = fopen(filename, "r");
  if (!f)
  {
    fprintf(stderr, "stirmake: Can't open substirfile %s.\n", filename);
    return -ENOENT;
  }
  ret = stiryydoparse(f, &stiryy2);
  fclose(f);
  if (ret)
  {
    fprintf(stderr, "stirmake: Can't parse substirfile %s.\n", filename);
    return -EBADMSG;
  }

  stiryy_free(&stiryy2);
  if (scopevarname)
  {
    int err = abce_cpush_mb(stiryy->main->abce, &stiryy->main->abce->dynscope);
    if (err != 0)
    {
      fprintf(stderr, "stirmake: Out of C stack in including substirfile %s.\n", filename);
      return -ENOMEM;
    }
    mbsc = &stiryy->main->abce->cstackbase[stiryy->main->abce->csp-1];
    mbs = abce_mb_cpush_create_string(stiryy->main->abce, scopevarname, strlen(scopevarname));
    if (mbs == NULL)
    {
      fprintf(stderr, "stirmake: Out of memory in including substirfile %s.\n", filename);
      return -ENOMEM;
    }
  }
  abce_mb_refdn(stiryy->main->abce, &stiryy->main->abce->dynscope);
  stiryy->main->abce->dynscope = abce_mb_refup(stiryy->main->abce, &stiryy->main->abce->cachebase[oldscopeidx]);
  if (scopevarname)
  {
    if (abce_sc_replace_val_mb(stiryy->main->abce, &stiryy->main->abce->dynscope, mbs, mbsc) != 0)
    {
      fprintf(stderr, "stirmake: Can't set scope to named variable %s.\n", scopevarname);
      return -ENOMEM;
    }
    abce_cpop(stiryy->main->abce);
    abce_cpop(stiryy->main->abce);
  }
  //get_abce(stiryy)->dynscope = oldscope;
  // free(prefix2); // let it leak, FIXME free it someday
  // free(projprefix2); // let it leak, FIXME free it someday
  free(prefix);
  free(projprefix);
  free(filename);
  return 0;
}

int do_fileinclude(struct stiryy *stiryy, const char *fname, int ignore)
{
  struct stiryy stiryy2 = STIRYY_EMPTY;
  int ret;
  FILE *f;

  stiryy_init(&stiryy2, stiryy->main, stiryy->curprefix, stiryy->curprojprefix, stiryy->main->abce->dynscope, stiryy->dirname, fname, 0);
  stiryy2.sameproject = stiryy->sameproject;

  errno = ENOENT;
  f = fopen(fname, "r");
  if (!f)
  {
    int errno_save = errno;
    if (errno == EMFILE || errno == ENFILE)
    {
      fprintf(stderr, "Out of file descriptors, probably infinite recursion when opening file %s\n", fname);
      return -errno_save;
    }
    if (ignore && (errno == ENOENT || errno == ENOTDIR))
    {
      return 0;
    }
    fprintf(stderr, "stirmake: Can't open substirfile %s.\n", fname);
    return -errno_save;
  }
  ret = stiryydoparse(f, &stiryy2);
  fclose(f);
  if (ret)
  {
    fprintf(stderr, "stirmake: Can't parse substirfile %s.\n", fname);
    return -EBADMSG;
  }

  stiryy_free(&stiryy2);

  return 0;
}

int
engine_stringlist(struct abce *abce,
                  size_t ip,
                  const char *directive,
                  char ***strs, size_t *strsz,
                  int allow_nil,
                  int *was_array)
{
  unsigned char tmpbuf[64] = {0};
  size_t tmpsiz = 0;
  size_t i;
  struct abce_mb *mb;

  *strs = NULL;
  *strsz = 0;
  if (was_array)
  {
    *was_array = 0;
  }

  abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_PUSH_DBL);
  abce_add_double_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ip);
  abce_add_ins_alt(tmpbuf, &tmpsiz, sizeof(tmpbuf), ABCE_OPCODE_JMP);

  if (abce->sp != 0)
  {
    abort();
  }
  if (abce_engine(abce, tmpbuf, tmpsiz) != 0)
  {
    printf("Error executing bytecode for %s directive\n", directive);
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
    return -EINVAL;
  }
  if (abce_getmbptr(&mb, abce, 0) != 0)
  {
    printf("can't get item from stack in %s\n", directive);
    return -EINVAL;
    //printf("expected array, got type %d\n", get_abce(amyplanyy)->err.mb.typ);
  }
  if (mb->typ == ABCE_T_S)
  {
    if (was_array)
    {
      *was_array = 0;
    }
    *strsz = 1;
    *strs = malloc(sizeof(**strs) * (*strsz));
    (*strs)[0] = stir_strdup(abce_mba_str(mb->u.area));
  }
  else if (mb->typ == ABCE_T_A)
  {
    if (was_array)
    {
      *was_array = 1;
    }
    for (i = 0; i < mb->u.area->u.ar.size; i++)
    {
      if (mb->u.area->u.ar.mbs[i].typ != ABCE_T_S &&
          ((!allow_nil) || mb->u.area->u.ar.mbs[i].typ != ABCE_T_N))
      {
        printf("expected string or @nil, got type %d for directive %s\n",
               mb->u.area->u.ar.mbs[i].typ, directive);
        return -EINVAL;
      }
    }
    *strsz = mb->u.area->u.ar.size;
    *strs = malloc(sizeof(**strs) * (*strsz));
    for (i = 0; i < *strsz; i++)
    {
      if (mb->u.area->u.ar.mbs[i].typ == ABCE_T_N)
      {
        (*strs)[i] = NULL;
        continue;
      }
      (*strs)[i] = stir_strdup(abce_mba_str(mb->u.area->u.ar.mbs[i].u.area));
    }
  }
  else
  {
    printf("expected str or array, got type %d in %s\n",
           mb->typ, directive);
    return -EINVAL;
  }
  if (abce->sp != 1)
  {
    abort();
  }
  abce_pop(abce);
  return 0;
}

int add_rule_yy(struct stiryy_main *stirmain, struct tgt *tgts, size_t tgtsz,
                struct dep *deps, size_t depsz,
                struct cmdsrc *shells,
                int phony, int rectgt, int detouch, int maybe, int dist,
                int cleanhook, int distcleanhook, int bothcleanhook,
                int deponly,
                char *prefix, size_t scopeidx, int lineno)
{
  if (stirmain->trial)
  {
    return 0;
  }
  if (stirmain->rule_in_progress)
  {
    return -EINVAL;
  }
  stiryy_main_emplace_rule(stirmain, prefix, scopeidx, lineno);
  stirmain->rules[stirmain->rulesz-1].bases = NULL;
  stirmain->rules[stirmain->rulesz-1].basesz = 0;
  stirmain->lastrule_basecapacity = 0;
  stirmain->rules[stirmain->rulesz-1].deps = deps;
  stirmain->rules[stirmain->rulesz-1].depsz = depsz;
  stirmain->lastrule_depcapacity = depsz;
  stirmain->rules[stirmain->rulesz-1].targets = tgts;
  stirmain->rules[stirmain->rulesz-1].targetsz = tgtsz;
  stirmain->lastrule_targetcapacity = tgtsz;
  stirmain->rules[stirmain->rulesz-1].shells = *shells;
  //stirmain->rules[stirmain->rulesz-1].scopeidx = scopeidx;
  //stirmain->rules[stirmain->rulesz-1].prefix = prefix;
  stirmain->rules[stirmain->rulesz-1].phony = !!phony;
  stirmain->rules[stirmain->rulesz-1].rectgt = !!rectgt;
  stirmain->rules[stirmain->rulesz-1].detouch = !!detouch;
  stirmain->rules[stirmain->rulesz-1].maybe = !!maybe;
  stirmain->rules[stirmain->rulesz-1].dist = !!dist;
  stirmain->rules[stirmain->rulesz-1].iscleanhook = !!cleanhook;
  stirmain->rules[stirmain->rulesz-1].isdistcleanhook = !!distcleanhook;
  stirmain->rules[stirmain->rulesz-1].isbothcleanhook = !!bothcleanhook;
  stirmain->rules[stirmain->rulesz-1].deponly = !!deponly;
  stirmain->rule_in_progress = 0;
  return 0;
}

static const char *rejectchars[256] = {
"\\x00", "\\x01", "\\x02", "\\x03", "\\x04", "\\x05", "\\x06", "\\x07",
"\\x08", "\\x09", "\\x0a", "\\x0b", "\\x0c", "\\x0d", "\\x0e", "\\x0f",
"\\x10", "\\x11", "\\x12", "\\x13", "\\x14", "\\x15", "\\x16", "\\x17",
"\\x18", "\\x19", "\\x1a", "\\x1b", "\\x1c", "\\x1d", "\\x1e", "\\x1f",
"\\x20", "\\x21", "\\x22", "\\x23", "\\x24", "\\x25", "\\x26", "\\x27",
"\\x28", "\\x29", "\\x2a", "\\x2b", "\\x2c", "\\x2d", "\\x2e", "\\x2f",
"\\x30", "\\x31", "\\x32", "\\x33", "\\x34", "\\x35", "\\x36", "\\x37",
"\\x38", "\\x39", "\\x3a", "\\x3b", "\\x3c", "\\x3d", "\\x3e", "\\x3f",
"\\x40", "\\x41", "\\x42", "\\x43", "\\x44", "\\x45", "\\x46", "\\x47",
"\\x48", "\\x49", "\\x4a", "\\x4b", "\\x4c", "\\x4d", "\\x4e", "\\x4f",
"\\x50", "\\x51", "\\x52", "\\x53", "\\x54", "\\x55", "\\x56", "\\x57",
"\\x58", "\\x59", "\\x5a", "\\x5b", "\\x5c", "\\x5d", "\\x5e", "\\x5f",
"\\x60", "\\x61", "\\x62", "\\x63", "\\x64", "\\x65", "\\x66", "\\x67",
"\\x68", "\\x69", "\\x6a", "\\x6b", "\\x6c", "\\x6d", "\\x6e", "\\x6f",
"\\x70", "\\x71", "\\x72", "\\x73", "\\x74", "\\x75", "\\x76", "\\x77",
"\\x78", "\\x79", "\\x7a", "\\x7b", "\\x7c", "\\x7d", "\\x7e", "\\x7f",
"\\x80", "\\x81", "\\x82", "\\x83", "\\x84", "\\x85", "\\x86", "\\x87",
"\\x88", "\\x89", "\\x8a", "\\x8b", "\\x8c", "\\x8d", "\\x8e", "\\x8f",
"\\x90", "\\x91", "\\x92", "\\x93", "\\x94", "\\x95", "\\x96", "\\x97",
"\\x98", "\\x99", "\\x9a", "\\x9b", "\\x9c", "\\x9d", "\\x9e", "\\x9f",
"\\xa0", "\\xa1", "\\xa2", "\\xa3", "\\xa4", "\\xa5", "\\xa6", "\\xa7",
"\\xa8", "\\xa9", "\\xaa", "\\xab", "\\xac", "\\xad", "\\xae", "\\xaf",
"\\xb0", "\\xb1", "\\xb2", "\\xb3", "\\xb4", "\\xb5", "\\xb6", "\\xb7",
"\\xb8", "\\xb9", "\\xba", "\\xbb", "\\xbc", "\\xbd", "\\xbe", "\\xbf",
"\\xc0", "\\xc1", "\\xc2", "\\xc3", "\\xc4", "\\xc5", "\\xc6", "\\xc7",
"\\xc8", "\\xc9", "\\xca", "\\xcb", "\\xcc", "\\xcd", "\\xce", "\\xcf",
"\\xd0", "\\xd1", "\\xd2", "\\xd3", "\\xd4", "\\xd5", "\\xd6", "\\xd7",
"\\xd8", "\\xd9", "\\xda", "\\xdb", "\\xdc", "\\xdd", "\\xde", "\\xdf",
"\\xe0", "\\xe1", "\\xe2", "\\xe3", "\\xe4", "\\xe5", "\\xe6", "\\xe7",
"\\xe8", "\\xe9", "\\xea", "\\xeb", "\\xec", "\\xed", "\\xee", "\\xef",
"\\xf0", "\\xf1", "\\xf2", "\\xf3", "\\xf4", "\\xf5", "\\xf6", "\\xf7",
"\\xf8", "\\xf9", "\\xfa", "\\xfb", "\\xfc", "\\xfd", "\\xfe", "\\xff",
};

static inline int file_escape_string_slowpath(FILE *f, unsigned char uch)
{
    if (!uch) return -EOVERFLOW;
    fputs(rejectchars[uch], f);
    return 0;
}

static uint8_t acceptchars[256] = {
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 0-15
  0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0, // 16-31
  1,0,0,1,1,1,1,0,1,1,1,1,1,1,1,1, // 32-47
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // 48-63
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // 64-79
  1,1,1,1,1,1,1,1,1,1,1,1,0,1,1,1, // 80-95
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1, // 96-111
  1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,0, // 112-127
};

void acceptchars_init(void)
{
  int i;
  acceptchars['"'] = 0;
  rejectchars['"'] = "\\\"";
  acceptchars['\''] = 0;
  rejectchars['\''] = "\\'";
  acceptchars['\\'] = 0;
  rejectchars['\\'] = "\\\\";
  acceptchars['\n'] = 0;
  rejectchars['\n'] = "\\n";
  acceptchars['\r'] = 0;
  rejectchars['\r'] = "\\r";
  acceptchars['\t'] = 0;
  rejectchars['\t'] = "\\t";
  if (is_ascii_full())
  {
    for (i = 0; i < 256; i++)
    {
      if (i == '"' || i == '\'' || i == '\\')
      {
        continue;
      }
      acceptchars[i] = 1;
    }
    for (i = 128; i < 256; i++)
    {
      acceptchars[i] = 0;
    }
    acceptchars['\x7f'] = 0;
    for (i = 0; i < 32; i++)
    {
      acceptchars[i] = 0;
    }
  }
  else
  {
    for (i = 0; i < 256; i++)
    {
      acceptchars[i] = 0;
    }
    acceptchars[' '] = 1;
    acceptchars['!'] = 1;
    acceptchars['#'] = 1;
    acceptchars['$'] = 1;
    acceptchars['%'] = 1;
    acceptchars['&'] = 1;
    acceptchars['('] = 1;
    acceptchars[')'] = 1;
    acceptchars['*'] = 1;
    acceptchars['+'] = 1;
    acceptchars[','] = 1;
    acceptchars['-'] = 1;
    acceptchars['.'] = 1;
    acceptchars['/'] = 1;
    acceptchars['0'] = 1;
    acceptchars['1'] = 1;
    acceptchars['2'] = 1;
    acceptchars['3'] = 1;
    acceptchars['4'] = 1;
    acceptchars['5'] = 1;
    acceptchars['6'] = 1;
    acceptchars['7'] = 1;
    acceptchars['8'] = 1;
    acceptchars['9'] = 1;
    acceptchars[':'] = 1;
    acceptchars[';'] = 1;
    acceptchars['<'] = 1;
    acceptchars['='] = 1;
    acceptchars['>'] = 1;
    acceptchars['?'] = 1;
    acceptchars['@'] = 1;
    acceptchars['A'] = 1;
    acceptchars['B'] = 1;
    acceptchars['C'] = 1;
    acceptchars['D'] = 1;
    acceptchars['E'] = 1;
    acceptchars['F'] = 1;
    acceptchars['G'] = 1;
    acceptchars['H'] = 1;
    acceptchars['I'] = 1;
    acceptchars['J'] = 1;
    acceptchars['K'] = 1;
    acceptchars['L'] = 1;
    acceptchars['M'] = 1;
    acceptchars['N'] = 1;
    acceptchars['O'] = 1;
    acceptchars['P'] = 1;
    acceptchars['Q'] = 1;
    acceptchars['R'] = 1;
    acceptchars['S'] = 1;
    acceptchars['T'] = 1;
    acceptchars['U'] = 1;
    acceptchars['V'] = 1;
    acceptchars['W'] = 1;
    acceptchars['X'] = 1;
    acceptchars['Y'] = 1;
    acceptchars['Z'] = 1;
    acceptchars['['] = 1;
    acceptchars[']'] = 1;
    acceptchars['^'] = 1;
    acceptchars['_'] = 1;
    acceptchars['`'] = 1;
    acceptchars['a'] = 1;
    acceptchars['b'] = 1;
    acceptchars['c'] = 1;
    acceptchars['d'] = 1;
    acceptchars['e'] = 1;
    acceptchars['f'] = 1;
    acceptchars['g'] = 1;
    acceptchars['h'] = 1;
    acceptchars['i'] = 1;
    acceptchars['j'] = 1;
    acceptchars['k'] = 1;
    acceptchars['l'] = 1;
    acceptchars['m'] = 1;
    acceptchars['n'] = 1;
    acceptchars['o'] = 1;
    acceptchars['p'] = 1;
    acceptchars['q'] = 1;
    acceptchars['r'] = 1;
    acceptchars['s'] = 1;
    acceptchars['t'] = 1;
    acceptchars['u'] = 1;
    acceptchars['v'] = 1;
    acceptchars['w'] = 1;
    acceptchars['x'] = 1;
    acceptchars['y'] = 1;
    acceptchars['z'] = 1;
    acceptchars['{'] = 1;
    acceptchars['|'] = 1;
    acceptchars['}'] = 1;
    acceptchars['~'] = 1;
  }
}

void file_escape_string(FILE *f, const char *str)
{
  const char *ptr = str;
  for (ptr = str; *ptr;)
  {
    unsigned char uch = (unsigned char)*ptr++;
    if (acceptchars[uch]) putc_unlocked(uch, f);
    else if (file_escape_string_slowpath(f, uch)) break;

    uch = (unsigned char)*(ptr++);
    if (acceptchars[uch]) putc_unlocked(uch, f);
    else if (file_escape_string_slowpath(f, uch)) break;

    uch = (unsigned char)*(ptr++);
    if (acceptchars[uch]) putc_unlocked(uch, f);
    else if (file_escape_string_slowpath(f, uch)) break;

    uch = (unsigned char)*(ptr++);
    if (acceptchars[uch]) putc_unlocked(uch, f);
    else if (file_escape_string_slowpath(f, uch)) break;

    uch = (unsigned char)*(ptr++);
    if (acceptchars[uch]) putc_unlocked(uch, f);
    else if (file_escape_string_slowpath(f, uch)) break;

    uch = (unsigned char)*(ptr++);
    if (acceptchars[uch]) putc_unlocked(uch, f);
    else if (file_escape_string_slowpath(f, uch)) break;

    uch = (unsigned char)*(ptr++);
    if (acceptchars[uch]) putc_unlocked(uch, f);
    else if (file_escape_string_slowpath(f, uch)) break;

    uch = (unsigned char)*(ptr++);
    if (acceptchars[uch]) putc_unlocked(uch, f);
    else if (file_escape_string_slowpath(f, uch)) break;
  }
}
