/*
%code requires {
#ifndef YY_TYPEDEF_YY_SCANNER_T
#define YY_TYPEDEF_YY_SCANNER_T
typedef void *yyscan_t;
#endif
#include "stiryy.h"
#include <sys/types.h>
}

%define api.prefix {stiryy}
*/

%{

/*
#define YYSTYPE STIRYYSTYPE
#define YYLTYPE STIRYYLTYPE
*/

#include "stiryy.h"
#include "yyutils.h"
#include "stiryy.tab.h"
#include "stiryy.lex.h"
#include "opcodesonly.h"
#include "abce/amyplanyy.h"
#include "abce/amyplanyyutils.h"
#include "abce/abceopcodes.h"
#include "abce/amyplanlocvarctx.h"
#include <arpa/inet.h>

void stiryyerror(/*YYLTYPE *yylloc,*/ yyscan_t scanner, struct stiryy *stiryy, const char *str)
{
        //fprintf(stderr, "error: %s at line %d col %d\n",str, yylloc->first_line, yylloc->first_column);
        // FIXME we need better location info!
        fprintf(stderr, "stir error: %s at line %d col %d\n", str, stiryyget_lineno(scanner), stiryyget_column(scanner));
}

int stiryywrap(yyscan_t scanner)
{
        return 1;
}

%}

%pure-parser
%lex-param {yyscan_t scanner}
%parse-param {yyscan_t scanner}
%parse-param {struct stiryy *stiryy}
/* %locations */

%union {
  int i;
  double d;
  char *s;
  struct escaped_string str;
  struct {
    int i;
    char *s;
  } both;
  struct {
    uint8_t has_i:1;
    uint8_t has_prio:1;
    int prio;
  } tokenoptstmp;
  struct {
    uint8_t i:1;
    int prio;
  } tokenopts;
}

/*
%destructor { free ($$.str); } STRING_LITERAL
%destructor { free ($$); } FREEFORM_TOKEN
%destructor { free ($$); } VARREF_LITERAL
%destructor { free ($$); } SHELL_COMMAND
%destructor { free ($$); } PERCENTLUA_LITERAL
*/


%token <s> PERCENTLUA_LITERAL
%token OPEN_BRACKET
%token CLOSE_BRACKET
%token OPEN_BRACE
%token CLOSE_BRACE
%token OPEN_PAREN
%token CLOSE_PAREN

%token <s> SHELL_COMMAND
%token ATTAB

%token NEWLINE

%token EQUALS
%token PLUSEQUALS
%token QMEQUALS
%token COLONEQUALS
%token PLUSCOLONEQUALS
%token QMCOLONEQUALS
%token COLON
%token COMMA
%token <str> STRING_LITERAL
%token <d> NUMBER
%token <s> VARREF_LITERAL
%token <s> FREEFORM_TOKEN
%token MAYBE_CALL
%token LT
%token GT
%token LE
%token GE
%token AT
%token FUNCTION
%token ENDFUNCTION
%token LOCVAR
%token RECDEP

%token DELAYVAR
%token DELAYEXPR
%token DELAYLISTEXPAND
%token SUFFILTER
%token SUFSUBONE
%token STRAPPEND
%token SUFSUB
%token PHONYRULE
%token DISTRULE
%token PATRULE
%token FILEINCLUDE
%token DIRINCLUDE
%token CDEPINCLUDESCURDIR
%token DYNO
%token LEXO
%token IMMO
%token DYN
%token LEX
%token IMM
%token D
%token L
%token I
%token DO
%token LO
%token IO
%token LOC
%token APPEND
%token APPEND_LIST
%token RETURN
%token ADD_RULE
%token RULE_DIST
%token RULE_PHONY
%token RULE_ORDINARY
%token PRINT


%token IF
%token ENDIF
%token WHILE
%token ENDWHILE
%token BREAK
%token CONTINUE

