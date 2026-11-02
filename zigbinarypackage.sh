#!/bin/sh

PKGDIR="packages/"

die()
{
  echo "$@"
  exit 1
}

mkdir -p "$PKGDIR" || die "Can't create package directory"

if [ "$1" = "arm64" ]; then
  export CC="zig cc -target aarch64-linux-gnu.2.17"
  export PKGARCH="arm64-linux-glibc2.17"
  export DEBARCH="arm64"
  export RPMTGT="aarch64"
elif [ "$1" = "arm32" ]; then
  export CC="zig cc -target arm-linux-gnueabihf.2.7"
  export PKGARCH="arm32hf-linux-glibc2.7"
  export DEBARCH="armhf"
  export RPMTGT="armv7hl"
elif [ "$1" = "amd64" ]; then
  export CC="zig cc -target x86_64-linux-gnu.2.7"
  export PKGARCH="amd64-linux-glibc2.7"
  export DEBARCH="amd64"
  export RPMTGT="x86_64"
elif [ "$1" = "i386" ]; then
  export CC="zig cc -target x86-linux-gnu.2.7"
  export PKGARCH="i386-linux-glibc2.7"
  export DEBARCH="i386"
  export RPMTGT="i686"
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
tar --format=posix --owner=root --group=wheel --mode="go-w" -czvf stirmakebin-"$PKGARCH"-"$vname".tar.gz stirmake-"$vname"/stirmake stirmake-"$vname"/smka stirmake-"$vname"/smkt stirmake-"$vname"/smkp stirmake-"$vname"/install.sh stirmake-"$vname"/stirmake.1 || die "Can't create tar.gz"
mkdir -p stirmake_"$nonvname"_"$DEBARCH/usr/bin" || die "mkdir fail"
mkdir -p stirmake_"$nonvname"_"$DEBARCH/usr/share/man/man1" || die "mkdir fail"
mkdir -p stirmake_"$nonvname"_"$DEBARCH/DEBIAN" || die "mkdir fail"
mkdir -p rpmbuild/SOURCES || die "mkdir fail"
mkdir -p rpmbuild/SPECS || die "mkdir fail"
cp -p stirmake-"$vname"/stirmake stirmake_"$nonvname"_"$DEBARCH/usr/bin" || die "cp fail"
ln -s stirmake stirmake_"$nonvname"_"$DEBARCH/usr/bin/smka" || die "ln fail"
ln -s stirmake stirmake_"$nonvname"_"$DEBARCH/usr/bin/smkt" || die "ln fail"
ln -s stirmake stirmake_"$nonvname"_"$DEBARCH/usr/bin/smkp" || die "ln fail"
cp -p stirmake-"$vname"/stirmake.1 stirmake_"$nonvname"_"$DEBARCH/usr/share/man/man1" || die "cp fail"
ln -s stirmake.1 stirmake_"$nonvname"_"$DEBARCH/usr/share/man/man1/smka.1" || die "ln fail"
ln -s stirmake.1 stirmake_"$nonvname"_"$DEBARCH/usr/share/man/man1/smkt.1" || die "ln fail"
ln -s stirmake.1 stirmake_"$nonvname"_"$DEBARCH/usr/share/man/man1/smkp.1" || die "ln fail"
echo "Package: stirmake" > stirmake_"$nonvname"_"$DEBARCH/DEBIAN/control" || die "echo fail"
echo "Version: $nonvname" >> stirmake_"$nonvname"_"$DEBARCH/DEBIAN/control" || die "echo fail"
echo "Architecture: $DEBARCH" >> stirmake_"$nonvname"_"$DEBARCH/DEBIAN/control" || die "echo fail"
echo "Maintainer: Juha-Matti Tilli" >> stirmake_"$nonvname"_"$DEBARCH/DEBIAN/control" || die "echo fail"
echo "Description: Stirmake, scalable true implementation of recursive make, for easy multi-dir builds." >> stirmake_"$nonvname"_"$DEBARCH/DEBIAN/control" || die "echo fail"
dpkg-deb --build -Zgzip stirmake_"$nonvname"_"$DEBARCH" || die "dpkg-deb fail"
echo "Name: stirmake" > rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "Version: $nonvnameu" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
#echo "Release: 1%{?dist}" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "Release: 1" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "Summary: Scalable true implementation of recursive make" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "License: MIT" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "URL: https://github.com/Aalto5G/stirmake" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "Source0: stirmake" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "%define debug_package %{nil}" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "%define __strip /bin/true" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "%description" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "Stirmake, scalable true implementation of recursive make, for easy multi-dir builds." >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "%prep" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "# We override %prep and do nothing because there is no source archive to extract" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "%setup -c -T" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "%install" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
#echo "rm -rf %{buildroot}" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "mkdir -p %{buildroot}/usr/bin" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "mkdir -p %{buildroot}/usr/share/man/man1" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "cp %{_sourcedir}/stirmake %{buildroot}/usr/bin" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "chmod +x %{buildroot}/usr/bin/stirmake" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "ln -s stirmake %{buildroot}/usr/bin/smka" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "ln -s stirmake %{buildroot}/usr/bin/smkt" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "ln -s stirmake %{buildroot}/usr/bin/smkp" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "cp %{_sourcedir}/stirmake.1 %{buildroot}/usr/share/man/man1" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "ln -s stirmake.1.gz %{buildroot}/usr/share/man/man1/smka.1.gz" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "ln -s stirmake.1.gz %{buildroot}/usr/share/man/man1/smkt.1.gz" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "ln -s stirmake.1.gz %{buildroot}/usr/share/man/man1/smkp.1.gz" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "%files" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "/usr/bin/stirmake" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "/usr/bin/smka" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "/usr/bin/smkt" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "/usr/bin/smkp" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "/usr/share/man/man1/stirmake.1.gz" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "/usr/share/man/man1/smka.1.gz" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "/usr/share/man/man1/smkt.1.gz" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
echo "/usr/share/man/man1/smkp.1.gz" >> rpmbuild/SPECS/stirmake.spec || die "echo fail"
cp -p stirmake-"$vname"/stirmake rpmbuild/SOURCES || die "cp fail"
cp -p stirmake-"$vname"/stirmake.1 rpmbuild/SOURCES || die "cp fail"
rpmbuild --target="$RPMTGT" -D "_topdir $tmpdir/rpmbuild" -ba "$tmpdir/rpmbuild/SPECS/stirmake.spec" || die "rpmbuild fail"
rm -rf stirmake-"$vname"
rm -rf stirmake_"$nonvname"_"$DEBARCH"
cd "$pwd" || die "Can't chdir back to old directory"
if [ -e "$PKGDIR/stirmakebin-$PKGARCH-$vname.tar.gz" ]; then
  die "Package stirmakebin-$PKGARCH-$vname.tar.gz already exists"
