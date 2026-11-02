#!/bin/sh

PKGDIR="packages/"

die()
{
  echo "$@"
  exit 1
}

mkdir -p "$PKGDIR" || die "Can't create package directory"

if [ "$1" = "arm64.7.8" ]; then
  export CC="zig cc -target aarch64-openbsd.7.8"
  export PKGARCH="arm64-openbsd-7.8"
  export MACHINE_ARCH="aarch64"
  export OS_VERSION="7.8"
#elif [ "$1" = "arm32.7.8" ]; then
#  export CC="zig cc -target arm-openbsd.7.8"
#  export PKGARCH="arm32hf-openbsd-7.8"
#  export MACHINE_ARCH="earmv7hf"
#  export OS_VERSION="7.8"
elif [ "$1" = "amd64.7.8" ]; then
  export CC="zig cc -target x86_64-openbsd.7.8"
  export PKGARCH="amd64-openbsd-7.8"
  export MACHINE_ARCH="amd64"
  export OS_VERSION="7.8"
elif [ "$1" = "i386.7.8" ]; then
  export CC="zig cc -target x86-openbsd.7.8"
  export PKGARCH="i386-openbsd-7.8"
  export MACHINE_ARCH="i386"
  export OS_VERSION="7.8"
elif [ "$1" = "arm64.7.9" ]; then
  export CC="zig cc -target aarch64-openbsd.7.9"
  export PKGARCH="arm64-openbsd-7.9"
  export MACHINE_ARCH="aarch64"
  export OS_VERSION="7.9"
#elif [ "$1" = "arm32.7.9" ]; then
#  export CC="zig cc -target arm-openbsd.7.9"
#  export PKGARCH="arm32hf-openbsd-7.9"
#  export MACHINE_ARCH="earmv7hf"
#  export OS_VERSION="7.9"
elif [ "$1" = "amd64.7.9" ]; then
  export CC="zig cc -target x86_64-openbsd.7.9"
  export PKGARCH="amd64-openbsd-7.9"
  export MACHINE_ARCH="amd64"
  export OS_VERSION="7.9"
elif [ "$1" = "i386.7.9" ]; then
  export CC="zig cc -target x86-openbsd.7.9"
  export PKGARCH="i386-openbsd-7.9"
  export MACHINE_ARCH="i386"
  export OS_VERSION="7.9"
elif [ "$1" = "arm64.8.0" ]; then
  export CC="zig cc -target aarch64-openbsd.8.0"
  export PKGARCH="arm64-openbsd-8.0"
  export MACHINE_ARCH="aarch64"
  export OS_VERSION="8.0"
#elif [ "$1" = "arm32.8.0" ]; then
#  export CC="zig cc -target arm-openbsd.8.0"
#  export PKGARCH="arm32hf-openbsd-8.0"
#  export MACHINE_ARCH="earmv7hf"
#  export OS_VERSION="8.0"
elif [ "$1" = "amd64.8.0" ]; then
  export CC="zig cc -target x86_64-openbsd.8.0"
  export PKGARCH="amd64-openbsd-8.0"
  export MACHINE_ARCH="amd64"
  export OS_VERSION="8.0"
