#!/bin/bash

if test -z "$MYSQL_SRC"
then
    echo "MYSQL_SRC variable pointing at Galera sources is not set. Can't continue."
    exit -1
fi

GCOMM_IMPL=${GCOMM_IMPL:-"galeracomm"}

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
if ! test -f configure
then
    BUILD/autorun.sh
fi
WSREP_REV=$(bzr revno); export WSREP_REV
time ./configure --with-wsrep > /dev/null

# MySQL has a mindblowing make system that requires extra/comp_err to be built
# for 'make dist'. comp_err requires prebuilt mysys and dbug but they are not
# built automatically by make dist.
pushd include; make > /dev/null; popd
pushd strings; make > /dev/null; popd
pushd mysys;   make > /dev/null; popd
pushd dbug;    make > /dev/null; popd
#pushd extra   && make && popd
#pushd scripts && make && popd

time make dist > /dev/null

MYSQL_VER=$(grep '#define VERSION' $MYSQL_SRC/include/config.h | sed s/\"//g | cut -d ' ' -f 3 | cut -d '-' -f 1-2)

export RPM_BUILD_ROOT=$BUILD_ROOT/redhat
mkdir -p $RPM_BUILD_ROOT/BUILD
mkdir -p $RPM_BUILD_ROOT/RPMS
mkdir -p $RPM_BUILD_ROOT/SOURCES
mkdir -p $RPM_BUILD_ROOT/SPECS
mkdir -p $RPM_BUILD_ROOT/SRPMS

mv mysql-$MYSQL_VER.tar.gz $RPM_BUILD_ROOT/SOURCES/

pushd support-files
rm -rf *.spec
time make
time rpmbuild --buildroot $RPM_BUILD_ROOT -bc --with wsrep mysql-$MYSQL_VER.spec server
popd

# must be single line or set -e will abort the script on amd64
uname -m | grep -q i686 && export CPU=pentium || export CPU=amd64


exit
# Build mysqld
if [ "$CONFIGURE" == "yes" ]
then
    rm -f config.status
    if [ "$DEBUG" == "yes" ]
    then
        BUILD/compile-$CPU-debug-wsrep
    else
        BUILD/compile-$CPU-wsrep
    fi
else # just recompile and relink with old configuration
    make
fi

######################################
##                                  ##
##      Making of distribution      ##
##                                  ##
######################################

# Create build directory structure
DIST_DIR=$BUILD_ROOT/dist
MYSQL_DIST_DIR=$DIST_DIR/mysql
GALERA_DIST_DIR=$DIST_DIR/galera
cd $BUILD_ROOT
rm -rf $DIST_DIR

# Install required MySQL files in the DIST_DIR
mkdir -p $MYSQL_DIST_DIR
install -m 644 LICENSE $DIST_DIR
install -m 755 mysql-galera $DIST_DIR
install -m 755 gcomm $DIST_DIR
install -m 644 LICENSE.mysql $MYSQL_DIST_DIR
install -m 644 README $DIST_DIR
install -m 644 QUICK_START $DIST_DIR
install -D -m 644 $MYSQL_SRC/sql/share/english/errmsg.sys $MYSQL_DIST_DIR/share/mysql/english/errmsg.sys
install -D -m 755 $MYSQL_SRC/sql/mysqld $MYSQL_DIST_DIR/libexec/mysqld
install -D -m 755 $MYSQL_SRC/scripts/wsrep_sst_mysqldump $MYSQL_DIST_DIR/bin/wsrep_sst_mysqldump
install -D -m 644 my.cnf $MYSQL_DIST_DIR/etc/my.cnf
tar -xzf mysql_var.tgz -C $MYSQL_DIST_DIR

# Copy required Galera libraries
GALERA_LIBS=$GALERA_DIST_DIR/lib
mkdir -p $GALERA_LIBS
install -m 644 LICENSE.galera $GALERA_DIST_DIR
cp -P $GALERA_SRC/galerautils/src/.libs/libgalerautils.so* $GALERA_LIBS
if test $GCOMM_IMPL = "galeracomm"
then
    cp -P $GALERA_SRC/galeracomm/common/src/.libs/libgcommcommonpp.so* $GALERA_LIBS
    cp -P $GALERA_SRC/galeracomm/transport/src/.libs/libgcommtransportpp.so* $GALERA_LIBS
    cp -P $GALERA_SRC/galeracomm/vs/src/.libs/libgcommvspp.so* $GALERA_LIBS
else
    cp -P $GALERA_SRC/gcomm/src/.libs/libgcomm.so* $GALERA_LIBS
fi
cp -P $GALERA_SRC/gcs/src/.libs/libgcs.so* $GALERA_LIBS
cp -P $GALERA_SRC/wsdb/src/.libs/libwsdb.so* $GALERA_LIBS
cp -P $GALERA_SRC/galera/src/.libs/libmmgalera.so* $GALERA_LIBS

GALERA_SBIN=""
if test $GCOMM_IMPL = "galeracomm"
    then
# Install vs backend daemon
    GALERA_SBIN=$GALERA_DIST_DIR/sbin
    mkdir -p $GALERA_SBIN
    install -D -m 755 $GALERA_SRC/galeracomm/vs/src/.libs/vsbes $GALERA_SBIN/vsbes
fi

# Strip binaries if not instructed otherwise
if test "$NO_STRIP" != "yes"
then
    strip $GALERA_LIBS/lib*.so
    if test $GCOMM_IMPL = "galeracomm"
	then
	strip $GALERA_SBIN/*
    fi
fi

# original MYSQL_VER=$(grep AM_INIT_AUTOMAKE\(mysql, configure.in | awk '{ print $2 }' | sed s/\)//)
MYSQL_VER=$(grep '#define VERSION' $MYSQL_SRC/include/config.h | sed s/\"//g | cut -d ' ' -f 3 | cut -d '-' -f 1-2)
RELEASE_NAME=$(echo mysql-$MYSQL_VER-$MYSQL_REV,$GALERA_REV | sed s/\:/_/g)
rm -rf $RELEASE_NAME
mv $DIST_DIR $RELEASE_NAME

# Pack the release
if [ "$TAR" == "yes" ]
then
    tar -czf $RELEASE_NAME.tgz $RELEASE_NAME
fi
#