%token DIV MUL ADD SUB SHL SHR NE EQ LOGICAL_AND LOGICAL_OR LOGICAL_NOT MOD BITWISE_AND BITWISE_OR BITWISE_NOT BITWISE_XOR


%token ERROR_TOK

%type<d> value
%type<d> valuelistentry
%type<d> maybeqmequals
%type<d> maybe_rec

%start st

%%

st: stirrules;

stirrules:
| stirrules stirrule
| stirrules NEWLINE
| stirrules assignrule
| stirrules PRINT NUMBER NEWLINE
{
  printf("%g\n", $3);
}
| stirrules FILEINCLUDE STRING_LITERAL NEWLINE
{
  free($3.str);
}
| stirrules FILEINCLUDE varref NEWLINE
| stirrules DIRINCLUDE STRING_LITERAL NEWLINE
{
  free($3.str);
}
| stirrules DIRINCLUDE varref NEWLINE
| stirrules CDEPINCLUDESCURDIR STRING_LITERAL NEWLINE
{
  stiryy_set_cdepinclude(stiryy, $3.str);
  free($3.str);
}
| stirrules CDEPINCLUDESCURDIR varref NEWLINE
| stirrules FUNCTION VARREF_LITERAL
{
  stiryy->ctx = amyplan_locvarctx_alloc(NULL, 2, (size_t)-1, (size_t)-1);
}
OPEN_PAREN maybe_parlist CLOSE_PAREN NEWLINE
{
  size_t funloc = stiryy->main->abce->bytecodesz;
  stiryy_add_fun_sym(stiryy, $3, 0, funloc);
  stiryy_add_byte(stiryy, ABCE_OPCODE_FUN_HEADER);
  stiryy_add_double(stiryy, stiryy->ctx->args);
}
  funlines
  ENDFUNCTION NEWLINE
{
  stiryy_add_byte(stiryy, ABCE_OPCODE_PUSH_NIL); // retval
  stiryy_add_byte(stiryy, ABCE_OPCODE_PUSH_DBL);
  stiryy_add_double(stiryy, stiryy->ctx->args); // argcnt
  stiryy_add_byte(stiryy, ABCE_OPCODE_PUSH_DBL);
  stiryy_add_double(stiryy, stiryy->ctx->sz - stiryy->ctx->args); // locvarcnt
  stiryy_add_byte(stiryy, ABCE_OPCODE_RETEX2);
  stiryy_add_byte(stiryy, ABCE_OPCODE_FUN_TRAILER);
  stiryy_add_double(stiryy, stiryy_symbol_add(stiryy, $3, strlen($3)));
  free($3);
  amyplan_locvarctx_free(stiryy->ctx);
  stiryy->ctx = NULL;
}
;

maybe_parlist:
| parlist
;

parlist:
VARREF_LITERAL
{ free($1); }
| parlist COMMA VARREF_LITERAL
{ free($3); }
;

funlines:
  locvarlines
  bodylines
;

locvarlines:
| locvarlines LOCVAR VARREF_LITERAL EQUALS expr NEWLINE
{
  free($3);
}
;

bodylines:
| bodylines statement
;

statement:
  lvalue EQUALS expr NEWLINE
| RETURN expr NEWLINE
| BREAK NEWLINE
| CONTINUE NEWLINE
| ADD_RULE OPEN_PAREN expr CLOSE_PAREN NEWLINE
| expr NEWLINE
| IF OPEN_PAREN expr CLOSE_PAREN NEWLINE
  bodylines
  ENDIF NEWLINE
| WHILE OPEN_PAREN expr CLOSE_PAREN NEWLINE
  bodylines
  ENDWHILE NEWLINE
;

lvalue:
  varref
| varref maybe_bracketexprlist OPEN_BRACKET expr CLOSE_BRACKET
| DYN OPEN_BRACKET expr CLOSE_BRACKET
| DYN OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist OPEN_BRACKET expr CLOSE_BRACKET
| LEX OPEN_BRACKET expr CLOSE_BRACKET
| LEX OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist OPEN_BRACKET expr CLOSE_BRACKET
| OPEN_PAREN expr CLOSE_PAREN maybe_bracketexprlist OPEN_BRACKET expr CLOSE_BRACKET
;

