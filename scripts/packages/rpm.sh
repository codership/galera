#!/bin/bash -eu

if [ $# -ne 1 ]
then
    echo "Usage: $0 <version>"
    exit 1
fi

set -x

# Absolute path of this script folder
SCRIPT_ROOT=$(cd $(dirname $0); pwd -P)
THIS_DIR=$(pwd -P)

RPM_TOP_DIR=$SCRIPT_ROOT/rpm_top_dir
rm -rf $RPM_TOP_DIR
mkdir -p $RPM_TOP_DIR/RPMS
ln -s ../../../ $RPM_TOP_DIR/BUILD

fast_cflags="-O3 -fno-omit-frame-pointer"
uname -m | grep -q i686 && \
cpu_cflags="-mtune=i686" || cpu_cflags="-mtune=core2"
RPM_OPT_FLAGS="$fast_cflags $cpu_cflags"
GALERA_SPEC=$SCRIPT_ROOT/galera.spec

$(which rpmbuild) --clean --define "_topdir $RPM_TOP_DIR" \
                  --define "optflags $RPM_OPT_FLAGS" \
                  --define "version $1" \
                  -bb --short-circuit -bi $GALERA_SPEC

uname -m | grep -q i686 && ARCH=i386 || ARCH=x86_64
mv $RPM_TOP_DIR/RPMS/$ARCH/galera-*.rpm ./

rm -rf $RPM_TOP_DIR

exit 0

