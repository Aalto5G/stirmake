#!/bin/sh

PKGDIR="packages/"

die()
{
  echo "$@"
  exit 1
}

mkdir -p "$PKGDIR" || die "Can't create package directory"

if [ "$1" = "arm64" ]; then
  export CC="zig cc -target aarch64-macos.11.0-none"
  export AR="zig ar"
  export PKGARCH="arm64-macos-11"
elif [ "$1" = "amd64" ]; then
  export CC="zig cc -target x86_64-macos.10.10-none"
  export AR="zig ar"
  export PKGARCH="amd64-macos.10.10"
else
  die "Unsupported architecture: $1"
fi

tmpdir="`mktemp -d`"
pwd="`pwd`"
cd "$tmpdir" || die "Can't goto tmp dir"
git clone --recursive https://github.com/Aalto5G/stirmake || die "Can't clone"
cd stirmake/stirc || die "Can't chdir"
smka -ja || die "Can't compile"
# These are unnecessary and increase tarball size:
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
tar --format=posix --owner=root:0 --group=root:0 --mode="go-w" -czvf stirmakebin-"$PKGARCH"-"$vname".tar.gz stirmake-"$vname"/stirmake stirmake-"$vname"/smka stirmake-"$vname"/smkt stirmake-"$vname"/smkp stirmake-"$vname"/install.sh stirmake-"$vname"/stirmake.1 || die "Can't create tar.gz"
rm -rf stirmake-"$vname"
cd "$pwd" || die "Can't chdir back to old directory"
if [ -e "$PKGDIR/stirmakebin-$PKGARCH-$vname.tar.gz" ]; then
  die "Package stirmakebin-$PKGARCH-$vname.tar.gz already exists"
else
  mv "$tmpdir/stirmakebin-$PKGARCH-$vname.tar.gz" "$PKGDIR"/ || die "Can't move package"
fi
rmdir "$tmpdir" || die "Can't rm temporary dir $tmpdir"
echo
echo "Package created:"
echo "stirmakebin-$PKGARCH-$vname.tar.gz"
