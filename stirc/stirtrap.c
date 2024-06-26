#include <sys/types.h>
#include <sys/stat.h>
#include <stdint.h>
#include <glob.h>
#include <unistd.h>
#include <fcntl.h>
#include "abce/abce.h"
#include "abce/abcetrees.h"
#include "stiropcodes.h"
#include "stirtrap.h"
#include "canon.h"
#include "stiryy.h"
#include "jsonyyutils.h"
#include "stircommon.h"

static inline void abce_npoppush(struct abce *abce, size_t n, struct abce_mb *mb)
{
        int ret;
        ret = abce_mb_stackreplace(abce, -(int64_t)n, mb);
#if POPABORTS
        if (ret != 0)
        {
                abort();
        }
#endif
        for (size_t i = 1; i < n; i++)
        {
                ret = abce_pop(abce);
#if POPABORTS
                if (ret != 0)
                {
                        abort();
                }
#endif
        }
}

#define VERIFYMB(idx, type) \
  if(1) { \
    int _getdbl_rettmp = abce_verifymb(abce, (idx), (type)); \
    if (_getdbl_rettmp != 0) \
    { \
      ret = _getdbl_rettmp; \
      break; \
    } \
  }

#define GETGENERIC(fun, val, idx) \
  if(1) { \
    int _getgen_rettmp = fun((val), abce, (idx)); \
    if (_getgen_rettmp != 0) \
    { \
      ret = _getgen_rettmp; \
      break; \
    } \
  }

#define GETMBPTR(mb, idx) GETGENERIC(abce_getmbptr, mb, idx)
#define GETMBARPTR(mb, idx) GETGENERIC(abce_getmbarptr, mb, idx)
#define GETMBSTRPTR(mb, idx) GETGENERIC(abce_getmbstrptr, mb, idx)

/*
  Planned argument for STIR_OPCODE_RULE_ADD:
{
  "tgts": [ // "name" has no default
    {"name": "a", "dist": false},
    {"name": "b", "dist": false}
  ],
  "deps": [ // default: []
    // default for all: false except "name" has no default
    // "rec" and "orderonly" can't be set simultaneously
    {"name": "c", "rec": false, "orderonly": false},
    {"name": "d", "rec": false, "orderonly": false}
  ],
  "attrs": { // default for all: false, default for "attrs": {}
    "phony": false, // if true, "rectgt"+"maybe"+"dist"+"detouch" must be false
    "rectgt": false, // if true, "maybe"+"detouch" must be false
    "detouch": false, // if true, "maybe"+"rectgt" must be false
    "maybe": false,
    "dist": false,
    "deponly": false, // if set, 5 preceding attributes must be false
    // zero or one of these must be true, two being true not permitted:
    // if any of these is true, "phony" xor "deponly" must be true
    // if any of these is true, "dist" must be false
    // if any of these is true, "rectgt" must be false
    // if any of these is true, "detouch" must be false
    // if any of these is true, "maybe" must be false
    // if any of these is true, "tgts" must be omitted
    "iscleanhook": false,
    "isdistcleanhook": false,
    "isbothcleanhook": false,
  },
  "shells": [ // default: []
    {
      "embed": @true, // default: @false
      "isfun": @false, // default: @false
      "ignore": @true, // if missing, default @false
      "ismake": @false, // if missing, default @false
      "noecho": @false, // if missing, default @false
      "cmds": [["true"], ["false"]]
    },
    {
      "embed": @false,
      "isfun": @false,
      "ignore": @true, // if missing, default @false
      "ismake": @false, // if missing, default @false
      "noecho": @false, // if missing, default @false
      "cmd": ["false"]
    },
    {
      "embed": @true,
      "isfun": @true,
      "ignore": @true, // if missing, default @false
      "ismake": @false, // if missing, default @false
      "noecho": @false, // if missing, default @false
      "fun": $FNMANY,
      "arg": $FNMANYARG
    },
    {
      "embed": @false,
      "isfun": @true,
      "ignore": @true, // if missing, default @false
      "ismake": @false, // if missing, default @false
      "noecho": @false, // if missing, default @false
      "fun": $FN,
      "arg": $FNARG
    }
  ]
}
 */

size_t cache_add_str_nul(struct abce *abce, const char *str, int *err)
{
  size_t ret;
  ret = abce_cache_add_str_nul(abce, str);
  if (ret == (size_t)-1)
  {
    *err = 1;
  }
  return ret;
}

struct attrstruct {
  unsigned phony:1;
  unsigned rectgt:1;
  unsigned detouch:1;
  unsigned maybe:1;
  unsigned dist:1;
  unsigned deponly:1;
  unsigned iscleanhook:1;
  unsigned isdistcleanhook:1;
  unsigned isbothcleanhook:1;
};

int attr_sanity(const struct attrstruct *as)
{
  int clean = 0;
  clean = as->iscleanhook + as->isdistcleanhook + as->isbothcleanhook;
  if (clean > 1)
  {
    return -EINVAL;
  }
  if (clean)
  {
    if (as->dist || as->rectgt || as->detouch || as->maybe)
    {
      return -EINVAL;
    }
    if (as->phony + as->deponly != 1)
    {
      return -EINVAL;
    }
    return 0;
  }
  if (as->deponly)
  {
    if (as->dist || as->maybe || as->detouch || as->rectgt || as->phony)
    {
      return -EINVAL;
    }
    return 0;
  }
  if (as->phony)
  {
    if (as->dist || as->maybe || as->detouch || as->rectgt)
    {
      return -EINVAL;
    }
    return 0;
  }
  if (as->rectgt)
  {
    if (as->maybe || as->detouch)
    {
      return -EINVAL;
    }
    return 0;
  }
  if (as->detouch)
  {
    if (as->maybe || as->rectgt)
    {
      return -EINVAL;
    }
    return 0;
  }
  return 0;
}

static inline void emit(char **buf, size_t *sz, size_t *cap, char ch)
{
  if ((*sz) + 2 >= *cap)
  {
    *cap = 2*(*cap) + 2;
    char *newbuf = realloc(*buf, *cap);
    if (newbuf == NULL)
    {
      free(*buf);
    }
    *buf = newbuf;
  }
  if (*buf == NULL)
  {
    return;
  }
  (*buf)[(*sz)++] = ch;
  (*buf)[(*sz)] = '\0';
}

char *stir_shellescape(const char *old, size_t oldsz, size_t *newszptr)
{
  size_t newsz = 0;
  size_t newcap = 0;
  char *newbuf = NULL;
  size_t i;
  *newszptr = 0;
  for (i = 0; i < oldsz; i++)
  {
    if (old[i] == '\n' || old[i] == '\t')
    { 
      emit(&newbuf, &newsz, &newcap, '"');
      emit(&newbuf, &newsz, &newcap, old[i]);
      emit(&newbuf, &newsz, &newcap, '"');
      continue;
    }
    if (   old[i] == '$' // var
        || old[i] == '\\' // escape
        || old[i] == '"' // quote
        || old[i] == '\'' // quote
        || old[i] == '*' // wildcard
        || old[i] == '?' // single-char wildcard
        || old[i] == '`' // inline command
        || old[i] == '<' // redir
        || old[i] == '>' // redir
        || old[i] == '#' // comment
        || old[i] == '&' // background, and-operator
        || old[i] == '(' // ???
        || old[i] == ')' // ???
        || old[i] == ';' // multiple commands on same line
        || old[i] == '|' // pipe
        || old[i] == '~' // home directory
        || old[i] == '%' // unknown, but safest to escape
        || old[i] == '=' // unknown, but safest to escape, probably set -k
        || old[i] == '[' // example: source.[ch]
        || old[i] == ']' // example: source.[ch], probably just [ would be ok
        || old[i] == '!' // history substitution
        || old[i] == '{' // brace expansion: source.{c,h}
        || old[i] == '}' // brace expansion, probably just } would be ok
        || old[i] == '^' // unknown, but safest to quote
       )
    {
      emit(&newbuf, &newsz, &newcap, '\\');
      emit(&newbuf, &newsz, &newcap, old[i]);
      continue;
    }
    emit(&newbuf, &newsz, &newcap, old[i]);
  }
  *newszptr = newsz;
  return newbuf;
}

