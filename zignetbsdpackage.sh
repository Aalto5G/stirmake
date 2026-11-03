#!/bin/sh

PKGDIR="packages/"

die()
{
  echo "$@"
  exit 1
}

mkdir -p "$PKGDIR" || die "Can't create package directory"

if [ "$1" = "arm64.10" ]; then
  export CC="zig cc -target aarch64-netbsd.10.1"
  export PKGARCH="arm64-netbsd-10.1"
  export MACHINE_ARCH="aarch64"
  export OS_VERSION="10.1"
elif [ "$1" = "arm32.10" ]; then
  export CC="zig cc -target arm-netbsd.10.1"
  export PKGARCH="arm32hf-netbsd-10.1"
  export MACHINE_ARCH="earmv7hf"
  export OS_VERSION="10.1"
elif [ "$1" = "amd64.10" ]; then
  export CC="zig cc -target x86_64-netbsd.10.1"
  export PKGARCH="amd64-netbsd-10.1"
  export MACHINE_ARCH="x86_64"
  export OS_VERSION="10.1"
elif [ "$1" = "i386.10" ]; then
  export CC="zig cc -target x86-netbsd.10.1"
  export PKGARCH="i386-netbsd-10.1"
  export MACHINE_ARCH="i386"
  export OS_VERSION="10.1"
elif [ "$1" = "arm64.11" ]; then
  export CC="zig cc -target aarch64-netbsd.11.0"
  export PKGARCH="arm64-netbsd-11.0"
  export MACHINE_ARCH="aarch64"
  export OS_VERSION="11.0"
elif [ "$1" = "arm32.11" ]; then
  export CC="zig cc -target arm-netbsd.11.0"
  export PKGARCH="arm32hf-netbsd-11.0"
  export MACHINE_ARCH="earmv7hf"
  export OS_VERSION="11.0"
elif [ "$1" = "amd64.11" ]; then
  export CC="zig cc -target x86_64-netbsd.11.0"
  export PKGARCH="amd64-netbsd-11.0"
  export MACHINE_ARCH="x86_64"
  export OS_VERSION="11.0"
elif [ "$1" = "i386.11" ]; then
  export CC="zig cc -target x86-netbsd.11.0"
  export PKGARCH="i386-netbsd-11.0"
  export MACHINE_ARCH="i386"
  export OS_VERSION="11.0"
else
  die "Unsupported architecture: $1"
fi

tmpdir="`mktemp -d`"
pwd="`pwd`"
cd "$tmpdir" || die "Can't goto tmp dir"
git clone --recursive https://github.com/Aalto5G/stirmake || die "Can't clone"
cd stirmake/stirc || die "Can't chdir"
smka -ja || die "Can't compile"
vname="`git describe --tags`"
nonvname="$vname"
case "$nonvname" in
  v*)
    nonvname="${vname#v}"
esac
nonvnameu="`echo "$nonvname"|tr '-' '_'`"
chmod 755 stirmake || die "chmod fail"
chmod 644 stirmake.1 || die "chmod fail"
mv stirmake .. || die "mv fail"
mv smka .. || die "mv fail"
mv smkt .. || die "mv fail"
mv smkp .. || die "mv fail"
mv install.sh .. || die "mv fail"
mv stirmake.1 .. || die "mv fail"
cd ../.. || die "Can't chdir"


mv stirmake stirmake-"$vname" || die "Can't rename directory"
tar --format=posix --owner=root:0 --group=wheel:0 --mode="go-w" -czvf stirmakebin-"$PKGARCH"-"$vname".tar.gz stirmake-"$vname"/stirmake stirmake-"$vname"/smka stirmake-"$vname"/smkt stirmake-"$vname"/smkp stirmake-"$vname"/install.sh stirmake-"$vname"/stirmake.1 || die "Can't create tar.gz"

mkdir -p stage/bin || die "mkdir fail"
mkdir -p stage/man/man1 || die "mkdir fail"

