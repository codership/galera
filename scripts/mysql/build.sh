#!/bin/bash

if test -z "$MYSQL_SRC"
then
    echo "MYSQL_SRC variable pointing at MySQL/wsrep sources is not set. Can't continue."
    exit -1
fi

GCOMM_IMPL=${GCOMM_IMPL:-"galeracomm"}

usage()
{
    echo -e "Usage: build.sh [OPTIONS] \n" \
	"Options:                      \n" \
        "    --stage <initial stage>   \n" \
	"    --last-stage <last stage> \n" \
	"    -s|--scratch    build everything from scratch\n"\
	"    -c|--configure  reconfigure the build system (implies -s)\n"\
	"    -b|--bootstap   rebuild the build system (implies -c)\n"\
	"    -o|--opt        configure build with debug disabled (implies -c)\n"\
	"    -d|--debug      configure build with debug enabled (implies -c)\n"\
	"    --with-spread   configure build with Spread (implies -c)\n"\
	"    --no-strip      prevent stripping of release binaries\n"\
	"    -r|--release <galera release>, otherwise revisions will be used"\
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
        -o|--opt)
            OPT=yes       # Compile without debug
            ;;
	-d|--debug)
            DEBUG=yes     # Compile with debug
	    NO_STRIP=yes  # Don't strip the binaries
	    ;;
        -r|--release)
            RELEASE="$2"  # Compile without debug
	    shift
            ;;
	-t|--tar)
            TAR=yes       # Create a TGZ package
            ;;
	--no-strip)
	    NO_STRIP=yes  # Don't strip the binaries
	    ;;
	--with*-spread)
	    WITH_SPREAD="$1"
	    ;;
	--help)
	    usage
	    exit 0
	    ;;
	*)
	    echo "Unrecognized option: $1"
	    usage
	    exit 1
	    ;;
    esac
    shift
done

set -x

# export command options for Galera build
export BOOTSTRAP CONFIGURE SCRATCH OPT DEBUG WITH_SPREAD

if [ "$OPT"   == "yes" ]; then CONFIGURE="yes"; fi
if [ "$DEBUG" == "yes" ]; then CONFIGURE="yes"; fi

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

scripts/build.sh # options are passed via environment variables

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
MYSQL_DIST_CNF=$MYSQL_DIST_DIR/etc/my.cnf
GALERA_DIST_DIR=$DIST_DIR/galera
cd $BUILD_ROOT
rm -rf $DIST_DIR

# Install required MySQL files in the DIST_DIR
mkdir -p $MYSQL_DIST_DIR
install -m 644 LICENSE $DIST_DIR
install -m 755 mysql-galera $DIST_DIR
install -m 755 vsbes $DIST_DIR
install -m 644 LICENSE.mysql $MYSQL_DIST_DIR
install -m 644 README $DIST_DIR
install -m 644 QUICK_START $DIST_DIR
install -D -m 644 $MYSQL_SRC/sql/share/english/errmsg.sys $MYSQL_DIST_DIR/share/mysql/english/errmsg.sys
install -D -m 755 $MYSQL_SRC/sql/mysqld $MYSQL_DIST_DIR/libexec/mysqld
install -D -m 755 $MYSQL_SRC/scripts/wsrep_sst_mysqldump $MYSQL_DIST_DIR/bin/wsrep_sst_mysqldump
install -D -m 644 my.cnf $MYSQL_DIST_CNF
cat $MYSQL_SRC/support-files/wsrep.cnf >> $MYSQL_DIST_CNF
tar -xzf mysql_var.tgz -C $MYSQL_DIST_DIR

# Copy required Galera libraries
GALERA_LIBS=$GALERA_DIST_DIR/lib
mkdir -p $GALERA_LIBS
install -m 644 LICENSE.galera $GALERA_DIST_DIR
cp -P $GALERA_SRC/galerautils/src/.libs/libgalerautils.so*   $GALERA_LIBS
cp -P $GALERA_SRC/galerautils/src/.libs/libgalerautils++.so* $GALERA_LIBS
#if test $GCOMM_IMPL = "galeracomm"
#then
    cp -P $GALERA_SRC/galeracomm/common/src/.libs/libgcommcommonpp.so* $GALERA_LIBS
    cp -P $GALERA_SRC/galeracomm/transport/src/.libs/libgcommtransportpp.so* $GALERA_LIBS
    cp -P $GALERA_SRC/galeracomm/vs/src/.libs/libgcommvspp.so* $GALERA_LIBS
#else
    cp -P $GALERA_SRC/gcomm/src/.libs/libgcomm.so* $GALERA_LIBS
#fi
cp -P $GALERA_SRC/gcs/src/.libs/libgcs.so* $GALERA_LIBS
cp -P $GALERA_SRC/wsdb/src/.libs/libwsdb.so* $GALERA_LIBS
cp -P $GALERA_SRC/galera/src/.libs/libmmgalera.so* $GALERA_LIBS

GALERA_SBIN=""
#if test $GCOMM_IMPL = "galeracomm"
#    then
# Install vs backend daemon
    GALERA_SBIN=$GALERA_DIST_DIR/sbin
    mkdir -p $GALERA_SBIN
    install -D -m 755 $GALERA_SRC/galeracomm/vs/src/.libs/vsbes $GALERA_SBIN/vsbes
#fi

# Strip binaries if not instructed otherwise
if test "$NO_STRIP" != "yes"
then
    strip $GALERA_LIBS/lib*.so
#    if test $GCOMM_IMPL = "galeracomm"
#	then
	strip $GALERA_SBIN/*
#    fi
fi

# original MYSQL_VER=$(grep AM_INIT_AUTOMAKE\(mysql, configure.in | awk '{ print $2 }' | sed s/\)//)
MYSQL_VER=$(grep '#define VERSION' $MYSQL_SRC/include/config.h | sed s/\"//g | cut -d ' ' -f 3 | cut -d '-' -f 1-2)
if [ "$RELEASE" != "" ]
then
    GALERA_RELEASE="galera-$RELEASE-$(uname -m)"
else
    GALERA_RELEASE="$MYSQL_REV,$GALERA_REV"
fi
RELEASE_NAME=$(echo mysql-$MYSQL_VER-$GALERA_RELEASE | sed s/\:/_/g)
rm -rf $RELEASE_NAME
mv $DIST_DIR $RELEASE_NAME

# Pack the release
if [ "$TAR" == "yes" ]
then
    tar -czf $RELEASE_NAME.tgz $RELEASE_NAME
fi
#
