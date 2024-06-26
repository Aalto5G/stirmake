%option nounput noinput
%option prefix="incyy"

%{
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include "yyutils.h"
#include "incyy.tab.h"

/*
#define YYSTYPE INCYYSTYPE
#define YYLTYPE INCYYLTYPE
*/

/*
#define YY_USER_ACTION yylloc->first_line = yylloc->last_line = yylineno; \
  yylloc->first_column = yycolumn; yylloc->last_column = yycolumn+yyleng-1; \
  yycolumn += yyleng;
*/
#define YY_USER_ACTION yycolumn += yyleng;

%}

%option reentrant bison-bridge

%%
  int last_newline_true = 1;

(\\[^\n]|[^ \t\n!:(\\#$=;])[^ \t\n!:(\\#$=;]*(\\(.|\n)[^ \t\n!:(\\#$=;]*)* {
  /*
    Ok, the past regexp might require some explanation.

    To start a free-form token, we need either:
    (A) an escaped character apart from newline (newline-escape begins a new 
        line without starting a token)
    (B) a character belonging to the permitted set which has everything except:
         (1) space
         (2) tab
         (3) newline
         (4) exclamation mark as it's special for some make rules
         (5) colon as it separates targets from prerequisites
         (6) beginning parenthesis as it looks into ar files on some platforms
         (7) backslash as it's the escape character
         (8) hash sign as it's the comment start character
         (9) dollar as it's used for variables
        (10) equality sign as it's variable assignment operator
        (11) semicolon as it begins a shell command inline with the rule

    Then we may have any number of permitted characters. After that, we may have
    escaped characters including newline and permitted characters in any
    configuration.
   */
  size_t idx;
  size_t len;
  char *cp;

  //len = strlen(yytext);
  len = yyleng;

  yylval->s = malloc(len+1);
  cp = yylval->s;

  // Re-count yylineno and yycolumn
  //if (memchr(yytext, '\n', len))
  if (strchr(yytext, '\n'))
  {
    yycolumn -= len;
    for (idx = 0; idx < len; idx++)
    {
      if (yytext[idx] == '\n')
      {
        ++yylineno;
        yycolumn=1;
        last_newline_true = 0;
      }
      else
      {
        ++yycolumn;
      }
    }
  }
#if 0
  char *in = yytext;
  char *natural_end = in + len;
  while (*in)
  {
    //char *end = memchr(in, '\\', natural_end - in);
    char *end = strchr(in, '\\');
    size_t seglen;
    if (end == NULL)
    {
      seglen = natural_end - in;
      memcpy(cp, in, seglen);
      cp += seglen;
      break;
      //end = natural_end;
    }
    seglen = end - in;
    memcpy(cp, in, seglen);
    cp += seglen;
    in += seglen;
    while (*in == '\\')
    {
      if (idx+1 >= len)
      {
        my_abort();
      }
      *cp++ = *++in;
      in++;
    }
  }
#else
  // Unescape string
  for (idx = 0; idx < len; idx++)
  {
    if (yytext[idx] == '\\')
    {
      if (idx+1 >= len)
      {
        my_abort();
      }
      *cp++ = yytext[++idx];
    }
    else
    {
      *cp++ = yytext[idx];
    }
  }
#endif
  *cp++ = '\0';

  //printf("Returning free-form token\n");

  return FREEFORM_TOKEN;
}

:         return COLON;

#[^\n]*  /* ignore comments */;

\n                                { ++yylineno; yycolumn=1; last_newline_true = 1; return NEWLINE; }
\\\n                              { ++yylineno; yycolumn=1; last_newline_true = 0;}
\t {
  if (yycolumn == 2 && last_newline_true)
  {
    // Make include-only files do not support commands
    return ERROR_TOK;
  }
  else
  {
    // ignore
  }
}
[ \t]                            /* ignore whitespaces */;
.            return ERROR_TOK;


%%
