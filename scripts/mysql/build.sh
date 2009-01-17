#!/bin/bash -x

if test -z "$MYSQL_SRC"
then
    echo "MYSQL_SRC variable pointing at Galera sources is not set. Can't continue."
    exit -1
fi

usage()
{
    echo -e "Usage: build.sh [OPTIONS] \n" \
	"Options:                      \n" \
        "    --stage <initial stage>   \n" \
	"    --last-stage <last stage> \n" \
	"    -s|--scratch    build everything from scratch\n"\
	"    -c|--configure  reconfigure the build system (implies -s)\n"\
	"    -b|--bootstap   rebuild the build system (implies -c)\n"\
	"    -r|--release    configure build with debug disabled (implies -c)\n"\
	"    -d|--debug      configure build with debug disabled (implies -c)\n"\
	"    --no-strip      prevent stripping of release binaries\n"\
        "\n -s and -b options affect only Galera build.\n"
}

# Parse command line
while test $# -gt 0
do
    case $1 in
	-b|--bootstrap)
            BOOTSTRAP=yes # Bootstrap the build system
            ;;
	-c|--configure)
            CONFIGURE=yes # Reconfigure the build system
            ;;
	-s|--scratch)
            SCRATCH=yes   # Build from scratch (run make clean)
            ;;
        -r|--release)
            RELEASE=yes   # Compile without debug
            ;;
	-d|--debug)
            DEBUG=yes     # Compile with debug
	    NO_STRIP=yes  # Don't strip the binaries
	    ;;
	-t|--tar)
            TAR=yes       # Create a TGZ package
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
export BOOTSTRAP CONFIGURE SCRATCH RELEASE DEBUG # for Galera build
if [ "$RELEASE" == "yes" ]; then CONFIGURE="yes"; fi
if [ "$DEBUG"   == "yes" ]; then CONFIGURE="yes"; fi

set -e

# Absolute path of this script folder
BUILD_ROOT=$(cd $(dirname $0); pwd -P)
GALERA_SRC=${GALERA_SRC:-$BUILD_ROOT/../../}
# Source paths are either absolute or relative to script, get absolute
MYSQL_SRC=$(cd $MYSQL_SRC; pwd -P; cd $BUILD_ROOT)
GALERA_SRC=$(cd $GALERA_SRC; pwd -P; cd $BUILD_ROOT)


######################################
##                                  ##
##          Build Galera            ##
##                                  ##
######################################
# Also obtain SVN revision information
cd $GALERA_SRC
GALERA_REV=$(svnversion | sed s/\:/,/g)

scripts/build.sh $@

######################################
##                                  ##
##           Build MySQL            ##
##                                  ##
######################################
# Obtain MySQL version and revision of Galera patch
cd $MYSQL_SRC
MYSQL_REV=$(bzr revno)

export MYSQL_REV
export GALERA_REV
export GALERA_SRC

# must be single line or set -e will abort the script on amd64
uname -m | grep -q i686 && export CPU=pentium || export CPU=amd64

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
install -D -m 644 my.cnf $MYSQL_DIST_DIR/etc/my.cnf
tar -xzf mysql_var.tgz -C $MYSQL_DIST_DIR

# Copy required Galera libraries
GALERA_LIBS=$GALERA_DIST_DIR/lib
mkdir -p $GALERA_LIBS
install -m 644 LICENSE.galera $GALERA_DIST_DIR
cp -P $GALERA_SRC/galerautils/src/.libs/libgalerautils.so* $GALERA_LIBS
cp -P $GALERA_SRC/galeracomm/common/src/.libs/libgcommcommonpp.so* $GALERA_LIBS
cp -P $GALERA_SRC/galeracomm/transport/src/.libs/libgcommtransportpp.so* $GALERA_LIBS
cp -P $GALERA_SRC/galeracomm/vs/src/.libs/libgcommvspp.so* $GALERA_LIBS
cp -P $GALERA_SRC/gcs/src/.libs/libgcs.so* $GALERA_LIBS
cp -P $GALERA_SRC/wsdb/src/.libs/libwsdb.so* $GALERA_LIBS
cp -P $GALERA_SRC/galera/src/.libs/libmmgalera.so* $GALERA_LIBS

# Install vs backend daemon
GALERA_SBIN=$GALERA_DIST_DIR/sbin
mkdir -p $GALERA_SBIN
install -D -m 755 $GALERA_SRC/galeracomm/vs/src/.libs/vsbes $GALERA_SBIN/vsbes

# Strip binaries if not instructed otherwise
if test "$NO_STRIP" != "yes"
then
    strip $GALERA_LIBS/lib*.so
    strip $GALERA_SBIN/*
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