int stir_trap_ruleadd(struct stiryy_main *main,
                      struct abce *abce, const char *prefix)
{
  struct abce_mb *tree;
  int ret = abce_verifymb(abce, -1, ABCE_T_T);
  int errcod = 0;
  int err = 0;
  size_t tgts, deps, attrs, shells, name, rec, orderonly, phony;
  size_t rectgt, detouch, maybe, dist, deponly;
  size_t iscleanhook, isdistcleanhook, isbothcleanhook;
  size_t embed, isfun, cmds, cmd, fun, arg;
  size_t ismake, noecho, ignore;
  struct abce_mb *tgtres, *depres, *attrsres, *mbstr, *shellsres;
  struct tgt *yytgts;
  size_t tgtsz;
  struct dep *yydeps;
  size_t depsz;
  struct cmdsrcitem *yyshells;
  size_t shellsz;
  size_t i;
  struct attrstruct attrstruct = {};

  if (ret != 0)
  {
    return ret;
  }
  if (abce_getmbptr(&tree, abce, -1) != 0)
  {
    abort();
  }
  tgts = cache_add_str_nul(abce, "tgts", &err);
  deps = cache_add_str_nul(abce, "deps", &err);
  attrs = cache_add_str_nul(abce, "attrs", &err);
  shells = cache_add_str_nul(abce, "shells", &err);
  name = cache_add_str_nul(abce, "name", &err);
  rec = cache_add_str_nul(abce, "rec", &err);
  orderonly = cache_add_str_nul(abce, "orderonly", &err);
  phony = cache_add_str_nul(abce, "phony", &err);
  rectgt = cache_add_str_nul(abce, "rectgt", &err);
  detouch = cache_add_str_nul(abce, "detouch", &err);
  maybe = cache_add_str_nul(abce, "maybe", &err);
  dist = cache_add_str_nul(abce, "dist", &err);
  deponly = cache_add_str_nul(abce, "deponly", &err);
  iscleanhook = cache_add_str_nul(abce, "iscleanhook", &err);
  isdistcleanhook = cache_add_str_nul(abce, "isdistcleanhook", &err);
  isbothcleanhook = cache_add_str_nul(abce, "isbothcleanhook", &err);
  embed = cache_add_str_nul(abce, "embed", &err);
  isfun = cache_add_str_nul(abce, "isfun", &err);
  cmds = cache_add_str_nul(abce, "cmds", &err);
  cmd = cache_add_str_nul(abce, "cmd", &err);
  fun = cache_add_str_nul(abce, "fun", &err);
  arg = cache_add_str_nul(abce, "arg", &err);
  ismake = cache_add_str_nul(abce, "ismake", &err);
  noecho = cache_add_str_nul(abce, "noecho", &err);
  ignore = cache_add_str_nul(abce, "ignore", &err);
  if (err)
  {
    abce_pop(abce);
    return -ENOMEM;
  }

  if (abce_tree_get_str(abce, &tgtres, tree, &abce->cachebase[tgts]) != 0)
  {
    abce->err.code = STIR_E_TARGETS_MISSING;
    abce_pop(abce);
    return -EINVAL;
  }
  if (tgtres->typ != ABCE_T_A)
  {
    abce->err.code = ABCE_E_EXPECT_ARRAY;
    abce->err.mb.typ = ABCE_T_N; // FIXME
    abce_pop(abce);
    return -EINVAL;
  }
  tgtsz = tgtres->u.area->u.ar.size;
  for (i = 0; i < tgtsz; i++)
  {
    struct abce_mb *mb = &tgtres->u.area->u.ar.mbs[i];
    struct abce_mb *attr1;
    if (mb->typ != ABCE_T_T)
    {
      abce->err.code = ABCE_E_EXPECT_TREE;
      abce->err.mb.typ = ABCE_T_N; // FIXME
      abce_pop(abce);
      return -EINVAL;
    }
    if (abce_tree_get_str(abce, &mbstr, mb, &abce->cachebase[name]) != 0)
    {
      abce->err.code = STIR_E_NAME_MISSING;
      abce_pop(abce);
      return -EINVAL;
    }
    if (mbstr->typ != ABCE_T_S)
    {
      abce->err.code = ABCE_E_EXPECT_STR;
      abce->err.mb.typ = ABCE_T_N; // FIXME
      abce_pop(abce);
      return -EINVAL;
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[dist]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
    }
  }

  if (abce_tree_get_str(abce, &depres, tree, &abce->cachebase[deps]) != 0)
  {
    depres = NULL;
  }
  if (depres && depres->typ != ABCE_T_A)
  {
    abce->err.code = ABCE_E_EXPECT_ARRAY;
    abce->err.mb.typ = ABCE_T_N; // FIXME
    abce_pop(abce);
    return -EINVAL;
  }
  depsz = 0;
  if (depres)
  {
    depsz = depres->u.area->u.ar.size;
  }
  for (i = 0; i < depsz; i++)
  {
    struct abce_mb *mb = &depres->u.area->u.ar.mbs[i];
    struct abce_mb *attr1;
    if (mb->typ != ABCE_T_T)
    {
      abce->err.code = ABCE_E_EXPECT_TREE;
      abce->err.mb.typ = ABCE_T_N; // FIXME
      abce_pop(abce);
      return -EINVAL;
    }
    if (abce_tree_get_str(abce, &mbstr, mb, &abce->cachebase[name]) != 0)
    {
      abce->err.code = STIR_E_NAME_MISSING;
      abce_pop(abce);
      return -EINVAL;
    }
    if (mbstr->typ != ABCE_T_S)
    {
      abce->err.code = ABCE_E_EXPECT_STR;
      abce->err.mb.typ = ABCE_T_N; // FIXME
      abce_pop(abce);
      return -EINVAL;
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[rec]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[orderonly]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
    }
  }

