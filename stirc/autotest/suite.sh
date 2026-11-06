#!/bin/sh

FAILCNT=0

fail()
{
  echo "Test failed"
  FAILCNT="`expr "$FAILCNT" + 1`"
}

runtest()
{
  echo "Executing test: $1"
  (cd "$1"; ./test.sh) || fail
}

#export VALGRIND="valgrind --error-exitcode=5 --quiet"
export VALGRIND=""

runtest "clean"
runtest "touchmode"
runtest "headerrm"
runtest "autotarget"
runtest "twotarget"
runtest "failures"
runtest "patrule"
runtest "cdeptargetslater"
runtest "recdep"
runtest "helpascii"
runtest "isfun"
runtest "nostirfile"

echo "Failure count: $FAILCNT"
if [ "$FAILCNT" = 0 ]; then
  exit 0
fi
exit 1