maybe_bracketexprlist:
| maybe_bracketexprlist OPEN_BRACKET expr CLOSE_BRACKET
;

maybeqmequals: EQUALS {$$ = 0;} | QMEQUALS {$$ = 1;} ;

assignrule:
VARREF_LITERAL QMCOLONEQUALS expr NEWLINE
{
  free($1);
  printf("not implemented yet\n");
  YYABORT;
}
| VARREF_LITERAL COLONEQUALS expr NEWLINE
{
  free($1);
  printf("not implemented yet\n");
  YYABORT;
}
| VARREF_LITERAL PLUSCOLONEQUALS expr NEWLINE
{
  free($1);
  printf("not implemented yet\n");
  YYABORT;
}
| VARREF_LITERAL maybeqmequals
{
  size_t funloc = stiryy->main->abce->bytecodesz;
  stiryy_add_fun_sym(stiryy, $1, $2, funloc);
  stiryy_add_byte(stiryy, ABCE_OPCODE_FUN_HEADER);
  stiryy_add_double(stiryy, 0);
}
expr NEWLINE
{
  printf("Assigning to %s\n", $1);
  stiryy_add_byte(stiryy, ABCE_OPCODE_RET);
  stiryy_add_byte(stiryy, ABCE_OPCODE_FUN_TRAILER);
  stiryy_add_double(stiryy, stiryy_symbol_add(stiryy, $1, strlen($1)));
  free($1);
}
| VARREF_LITERAL PLUSEQUALS
{
  size_t funloc = stiryy->main->abce->bytecodesz;
  size_t oldloc = stiryy_add_fun_sym(stiryy, $1, 0, funloc);
  if (oldloc == (size_t)-1)
  {
    printf("Can't find old symbol function\n");
    YYABORT;
  }
  stiryy_add_byte(stiryy, ABCE_OPCODE_FUN_HEADER);
  stiryy_add_double(stiryy, 0);
  stiryy_add_byte(stiryy, ABCE_OPCODE_PUSH_DBL);
  stiryy_add_double(stiryy, oldloc);
  stiryy_add_byte(stiryy, ABCE_OPCODE_PUSH_FROM_CACHE);
  stiryy_add_byte(stiryy, ABCE_OPCODE_CALL_IF_FUN);
  // FIXME what if it's not a list?
  stiryy_add_byte(stiryy, ABCE_OPCODE_DUP_NONRECURSIVE);
/*
  stiryy_add_byte(stiryy, STIRBCE_OPCODE_FUNIFY);
  stiryy_add_byte(stiryy, STIRBCE_OPCODE_PUSH_DBL);
  stiryy_add_double(stiryy, 0); // arg cnt
  stiryy_add_byte(stiryy, STIRBCE_OPCODE_CALL);
*/
}
expr NEWLINE
{
  printf("Plus-assigning to %s\n", $1);
  // FIXME what if it's not a list?
  stiryy_add_byte(stiryy, ABCE_OPCODE_APPENDALL_MAINTAIN);
  stiryy_add_byte(stiryy, ABCE_OPCODE_RET);
  stiryy_add_byte(stiryy, ABCE_OPCODE_FUN_TRAILER);
  stiryy_add_double(stiryy, stiryy_symbol_add(stiryy, $1, strlen($1)));
  free($1);
}
;

value:
  STRING_LITERAL
{
  size_t symid = stiryy_symbol_add(stiryy, $1.str, $1.sz);
  stiryy_add_byte(stiryy, ABCE_OPCODE_PUSH_DBL);
  stiryy_add_double(stiryy, symid);
  stiryy_add_byte(stiryy, ABCE_OPCODE_PUSH_FROM_CACHE);
  free($1.str);
  $$ = 0;
}
| varref
{
  $$ = 0;
}
| dict
{
  $$ = 0;
}
| list
{
  $$ = 0;
}
| DELAYVAR OPEN_PAREN varref CLOSE_PAREN
{
  $$ = 0;
}
| DELAYLISTEXPAND OPEN_PAREN expr CLOSE_PAREN
{
  $$ = 1;
}
| DELAYEXPR OPEN_PAREN expr CLOSE_PAREN
{
  $$ = 0;
}
;

