#include <stdio.h>
#include <stdlib.h>
#include "yyutils.h"
#include "stiryy.h"

size_t symbol_add(struct stiryy *stiryy, const char *symbol, size_t symlen)
{
  abort();
}
#if 0
size_t stiryy_add_fun_sym(struct stiryy *stiryy, const char *symbol, int maybe, size_t loc)
{
  abort();
}
#endif

int main(int argc, char **argv)
{
  FILE *f = fopen("Stirfile", "r");
  struct abce abce;
  struct stiryy_main main = {.abce = &abce};
  struct stiryy stiryy = {};
  abce_init(&abce);
  stiryy_init(&stiryy, &main, ".", ".", abce.dynscope);
  if (!f)
  {
    abort();
  }
  stiryydoparse(f, &stiryy);
  fclose(f);
}
