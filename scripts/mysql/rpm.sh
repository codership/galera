#!/bin/bash -e

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
export WSPATCH_REVNO=$WSREP_REV
if [ -r "VERSION" ]
then
    . "VERSION"
    WSREP_API=$(grep WSREP_INTERFACE_VERSION wsrep/wsrep_api.h | cut -d '"' -f 2)
    WSREP_PATCH=$(grep SET\(WSREP_PATCH_VERSION cmake/wsrep.cmake | cut -d '"' -f 2)
    export MYSQL_VER=$MYSQL_VERSION_MAJOR.$MYSQL_VERSION_MINOR.$MYSQL_VERSION_PATCH
else
    MYSQL_VERSION_MINOR=1
    WSREP_API=$(grep WSREP_API= config/ac-macros/wsrep.m4 | cut -d '=' -f 2)
    WSREP_PATCH=$(grep WSREP_PATCH= config/ac-macros/wsrep.m4 | cut -d '=' -f 2)
    export MYSQL_VER=`grep AC_INIT configure.in | awk -F '[' '{ print $3 }'| awk -F ']' '{ print $1 }'`
fi

if test -z "$MYSQL_VER"
then
    echo "Could not determine mysql version."
    exit -1
fi

MYSQL_VERSION_EXTRA="_wsrep_$WSREP_API.$WSREP_PATCH"
MYSQL_VERSION_FINAL=${MYSQL_VER}${MYSQL_VERSION_EXTRA}

popd #MYSQL_SRC

RPM_BUILD_ROOT=$(pwd)/redhat
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

MYSQL_DIST=$(tar -tzf $MYSQL_DIST_TARBALL | head -n1 | sed 's/\/$//')
rm -rf $MYSQL_DIST; tar -xzf $MYSQL_DIST_TARBALL

# rename according to MYSQL_VERSION_FINAL
test "$MYSQL_DIST" != "mysql-$MYSQL_VERSION_FINAL" && \
    rm -rf "mysql-$MYSQL_VERSION_FINAL"             && \
    mv "$MYSQL_DIST" "mysql-$MYSQL_VERSION_FINAL"   && \
    MYSQL_DIST="mysql-$MYSQL_VERSION_FINAL"

pushd $MYSQL_DIST

if test -r "$2" # check if patch name was supplied
then # patch as a file
    WSREP_PATCH=$(cd $(dirname "$2"); pwd -P)/$(basename "$2")
else # generate patch for this particular MySQL version from LP
    WSREP_PATCH=$($SCRIPT_ROOT/get_patch.sh mysql-$MYSQL_VER $MYSQL_SRC)
fi

# patch freaks out on .bzrignore which doesn't exist in source dist and
# returns error code
patch -p1 -f < $WSREP_PATCH || :
# need to fix permissions on 5.1
[ $MYSQL_VERSION_MINOR -eq 1 ] && chmod a+x ./BUILD/*wsrep

# update configure script for 5.1
test $MYSQL_VERSION_MINOR -le 5 && ./BUILD/autorun.sh

time tar -C .. -czf $RPM_BUILD_ROOT/SOURCES/"$MYSQL_DIST.tar.gz" \
              "$MYSQL_DIST"

######################################
##                                  ##
##         Create spec file         ##
##                                  ##
######################################

export MAKE="make -j $(cat /proc/cpuinfo | grep -c ^processor)"

[ "$MYSQL_VERSION_MAJOR$MYSQL_VERSION_MINOR" -ge "56" ] \
&& MEMCACHED_OPT="-DWITH_LIBEVENT=system -DWITH_INNODB_MEMCACHED=ON" \
|| MEMCACHED_OPT=""
export MEMCACHED_OPT # for RPM build

if [ $MYSQL_VERSION_MINOR -eq 1 ]
then
    ./configure --with-wsrep > /dev/null
    pushd support-files && rm -rf *.spec &&  make > /dev/null &&  popd
else
    cmake \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
    -DBUILD_CONFIG=mysql_release \
    -DWITH_WSREP=1 \
    -DWITH_EXTRA_CHARSETS=all \
    -DWITH_SSL=system \
    -DWITH_ZLIB=system \
    $MEMCACHED_OPT
    $MYSQL_SRC \
    && make -S
fi

######################################
##                                  ##
##       Build binary tar.gz        ##
##                                  ##
######################################

[ $MYSQL_VERSION_MINOR -eq 1 ] && make bin-dist || make package

# Fix the name of the binary package to contain wsrep suffix
OLD_BIN_NAME=$(ls mysql-$MYSQL_VER-linux-*.tar.gz | sed s/\.tar\.gz//)
NEW_BIN_NAME=$(echo $OLD_BIN_NAME | sed s/-linux/$MYSQL_VERSION_EXTRA-linux/)
echo "Repacking $OLD_BIN_NAME -> $NEW_BIN_NAME"
tar -xzf $OLD_BIN_NAME.tar.gz && rm $OLD_BIN_NAME.tar.gz
mv $OLD_BIN_NAME $NEW_BIN_NAME
tar -czf $NEW_BIN_NAME.tar.gz $NEW_BIN_NAME && rm -rf $NEW_BIN_NAME
popd # MYSQL_DIST

WSREP_SPEC=${WSREP_SPEC:-"$MYSQL_DIST/support-files/mysql.spec"}
mv $WSREP_SPEC $RPM_BUILD_ROOT/SPECS/$MYSQL_DIST.spec
WSREP_SPEC=$RPM_BUILD_ROOT/SPECS/$MYSQL_DIST.spec

mv $WSREP_PATCH ./$MYSQL_DIST.patch
mv $MYSQL_DIST/$MYSQL_DIST-linux-*.tar.gz ./
#cleaning intermedieate sources:
rm -rf $MYSQL_DIST

######################################
##                                  ##
##            Build RPM             ##
##                                  ##
######################################

if [ $MYSQL_VERSION_MINOR == 1 ]
then
    # cflags vars might be obsolete with 5.5
    wsrep_cflags="-DWSREP_PROC_INFO -DMYSQL_MAX_VARIABLE_VALUE_LEN=2048"
    fast_cflags="-O3 -fno-omit-frame-pointer"
    uname -m | grep -q i686 && \
    cpu_cflags="-mtune=i686" || cpu_cflags="-mtune=core2"
    RPM_OPT_FLAGS="$fast_cflags $cpu_cflags $wsrep_cflags"
fi

RPMBUILD()
{
    if [ $MYSQL_VERSION_MINOR -lt 5 ]
    then
        WSREP_RPM_OPTIONS=(--with wsrep --with yassl \
                           --define "optflags $RPM_OPT_FLAGS")
    else
        WSREP_RPM_OPTIONS=(--define='with_wsrep 1' \
                           --define='distro_specific 1' \
                           --define='runselftest 0' \
                           --define='with_ssl system' \
                           --define='mysql_packager Codership Oy <info@codership.com>')
    fi

    $(which rpmbuild) --rmsource --define "_topdir $RPM_BUILD_ROOT" \
                      "${WSREP_RPM_OPTIONS[@]}" -ba $WSREP_SPEC
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
mv $WSREP_SPEC ./
uname -m | grep -q i686 && ARCH=i386 || ARCH=x86_64
mv $RPM_BUILD_ROOT/RPMS/$ARCH/MySQL-server-*.rpm ./

# remove the patch file if is was automatically generated
if test ! -r "$2"; then rm -rf $WSREP_PATCH; fi

exit 0