cp -p stirmake-"$vname"/stirmake stage/bin || die "cp fail"
ln -s stirmake stage/bin/smka || die "ln fail"
ln -s stirmake stage/bin/smkt || die "ln fail"
ln -s stirmake stage/bin/smkp || die "ln fail"
cp -p stirmake-"$vname"/stirmake.1 stage/man/man1 || die "cp fail"
ln -s stirmake.1 stage/man/man1/smka.1 || die "ln fail"
ln -s stirmake.1 stage/man/man1/smkt.1 || die "ln fail"
ln -s stirmake.1 stage/man/man1/smkp.1 || die "ln fail"
find stage/bin stage/man -type f -print0|xargs -0 du -bc|tail -n1|cut -f1 > stage/+SIZE_ALL || die "du fail"
find stage/bin stage/man -type f -print0|xargs -0 du -bc|tail -n1|cut -f1 > stage/+SIZE_PKG || die "du fail"
echo "Scalable true implementation of recursive make" > stage/+COMMENT || die "echo fail"
echo "Stirmake, scalable true implementation of recursive make, for easy multi-dir builds." > stage/+DESC || die "echo fail"
echo "" >> stage/+DESC || die "echo fail"
echo "Homepage:" >> stage/+DESC || die "echo fail"
echo "https://github.com/Aalto5G/stirmake" >> stage/+DESC || die "echo fail"
echo "MACHINE_ARCH=${MACHINE_ARCH}" > stage/+BUILD_INFO || die "echo fail"
echo "OS_VERSION=${OS_VERSION}" >> stage/+BUILD_INFO || die "echo fail"
echo "OPSYS=NetBSD" >> stage/+BUILD_INFO || die "echo fail"
echo "LICENSE=mit" >> stage/+BUILD_INFO || die "echo fail"
echo "PKGTOOLS_VERSION=20091115" >> stage/+BUILD_INFO || die "echo fail"
echo "@cwd /usr/pkg" > stage/+CONTENTS || die "echo fail"
echo "@name stirmake-$nonvname" >> stage/+CONTENTS || die "echo fail"
echo "bin/stirmake" >> stage/+CONTENTS || die "echo fail"
echo "@comment MD5:`md5sum stage/bin/stirmake|awk '{print $1}'`" >> stage/+CONTENTS || die "echo fail"
echo "bin/smka" >> stage/+CONTENTS || die "echo fail"
echo "bin/smkt" >> stage/+CONTENTS || die "echo fail"
echo "bin/smkp" >> stage/+CONTENTS || die "echo fail"
echo "man/man1/stirmake.1" >> stage/+CONTENTS || die "echo fail"
echo "@comment MD5:`md5sum stage/man/man1/stirmake.1|awk '{print $1}'`" >> stage/+CONTENTS || die "echo fail"
echo "man/man1/smka.1" >> stage/+CONTENTS || die "echo fail"
echo "man/man1/smkt.1" >> stage/+CONTENTS || die "echo fail"
echo "man/man1/smkp.1" >> stage/+CONTENTS || die "echo fail"
echo "@cwd /usr/pkg" >> stage/+CONTENTS || die "echo fail"
echo "@ignore" >> stage/+CONTENTS || die "echo fail"
echo "+COMMENT" >> stage/+CONTENTS || die "echo fail"
echo "@ignore" >> stage/+CONTENTS || die "echo fail"
echo "+DESC" >> stage/+CONTENTS || die "echo fail"
echo "@ignore" >> stage/+CONTENTS || die "echo fail"
echo "+BUILD_INFO" >> stage/+CONTENTS || die "echo fail"
echo "@ignore" >> stage/+CONTENTS || die "echo fail"
echo "+SIZE_PKG" >> stage/+CONTENTS || die "echo fail"
echo "@ignore" >> stage/+CONTENTS || die "echo fail"
echo "+SIZE_ALL" >> stage/+CONTENTS || die "echo fail"

tar --format=posix --owner=root:0 --group=wheel:0 --mode="go-w" --no-recursion -cf stirmake-"$nonvname".tar -C stage +CONTENTS +COMMENT +DESC +BUILD_INFO +SIZE_PKG +SIZE_ALL || die "tar fail"
tar --format=posix --owner=root:0 --group=wheel:0 --mode="go-w" -P --no-recursion -rf stirmake-"$nonvname".tar -C stage \
  bin/stirmake \
  bin/smka \
  bin/smkt \
  bin/smkp \
  man/man1/stirmake.1 \
  man/man1/smka.1 \
  man/man1/smkt.1 \
  man/man1/smkp.1 || die "tar fail"
xz -9 stirmake-"$nonvname".tar || die "xz fail"
mv stirmake-"$nonvname".tar.xz stirmake-"$nonvname"-"$PKGARCH".tgz || die "mv fail"

rm -rf stirmake-"$vname"
rm -rf stirmake_"$nonvname"_"$DEBARCH"
rm -rf stage
cd "$pwd" || die "Can't chdir back to old directory"
if [ -e "$PKGDIR/stirmakebin-$PKGARCH-$vname.tar.gz" ]; then
  die "Package stirmakebin-$PKGARCH-$vname.tar.gz already exists"
else
  mv "$tmpdir/stirmakebin-$PKGARCH-$vname.tar.gz" "$PKGDIR"/ || die "Can't move package"
fi
if [ -e "$PKGDIR"/stirmake-"$nonvname"-"$PKGARCH".tgz ]; then
  die "Package stirmake-${nonvname}-$PKGARCH.tgz already exists"
else
  mv "$tmpdir/stirmake-${nonvname}-$PKGARCH.tgz" "$PKGDIR"/ || die "Can't move package"
fi
rmdir "$tmpdir" || die "Can't rm temporary dir $tmpdir"
echo
echo "Package created:"
echo "stirmakebin-$PKGARCH-$vname.tar.gz"
echo "stirmake-${nonvname}-$PKGARCH.tgz"
