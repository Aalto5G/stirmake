#include <stdint.h>
#include "abce/abce.h"
#include "abce/abcetrees.h"
#include "stiropcodes.h"
#include "stirtrap.h"
#include "canon.h"
#include "stiryy.h"

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

#define GETMB(mb, idx) GETGENERIC(abce_getmb, mb, idx)
#define GETMBAR(mb, idx) GETGENERIC(abce_getmbar, mb, idx)

int stir_trap(void **pbaton, uint16_t ins, unsigned char *addcode, size_t addsz)
{
  int ret = 0;
  size_t i;
  char *prefix, *backpath;
  struct abce *abce = ABCE_CONTAINER_OF(pbaton, struct abce, trap_baton);
  struct abce_mb mb = {};
  struct stiryy_main *main = *pbaton;
  switch (ins)
  {
    case STIR_OPCODE_SUFSUBONE:
    {
      struct abce_mb oldsuf = {};
      struct abce_mb newsuf = {};
      struct abce_mb base = {};
      struct abce_mb newstr = {};
      size_t bsz, osz, nsz;
      VERIFYMB(-1, ABCE_T_S); // newsuffix
      VERIFYMB(-2, ABCE_T_S); // oldsuffix
      VERIFYMB(-3, ABCE_T_S); // base
      GETMB(&newsuf, -1);
      GETMB(&oldsuf, -2);
      GETMB(&base, -3);
      bsz = base.u.area->u.str.size;
      osz = oldsuf.u.area->u.str.size;
      nsz = newsuf.u.area->u.str.size;
      if (   bsz < osz
          || memcmp(&base.u.area->u.str.buf[bsz-osz], oldsuf.u.area->u.str.buf,
                    osz) != 0)
      {
        fprintf(stderr, "stirmake: %s does not end with %s\n",
                base.u.area->u.str.buf, oldsuf.u.area->u.str.buf);
        abce->err.code = STIR_E_SUFFIX_NOT_FOUND;
        abce->err.mb = abce_mb_refup(abce, &base);
        abce_mb_refdn(abce, &oldsuf);
        abce_mb_refdn(abce, &newsuf);
        abce_mb_refdn(abce, &base);
        return -EINVAL;
      }
      newstr = abce_mb_create_string_to_be_filled(abce, bsz-osz+nsz);
      if (newstr.typ == ABCE_T_N)
      {
        abce_mb_refdn(abce, &oldsuf);
        abce_mb_refdn(abce, &newsuf);
        abce_mb_refdn(abce, &base);
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -ENOMEM;
      }
      memcpy(newstr.u.area->u.str.buf, base.u.area->u.str.buf, bsz-osz);
      memcpy(newstr.u.area->u.str.buf + (bsz - osz),
             newsuf.u.area->u.str.buf, nsz);
      newstr.u.area->u.str.buf[bsz-osz+nsz] = '\0';
      abce_pop(abce);
      abce_pop(abce);
      abce_pop(abce);
      if (abce_push_mb(abce, &newstr) != 0)
      {
        my_abort();
      }
      abce_mb_refdn(abce, &oldsuf);
      abce_mb_refdn(abce, &newsuf);
      abce_mb_refdn(abce, &base);
      abce_mb_refdn(abce, &newstr);
      return 0;
    }
    // FIXME what if there are many deps? Is it added for all rules?
    case STIR_OPCODE_DEP_ADD:
    {
      struct abce_mb depar = {};
      struct abce_mb tgtar = {};
      struct abce_mb tree = {};
      struct abce_mb orderonly = {};
      struct abce_mb rec = {};
      struct abce_mb *orderonlyres = NULL;
      struct abce_mb *recres = NULL;
      if (!main->parsing)
      {
        fprintf(stderr, "stirmake: trying to add dep after parsing stage\n");
        abce->err.code = STIR_E_RULECHANGE_NOT_PERMITTED;
        return -EINVAL;
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
      VERIFYMB(-1, ABCE_T_T);
      VERIFYMB(-2, ABCE_T_A);
      VERIFYMB(-3, ABCE_T_A);
      GETMB(&tree, -1);
      GETMB(&depar, -2);
      GETMB(&tgtar, -3);
      for (i = 0; i < depar.u.area->u.ar.size; i++)
      {
        const struct abce_mb *mb = &depar.u.area->u.ar.mbs[i];
        if (mb->typ != ABCE_T_S)
        {
          abce_mb_refdn(abce, &depar);
          abce_mb_refdn(abce, &tgtar);
          abce_mb_refdn(abce, &tree);
          abce->err.code = ABCE_E_EXPECT_STR;
          abce->err.mb = abce_mb_refup(abce, mb);
          return -EINVAL;
        }
      }
      for (i = 0; i < tgtar.u.area->u.ar.size; i++)
      {
        const struct abce_mb *mb = &tgtar.u.area->u.ar.mbs[i];
        if (mb->typ != ABCE_T_S)
        {
          abce_mb_refdn(abce, &depar);
          abce_mb_refdn(abce, &tgtar);
          abce_mb_refdn(abce, &tree);
          abce->err.code = ABCE_E_EXPECT_STR;
          abce->err.mb = abce_mb_refup(abce, mb);
          return -EINVAL;
        }
      }

      rec = abce_mb_create_string_nul(abce, "rec");
      if (rec.typ == ABCE_T_N)
      {
        abce_mb_refdn(abce, &depar);
        abce_mb_refdn(abce, &tgtar);
        abce_mb_refdn(abce, &tree);
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -ENOMEM;
      }
      if (abce_tree_get_str(abce, &recres, &tree, &rec) != 0)
      {
        recres = NULL;
      }
      abce_mb_refdn(abce, &rec);

      orderonly = abce_mb_create_string_nul(abce, "orderonly");
      if (orderonly.typ == ABCE_T_N)
      {
        abce_mb_refdn(abce, &depar);
        abce_mb_refdn(abce, &tgtar);
        abce_mb_refdn(abce, &tree);
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -ENOMEM;
      }
      if (abce_tree_get_str(abce, &orderonlyres, &tree, &orderonly) != 0)
      {
        orderonlyres = NULL;
      }
      abce_mb_refdn(abce, &orderonly);
      if (recres && recres->typ != ABCE_T_B && recres->typ != ABCE_T_D)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb = abce_mb_refup(abce, recres);
        //abce_mb_refdn(abce, &recres);
        //abce_mb_refdn(abce, &orderonlyres);
        abce_mb_refdn(abce, &depar);
        abce_mb_refdn(abce, &tgtar);
        abce_mb_refdn(abce, &tree);
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -EINVAL;
      }
      if (orderonlyres && orderonlyres->typ != ABCE_T_B && orderonlyres->typ != ABCE_T_D)
      {
        abce->err.code = ABCE_E_EXPECT_BOOL;
        abce->err.mb = abce_mb_refup(abce, orderonlyres);
        //abce_mb_refdn(abce, &recres);
        //abce_mb_refdn(abce, &orderonlyres);
        abce_mb_refdn(abce, &depar);
        abce_mb_refdn(abce, &tgtar);
        abce_mb_refdn(abce, &tree);
        abce_pop(abce);
        abce_pop(abce);
        abce_pop(abce);
        return -EINVAL;
      }

      stiryy_main_emplace_rule(main, prefix, abce->dynscope.u.area->u.sc.locidx);
      for (i = 0; i < tgtar.u.area->u.ar.size; i++)
      {
        const struct abce_mb *mb = &tgtar.u.area->u.ar.mbs[i];
        stiryy_main_set_tgt(main, prefix, mb->u.area->u.str.buf);
      }
      for (i = 0; i < depar.u.area->u.ar.size; i++)
      {
        const struct abce_mb *mb = &depar.u.area->u.ar.mbs[i];
        stiryy_main_set_dep(main, prefix, mb->u.area->u.str.buf, recres && recres->u.d != 0, orderonlyres && orderonlyres->u.d != 0);
      }
      stiryy_main_mark_deponly(main);

      //abce_mb_refdn(abce, &recres);
      //abce_mb_refdn(abce, &orderonlyres);
      abce_mb_refdn(abce, &depar);
      abce_mb_refdn(abce, &tgtar);
      abce_mb_refdn(abce, &tree);
      abce_pop(abce);
      abce_pop(abce);
      abce_pop(abce);
      return 0;
    }
    case STIR_OPCODE_RULE_ADD:
      if (abce_scope_get_userdata(&abce->dynscope))
      {
        prefix =
          ((struct scope_ud*)abce_scope_get_userdata(&abce->dynscope))->prefix;
      }
      else
      {
        prefix = ".";
      }
      return -EILSEQ;
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
      mb = abce_mb_create_string(abce, backpath, strlen(backpath));
      free(backpath);
      if (mb.typ == ABCE_T_N)
      {
        return -ENOMEM;
      }
      if (abce_push_mb(abce, &mb) != 0)
      {
        abce->err.code = ABCE_E_STACK_OVERFLOW;
        abce->err.mb = abce_mb_refup_noinline(abce, &mb);
        return -EOVERFLOW;
      }
      abce_mb_refdn(abce, &mb);
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
      mb = abce_mb_create_string(abce, prefix, strlen(prefix));
      if (mb.typ == ABCE_T_N)
      {
        return -ENOMEM;
      }
      if (abce_push_mb(abce, &mb) != 0)
      {
        abce->err.code = ABCE_E_STACK_OVERFLOW;
        abce->err.mb = abce_mb_refup_noinline(abce, &mb);
        return -EOVERFLOW;
      }
      abce_mb_refdn(abce, &mb);
      return 0;
    default:
      abce->err.code = ABCE_E_UNKNOWN_INSTRUCTION;
      abce->err.mb.typ = ABCE_T_D;
      abce->err.mb.u.d = ins;
      return -EILSEQ;
  }
  return ret;
}
