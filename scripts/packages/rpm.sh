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
GALERA_SPEC=$SCRIPT_ROOT/galera-obs.spec

RELEASE=${RELEASE:-"1"}
DISTRO_VERSION=

if  [ -r /etc/os-release ]
then
    source /etc/os-release
elif [ -r /etc/SuSE-release ]
then
    DISTRO_VERSION=sles$(rpm -qf --qf '%{version}\n' /etc/SuSE-release | cut -d. -f1)
fi

DIST_TAG=
# %dist does not return a value for sles12
# https://bugs.centos.org/view.php?id=3239
if [ "${DISTRO_VERSION}" = "sles12" ]
then
  DIST_TAG=".sles12"
fi

if [ "${DISTRO_VERSION}" = "sles42" ]
then
  DIST_TAG=".sles42"
fi

if [ -z "$DIST_TAG" ]
then
  DIST_TAG=$(rpm --eval "%{dist}")
  if [ "$DIST_TAG" = "%{dist}" ]
  then
    DIST_TAG=
  fi
fi

# from /etc/os-release
if  [ -z "$DIST_TAG" ]
then
  DIST_TAG=".${ID}${VERSION_ID%%.*}"
fi

$(which rpmbuild) --clean --define "_topdir $RPM_TOP_DIR" \
                  --define "optflags $RPM_OPT_FLAGS" \
                  --define "version $1" \
                  --define "release $RELEASE" \
                  -bb --short-circuit -bi $GALERA_SPEC

RPM_ARCH=$(uname -m | sed s/i686/i386/)

mv $RPM_TOP_DIR/RPMS/$RPM_ARCH/galera-*.rpm ./

rm -rf $RPM_TOP_DIR

exit 0

