#!/bin/sh

PKGDIR="packages/"

die()
{
  echo "$@"
  exit 1
}

mkdir -p "$PKGDIR" || die "Can't create package directory"

if [ "$1" = "arm64.14" ]; then
  export CC="zig cc -target aarch64-freebsd.14.0"
  export PKGARCH="arm64-freebsd-14.0"
  export PKGARCHNAME="freebsd:14:aarch64"
elif [ "$1" = "arm64.15" ]; then
  export CC="zig cc -target aarch64-freebsd.15.0"
  export PKGARCH="arm64-freebsd-15.0"
  export PKGARCHNAME="freebsd:15:aarch64"
elif [ "$1" = "arm32.14" ]; then
  export CC="zig cc -target arm-freebsd.14.0"
  export PKGARCH="arm32hf-freebsd-14.0"
  export PKGARCHNAME="freebsd:14:armv7"
elif [ "$1" = "arm32.15" ]; then
  export CC="zig cc -target arm-freebsd.15.0"
  export PKGARCH="arm32hf-freebsd-15.0"
  export PKGARCHNAME="freebsd:15:armv7"
elif [ "$1" = "amd64.14" ]; then
  export CC="zig cc -target x86_64-freebsd.14.0"
  export PKGARCH="amd64-freebsd-14.0"
  export PKGARCHNAME="freebsd:14:amd64"
elif [ "$1" = "amd64.15" ]; then
  export CC="zig cc -target x86_64-freebsd.15.0"
  export PKGARCH="amd64-freebsd-15.0"
  export PKGARCHNAME="freebsd:15:amd64"
elif [ "$1" = "i386.14" ]; then
  export CC="zig cc -target x86-freebsd.14.0"
  export PKGARCH="i386-freebsd-14.0"
  export PKGARCHNAME="freebsd:14:i386"
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

mkdir -p stage/usr/local/bin || die "mkdir fail"
mkdir -p stage/usr/local/share/man/man1 || die "mkdir fail"

cp -p stirmake-"$vname"/stirmake stage/usr/local/bin || die "cp fail"
ln -s stirmake stage/usr/local/bin/smka || die "ln fail"
ln -s stirmake stage/usr/local/bin/smkt || die "ln fail"
ln -s stirmake stage/usr/local/bin/smkp || die "ln fail"
cp -p stirmake-"$vname"/stirmake.1 stage/usr/local/share/man/man1 || die "cp fail"
ln -s stirmake.1 stage/usr/local/share/man/man1/smka.1 || die "ln fail"
ln -s stirmake.1 stage/usr/local/share/man/man1/smkt.1 || die "ln fail"
ln -s stirmake.1 stage/usr/local/share/man/man1/smkp.1 || die "ln fail"
echo '{' > stage/+MANIFEST || die "echo fail"
echo '"name": "stirmake",' >> stage/+MANIFEST || die "echo fail"
echo "\"version\": \"$nonvname\"," >> stage/+MANIFEST || die "echo fail"
echo '"origin": "devel/stirmake",' >> stage/+MANIFEST || die "echo fail"
echo "\"arch\": \"$PKGARCHNAME\"," >> stage/+MANIFEST || die "echo fail"
echo '"comment": "Scalable true implementation of recursive make",' >> stage/+MANIFEST || die "echo fail"
echo '"desc": "Stirmake, scalable true implementation of recursive make, for easy multi-dir builds.",' >> stage/+MANIFEST || die "echo fail"
echo '"maintainer": "juha-matti.tilli@iki.fi",' >> stage/+MANIFEST || die "echo fail"
echo '"www": "https://github.com/Aalto5G/stirmake",' >> stage/+MANIFEST || die "echo fail"
echo '"prefix": "/usr/local",' >> stage/+MANIFEST || die "echo fail"
echo "\"flatsize\": `du -bc stage/usr/local|tail -n1|cut -f1`," >> stage/+MANIFEST || die "echo fail"
echo '"files": {' >> stage/+MANIFEST || die "echo fail"
echo "  \"/usr/local/bin/stirmake\": \"`sha256sum stage/usr/local/bin/stirmake|awk '{print $1}'`\"," >> stage/+MANIFEST || die "echo fail"
echo "  \"/usr/local/bin/smka\": \"-\"," >> stage/+MANIFEST || die "echo fail"
echo "  \"/usr/local/bin/smkt\": \"-\"," >> stage/+MANIFEST || die "echo fail"
echo "  \"/usr/local/bin/smkp\": \"-\"," >> stage/+MANIFEST || die "echo fail"
echo "  \"/usr/local/share/man/man1/stirmake.1\": \"`sha256sum stage/usr/local/share/man/man1/stirmake.1|awk '{print $1}'`\"," >> stage/+MANIFEST || die "echo fail"
echo "  \"/usr/local/share/man/man1/smka.1\": \"-\"," >> stage/+MANIFEST || die "echo fail"
echo "  \"/usr/local/share/man/man1/smkt.1\": \"-\"," >> stage/+MANIFEST || die "echo fail"
echo "  \"/usr/local/share/man/man1/smkp.1\": \"-\"" >> stage/+MANIFEST || die "echo fail"
echo "}" >> stage/+MANIFEST || die "echo fail"
echo '}' >> stage/+MANIFEST || die "echo fail"