  if (abce_tree_get_str(abce, &shellsres, tree, &abce->cachebase[shells]) != 0)
  {
    shellsres = NULL;
  }
  if (shellsres && shellsres->typ != ABCE_T_A)
  {
    abce->err.code = ABCE_E_EXPECT_ARRAY;
    abce->err.mb.typ = ABCE_T_N; // FIXME
    abce_pop(abce);
    return -EINVAL;
  }
  shellsz = 0;
  if (shellsres)
  {
    shellsz = shellsres->u.area->u.ar.size;
  }
  for (i = 0; i < shellsz; i++)
  {
    struct abce_mb *mb = &shellsres->u.area->u.ar.mbs[i];
    struct abce_mb *attr1;
    if (mb->typ != ABCE_T_T)
    {
      abce->err.code = ABCE_E_EXPECT_TREE;
      abce->err.mb.typ = ABCE_T_N; // FIXME
      abce_pop(abce);
      return -EINVAL;
    }
    int boolembed = 0;
    int boolisfun = 0;

    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[embed]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      boolembed = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[isfun]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      boolisfun = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[ismake]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[noecho]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[ignore]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
    }
    if (boolisfun)
    {
      if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[fun]) != 0)
      {
        abce->err.code = STIR_E_NO_SHELLARG;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      if (attr1->typ != ABCE_T_F)
      {
        abce->err.code = ABCE_E_EXPECT_FUNC;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[arg]) != 0)
      {
        abce->err.code = STIR_E_NO_SHELLARG;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
    }
    else if (boolembed)
    {
      size_t j;
      if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[cmds]) != 0)
      {
        abce->err.code = STIR_E_NO_SHELLARG;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      if (attr1->typ != ABCE_T_A)
      {
        abce->err.code = ABCE_E_EXPECT_ARRAY;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      for (j = 0; j < attr1->u.area->u.ar.size; j++)
      {
        size_t k;
        if (attr1->u.area->u.ar.mbs[j].typ != ABCE_T_A)
        {
          abce->err.code = ABCE_E_EXPECT_ARRAY;
          abce->err.mb.typ = ABCE_T_N; // FIXME
          abce_pop(abce);
          return -EINVAL;
        }
        for (k = 0; k < attr1->u.area->u.ar.mbs[j].u.area->u.ar.size; k++)
        {
          if (attr1->u.area->u.ar.mbs[j].u.area->u.ar.mbs[k].typ != ABCE_T_S)
          {
            abce->err.code = ABCE_E_EXPECT_STR;
            abce->err.mb.typ = ABCE_T_N; // FIXME
            abce_pop(abce);
            return -EINVAL;
          }
        }
      }
    }
    else
    {
      size_t j;
      if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[cmd]) != 0)
      {
        abce->err.code = STIR_E_NO_SHELLARG;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      if (attr1->typ != ABCE_T_A)
      {
        abce->err.code = ABCE_E_EXPECT_ARRAY;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      for (j = 0; j < attr1->u.area->u.ar.size; j++)
      {
        if (attr1->u.area->u.ar.mbs[j].typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
          abce->err.mb.typ = ABCE_T_N; // FIXME
          abce_pop(abce);
          return -EINVAL;
        }
      }
    }
  }

  if (abce_tree_get_str(abce, &attrsres, tree, &abce->cachebase[attrs]) != 0)
  {
    attrsres = NULL;
  }
  if (attrsres && attrsres->typ != ABCE_T_T)
  {
    abce->err.code = ABCE_E_EXPECT_TREE;
    abce->err.mb.typ = ABCE_T_N; // FIXME
    abce_pop(abce);
    return -EINVAL;
  }
  if (attrsres)
  {
    struct abce_mb *attr1;
    if (abce_tree_get_str(abce, &attr1, attrsres, &abce->cachebase[phony]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      attrstruct.phony = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, attrsres, &abce->cachebase[rectgt]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      attrstruct.rectgt = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, attrsres, &abce->cachebase[detouch]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      attrstruct.detouch = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, attrsres, &abce->cachebase[maybe]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      attrstruct.maybe = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, attrsres, &abce->cachebase[dist]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      attrstruct.dist = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, attrsres, &abce->cachebase[deponly]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      attrstruct.deponly = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, attrsres, &abce->cachebase[iscleanhook]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      attrstruct.iscleanhook = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, attrsres, &abce->cachebase[isdistcleanhook]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      attrstruct.isdistcleanhook = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, attrsres, &abce->cachebase[isbothcleanhook]) == 0)
    {
      if (attr1->typ != ABCE_T_D && attr1->typ != ABCE_T_B)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb.typ = ABCE_T_N; // FIXME
        abce_pop(abce);
        return -EINVAL;
      }
      attrstruct.isbothcleanhook = !!attr1->u.d;
    }
  }

  yytgts = malloc(sizeof(*yytgts)*tgtsz);
  yydeps = malloc(sizeof(*yydeps)*depsz);
  yyshells = malloc(sizeof(*yyshells)*shellsz);

  for (i = 0; i < tgtsz; i++)
  {
    struct abce_mb *mb = &tgtres->u.area->u.ar.mbs[i];
    size_t namsz;
    struct abce_mb *attr1;
    char *nam;
    if (abce_tree_get_str(abce, &mbstr, mb, &abce->cachebase[name]) != 0)
    {
      my_abort();
    }
    namsz = strlen(mbstr->u.area->u.str.buf) + strlen(prefix) + 2;
    nam = malloc(namsz); // FIXME leaks
    if (snprintf(nam, namsz, "%s/%s", prefix, mbstr->u.area->u.str.buf) >= namsz)
    {
      my_abort();
    }
    yytgts[i].name = canon(nam);
    free(nam);
    yytgts[i].namenodir = strdup(mbstr->u.area->u.str.buf);
    yytgts[i].is_dist = 0;
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[dist]) == 0)
    {
      yytgts[i].is_dist = !!attr1->u.d;
    }
  }
  for (i = 0; i < depsz; i++)
  {
    struct abce_mb *mb = &depres->u.area->u.ar.mbs[i];
    struct abce_mb *attr1;
    size_t namsz;
    char *nam;
    if (abce_tree_get_str(abce, &mbstr, mb, &abce->cachebase[name]) != 0)
    {
      my_abort();
    }
    namsz = strlen(mbstr->u.area->u.str.buf) + strlen(prefix) + 2;
    nam = malloc(namsz); // FIXME leaks
    if (snprintf(nam, namsz, "%s/%s", prefix, mbstr->u.area->u.str.buf) >= namsz)
    {
      my_abort();
    }
    yydeps[i].name = canon(nam);
    free(nam);
    yydeps[i].namenodir = strdup(mbstr->u.area->u.str.buf);
    yydeps[i].rec = 0;
    yydeps[i].orderonly = 0;
    yydeps[i].wait = 0;
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[rec]) == 0)
    {
      yydeps[i].rec = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[orderonly]) == 0)
    {
      yydeps[i].orderonly = !!attr1->u.d;
    }
  }
  for (i = 0; i < shellsz; i++)
  {
    struct abce_mb *mb = &shellsres->u.area->u.ar.mbs[i];
    struct abce_mb *attr1;
    int boolembed = 0;
    int boolisfun = 0;
    int boolismake = 0, boolnoecho = 0, boolignore = 0;

    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[embed]) == 0)
    {
      boolembed = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[isfun]) == 0)
    {
      boolisfun = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[ismake]) == 0)
    {
      boolismake = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[noecho]) == 0)
    {
      boolnoecho = !!attr1->u.d;
    }
    if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[ignore]) == 0)
    {
      boolignore = !!attr1->u.d;
    }
    if (boolisfun)
    {
      if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[fun]) != 0)
      {
        my_abort();
      }
      yyshells[i].merge = !!boolembed;
      yyshells[i].ignore = !!boolignore;
      yyshells[i].noecho = !!boolnoecho;
      yyshells[i].ismake = !!boolismake;
      yyshells[i].isfun = 1;
      yyshells[i].iscode = 0;
      yyshells[i].sz = 0;
      yyshells[i].capacity = 0;
      yyshells[i].u.funarg.funidx = attr1->u.d;
      if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[arg]) != 0)
      {
        my_abort();
      }
      int64_t argidx = abce_cache_add(abce, attr1);
      yyshells[i].u.funarg.argidx = argidx;
      if (argidx < 0)
      {
        // OK, we could handle this error better, FIXME do it!
        my_abort();
      }
    }
    else if (boolembed)
    {
      size_t j;
      if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[cmds]) != 0)
      {
        my_abort();
      }
      yyshells[i].merge = 1;
      yyshells[i].iscode = 0;
      yyshells[i].isfun = 0;
      yyshells[i].ignore = !!boolignore;
      yyshells[i].noecho = !!boolnoecho;
      yyshells[i].ismake = !!boolismake;
      yyshells[i].sz = attr1->u.area->u.ar.size;
      yyshells[i].capacity = attr1->u.area->u.ar.size+1;
      yyshells[i].u.cmds = // FIXME leaks
        malloc(yyshells[i].capacity*sizeof(*yyshells[i].u.cmds));
      for (j = 0; j < attr1->u.area->u.ar.size; j++)
      {
        size_t k;
        yyshells[i].u.cmds[j] = // FIXME leaks
          malloc(  (attr1->u.area->u.ar.mbs[j].u.area->u.ar.size+1)
                 * sizeof(*yyshells[i].u.cmds[j]));
        for (k = 0; k < attr1->u.area->u.ar.mbs[j].u.area->u.ar.size; k++)
        {
          yyshells[i].u.cmds[j][k] = strdup(
            attr1->u.area->u.ar.mbs[j].u.area->u.ar.mbs[k].u.area->u.str.buf);
        }
        yyshells[i].u.cmds[j][attr1->u.area->u.ar.mbs[j].u.area->u.ar.size] =
          NULL;
      }
      yyshells[i].u.cmds[attr1->u.area->u.ar.size] = NULL;
    }
    else
    {
      size_t j;
      if (abce_tree_get_str(abce, &attr1, mb, &abce->cachebase[cmd]) != 0)
      {
        my_abort();
      }
      yyshells[i].merge = 0;
      yyshells[i].iscode = 0;
      yyshells[i].isfun = 0;
      yyshells[i].ignore = !!boolignore;
      yyshells[i].noecho = !!boolnoecho;
      yyshells[i].ismake = !!boolismake;
      yyshells[i].sz = attr1->u.area->u.ar.size;
      yyshells[i].capacity = attr1->u.area->u.ar.size+1;
      yyshells[i].u.args =
        malloc(yyshells[i].capacity*sizeof(*yyshells[i].u.args));
      for (j = 0; j < attr1->u.area->u.ar.size; j++)
      {
        yyshells[i].u.args[j] = strdup(
          attr1->u.area->u.ar.mbs[j].u.area->u.str.buf);
      }
      yyshells[i].u.args[attr1->u.area->u.ar.size] = NULL;
    }
  }

  struct cmdsrc shellsrc = {
    .itemsz = shellsz, .itemcapacity = shellsz, .items = yyshells
  };

  char *prefix_ugh = strdup(prefix); // RFE make add_rule_yy take const char*
  errcod = 0;
  abce->err.code = ABCE_E_NONE;
  if (attr_sanity(&attrstruct) != 0)
  {
    errcod = -EINVAL;
    abce->err.code = STIR_E_INSANE_RULE;
    abce->err.mb.typ = ABCE_T_N;
  }
  if (   attrstruct.iscleanhook
      || attrstruct.isdistcleanhook
      || attrstruct.isbothcleanhook)
  {
    if (tgtsz > 0)
    {
      errcod = -EINVAL;
      abce->err.code = STIR_E_INSANE_RULE;
      abce->err.mb.typ = ABCE_T_N;
    }
    else
    {
      yytgts = NULL;
    }
  }
  if (errcod == 0 &&
      add_rule_yy(main, yytgts, tgtsz, yydeps, depsz, &shellsrc,
                  attrstruct.phony, attrstruct.rectgt, attrstruct.detouch,
                  attrstruct.maybe, attrstruct.dist, attrstruct.iscleanhook,
                  attrstruct.isdistcleanhook, attrstruct.isbothcleanhook,
                  attrstruct.deponly,
                  prefix_ugh, abce->dynscope.u.area->u.sc.locidx) != 0)
  {
    errcod = -EINVAL;
  }
  if (errcod == 0 && attrstruct.iscleanhook)
  {
    stiryy_main_set_cleanhooktgt(main, prefix_ugh, "CLEAN");
  }
  if (errcod == 0 && attrstruct.isdistcleanhook)
  {
    stiryy_main_set_cleanhooktgt(main, prefix_ugh, "DISTCLEAN");
  }
  if (errcod == 0 && attrstruct.isbothcleanhook)
  {
    stiryy_main_set_cleanhooktgt(main, prefix_ugh, "BOTHCLEAN");
  }

  free(prefix_ugh);
  //free(yytgts); // Can't free this now
  //free(yydeps); // Can't free this now
  //free(yyshells); // Can't free this now
  abce_pop(abce);
  if (errcod && abce->err.code == ABCE_E_NONE)
  {
    abce->err.code = STIR_E_RULE_FROM_WITHIN_MIDRULE;
    abce->err.mb.typ = ABCE_T_N;
  }
  return errcod;
}