elif [ "$1" = "i386.8.0" ]; then
  export CC="zig cc -target x86-openbsd.8.0"
  export PKGARCH="i386-openbsd-8.0"
  export MACHINE_ARCH="i386"
  export OS_VERSION="8.0"
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
tar --format=posix --owner=root --group=wheel --mode="go-w" -czvf stirmakebin-"$PKGARCH"-"$vname".tar.gz stirmake-"$vname"/stirmake stirmake-"$vname"/smka stirmake-"$vname"/smkt stirmake-"$vname"/smkp stirmake-"$vname"/install.sh stirmake-"$vname"/stirmake.1 || die "Can't create tar.gz"

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
echo "Scalable true implementation of recursive make" > stage/+DESC || die "echo fail"
echo "Stirmake, scalable true implementation of recursive make, for easy multi-dir builds." >> stage/+DESC || die "echo fail"
echo "" >> stage/+DESC || die "echo fail"
echo "Maintainer: Juha-Matti Tilli <juha-matti.tilli@iki.fi>" >> stage/+DESC || die "echo fail"
echo "" >> stage/+DESC || die "echo fail"
echo "WWW: https://github.com/Aalto5G/stirmake" >> stage/+DESC || die "echo fail"
echo "@name stirmake-$nonvname-$PKGARCH" > stage/+CONTENTS || die "echo fail"
echo "@arch $MACHINE_ARCH" >> stage/+CONTENTS || die "echo fail"
echo "+DESC" >> stage/+CONTENTS || die "echo fail"
echo "@sha `sha256sum stage/+DESC|awk '{print $1}'|xxd -r -p|base64`" >> stage/+CONTENTS || die "echo fail"
echo "@size `du -bc stage/+DESC |tail -1|cut -f1`" >> stage/+CONTENTS || die "echo fail"
echo "@cwd /usr/local" >> stage/+CONTENTS || die "echo fail"
echo "@bin bin/stirmake" >> stage/+CONTENTS || die "echo fail"
echo "@sha `sha256sum stage/bin/stirmake|awk '{print $1}'|xxd -r -p|base64`" >> stage/+CONTENTS || die "echo fail"
echo "@size `du -bc stage/bin/stirmake |tail -1|cut -f1`" >> stage/+CONTENTS || die "echo fail"
echo "@ts `stat -c %Y stage/bin/stirmake`" >> stage/+CONTENTS || die "echo fail"
#echo "@owner root" >> stage/+CONTENTS || die "echo fail"
#echo "@group bin" >> stage/+CONTENTS || die "echo fail"
echo "bin/smka" >> stage/+CONTENTS || die "echo fail"
echo "@symlink stirmake" >> stage/+CONTENTS || die "echo fail"
echo "bin/smkt" >> stage/+CONTENTS || die "echo fail"
echo "@symlink stirmake" >> stage/+CONTENTS || die "echo fail"
echo "bin/smkp" >> stage/+CONTENTS || die "echo fail"
echo "@symlink stirmake" >> stage/+CONTENTS || die "echo fail"
echo "@man man/man1/stirmake.1" >> stage/+CONTENTS || die "echo fail"
echo "@sha `sha256sum stage/man/man1/stirmake.1|awk '{print $1}'|xxd -r -p|base64`" >> stage/+CONTENTS || die "echo fail"
echo "@size `du -bc stage/man/man1/stirmake.1 |tail -1|cut -f1`" >> stage/+CONTENTS || die "echo fail"
echo "@ts `stat -c %Y stage/man/man1/stirmake.1`" >> stage/+CONTENTS || die "echo fail"
#echo "@owner root" >> stage/+CONTENTS || die "echo fail"
#echo "@group bin" >> stage/+CONTENTS || die "echo fail"
echo "man/man1/smka.1" >> stage/+CONTENTS || die "echo fail"
echo "@symlink stirmake.1" >> stage/+CONTENTS || die "echo fail"
echo "man/man1/smkt.1" >> stage/+CONTENTS || die "echo fail"
echo "@symlink stirmake.1" >> stage/+CONTENTS || die "echo fail"
echo "man/man1/smkp.1" >> stage/+CONTENTS || die "echo fail"
echo "@symlink stirmake.1" >> stage/+CONTENTS || die "echo fail"


tar --format=posix --owner=root --group=bin --mode="go-w" --no-recursion -cf stirmake-"$nonvname".tar -C stage +CONTENTS +DESC || die "tar fail"
tar --format=posix --owner=root --group=bin --mode="go-w" -P --no-recursion -rf stirmake-"$nonvname".tar -C stage \
  bin/stirmake \
  bin/smka \
  bin/smkt \
  bin/smkp \
  man/man1/stirmake.1 \
  man/man1/smka.1 \
  man/man1/smkt.1 \
  man/man1/smkp.1 || die "tar fail"
gzip -9 stirmake-"$nonvname".tar || die "gzip fail"
mv stirmake-"$nonvname".tar.gz stirmake-"$nonvname"-"$PKGARCH".tgz || die "mv fail"

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
