#!/bin/bash

if test -z "$MYSQL_SRC"
then
    echo "MYSQL_SRC variable pointing at MySQL/wsrep sources is not set. Can't continue."
    exit -1
fi

usage()
{
    echo -e "Usage: $0 <pristine src tarball> [patch file] [spec file]"
}

# Parse command line
if test $# -lt 1
then
    usage
    exit 1
fi

set -e

# Absolute path of this script folder
SCRIPT_ROOT=$(cd $(dirname $0); pwd -P)
THIS_DIR=$(pwd -P)

set -x

MYSQL_DIST_TARBALL=$(cd $(dirname "$1"); pwd -P)/$(basename "$1")

######################################
##                                  ##
##          Prepare patch           ##
##                                  ##
######################################
# Source paths are either absolute or relative to script, get absolute
MYSQL_SRC=$(cd $MYSQL_SRC; pwd -P; cd $THIS_DIR)
pushd $MYSQL_SRC
export WSREP_REV=$(bzr revno)
export MYSQL_VER=`grep PACKAGE_VERSION include/config.h | awk '{gsub(/\"/,""); print $3; }'`
if test -z "$MYSQL_VER"
then # configure should help us here
    ./configure
    export MYSQL_VER=`grep PACKAGE_VERSION include/config.h | awk '{gsub(/\"/,""); print $3; }'`
    if test -z "$MYSQL_VER"
    then
	echo "Could not determine mysql version."
	exit -1
    fi
fi

popd #MYSQL_SRC

RPM_BUILD_ROOT=/tmp/redhat
rm -rf $RPM_BUILD_ROOT
mkdir -p $RPM_BUILD_ROOT
pushd $RPM_BUILD_ROOT
mkdir -p BUILD RPMS SOURCES SPECS SRPMS
pushd RPMS
mkdir -p athlon i386 i486 i586 i686 noarch x86_64
popd; popd

######################################
##                                  ##
##      Prepare patched source      ##
##                                  ##
######################################
#FIXME: fix spec file to make rpmbuild do it

MYSQL_DIST=$(tar -tzf $MYSQL_DIST_TARBALL | head -n1)
rm -rf $MYSQL_DIST; tar -xzf $MYSQL_DIST_TARBALL
pushd $MYSQL_DIST

if test -r "$2" # check if patch name was supplied
then # patch as a file
    WSREP_PATCH=$(cd $(dirname "$2"); pwd -P)/$(basename "$2")
else # generate patch for this particular MySQL version from LP
    WSREP_PATCH=$($SCRIPT_ROOT/get_patch.sh mysql-$MYSQL_VER $MYSQL_SRC)
fi
# patch freaks out on .bzrignore which doesn't exist in source dist and
# returns error code - running in subshell to ignore it
(patch -p1 -f < $WSREP_PATCH)
time ./BUILD/autorun.sh # update configure script
time tar -C .. -czf $RPM_BUILD_ROOT/SOURCES/$(basename "$MYSQL_DIST_TARBALL") \
              "$MYSQL_DIST"

######################################
##                                  ##
##         Create spec file         ##
##                                  ##
######################################
time ./configure --with-wsrep > /dev/null
pushd support-files; rm -rf *.spec;  make > /dev/null; popd
popd # MYSQL_DIST

WSREP_SPEC=${WSREP_SPEC:-"$MYSQL_DIST/support-files/mysql-$MYSQL_VER.spec"}
mv $WSREP_SPEC $RPM_BUILD_ROOT/SPECS/
WSREP_SPEC=$RPM_BUILD_ROOT/SPECS/mysql-$MYSQL_VER.spec

#cleaning intermedieate sources:
cp $WSREP_PATCH ./mysql-$MYSQL_VER-wsrep-0.8.1.patch
rm -rf $MYSQL_DIST

######################################
##                                  ##
##             Build it             ##
##                                  ##
######################################
wsrep_cflags="-DWSREP_PROC_INFO -DMYSQL_MAX_VARIABLE_VALUE_LEN=2048"
fast_cflags="-O3 -fno-omit-frame-pointer -mtune=core2"
#i686_cflags="-march=i686 -mtune=i686"
#amd64_cflags="-m64 -mtune=opteron"
uname -m | grep -q i686 && \
cpu_cflags="-mtune=i686" || cpu_cflags="-mtune=core2"
export RPM_OPT_FLAGS="$fast_cflags $cpu_cflags $wsrep_cflags"
export MAKE="make -j $(cat /proc/cpuinfo | grep -c ^processor)"

RPMBUILD()
{
$(which rpmbuild) --clean --rmsource --define "_topdir $RPM_BUILD_ROOT" \
        	  --define "optflags $RPM_OPT_FLAGS" --with wsrep \
        	  -ba $WSREP_SPEC
}

pushd "$RPM_BUILD_ROOT"
if [ "$(whoami)" == "root" ]
then
    chown -R mysql $RPM_BUILD_ROOT
    su mysql -c RPMBUILD
else
    RPMBUILD
fi
popd

######################################
##                                  ##
##     Copy required files here     ##
##                                  ##
######################################
pwd
cp $WSREP_SPEC ./
uname -m | grep -q i686 && ARCH=i686 || ARCH=x86_64
cp $RPM_BUILD_ROOT/RPMS/$ARCH/MySQL-server-*.rpm ./

# remove the patch file if is was automatically generated
if test ! -r "$2"; then rm -rf $WSREP_PATCH; fi

exit 0