else
  mv "$tmpdir/stirmakebin-$PKGARCH-$vname.tar.gz" "$PKGDIR"/ || die "Can't move package"
fi
if [ -e "$PKGDIR"/stirmake_"$nonvname"_"$DEBARCH".deb ]; then
  die "Package stirmake_${nonvname}_$DEBARCH.deb already exists"
else
  mv "$tmpdir/stirmake_${nonvname}_$DEBARCH.deb" "$PKGDIR"/ || die "Can't move package"
fi
if [ -e "$PKGDIR"/stirmake-${nonvnameu}-1.$RPMTGT.rpm ]; then
  die "Package stirmake-${nonvnameu}-1.$RPMTGT.rpm already exists"
else
  mv "$tmpdir/rpmbuild/RPMS/$RPMTGT/stirmake-${nonvnameu}-1.$RPMTGT.rpm" "$PKGDIR"/ || die "Can't move package"
fi
rm -rf "$tmpdir/rpmbuild"
rmdir "$tmpdir" || die "Can't rm temporary dir $tmpdir"
echo
echo "Package created:"
echo "stirmakebin-$PKGARCH-$vname.tar.gz"
echo "stirmake_${nonvname}_${DEBARCH}.deb"
echo "stirmake-${nonvnameu}-1_${RPMTGT}.rpm"
