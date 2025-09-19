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
mkdir -p $RPM_TOP_DIR/{BUILD,RPMS,SOURCES,SPECS,SRPMS}
PROJECT_ROOT=$(cd $SCRIPT_ROOT/../..; pwd -P)
CMAKE_BUILD_DIR=$RPM_TOP_DIR/cmake_build
mkdir -p $CMAKE_BUILD_DIR

pushd $CMAKE_BUILD_DIR > /dev/null
# Generate CPack config
cmake $PROJECT_ROOT
# Create source package with the specified version
cpack --config CPackSourceConfig.cmake -D CPACK_PACKAGE_VERSION=$1 -G TGZ
mv galera-4-$1.tar.gz $RPM_TOP_DIR/SOURCES/
popd > /dev/null

export CK_TIMEOUT_MULTIPLIER=7

GALERA_SPEC=$SCRIPT_ROOT/galera-4.spec

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
elif [ "${DISTRO_VERSION}" = "sles42" ]
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
  VERSION_ID=${VERSION_ID:-}
  DIST_TAG=".${ID:-unknown}${VERSION_ID%%.*}"
fi

rpmbuild --clean --define "_topdir $RPM_TOP_DIR" \
                  --define "version $1" \
                  --define "dist ${DIST_TAG}" \
                  ${JOBS:+--define "_smp_mflags -j$JOBS"} \
                  -bb $GALERA_SPEC || (rpm --showrc && exit 1)

RPM_ARCH=$(rpm --showrc | grep "^build arch" | awk '{print $4}')

mv $RPM_TOP_DIR/RPMS/$RPM_ARCH/galera-*.rpm ./

rm -rf $RPM_TOP_DIR

exit 0