varref:
  VARREF_LITERAL
{
  free($1);
}
| DO VARREF_LITERAL
{
  free($2);
}
| LO VARREF_LITERAL
{
  free($2);
}
| IO VARREF_LITERAL
{
  free($2);
}
| D VARREF_LITERAL
{
  free($2);
}
| L VARREF_LITERAL
{
  free($2);
}
| I VARREF_LITERAL
{
  free($2);
}
;

expr: expr11;

expr1:
  expr0
| LOGICAL_NOT expr1
| BITWISE_NOT expr1
| ADD expr1
| SUB expr1
;

expr2:
  expr1
| expr2 MUL expr1
| expr2 DIV expr1
| expr2 MOD expr1
;

expr3:
  expr2
| expr3 ADD expr2
| expr3 SUB expr2
;

expr4:
  expr3
| expr4 SHL expr3
| expr4 SHR expr3
;

expr5:
  expr4
| expr5 LT expr4
| expr5 LE expr4
| expr5 GT expr4
| expr5 GE expr4
;

expr6:
  expr5
| expr6 EQ expr5
| expr6 NE expr5
;

expr7:
  expr6
| expr7 BITWISE_AND expr6
;

expr8:
  expr7
| expr8 BITWISE_XOR expr7
;

expr9:
  expr8
| expr9 BITWISE_OR expr8
;

expr10:
  expr9
| expr10 LOGICAL_AND expr9

expr11:
  expr10
| expr11 LOGICAL_OR expr10


expr0:
  OPEN_PAREN expr CLOSE_PAREN
| OPEN_PAREN expr CLOSE_PAREN OPEN_PAREN maybe_arglist CLOSE_PAREN
| OPEN_PAREN expr CLOSE_PAREN MAYBE_CALL
| dict maybe_bracketexprlist
| list maybe_bracketexprlist
| STRING_LITERAL
{
  free($1.str);
}
| NUMBER
{
  stiryy_add_byte(stiryy, ABCE_OPCODE_PUSH_DBL);
  stiryy_add_double(stiryy, $1);
}
| lvalue
| lvalue OPEN_PAREN maybe_arglist CLOSE_PAREN
| lvalue MAYBE_CALL
| IMM OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist
| IMM OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist OPEN_PAREN maybe_arglist CLOSE_PAREN
| IMM OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist MAYBE_CALL
| DYNO OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist
| DYNO OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist OPEN_PAREN maybe_arglist CLOSE_PAREN
| DYNO OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist MAYBE_CALL
| LEXO OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist
| LEXO OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist OPEN_PAREN maybe_arglist CLOSE_PAREN
| LEXO OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist MAYBE_CALL
| IMMO OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist
| IMMO OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist OPEN_PAREN maybe_arglist CLOSE_PAREN
| IMMO OPEN_BRACKET expr CLOSE_BRACKET maybe_bracketexprlist MAYBE_CALL
| LOC OPEN_BRACKET STRING_LITERAL CLOSE_BRACKET maybe_bracketexprlist
{
  free($3.str);
}
| LOC OPEN_BRACKET STRING_LITERAL CLOSE_BRACKET maybe_bracketexprlist OPEN_PAREN maybe_arglist CLOSE_PAREN
{
  free($3.str);
}
| LOC OPEN_BRACKET STRING_LITERAL CLOSE_BRACKET maybe_bracketexprlist MAYBE_CALL
{
  free($3.str);
}
| SUFSUBONE OPEN_PAREN expr COMMA expr COMMA expr CLOSE_PAREN
| SUFSUB OPEN_PAREN expr COMMA expr COMMA expr CLOSE_PAREN
| SUFFILTER OPEN_PAREN expr COMMA expr CLOSE_PAREN
| APPEND OPEN_PAREN expr COMMA expr CLOSE_PAREN
| APPEND_LIST OPEN_PAREN expr COMMA expr CLOSE_PAREN
| STRAPPEND OPEN_PAREN expr COMMA expr CLOSE_PAREN
{ stiryy_add_byte(stiryy, ABCE_OPCODE_STRAPPEND); }
| RULE_DIST
| RULE_PHONY
| RULE_ORDINARY
;

