#!/bin/sh

die()
{
  echo "$@"
  exit 1
}

./zigbinarypackage.sh arm64 || die "arm64-linux"
./zigbinarypackage.sh arm32 || die "arm32-linux"
./zigbinarypackage.sh amd64 || die "amd64-linux"
./zigbinarypackage.sh i386 || die "i386-linux"
./zigmacpackage.sh amd64 || die "amd64-mac"
./zigmacpackage.sh arm64 || die "arm64-mac"
# Disable these for now:
#./zigfreebsdpackage.sh arm64.14 || die "arm64-freebsd.14"
#./zigfreebsdpackage.sh arm32.14 || die "arm32-freebsd.14"
#./zigfreebsdpackage.sh amd64.14 || die "amd64-freebsd.14"
#./zigfreebsdpackage.sh i386.14 || die "i386-freebsd.14"
#./zigfreebsdpackage.sh arm64.15 || die "arm64-freebsd.15"
#./zigfreebsdpackage.sh arm32.15 || die "arm32-freebsd.15"
#./zigfreebsdpackage.sh amd64.15 || die "amd64-freebsd.15"
#./zignetbsdpackage.sh arm64.10 || die "arm64-netbsd.10"
#./zignetbsdpackage.sh arm32.10 || die "arm32-netbsd.10"
#./zignetbsdpackage.sh amd64.10 || die "amd64-netbsd.10"
#./zignetbsdpackage.sh i386.10 || die "i386-netbsd.10"
#./zignetbsdpackage.sh arm64.11 || die "arm64-netbsd.11"
#./zignetbsdpackage.sh arm32.11 || die "arm32-netbsd.11"
#./zignetbsdpackage.sh amd64.11 || die "amd64-netbsd.11"
#./zignetbsdpackage.sh i386.11 || die "i386-netbsd.11"
#./zigopenbsdpackage.sh arm64.7.8 || die "arm64-openbsd.7.8"
#./zigopenbsdpackage.sh amd64.7.8 || die "amd64-openbsd.7.8"
#./zigopenbsdpackage.sh i386.7.8 || die "i386-openbsd.7.8"
#./zigopenbsdpackage.sh arm64.7.9 || die "arm64-openbsd.7.9"
#./zigopenbsdpackage.sh amd64.7.9 || die "amd64-openbsd.7.9"
#./zigopenbsdpackage.sh i386.7.9 || die "i386-openbsd.7.9"
#./zigopenbsdpackage.sh arm64.8.0 || die "arm64-openbsd.8.0"
#./zigopenbsdpackage.sh amd64.8.0 || die "amd64-openbsd.8.0"
#./zigopenbsdpackage.sh i386.8.0 || die "i386-openbsd.8.0"

echo "OK"
