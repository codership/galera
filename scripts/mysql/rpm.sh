#!/bin/bash

if test -z "$MYSQL_SRC"
then
    echo "MYSQL_SRC variable pointing at MySQL/wsrep sources is not set. Can't continue."
    exit -1
fi

usage()
{
    echo -e "Usage: $0 <pristine src tarball> <patch> [spec file]"
}

# Parse command line
if test $# -lt 2
then
    usage
    exit 1
fi

set -x
set -e

MYSQL_DIST_TARBALL=$(cd $(dirname "$1"); pwd -P)/$(basename "$1")
WSREP_PATCH=$(cd $(dirname "$2"); pwd -P)/$(basename "$2")
#WSREP_SPEC="$3"
# NOTE: later WSREP_PATCH and WSREP_SPEC can be generated from the working copy

# Absolute path of this script folder
SCRIPT_ROOT=$(cd $(dirname $0); pwd -P)
THIS_DIR=$(pwd -P)

######################################
##                                  ##
##          Prepare patch           ##
##                                  ##
######################################
# Source paths are either absolute or relative to script, get absolute
MYSQL_SRC=$(cd $MYSQL_SRC; pwd -P; cd $THIS_DIR)
pushd $MYSQL_SRC
export WSREP_REV=$(bzr revno)
#MYSQL_LP_REV=$(bzr tags | grep -m1 $MYSQL_VER | awk '{ print $2 }')
#WSREP_PATCH="$THIS_DIR/$MYSQL_LP_REV-$WSREP_REV.diff"
#bzr diff -p1 --diff-options=' -x .bzrignore ' > "$WSREP_PATCH"
popd #MYSQL_SRC

RPM_BUILD_ROOT=/tmp/redhat
rm -rf RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
pushd $RPM_BUILD_ROOT
mkdir -p BUILD RPMS SOURCES SPECS SRPMS
pushd RPMS
mkdir -p athlon i386 i486 i586 i686 noarch
popd; popd

######################################
##                                  ##
##          Prepare sources         ##
##                                  ##
######################################
#FIXME: fix spec file to make rpmbuild do it

MYSQL_DIST=$(tar -tzf $MYSQL_DIST_TARBALL | head -n1)
rm -rf $MYSQL_DIST; tar -xzf $MYSQL_DIST_TARBALL
pushd $MYSQL_DIST
# patch freaks out on .bzrignore which doesn't exist in source dist and
# returns error code - running in a subshell because of this
(patch -p1 -f < $WSREP_PATCH)
time ./BUILD/autorun.sh
#time ./configure --with-wsrep > /dev/null # TODO: this should be moved one step
time tar -C .. -czf $RPM_BUILD_ROOT/SOURCES/$(basename "$MYSQL_DIST_TARBALL") \
              "$MYSQL_DIST"

######################################
##                                  ##
##         Create spec file         ##
##                                  ##
######################################
time ./configure --with-wsrep > /dev/null
pushd support-files; rm -rf *.spec;  make > /dev/null; popd
MYSQL_VER=$(grep 'MYSQL_NO_DASH_VERSION' Makefile | cut -d ' ' -f 3)
popd # MYSQL_DIST

WSREP_SPEC=${WSREP_SPEC:-"$MYSQL_DIST/support-files/mysql-$MYSQL_VER.spec"}
mv $WSREP_SPEC $RPM_BUILD_ROOT/SPECS/
WSREP_SPEC=$RPM_BUILD_ROOT/SPECS/mysql-$MYSQL_VER.spec

#cleaning intermedieate sources:
rm -rf $MYSQL_DIST

i686_cflags="-march=i686 -mtune=i686"
amd64_cflags="-m64 -mtune=opteron"
fast_cflags="-O3 -fno-omit-frame-pointer"
uname -m | grep -q i686 && \
export RPM_OPT_FLAGS="$i686_cflags $fast_cflags"   || \
export RPM_OPT_FLAGS="$amd64_cflags $fast_cflags"

RPMBUILD="rpmbuild --clean --rmsource \
          --define \"_topdir $RPM_BUILD_ROOT\" \
          --define \"optflags $RPM_OPT_FLAGS\" \
          --with wsrep -ba $WSREP_SPEC "

cd "$RPM_BUILD_ROOT"
if [ "$(whoami)" == "root" ]
then
    chown -R mysql $RPM_BUILD_ROOT
    su mysql -c "$RPMBUILD"
else
    $RPMBUILD
fi

exit 0