echo 'name: "stirmake"' > stage/+COMPACT_MANIFEST || die "echo fail"
echo "version: \"$nonvname\"" >> stage/+COMPACT_MANIFEST || die "echo fail"
echo 'origin: "devel/stirmake"' >> stage/+COMPACT_MANIFEST || die "echo fail"
echo 'comment: "Scalable true implementation of recursive make"' >> stage/+COMPACT_MANIFEST || die "echo fail"
echo 'maintainer: "juha-matti.tilli@iki.fi"' >> stage/+COMPACT_MANIFEST || die "echo fail"
echo 'www: "https://github.com/Aalto5G/stirmake"' >> stage/+COMPACT_MANIFEST || die "echo fail"
echo 'prefix: "/usr/local"' >> stage/+COMPACT_MANIFEST || die "echo fail"

tar --format=posix --no-recursion -cf stirmake-"$nonvname".tar -C stage +MANIFEST +COMPACT_MANIFEST || die "tar fail"
tar --format=posix -P --no-recursion --transform='s|^usr/|/usr/|' -rf stirmake-"$nonvname".tar -C stage \
  usr/local/bin/stirmake \
  usr/local/bin/smka \
  usr/local/bin/smkt \
  usr/local/bin/smkp \
  usr/local/share/man/man1/stirmake.1 \
  usr/local/share/man/man1/smka.1 \
  usr/local/share/man/man1/smkt.1 \
  usr/local/share/man/man1/smkp.1 || die "tar fail"
xz -9 stirmake-"$nonvname".tar || die "xz fail"
mv stirmake-"$nonvname".tar.xz stirmake-"$nonvname"-"$PKGARCH".pkg || die "mv fail"

rm -rf stirmake-"$vname"
rm -rf stirmake_"$nonvname"_"$DEBARCH"
rm -rf stage
cd "$pwd" || die "Can't chdir back to old directory"
if [ -e "$PKGDIR/stirmakebin-$PKGARCH-$vname.tar.gz" ]; then
  die "Package stirmakebin-$PKGARCH-$vname.tar.gz already exists"
else
  mv "$tmpdir/stirmakebin-$PKGARCH-$vname.tar.gz" "$PKGDIR"/ || die "Can't move package"
fi
if [ -e "$PKGDIR"/stirmake-"$nonvname"-"$PKGARCH".pkg ]; then
  die "Package stirmake-${nonvname}-$PKGARCH.pkg already exists"
else
  mv "$tmpdir/stirmake-${nonvname}-$PKGARCH.pkg" "$PKGDIR"/ || die "Can't move package"
fi
rmdir "$tmpdir" || die "Can't rm temporary dir $tmpdir"
echo
echo "Package created:"
echo "stirmakebin-$PKGARCH-$vname.tar.gz"
echo "stirmake-${nonvname}-$PKGARCH.pkg"