maybe_arglist:
| arglist
;

arglist:
expr
| arglist COMMA expr
;

list:
OPEN_BRACKET
{
  stiryy_add_byte(stiryy, ABCE_OPCODE_PUSH_NEW_ARRAY);
}
maybe_valuelist
CLOSE_BRACKET
;

dict:
OPEN_BRACE maybe_dictlist CLOSE_BRACE
;

maybe_dictlist:
| dictlist
;

dictlist:
  dictentry
| dictlist COMMA dictentry
;

dictentry:
  STRING_LITERAL COLON value
{
  free($1.str);
}
;

maybe_valuelist:
| valuelist
;

valuelist:
  valuelistentry
{
  if ($1)
  {
    stiryy_add_byte(stiryy, ABCE_OPCODE_APPENDALL_MAINTAIN);
  }
  else
  {
    stiryy_add_byte(stiryy, ABCE_OPCODE_APPEND_MAINTAIN);
  }
}
| valuelist COMMA
  valuelistentry
{
  if ($3)
  {
    stiryy_add_byte(stiryy, ABCE_OPCODE_APPENDALL_MAINTAIN);
  }
  else
  {
    stiryy_add_byte(stiryy, ABCE_OPCODE_APPEND_MAINTAIN);
  }
}
;

valuelistentry:
  AT varref
{
  $$ = 0;
}
| value
{
  $$ = $1;
};

stirrule:
  targetspec COLON depspec NEWLINE shell_commands
| PHONYRULE COLON targetspec COLON depspec NEWLINE shell_commands
| DISTRULE COLON targetspec COLON depspec NEWLINE shell_commands
| PATRULE COLON targetspec COLON targetspec COLON depspec NEWLINE shell_commands
;

shell_commands:
| shell_commands shell_command
;

