#!/bin/sh

# Linux:
# - /usr/local/share/man (preferred)
# - /usr/local/man (old standard)
# - /usr/share/man
# - ~/.local/share/man (preferred)
# - ~/.local/man (available for some reason)
# - /opt/PACKAGENAME/share/man
# - /opt/man (reserved for sysadmin)
# MacOS: /usr/local/share/man
# FreeBSD: /usr/local/share/man
# OpenBSD: /usr/local/man
# NetBSD: /usr/pkg/man, /usr/local/man
# DragonflyBSD: /usr/local/man


if [ '!' -x "stirmake" ]; then
  echo "stirmake not made"
  exit 1
fi

echocmd()
{
  echo "$@"
  "$@"
}

RELOGIN=0
ISLOCAL=0
PREFIX="$1"

if [ "a$PREFIX" = "a" ]; then
  PREFIX=~/.local
  ISLOCAL=1
  if [ '!' -d "$PREFIX" ]; then
    RELOGIN=1
  fi
  echocmd mkdir -p "$PREFIX"
  case "$(uname -s)" in
    Linux)
      ;;
    *)
      echo "Warning: trying to install to $PREFIX under non-Linux"
      echo "This may not do what you want"
      ;;
  esac
fi

P="$PREFIX"
PMAN="$PREFIX"
if [ "$(uname -s)" = "FreeBSD" ]; then
  PMAN="$PREFIX/share"
elif [ "$P" = "/usr" -o "$P" = "/usr/" ]; then
  PMAN="$PREFIX/share"
elif [ "$ISLOCAL" = 1 ]; then
  PMAN="$PREFIX/share"
elif [ "$P" = "/usr/local" -o "$P" = "/usr/local/" ]; then
  case "$(uname -s)" in
    Linux)
      PMAN="$PREFIX/share"
      ;;
    Darwin)
      PMAN="$PREFIX/share"
      ;;
  esac
else
  case "$P" in
    */.local)
      PMAN="$PREFIX/share"
      ;;
    */.local/)
      PMAN="$PREFIX/share"
      ;;
    /opt)
      PMAN="$PREFIX"
      ;;
    /opt/)
      PMAN="$PREFIX"
      ;;
    /opt/*)
      PMAN="$PREFIX/share"
      ;;
  esac
fi
H="`hostname`"

if [ '!' -w "$P" ]; then
  echo "No write permissions to $P"
  exit 1
fi
if [ '!' -d "$P" ]; then
  echo "Not a valid directory: $P"
  exit 1
fi

instbin()
{
  if [ -e "$P/bin/$1" ]; then
    echocmd ln "$P/bin/$1" "$P/bin/.$1.smkinstold.$$.$H" || exit 1
  fi
  echocmd cp "$1" "$P/bin/.$1.smkinstnew.$$.$H" || exit 1
  echocmd mv "$P/bin/.$1.smkinstnew.$$.$H" "$P/bin/$1" || exit 1
  if [ -e "$P/bin/.$1.smkinstold.$$.$H" ]; then
    # If you mount binaries across NFS, and run this command on the NFS server,
    # you might want to comment out this rm command.
    echocmd rm "$P/bin/.$1.smkinstold.$$.$H" || exit 1
  fi
}

instsym()
{
  if [ "`readlink "$P/bin/$1"`" != "stirmake" ]; then
    echocmd ln -s stirmake "$P/bin/.$1.smkinstnew.$$.$H" || exit 1
    echocmd mv "$P/bin/.$1.smkinstnew.$$.$H" "$P/bin/$1" || exit 1
  fi
}

instman()
{
  echocmd mkdir -p "$PMAN/man/man$2" || exit 1
  echocmd cp "$1.$2" "$PMAN/man/man$2/.$1.$2.smkinstnew.$$.$H" || exit 1
  echocmd mv "$PMAN/man/man$2/.$1.$2.smkinstnew.$$.$H" "$PMAN/man/man$2/$1.$2" || exit 1
}

instmansym()
{
  echocmd mkdir -p "$PMAN/man/man$2" || exit 1
  if [ "`readlink "$PMAN/man/man$2/$1.$2"`" != "stirmake.1" ]; then
    echocmd ln -s "stirmake.1" "$PMAN/man/man$2/.$1.$2.smkinstnew.$$.$H" || exit 1
    echocmd mv "$PMAN/man/man$2/.$1.$2.smkinstnew.$$.$H" "$PMAN/man/man$2/$1.$2" || exit 1
  fi
}

# Ensure bin directory is there
if [ "$ISLOCAL" = "1" ]; then
  if [ '!' -d "$P"/bin ]; then
    RELOGIN=1
  fi
fi
echocmd mkdir -p "$P/bin" || exit 1

# Install binary
instbin stirmake

# Install symlinks
instsym smka
instsym smkp
instsym smkt

# Install man page
instman stirmake 1

# Install man page symlinks
instmansym smka 1
instmansym smkp 1
instmansym smkt 1

echo "All done, stirmake has been installed to $P"
if [ "$RELOGIN" = "1" ]; then
  echo "As ~/.local/bin did not exist, you may need to re-log-in"
fi
