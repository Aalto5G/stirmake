#!/bin/sh

die()
{
  echo "$@"
  exit 1
}

rm -f .stir.db

$VALGRIND ../../smka -s > /dev/null 2>&1
STORE="$?"
if [ "$STORE" = 0 ]; then
  die "Execution of smka succeeded unexpectedly"
elif [ "$STORE" = 5 ]; then
  die "Execution of smka failed due to memory errors"
else
  true
fi

if [ -e .stir.db ]; then
  die ".stir.db exists after smka in empty directory"
fi

echo OK
