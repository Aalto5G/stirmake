.SUFFIXES:

SRC_LIB := yyutils.c incyyutils.c dbyyutils.c canon.c stirtrap.c jsonyyutils.c syncbuf.c bypid.c mymalloc.c stringtab.c db.c stirutils.c statcache.c bytgt.c bydep.c adddep.c rule.c accesscache.c incnonyy.c mygetline.c dbnonyy.c
SRC := $(SRC_LIB) stir.c inc.c stirmake.c jsonyytest.c

BYACC ?= byacc
FLEX ?= flex

LEX_LIB := stiryy.l jsonyy.l
LEX := $(LEX_LIB)

YACC_LIB := stiryy.y jsonyy.y
YACC := $(YACC_LIB)

LEXGEN_LIB := $(patsubst %.l,%.lex.c,$(LEX_LIB))
LEXGEN := $(patsubst %.l,%.lex.c,$(LEX))

YACCGEN_LIB := $(patsubst %.y,%.tab.c,$(YACC_LIB))
YACCGEN := $(patsubst %.y,%.tab.c,$(YACC))

GEN_LIB := $(patsubst %.l,%.lex.c,$(LEX_LIB)) $(patsubst %.y,%.tab.c,$(YACC_LIB))
GEN := $(patsubst %.l,%.lex.c,$(LEX)) $(patsubst %.y,%.tab.c,$(YACC))

OBJ_LIB := $(patsubst %.c,%.o,$(SRC_LIB))
OBJ := $(patsubst %.c,%.o,$(SRC))

OBJGEN_LIB := $(patsubst %.c,%.o,$(GEN_LIB))
OBJGEN := $(patsubst %.c,%.o,$(GEN))

ASM_LIB := $(patsubst %.c,%.s,$(SRC_LIB))
ASM := $(patsubst %.c,%.s,$(SRC))

ASMGEN_LIB := $(patsubst %.c,%.s,$(GEN_LIB))
ASMGEN := $(patsubst %.c,%.s,$(GEN))

DEP_LIB := $(patsubst %.c,%.d,$(SRC_LIB))
DEP := $(patsubst %.c,%.d,$(SRC))

DEPGEN_LIB := $(patsubst %.c,%.d,$(GEN_LIB))
DEPGEN := $(patsubst %.c,%.d,$(GEN))

.PHONY: all wc

all: jsonyytest stir inc abce stirmake smka smkt smkp

.PHONY: abce

wc:
	wc -l $(LEX) $(YACC) $(SRC) $(filter-out %.lex.h %.tab.h git.h,$(wildcard *.h))

#LUAINC:=/usr/include/lua5.3
#LUALIB:=/usr/lib/x86_64-linux-gnu/liblua5.3.a
LUAINCS:=
LUALIBS:=
WITH_LUA?=no
NO_MMAP?=yes
VALGRIND_DETECTION?=no
NO_MEMPARSE?=no
ifeq ($(WITH_LUA),yes)
  LUAINCS:=$(shell pkg-config --cflags luajit)
  LUALIBS:=$(shell pkg-config --libs luajit)
endif

ifeq ($(WITH_LUA),yes)
abce:
	cd abce && $(MAKE) WITH_LUA=yes LUAINCS="$(LUAINCS)" LUALIBS="$(LUALIBS)" CFLAGS="$(CFLAGS)"
else
abce:
	cd abce && $(MAKE) CFLAGS="$(CFLAGS)"
endif

#CC=gcc
#CC=clang
CC?=cc
CFLAGS?=-O3 -Wall -Wextra -Wno-unused-parameter -g

ifeq ($(VALGRIND_DETECTION),yes)
  CFLAGS += -DVALGRIND_DETECTION=1
endif
ifeq ($(WITH_LUA),yes)
  CFLAGS += $(LUAINCS) -DWITH_LUA
else
  LUALIBS :=
endif
ifeq ($(NO_MMAP),yes)
  CFLAGS += -DABCE_NO_MMAP=1 -DSTIR_NO_MMAP=1
endif
ifeq ($(NO_MEMPARSE),yes)
  CFLAGS += -DSTIR_NO_MEMPARSE=1
endif

smka:
	rm -f smka
	ln -s stirmake smka
smkt:
	rm -f smkt
	ln -s stirmake smkt
smkp:
	rm -f smkp
	ln -s stirmake smkp

stirmake: stirmake.o libstirmake.a Makefile abce
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $(filter %.o,$^) abce/libabce.a $(filter %.a,$^) abce/libabce.a $(LUALIBS) -lm

stir: stir.o libstirmake.a Makefile abce
	$(CC) $(CCFLAGS) -o $@ $(filter %.o,$^) abce/libabce.a $(filter %.a,$^) abce/libabce.a $(LUALIBS) -lm