shell_command:
  SHELL_COMMAND NEWLINE
{
  char *outbuf = NULL;
  size_t outcap = 0;
  size_t outsz = 0;
  size_t len = strlen($1);
  size_t i;
  stiryy_add_shell_section(stiryy);
  for (i = 0; i < len; i++)
  {
    if ($1[i] == '\\')
    {
      if (i+1 >= len)
      {
        abort();
      }
      if (outsz >= outcap)
      {
        outcap = 2*outcap + 16;
        outbuf = realloc(outbuf, outcap);
      }
      outbuf[outsz++] = $1[i+1];
      i++;
      continue;
    }
    else if ($1[i] == '$')
    {
      if (i+1 < len && $1[i+1] == '<')
      {
        char *dep;
        size_t deplen;
        struct stiryyrule *rule = &stiryy->main->rules[stiryy->main->rulesz - 1];
        if (rule->depsz <= 0)
        {
          fprintf(stderr, "$< on rule with no dependencies\n");
          abort();
        }
        dep = rule->deps[0].name;
        deplen = strlen(dep);
        if (outsz + deplen > outcap)
        {
          outcap = 2*outcap + deplen;
          outbuf = realloc(outbuf, outcap);
        }
        memcpy(&outbuf[outsz], dep, deplen);
        outsz += deplen;
        i++;
        continue;
      }
      if (i+1 < len && $1[i+1] == '@')
      {
        char *tgt;
        size_t tgtlen;
        struct stiryyrule *rule = &stiryy->main->rules[stiryy->main->rulesz - 1];
        if (rule->targetsz <= 0)
        {
          fprintf(stderr, "$@ on rule with no targets\n");
          abort();
        }
        tgt = rule->targets[0];
        tgtlen = strlen(tgt);
        if (outsz + tgtlen > outcap)
        {
          outcap = 2*outcap + tgtlen;
          outbuf = realloc(outbuf, outcap);
        }
        memcpy(&outbuf[outsz], tgt, tgtlen);
        outsz += tgtlen;
        i++;
        continue;
      }
      abort();
    }
    else if ($1[i] == ' ')
    {
      while ($1[i] == ' ')
      {
        i++;
      }
      i--; // will be incremented by for
      if (outsz >= outcap)
      {
        outcap = 2*outcap + 16;
        outbuf = realloc(outbuf, outcap);
      }
      outbuf[outsz++] = '\0';
      stiryy_add_shell(stiryy, outbuf);
      outsz = 0;
      continue;
    }
    if (outsz >= outcap)
    {
      outcap = 2*outcap + 16;
      outbuf = realloc(outbuf, outcap);
    }
    outbuf[outsz++] = $1[i];
  }
  if (outsz >= outcap)
  {
    outcap = 2*outcap + 16;
    outbuf = realloc(outbuf, outcap);
  }
  outbuf[outsz++] = '\0';
  printf("\tshell\n");
  stiryy_add_shell(stiryy, outbuf);
  free($1);
  free(outbuf);
  stiryy_add_shell(stiryy, NULL);
  // FIXME multiple shell command lines
}
| ATTAB expr NEWLINE
{
  printf("\tshell expr\n");
}
;

targetspec:
  targets
| list
{
  printf("targetlist\n");
}
;
  
depspec:
  deps
| list
{
  printf("deplist\n");
}
;
  

targets:
  FREEFORM_TOKEN
{
  if (!stiryy->main->freeform_token_seen)
  {
    printf("Recommend using string literals instead of free-form tokens\n");
    stiryy->main->freeform_token_seen=1;
  }
  printf("target1 %s\n", $1);
  stiryy_emplace_rule(stiryy);
  stiryy_set_tgt(stiryy, $1);
  free($1);
}
| STRING_LITERAL
{
  printf("target1 %s\n", $1.str);
  stiryy_emplace_rule(stiryy);
  stiryy_set_tgt(stiryy, $1.str);
  free($1.str);
}
| VARREF_LITERAL
{
  stiryy_emplace_rule(stiryy);
  printf("target1ref\n");
  free($1);
}
| targets FREEFORM_TOKEN
{
  if (!stiryy->main->freeform_token_seen)
  {
    printf("Recommend using string literals instead of free-form tokens\n");
    stiryy->main->freeform_token_seen=1;
  }
  printf("target %s\n", $2);
  stiryy_set_tgt(stiryy, $2);
  free($2);
}
| targets STRING_LITERAL
{
  printf("target %s\n", $2.str);
  stiryy_set_tgt(stiryy, $2.str);
  free($2.str);
}
| targets VARREF_LITERAL
{
  printf("targetref\n");
  free($2);
}
;

maybe_rec:
{
  $$ = 0;
}
| RECDEP
{
  $$ = 1;
}
;

deps:
| deps maybe_rec FREEFORM_TOKEN
{
  if (!stiryy->main->freeform_token_seen)
  {
    printf("Recommend using string literals instead of free-form tokens\n");
    stiryy->main->freeform_token_seen=1;
  }
  printf("dep %s rec? %d\n", $3, (int)$2);
  stiryy_set_dep(stiryy, $3, $2);
  free($3);
}
| deps maybe_rec STRING_LITERAL
{
  printf("dep %s rec? %d\n", $3.str, (int)$2);
  stiryy_set_dep(stiryy, $3.str, $2);
  free($3.str);
}
| deps maybe_rec VARREF_LITERAL
{
  printf("depref\n");
  free($3);
}
;
