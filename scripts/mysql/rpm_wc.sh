#!/bin/bash

# This script tries to build RPMs from MySQL/wsrep working copy
# probably will never work due to lack of essential files (manpages, etc.)

if test -z "$MYSQL_SRC"
then
    echo "MYSQL_SRC variable pointing at MySQL/wsrep sources is not set. Can't continue."
    exit -1
fi

usage()
{
    echo -e "Usage: build.sh [OPTIONS] \n" \
	"Options:                      \n" \
	"    -r|--release    configure build with debug disabled (implies -c)\n"\
	"    -d|--debug      configure build with debug enabled (implies -c)\n"\
	"    --no-strip      prevent stripping of release binaries\n"\
        "\n -s and -b options affect only Galera build.\n"
}

# Parse command line
while test $# -gt 0
do
    case $1 in
        -r|--release)
            RELEASE=yes   # Compile without debug
            ;;
	-d|--debug)
            DEBUG=yes     # Compile with debug
	    NO_STRIP=yes  # Don't strip the binaries
	    ;;
	--no-strip)
	    NO_STRIP=yes  # Don't strip the binaries
	    ;;
	--help)
	    usage
	    exit 0
	    ;;
	*)
	    usage
	    exit 1
	    ;;
    esac
    shift
done

set -x
set -e

# Absolute path of this script folder
BUILD_ROOT=$(cd $(dirname $0); pwd -P)
#GALERA_SRC=${GALERA_SRC:-$BUILD_ROOT/../../}
# Source paths are either absolute or relative to script, get absolute
MYSQL_SRC=$(cd $MYSQL_SRC; pwd -P; cd $BUILD_ROOT)


######################################
##                                  ##
##           Build MySQL            ##
##                                  ##
######################################
# Obtain MySQL version and revision of Galera patch
pushd $MYSQL_SRC

# make dist
if test -f Makefile
then
    time make maintainer-clean > /dev/null
#    make distclean
fi
#if ! test -f configure
#then
    time BUILD/autorun.sh
#fi
WSREP_REV=$(bzr revno); export WSREP_REV
time ./configure --with-wsrep > /dev/null

# MySQL has a mindblowing make system that requires extra/comp_err to be built
# for 'make dist'. comp_err requires prebuilt mysys and dbug but they are not
# built automatically by make dist.
pushd include; make > /dev/null; popd
pushd strings; make > /dev/null; popd
pushd mysys;   make > /dev/null; popd
pushd dbug;    make > /dev/null; popd
pushd support-files; rm -rf *.spec;  make > /dev/null; popd
pushd libmysql;   make link_sources > /dev/null; popd
pushd libmysql_r; make link_sources > /dev/null; popd
pushd libmysqld;  make link_sources > /dev/null; popd
pushd client;     make link_sources > /dev/null; popd
time make dist > /dev/null

MYSQL_VER=$(grep 'MYSQL_NO_DASH_VERSION' $MYSQL_SRC/Makefile | cut -d ' ' -f 3)

#if test -d /usr/src/redhat
#then
#export RPM_BUILD_ROOT=/usr/src/redhat
#else
RPM_BUILD_ROOT=/tmp/redhat
#fi
mkdir -p $RPM_BUILD_ROOT
pushd $RPM_BUILD_ROOT
mkdir -p BUILD RPMS SOURCES SPECS SRPMS
pushd RPMS
mkdir -p athlon i386 i486 i586 i686 noarch
popd; popd

mv mysql-$MYSQL_VER.tar.gz $RPM_BUILD_ROOT/SOURCES/

MYSQL_SPEC=$MYSQL_SRC/support-files/mysql-$MYSQL_VER.spec
mv $MYSQL_SPEC $RPM_BUILD_ROOT/SPECS
MYSQL_SPEC=$RPM_BUILD_ROOT/SPECS/mysql-$MYSQL_VER.spec

i686_cflags="-march=i686 -mtune=i686"
amd64_cflags="-m64 -mtune=opteron"
fast_cflags="-O3 -fno-omit-frame-pointer"
uname -m | grep -q i686 && \
export RPM_OPT_FLAGS="$i686_cflags $fast_cflags"   || \
export RPM_OPT_FLAGS="$amd64_cflags $fast_cflags"


RPMBUILD="rpmbuild --clean --rmsource \
          --define \"_topdir $RPM_BUILD_ROOT\" \
          --define \"optflags $RPM_OPT_FLAGS\" \
          --with wsrep -ba $MYSQL_SPEC \
          2>&1 > $RPM_BUILD_ROOT/rpmbuild.log"

chown -R mysql $RPM_BUILD_ROOT
su mysql -c "$RPMBUILD"

exit 0