jsonyytest: jsonyytest.o libstirmake.a Makefile abce
	$(CC) $(CCFLAGS) -o $@ $(filter %.o,$^) abce/libabce.a $(filter %.a,$^) abce/libabce.a $(LUALIBS) -lm

inc: inc.o libstirmake.a Makefile
	$(CC) $(CCFLAGS) -o $@ $(filter %.o,$^) $(filter %.a,$^) $(LUALIBS) -lm

libstirmake.a: $(OBJ_LIB) $(OBJGEN_LIB) Makefile
	rm -f $@
	ar rvs $@ $(filter %.o,$^)

$(OBJ): %.o: %.c %.d Makefile
	$(CC) $(CFLAGS) -c -o $*.o $*.c
$(OBJGEN): %.o: %.c %.h %.d Makefile
	$(CC) $(CFLAGS) -c -o $*.o $*.c -Wno-sign-compare -Wno-missing-prototypes

$(DEP): %.d: %.c Makefile
	$(CC) $(CFLAGS) -MM -MP -MT "$*.d $*.o" -o $*.d $*.c
$(DEPGEN): %.d: %.c %.h Makefile
	$(CC) $(CFLAGS) -MM -MP -MT "$*.d $*.o" -o $*.d $*.c

stiryy.lex.d: stiryy.tab.h stiryy.lex.h
stiryy.lex.o: stiryy.tab.h stiryy.lex.h
stiryy.tab.d: stiryy.tab.h stiryy.lex.h
stiryy.tab.o: stiryy.tab.h stiryy.lex.h

stiryy.lex.c: stiryy.l Makefile
	$(FLEX) --outfile=stiryy.lex.c --header-file=/dev/null stiryy.l
stiryy.lex.h: stiryy.l Makefile
	$(FLEX) --outfile=/dev/null --header-file=stiryy.lex.h stiryy.l
stiryy.tab.c: stiryy.y Makefile
	$(BYACC) -d -p stiryy -b stiryy -o .tmpc.stiryy.tab.c stiryy.y
	rm .tmpc.stiryy.tab.h
	mv .tmpc.stiryy.tab.c stiryy.tab.c
stiryy.tab.h: stiryy.y Makefile
	$(BYACC) -d -p stiryy -b stiryy -o .tmph.stiryy.tab.c stiryy.y
	rm .tmph.stiryy.tab.c
	mv .tmph.stiryy.tab.h stiryy.tab.h

jsonyy.lex.d: jsonyy.tab.h jsonyy.lex.h
jsonyy.lex.o: jsonyy.tab.h jsonyy.lex.h
jsonyy.tab.d: jsonyy.tab.h jsonyy.lex.h
jsonyy.tab.o: jsonyy.tab.h jsonyy.lex.h

jsonyy.lex.c: jsonyy.l Makefile
	$(FLEX) --outfile=jsonyy.lex.c --header-file=/dev/null jsonyy.l
jsonyy.lex.h: jsonyy.l Makefile
	$(FLEX) --outfile=/dev/null --header-file=jsonyy.lex.h jsonyy.l
jsonyy.tab.c: jsonyy.y Makefile
	$(BYACC) -d -p jsonyy -b jsonyy -o .tmpc.jsonyy.tab.c jsonyy.y
	rm .tmpc.jsonyy.tab.h
	mv .tmpc.jsonyy.tab.c jsonyy.tab.c
jsonyy.tab.h: jsonyy.y Makefile
	$(BYACC) -d -p jsonyy -b jsonyy -o .tmph.jsonyy.tab.c jsonyy.y
	rm .tmph.jsonyy.tab.c
	mv .tmph.jsonyy.tab.h jsonyy.tab.h

stiryy.tab.c: git.h
stirmake.d: git.h
stirmake.o: git.h
yyutils.d: git.h
yyutils.o: git.h

ifneq (,$(wildcard ../.git/logs/HEAD))
git.h: ../.git/logs/HEAD
	echo "#ifndef _GIT_H_" > git.h
	echo "#define _GIT_H_" >> git.h
	echo "static const char gitshas[][20] = {" >> git.h
	git log --pretty=format:\"%h\", --abbrev=40|sed 's/\([0-9A-Fa-f][0-9A-Fa-f]\)/\\x\1/g' >> git.h
	echo "};" >> git.h
	printf "%s" "static const char * const gitversion = \"" >> git.h
	printf "%s" `git describe --tags|sed 's/^v//g'` >> git.h
	echo "\";" >> git.h
	echo "#endif" >> git.h
endif

.PHONY: install

install:
	./install.sh $(DESTDIR)

-include *.d