int stir_trap(void **pbaton, uint16_t ins, unsigned char *addcode, size_t addsz)
{
  int ret = 0;
  size_t i;
  char *prefix, *backpath;
  struct abce *abce = ABCE_CONTAINER_OF(pbaton, struct abce, trap_baton);
  struct abce_mb *mb;
  struct stiryy_main *main = *pbaton;
  switch (ins)
  {
    case STIR_OPCODE_SUFFILTER:
    {
      struct abce_mb *suf;
      struct abce_mb *bases;
      struct abce_mb *mods;
      size_t bcnt, bsz, ssz, i;
      VERIFYMB(-1, ABCE_T_S); // suffix
      VERIFYMB(-2, ABCE_T_A); // bases
      mods = abce_mb_cpush_create_array(abce);
      if (mods == NULL)
      {
        return -ENOMEM;
      }
      GETMBPTR(&suf, -1);
      GETMBPTR(&bases, -2);
      bcnt = bases->u.area->u.ar.size;
      ssz = suf->u.area->u.str.size;
      for (i = 0; i < bcnt; i++)
      {
        if (bases->u.area->u.ar.mbs[i].typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, &bases->u.area->u.ar.mbs[i]);
	  abce_cpop(abce);
          return -EINVAL;
        }
        bsz = bases->u.area->u.ar.mbs[i].u.area->u.str.size;
        if (   bsz < ssz
            || memcmp(&bases->u.area->u.ar.mbs[i].u.area->u.str.buf[bsz-ssz],
                      suf->u.area->u.str.buf, ssz) != 0)
        {
          continue;
        }
        if (abce_mb_array_append(abce, mods, &bases->u.area->u.ar.mbs[i]) != 0)
        {
          abce_pop(abce);
          abce_pop(abce);
	  abce_cpop(abce);
          return -ENOMEM;
        }
      }
      abce_npoppush(abce, 2, mods);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_SUFFILTEROUT:
    {
      struct abce_mb *suf;
      struct abce_mb *bases;
      struct abce_mb *mods;
      size_t bcnt, bsz, ssz, i;
      VERIFYMB(-1, ABCE_T_S); // suffix
      VERIFYMB(-2, ABCE_T_A); // bases
      mods = abce_mb_cpush_create_array(abce);
      if (mods == NULL)
      {
        return -ENOMEM;
      }
      GETMBPTR(&suf, -1);
      GETMBPTR(&bases, -2);
      bcnt = bases->u.area->u.ar.size;
      ssz = suf->u.area->u.str.size;
      for (i = 0; i < bcnt; i++)
      {
        if (bases->u.area->u.ar.mbs[i].typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, &bases->u.area->u.ar.mbs[i]);
	  abce_cpop(abce);
          return -EINVAL;
        }
        bsz = bases->u.area->u.ar.mbs[i].u.area->u.str.size;
        if (!(   bsz < ssz
              || memcmp(&bases->u.area->u.ar.mbs[i].u.area->u.str.buf[bsz-ssz],
                        suf->u.area->u.str.buf, ssz) != 0))
        {
          continue;
        }
        if (abce_mb_array_append(abce, mods, &bases->u.area->u.ar.mbs[i]) != 0)
        {
          abce_pop(abce);
          abce_pop(abce);
          abce_pop(abce);
	  abce_cpop(abce);
          return -ENOMEM;
        }
      }
      abce_npoppush(abce, 2, mods);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHBASENAMEALL:
    {
      struct abce_mb *bases;
      struct abce_mb *newstr;
      struct abce_mb *mods;
      size_t bcnt, bsz, i;
      VERIFYMB(-1, ABCE_T_A); // bases
      mods = abce_mb_cpush_create_array(abce);
      if (mods == NULL)
      {
        return -ENOMEM;
      }
      GETMBPTR(&bases, -1);
      bcnt = bases->u.area->u.ar.size;
      for (i = 0; i < bcnt; i++)
      {
        char *cutpoint, *buf, *cutpoint2;
        if (bases->u.area->u.ar.mbs[i].typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, &bases->u.area->u.ar.mbs[i]);
	  abce_cpop(abce);
          return -EINVAL;
        }
        bsz = bases->u.area->u.ar.mbs[i].u.area->u.str.size;
        buf = bases->u.area->u.ar.mbs[i].u.area->u.str.buf;
        cutpoint = my_memrchr(buf, '.', bsz);
        cutpoint2 = my_memrchr(buf, '/', bsz);
        if (cutpoint == NULL || cutpoint < cutpoint2)
        {
          if (abce_mb_array_append(abce, mods,
                                   &bases->u.area->u.ar.mbs[i]) != 0)
          {
	    abce_cpop(abce);
            abce_pop(abce);
            abce_pop(abce);
            return -ENOMEM;
          }
          continue;
        }
        newstr = abce_mb_cpush_create_string(abce, buf, cutpoint-buf);
        if (newstr == NULL)
        {
	  abce_cpop(abce);
          return -ENOMEM;
        }
        if (abce_mb_array_append(abce, mods, newstr) != 0)
        {
	  abce_cpop(abce);
	  abce_cpop(abce);
          abce_pop(abce);
          abce_pop(abce);
          return -ENOMEM;
        }
	abce_cpop(abce);
      }
      abce_npoppush(abce, 1, mods);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHSUFFIXALL:
    {
      struct abce_mb *bases;
      struct abce_mb *newstr;
      struct abce_mb *mods;
      size_t bcnt, bsz, i;
      VERIFYMB(-1, ABCE_T_A); // bases
      mods = abce_mb_cpush_create_array(abce);
      if (mods == NULL)
      {
        return -ENOMEM;
      }
      GETMBPTR(&bases, -1);
      bcnt = bases->u.area->u.ar.size;
      for (i = 0; i < bcnt; i++)
      {
        char *cutpoint, *buf, *cutpoint2;
        if (bases->u.area->u.ar.mbs[i].typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, &bases->u.area->u.ar.mbs[i]);
	  abce_cpop(abce);
          return -EINVAL;
        }
        bsz = bases->u.area->u.ar.mbs[i].u.area->u.str.size;
        buf = bases->u.area->u.ar.mbs[i].u.area->u.str.buf;
        cutpoint = my_memrchr(buf, '.', bsz);
        cutpoint2 = my_memrchr(buf, '/', bsz);
        if (cutpoint == NULL || cutpoint < cutpoint2)
        {
          newstr = abce_mb_cpush_create_string(abce, "", 0);
          if (newstr == NULL)
          {
	    abce_cpop(abce);
            return -ENOMEM;
          }
          if (abce_mb_array_append(abce, mods, newstr) != 0)
          {
	    abce_cpop(abce);
	    abce_cpop(abce);
            abce_pop(abce);
            abce_pop(abce);
            return -ENOMEM;
          }
	  abce_cpop(abce);
          continue;
        }
        newstr = abce_mb_cpush_create_string(abce, cutpoint, bsz-(cutpoint-buf));
        if (newstr == NULL)
        {
          return -ENOMEM;
        }
        if (abce_mb_array_append(abce, mods, newstr) != 0)
        {
          abce_pop(abce);
          abce_pop(abce);
	  abce_cpop(abce);
	  abce_cpop(abce);
          return -ENOMEM;
        }
	abce_cpop(abce);
      }
      abce_npoppush(abce, 1, mods);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHNOTDIRALL:
    {
      struct abce_mb *bases;
      struct abce_mb *newstr;
      struct abce_mb *mods;
      size_t bcnt, bsz, i;
      VERIFYMB(-1, ABCE_T_A); // bases
      mods = abce_mb_cpush_create_array(abce);
      if (mods == NULL)
      {
        return -ENOMEM;
      }
      GETMBPTR(&bases, -1);
      bcnt = bases->u.area->u.ar.size;
      for (i = 0; i < bcnt; i++)
      {
        char *cutpoint, *buf;
        if (bases->u.area->u.ar.mbs[i].typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, &bases->u.area->u.ar.mbs[i]);
	  abce_cpop(abce);
          return -EINVAL;
        }
        bsz = bases->u.area->u.ar.mbs[i].u.area->u.str.size;
        buf = bases->u.area->u.ar.mbs[i].u.area->u.str.buf;
        cutpoint = my_memrchr(buf, '/', bsz);
        if (cutpoint == NULL)
        {
          if (abce_mb_array_append(abce, mods,
                                   &bases->u.area->u.ar.mbs[i]) != 0)
          {
            abce_pop(abce);
	    abce_cpop(abce);
            return -ENOMEM;
          }
          continue;
        }
        newstr = abce_mb_cpush_create_string(abce, cutpoint+1, bsz-(cutpoint+1-buf));
        if (newstr == NULL)
        {
	  abce_cpop(abce);
          return -ENOMEM;
        }
        if (abce_mb_array_append(abce, mods, newstr) != 0)
        {
          abce_pop(abce);
	  abce_cpop(abce);
	  abce_cpop(abce);
          return -ENOMEM;
        }
	abce_cpop(abce);
      }
      abce_npoppush(abce, 1, mods);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHDIRALL:
    {
      struct abce_mb *bases;
      struct abce_mb *newstr;
      struct abce_mb *mods;
      size_t bcnt, bsz, i;
      VERIFYMB(-1, ABCE_T_A); // bases
      mods = abce_mb_cpush_create_array(abce);
      if (mods == NULL)
      {
        return -ENOMEM;
      }
      GETMBPTR(&bases, -1); // now the indices are different
      bcnt = bases->u.area->u.ar.size;
      for (i = 0; i < bcnt; i++)
      {
        char *cutpoint, *buf;
        if (bases->u.area->u.ar.mbs[i].typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, &bases->u.area->u.ar.mbs[i]);
	  abce_cpop(abce);
          return -EINVAL;
        }
        bsz = bases->u.area->u.ar.mbs[i].u.area->u.str.size;
        buf = bases->u.area->u.ar.mbs[i].u.area->u.str.buf;
        cutpoint = my_memrchr(buf, '/', bsz);
        if (cutpoint == NULL)
        {
          newstr = abce_mb_cpush_create_string(abce, "./", 2);
          if (newstr == NULL)
          {
	    abce_cpop(abce);
            return -ENOMEM;
          }
          if (abce_mb_array_append(abce, mods, newstr) != 0)
          {
            abce_pop(abce);
            abce_pop(abce);
	    abce_cpop(abce);
	    abce_cpop(abce);
            return -ENOMEM;
          }
	  abce_cpop(abce);
          continue;
        }
        newstr = abce_mb_cpush_create_string(abce, buf, cutpoint + 1 - buf);
        if (newstr == NULL)
        {
	  abce_cpop(abce);
          return -ENOMEM;
        }
        if (abce_mb_array_append(abce, mods, newstr) != 0)
        {
          abce_pop(abce);
          abce_pop(abce);
	  abce_cpop(abce);
	  abce_cpop(abce);
          return -ENOMEM;
        }
	abce_cpop(abce);
      }
      abce_npoppush(abce, 1, mods);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_JSON_IN:
    {
      struct abce_mb *base;
      struct abce_mb *mb;
      struct jsonyy jsonyy = {.abce = abce};
      int fdolddir;
      VERIFYMB(-1, ABCE_T_S); // base
      fdolddir = open(".", O_RDONLY);
      if (fdolddir < 0)
      {
        abce->err.code = STIR_E_DIR_NOT_FOUND;
        abce->err.mb.typ = ABCE_T_N;
        return -ENOTDIR;
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
      if (chdir(prefix) != 0)
      {
        abce->err.code = STIR_E_DIR_NOT_FOUND;
        abce->err.mb.typ = ABCE_T_N;
        return -ENOTDIR;
      }
      GETMBPTR(&base, -1);
      if (jsonyynameparse(base->u.area->u.str.buf, &jsonyy) != 0)
      {
        abce->err.code = STIR_E_BAD_JSON;
        abce->err.mb.typ = ABCE_T_N;
        return -EBADMSG;
      }
      if (fchdir(fdolddir) != 0)
      {
        abce->err.code = STIR_E_DIR_NOT_FOUND;
        abce->err.mb.typ = ABCE_T_N;
        return -ENOTDIR;
      }
      close(fdolddir);
      GETMBPTR(&mb, -1); // JSON parser has pushed stuff to stack
      abce_npoppush(abce, 2, mb);
      return 0;
    }
    case STIR_OPCODE_GLOB:
    {
      struct abce_mb *base;
      struct abce_mb *newstr;
      struct abce_mb *mods;
      glob_t globbuf;
      size_t i;
      int fdolddir;
      VERIFYMB(-1, ABCE_T_S); // base
      mods = abce_mb_cpush_create_array(abce);
      if (mods == NULL)
      {
        return -ENOMEM;
      }
      fdolddir = open(".", O_RDONLY);
      if (fdolddir < 0)
      {
        abce->err.code = STIR_E_DIR_NOT_FOUND;
        abce->err.mb.typ = ABCE_T_N;
	abce_cpop(abce);
        return -ENOTDIR;
      }
      globbuf.gl_offs = 0;
      if (abce_scope_get_userdata(&abce->dynscope))
      {
        prefix =
          ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prefix;
      }
      else
      {
        prefix = ".";
      }
      if (chdir(prefix) != 0)
      {
        abce->err.code = STIR_E_DIR_NOT_FOUND;
        abce->err.mb.typ = ABCE_T_N;
	abce_cpop(abce);
        return -ENOTDIR;
      }
      GETMBPTR(&base, -1);
      int ret = glob(base->u.area->u.str.buf, 0, NULL, &globbuf);
      if (ret != 0 && ret != GLOB_NOMATCH)
      {
        abce->err.code = STIR_E_GLOB_FAILED;
        abce->err.mb.typ = ABCE_T_N;
	abce_cpop(abce);
        return -EINVAL;
      }
      else if (ret == GLOB_NOMATCH)
      {
        globbuf.gl_pathc = 0;
      }
      if (fchdir(fdolddir) != 0)
      {
        abce->err.code = STIR_E_DIR_NOT_FOUND;
        abce->err.mb.typ = ABCE_T_N;
        globfree(&globbuf);
	abce_cpop(abce);
        return -ENOTDIR;
      }
      close(fdolddir);
      for (i = 0; i < globbuf.gl_pathc; i++)
      {
        newstr = abce_mb_cpush_create_string(abce, globbuf.gl_pathv[i],
                                             strlen(globbuf.gl_pathv[i]));
        if (newstr == NULL)
        {
          abce_cpop(abce);
          return -ENOMEM;
        }
        if (abce_mb_array_append(abce, mods, newstr) != 0)
        {
	  abce_cpop(abce);
          abce_cpop(abce);
          abce_pop(abce);
          return -ENOMEM;
        }
	abce_cpop(abce);
      }
      globfree(&globbuf);
      abce_npoppush(abce, 1, mods);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHSIMPLIFYALL:
    {
      struct abce_mb *bases;
      struct abce_mb *newstr;
      struct abce_mb *mods;
      size_t bcnt, bsz, i;
      VERIFYMB(-1, ABCE_T_A); // bases
      mods = abce_mb_cpush_create_array(abce);
      if (mods == NULL)
      {
        return -ENOMEM;
      }
      GETMBPTR(&bases, -1); // now the indices are different
      bcnt = bases->u.area->u.ar.size;
      for (i = 0; i < bcnt; i++)
      {
        char *can;
        if (bases->u.area->u.ar.mbs[i].typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, &bases->u.area->u.ar.mbs[i]);
          return -EINVAL;
        }
        bsz = bases->u.area->u.ar.mbs[i].u.area->u.str.size;
        can = canon(bases->u.area->u.ar.mbs[i].u.area->u.str.buf);
        newstr = abce_mb_cpush_create_string(abce, can, strlen(can));
        free(can);
        if (newstr == NULL)
        {
          return -ENOMEM;
        }
        if (abce_mb_array_append(abce, mods, newstr) != 0)
        {
          abce_pop(abce);
          abce_pop(abce);
	  abce_cpop(abce);
          return -ENOMEM;
        }
	abce_cpop(abce);
      }
      abce_npoppush(abce, 1, mods);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_SUFSUBALL:
    {
      struct abce_mb *oldsuf;
      struct abce_mb *newsuf;
      struct abce_mb *bases;
      struct abce_mb *newstr;
      struct abce_mb *mods;
      size_t bcnt, bsz, osz, nsz, i;
      VERIFYMB(-1, ABCE_T_S); // newsuffix
      VERIFYMB(-2, ABCE_T_S); // oldsuffix
      VERIFYMB(-3, ABCE_T_A); // bases
      mods = abce_mb_cpush_create_array(abce);
      if (mods == NULL)
      {
        return -ENOMEM;
      }
      GETMBPTR(&newsuf, -1);
      GETMBPTR(&oldsuf, -2);
      GETMBPTR(&bases, -3);
      bcnt = bases->u.area->u.ar.size;
      osz = oldsuf->u.area->u.str.size;
      nsz = newsuf->u.area->u.str.size;
      for (i = 0; i < bcnt; i++)
      {
        if (bases->u.area->u.ar.mbs[i].typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, &bases->u.area->u.ar.mbs[i]);
          abce_cpop(abce);
          return -EINVAL;
        }
        bsz = bases->u.area->u.ar.mbs[i].u.area->u.str.size;
        if (   bsz < osz
            || memcmp(&bases->u.area->u.ar.mbs[i].u.area->u.str.buf[bsz-osz],
                      oldsuf->u.area->u.str.buf, osz) != 0)
        {
          fprintf(stderr, "stirmake: %s does not end with %s\n",
                  bases->u.area->u.ar.mbs[i].u.area->u.str.buf,
                  oldsuf->u.area->u.str.buf);
          abce->err.code = STIR_E_SUFFIX_NOT_FOUND;
	  abce_mb_errreplace_noinline(abce, &bases->u.area->u.ar.mbs[i]);
          abce_cpop(abce);
          return -EINVAL;
        }
      }
      for (i = 0; i < bcnt; i++)
      {
        bsz = bases->u.area->u.ar.mbs[i].u.area->u.str.size;
        newstr = abce_mb_cpush_create_string_to_be_filled(abce, bsz-osz+nsz);
        if (newstr == NULL)
        {
          abce_pop(abce);
          abce_pop(abce);
          abce_pop(abce);
          abce_cpop(abce);
          return -ENOMEM;
        }
        memcpy(newstr->u.area->u.str.buf,
               bases->u.area->u.ar.mbs[i].u.area->u.str.buf, bsz-osz);
        memcpy(newstr->u.area->u.str.buf + (bsz - osz),
               newsuf->u.area->u.str.buf, nsz);
        newstr->u.area->u.str.buf[bsz-osz+nsz] = '\0';
        if (abce_mb_array_append(abce, mods, newstr) != 0)
        {
          abce_pop(abce);
          abce_pop(abce);
          abce_pop(abce);
          abce_cpop(abce);
	  abce_cpop(abce);
          return -ENOMEM;
        }
	abce_cpop(abce);
      }
      abce_npoppush(abce, 3, mods);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHSIMPLIFY:
    {
      struct abce_mb *base;
      struct abce_mb *newstr;
      char *can;
      VERIFYMB(-1, ABCE_T_S); // base
      GETMBPTR(&base, -1);

      can = canon(base->u.area->u.str.buf); // RFE '\0' in filename
      newstr = abce_mb_cpush_create_string(abce, can, strlen(can));
      free(can);
      if (newstr == NULL)
      {
        abce_pop(abce);
        return -ENOMEM;
      }
      abce_npoppush(abce, 1, newstr);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHBASENAME:
    {
      struct abce_mb *base;
      struct abce_mb *newstr;
      size_t bsz;
      char *cutpoint, *cutpoint2;
      VERIFYMB(-1, ABCE_T_S); // base
      GETMBPTR(&base, -1);
      bsz = base->u.area->u.str.size;

      cutpoint = my_memrchr(base->u.area->u.str.buf, '.', bsz);
      cutpoint2 = my_memrchr(base->u.area->u.str.buf, '/', bsz);
      if (cutpoint == NULL || cutpoint < cutpoint2)
      {
        abce_npoppush(abce, 1, base);
        return 0;
      }

      newstr = abce_mb_cpush_create_string(abce, base->u.area->u.str.buf,
                                           cutpoint - base->u.area->u.str.buf);
      if (newstr == NULL)
      {
        abce_pop(abce);
        return -ENOMEM;
      }
      abce_npoppush(abce, 1, newstr);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHDIR:
    {
      struct abce_mb *base;
      struct abce_mb *newstr;
      size_t bsz;
      char *cutpoint;
      VERIFYMB(-1, ABCE_T_S); // base
      GETMBPTR(&base, -1);
      bsz = base->u.area->u.str.size;

      cutpoint = my_memrchr(base->u.area->u.str.buf, '/', bsz);
      if (cutpoint == NULL)
      {
        newstr = abce_mb_cpush_create_string(abce, "./", 2);
        if (newstr == NULL)
        {
	  abce_pop(abce);
          return -ENOMEM;
        }
        abce_npoppush(abce, 1, newstr);
        abce_cpop(abce);
        return 0;
      }

      newstr = abce_mb_cpush_create_string(abce, base->u.area->u.str.buf,
                                           cutpoint + 1 - base->u.area->u.str.buf);
      if (newstr == NULL)
      {
        abce_pop(abce);
        return -ENOMEM;
      }
      abce_npoppush(abce, 1, newstr);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHNOTDIR:
    {
      struct abce_mb *base;
      struct abce_mb *newstr;
      size_t bsz;
      char *cutpoint;
      VERIFYMB(-1, ABCE_T_S); // base
      GETMBPTR(&base, -1);
      bsz = base->u.area->u.str.size;

      cutpoint = my_memrchr(base->u.area->u.str.buf, '/', bsz);
      if (cutpoint == NULL)
      {
        abce_npoppush(abce, 1, base);
        return 0;
      }

      newstr = abce_mb_cpush_create_string(abce, cutpoint+1,
                                           bsz - (cutpoint+1-base->u.area->u.str.buf));
      if (newstr == NULL)
      {
        abce_pop(abce);
        return -ENOMEM;
      }
      abce_npoppush(abce, 1, newstr);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_PATHSUFFIX:
    {
      struct abce_mb *base;
      struct abce_mb *newstr;
      size_t bsz;
      char *cutpoint, *cutpoint2;
      VERIFYMB(-1, ABCE_T_S); // base
      GETMBPTR(&base, -1);
      bsz = base->u.area->u.str.size;

      cutpoint = my_memrchr(base->u.area->u.str.buf, '.', bsz);
      cutpoint2 = my_memrchr(base->u.area->u.str.buf, '/', bsz);
      if (cutpoint == NULL || cutpoint < cutpoint2)
      {
        newstr = abce_mb_cpush_create_string(abce, "", 0);
        if (newstr == NULL)
        {
          return -ENOMEM;
        }
	abce_npoppush(abce, 1, newstr);
	abce_cpop(abce);
        return 0;
      }

      newstr = abce_mb_cpush_create_string(abce, cutpoint,
                                           bsz - (cutpoint-base->u.area->u.str.buf));
      if (newstr == NULL)
      {
        abce_pop(abce);
        return -ENOMEM;
      }
      abce_npoppush(abce, 1, newstr);
      abce_cpop(abce);
      return 0;
    }
    case STIR_OPCODE_SUFSUBONE:
    {
      struct abce_mb *oldsuf;
      struct abce_mb *newsuf;
      struct abce_mb *base;
      struct abce_mb *newstr;
      size_t bsz, osz, nsz;
      VERIFYMB(-1, ABCE_T_S); // newsuffix
      VERIFYMB(-2, ABCE_T_S); // oldsuffix
      VERIFYMB(-3, ABCE_T_S); // base
      GETMBPTR(&newsuf, -1);
      GETMBPTR(&oldsuf, -2);
      GETMBPTR(&base, -3);
      bsz = base->u.area->u.str.size;
      osz = oldsuf->u.area->u.str.size;
      nsz = newsuf->u.area->u.str.size;
      if (   bsz < osz
          || memcmp(&base->u.area->u.str.buf[bsz-osz], oldsuf->u.area->u.str.buf,
                    osz) != 0)
      {
        fprintf(stderr, "stirmake: %s does not end with %s\n",
                base->u.area->u.str.buf, oldsuf->u.area->u.str.buf);
        abce->err.code = STIR_E_SUFFIX_NOT_FOUND;
	abce_mb_errreplace_noinline(abce, base);
        return -EINVAL;
      }
      newstr = abce_mb_cpush_create_string_to_be_filled(abce, bsz-osz+nsz);
      if (newstr == NULL)
      {
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -ENOMEM;
      }
      memcpy(newstr->u.area->u.str.buf, base->u.area->u.str.buf, bsz-osz);
      memcpy(newstr->u.area->u.str.buf + (bsz - osz),
             newsuf->u.area->u.str.buf, nsz);
      newstr->u.area->u.str.buf[bsz-osz+nsz] = '\0';
      abce_npoppush(abce, 3, newstr);
      abce_cpop(abce);
      return 0;
    }
    // FIXME what if there are many deps? Is it added for all rules?
    case STIR_OPCODE_DEP_ADD:
    {
      struct abce_mb *depar;
      struct abce_mb *tgtar;
      struct abce_mb *tree;
      struct abce_mb *orderonly;
      struct abce_mb *rec;
      struct abce_mb *wait;
      struct abce_mb *orderonlyres = NULL;
      struct abce_mb *recres = NULL;
      struct abce_mb *waitres = NULL;
      if (abce_scope_get_userdata(&abce->dynscope))
      {
        prefix =
          ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prefix;
      }
      else
      {
        prefix = ".";
      }
      VERIFYMB(-1, ABCE_T_T);
      VERIFYMB(-2, ABCE_T_A);
      VERIFYMB(-3, ABCE_T_A);
      GETMBPTR(&tree, -1);
      GETMBPTR(&depar, -2);
      GETMBPTR(&tgtar, -3);

      for (i = 0; i < depar->u.area->u.ar.size; i++)
      {
        const struct abce_mb *mb = &depar->u.area->u.ar.mbs[i];
        if (mb->typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, mb);
          return -EINVAL;
        }
      }
      for (i = 0; i < tgtar->u.area->u.ar.size; i++)
      {
        const struct abce_mb *mb = &tgtar->u.area->u.ar.mbs[i];
        if (mb->typ != ABCE_T_S)
        {
          abce->err.code = ABCE_E_EXPECT_STR;
	  abce_mb_errreplace_noinline(abce, mb);
          return -EINVAL;
        }
      }

      rec = abce_mb_cpush_create_string_nul(abce, "rec");
      if (rec == NULL)
      {
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -ENOMEM;
      }
      if (abce_tree_get_str(abce, &recres, tree, rec) != 0)
      {
        recres = NULL;
      }
      abce_cpop(abce);

      orderonly = abce_mb_cpush_create_string_nul(abce, "orderonly");
      if (orderonly == NULL)
      {
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -ENOMEM;
      }
      if (abce_tree_get_str(abce, &orderonlyres, tree, orderonly) != 0)
      {
        orderonlyres = NULL;
      }
      abce_cpop(abce);

      wait = abce_mb_cpush_create_string_nul(abce, "wait");
      if (wait == NULL)
      {
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -ENOMEM;
      }
      if (abce_tree_get_str(abce, &waitres, tree, wait) != 0)
      {
        waitres = NULL;
      }
      abce_cpop(abce);

      if (recres && recres->typ != ABCE_T_B && recres->typ != ABCE_T_D)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
	abce_mb_errreplace_noinline(abce, recres);
        //abce_mb_refdn(abce, &recres);
        //abce_mb_refdn(abce, &orderonlyres);
        //abce_mb_refdn(abce, &waitres);
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -EINVAL;
      }
      if (orderonlyres && orderonlyres->typ != ABCE_T_B && orderonlyres->typ != ABCE_T_D)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
	abce_mb_errreplace_noinline(abce, orderonlyres);
        //abce_mb_refdn(abce, &recres);
        //abce_mb_refdn(abce, &orderonlyres);
        //abce_mb_refdn(abce, &waitres);
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -EINVAL;
      }
      if (waitres && waitres->typ != ABCE_T_B && waitres->typ != ABCE_T_D)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
	abce_mb_errreplace_noinline(abce, waitres);
        //abce_mb_refdn(abce, &recres);
        //abce_mb_refdn(abce, &orderonlyres);
        //abce_mb_refdn(abce, &waitres);
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -EINVAL;
      }

      if (!main->parsing)
      {
        char **tgts = malloc(sizeof(*tgts) * tgtar->u.area->u.ar.size);
        char **deps = malloc(sizeof(*deps) * depar->u.area->u.ar.size);
        for (i = 0; i < depar->u.area->u.ar.size; i++)
        {
          const struct abce_mb *mb = &depar->u.area->u.ar.mbs[i];
          deps[i] = mb->u.area->u.str.buf;
        }
        for (i = 0; i < tgtar->u.area->u.ar.size; i++)
        {
          const struct abce_mb *mb = &tgtar->u.area->u.ar.mbs[i];
          tgts[i] = mb->u.area->u.str.buf;
        }
        if (add_dep_after_parsing_stage(tgts, tgtar->u.area->u.ar.size,
                                        deps, depar->u.area->u.ar.size,
                                        prefix,
                                        recres && recres->u.d != 0,
                                        orderonlyres && orderonlyres->u.d != 0,
                                        waitres && waitres->u.d != 0)
            != 0)
        {
          abce->err.code = STIR_E_RULE_NOT_FOUND;
          abce->err.mb.typ = ABCE_T_N;
          return -ENOENT;
        }
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return 0;
      }

      stiryy_main_emplace_rule(main, prefix, abce->dynscope.u.area->u.sc.locidx);
      for (i = 0; i < tgtar->u.area->u.ar.size; i++)
      {
        const struct abce_mb *mb = &tgtar->u.area->u.ar.mbs[i];
        stiryy_main_set_tgt(main, prefix, mb->u.area->u.str.buf, 0);
      }
      for (i = 0; i < depar->u.area->u.ar.size; i++)
      {
        const struct abce_mb *mb = &depar->u.area->u.ar.mbs[i];
        stiryy_main_set_dep(main, prefix, mb->u.area->u.str.buf, recres && recres->u.d != 0, orderonlyres && orderonlyres->u.d != 0, waitres && waitres->u.d != 0);
      }
      stiryy_main_mark_deponly(main);

      //abce_mb_refdn(abce, &recres);
      //abce_mb_refdn(abce, &orderonlyres);
      abce_pop(abce);
      abce_pop(abce);
      abce_pop(abce);
      return 0;
    }
    case STIR_OPCODE_RULE_ADD:
      if (!main->parsing)
      {
        abce->err.code = STIR_E_RULECHANGE_NOT_PERMITTED;
        abce->err.mb.typ = ABCE_T_N;
        return -EACCES;
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
      return stir_trap_ruleadd(main, abce, prefix);
    case STIR_OPCODE_TOP_DIR:
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
      mb = abce_mb_cpush_create_string(abce, backpath, strlen(backpath));
      free(backpath);
      if (mb == NULL)
      {
        return -ENOMEM;
      }
      if (abce_push_mb(abce, mb) != 0)
      {
        abce->err.code = ABCE_E_STACK_OVERFLOW;
	abce_mb_errreplace_noinline(abce, mb);
        abce_cpop(abce);
        return -EOVERFLOW;
      }
      abce_cpop(abce);
      return 0;
    case STIR_OPCODE_CUR_DIR_FROM_TOP:
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
      mb = abce_mb_cpush_create_string(abce, prefix, strlen(prefix));
      if (mb == NULL)
      {
        return -ENOMEM;
      }
      if (abce_push_mb(abce, mb) != 0)
      {
        abce->err.code = ABCE_E_STACK_OVERFLOW;
	abce_mb_errreplace_noinline(abce, mb);
        abce_cpop(abce);
        return -EOVERFLOW;
      }
      abce_cpop(abce);
      return 0;
    case STIR_OPCODE_SHELL_ESCAPE:
      {
        struct abce_mb *mbarg, *mbnew;
        size_t newsz;
        char *newstr;
        GETMBSTRPTR(&mbarg, -1);
        newstr = stir_shellescape(mbarg->u.area->u.str.buf,
                                  mbarg->u.area->u.str.size, &newsz);
        if (newstr == NULL)
        {
          return -ENOMEM;
        }
        mbnew = abce_mb_cpush_create_string(abce, newstr, newsz);
        if (mbnew == NULL)
        {
          free(newstr);
          return -ENOMEM;
        }
	abce_npoppush(abce, 1, mbnew);
	abce_cpop(abce);
        free(newstr);
        return 0;
      }
    default:
      abce->err.code = ABCE_E_UNKNOWN_INSTRUCTION;
      abce->err.mb.typ = ABCE_T_D;
      abce->err.mb.u.d = ins;
      return -EILSEQ;
  }
  return ret;
}
