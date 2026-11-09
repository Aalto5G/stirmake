#!/bin/sh

die()
{
  echo "$@"
  exit 1
}

docmd()
{
  echo "$@"
  "$@"
  return $?
}

doflex()
{
  if [ -e "$1" -a -e "$2" ]; then
    if which $FLEX > /dev/null; then
      return 0
    else
      echo "No flex but targets exist"
      return 1
    fi
  fi
  return 0
}
dobyacc()
{
  if [ -e "$1" -a -e "$2" ]; then
    if which $BYACC > /dev/null; then
      return 0
    else
      echo "No byacc but targets exist"
      return 1
    fi
  fi
  return 0
}

export FLEX="${FLEX:-flex}"
export BYACC="${BYACC:-byacc}"
export CC="${CC:-cc}"
export CFLAGS="${CFLAGS:--O3 -Wall -Wextra -Wno-unused-parameter -g}"
export LDFLAGS="${LDFLAGS:-}"
export STIR_LUAINCS="${STIR_LUAINCS:-}"
export STIR_LUALIBS="${STIR_LUALIBS:-}"
export STIR_NO_MMAP="${STIR_NO_MMAP:-1}"
export STIR_VALGRIND_DETECTION="${STIR_VALGRIND_DETECTION:-0}"

if [ "$STIR_NO_MMAP" != "" -a "$STIR_NO_MMAP" != "0" ]; then
  export CFLAGS="${CFLAGS} -DABCE_NO_MMAP=1 -DSTIR_NO_MMAP=1"
fi
if [ "$STIR_VALGRIND_DETECTION" != "" -a "$STIR_VALGRIND_DETECTION" != "0" ]; then
  export CFLAGS="${CFLAGS} -DVALGRIND_DETECTION=1"
fi
if [ "$STIR_NO_MEMPARSE" != "" -a "$STIR_NO_MEMPARSE" != "0" ]; then
  export CFLAGS="${CFLAGS} -DSTIR_NO_MEMPARSE=1"
fi
if [ "$STIR_WITH_LUA" != "" -a "$STIR_WITH_LUA" != "0" ]; then
  if [ "$STIR_LUAINCS" = "" ]; then
    export STIR_LUAINCS="$(pkg-config --cflags luajit)"
  fi
  if [ "$STIR_LUALIBS" = "" ]; then
    export STIR_LUALIBS="$(pkg-config --libs luajit)"
  fi
  export CFLAGS="${CFLAGS} ${STIR_LUAINCS} -DWITH_LUA"
fi

(cd abce; sh bootstrapnomake.sh) || die "abce"

libobjs=""

if [ -e ../.git/logs/HEAD ]; then
  echo "#ifndef _GIT_H_" > git.h
  echo "#define _GIT_H_" >> git.h
  echo "static const char gitshas[][20] = {" >> git.h
  git log --pretty=format:\"%h\", --abbrev=40|sed 's/\([0-9A-Fa-f][0-9A-Fa-f]\)/\\x\1/g' >> git.h
  echo "};" >> git.h
  printf "%s" "static const char * const gitversion = \"" >> git.h
  printf "%s" `git describe --tags|sed 's/^v//g'` >> git.h
  echo "\";" >> git.h
  echo "#endif" >> git.h
fi

for a in *.l; do
  if [ "$a" = "incyy.l" ]; then
    continue
  fi
  if [ "$a" = "dbyy.l" ]; then
    continue
  fi
  base="`echo "$a"|sed 's/.l$//g'`"
  if doflex "$base.lex.c" "$base.lex.h"; then
    docmd $FLEX --outfile="$base.lex.c" --header-file="$base.lex.h" "$a" || die "flex"
  fi
done
for a in *.y; do
  if [ "$a" = "incyy.y" ]; then
    continue
  fi
  if [ "$a" = "dbyy.y" ]; then
    continue
  fi
  base="`echo "$a"|sed 's/.y$//g'`"
  if dobyacc "$base.tab.c" "$base.tab.h"; then
    docmd $BYACC -d -p "$base" -b "$base" -o "$base.tab.c" "$a" || die "byacc"
  fi
done
for a in *.c; do
  base="`echo "$a"|sed 's/.c$//g'`"
  if grep -q '^int main(int' "$a"; then
    true
  else
    libobjs="$libobjs $base.o"
  fi
  docmd $CC $CFLAGS -Wno-sign-compare -Wno-missing-prototypes -c -o "$base.o" "$a" || die "cc"
done

docmd rm -f libstirmake.a
docmd ar rvs libstirmake.a $libobjs || die "ar"

docmd $CC $CFLAGS $LDFLAGS -o stirmake stirmake.o libstirmake.a abce/libabce.a $STIR_LUALIBS -lm || die "cclink"

docmd rm -f smka
docmd rm -f smkt
docmd rm -f smkp
docmd ln -s stirmake smka
docmd ln -s stirmake smkt
docmd ln -s stirmake smkp
