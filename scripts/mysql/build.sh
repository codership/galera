#!/bin/bash

if test -z "$MYSQL_SRC"
then
    echo "MYSQL_SRC variable pointing at MySQL/wsrep sources is not set. Can't continue."
    exit -1
fi

# Initializing variables to defaults
uname -m | grep -q i686 && CPU=pentium || CPU=amd64
DEBUG=no
DEBUG_LEVEL=1
NO_STRIP=no
RELEASE=""
TAR=no
BIN_DIST=no
PACKAGE=no
INSTALL=no
CONFIGURE=no
SKIP_BUILD=no
SKIP_CONFIGURE=no
SKIP_CLIENTS=no
SCRATCH=no
SCONS="yes"
JOBS=$(cat /proc/cpuinfo | grep -c ^processor)
GCOMM_IMPL=${GCOMM_IMPL:-"galeracomm"}

usage()
{
    cat <<EOF
Usage: build.sh [OPTIONS]
Options:
    --stage <initial stage>
    --last-stage <last stage>
    -s|--scratch      build everything from scratch
    -c|--configure    reconfigure the build system (implies -s)
    -b|--bootstap     rebuild the build system (implies -c)
    -o|--opt          configure build with debug disabled (implies -c)
    -m32/-m64         build 32/64-bit binaries on x86
    -d|--debug        configure build with debug enabled (implies -c)
    -dl|--debug-level set debug level (1, implies -c)
    --with-spread     configure build with Spread (implies -c)
    --no-strip        prevent stripping of release binaries
    -j|--jobs         number of parallel compilation jobs (${JOBS})
    -p|--package      create DEB/RPM packages (depending on the distribution)
    --bin             create binary tar package
    -t|--tar          create a demo test distribution
    --sb|--skip-build skip the actual build, use the existing binaries
    --sc|--skip-configure skip configure
    --skip-clients    don't include client binaries in test package
    --scons           use scons to build galera libraries (yes)
    -r|--release <galera release>, otherwise revisions will be used

-s and -b options affect only Galera build.
EOF
}

# Parse command line
while test $# -gt 0
do
    case $1 in
        -b|--bootstrap)
            BOOTSTRAP="yes" # Bootstrap the build system
            ;;
        --bin)
            BIN_DIST="yes"
            ;;
        -c|--configure)
            CONFIGURE="yes" # Reconfigure the build system
            ;;
        -s|--scratch)
            SCRATCH="yes"   # Build from scratch (run make clean)
            ;;
        -o|--opt)
            OPT="yes"       # Compile without debug
            ;;
        -d|--debug)
            DEBUG="yes"     # Compile with debug
            NO_STRIP="yes"  # Don't strip the binaries
            ;;
        --dl|--debug-level)
            shift;
            DEBUG_LEVEL=$1
            ;;
        -r|--release)
            RELEASE="$2"    # Compile without debug
            shift
            ;;
        -t|--tar)
            TAR="yes"       # Create a TGZ package
            ;;
        -i|--install)
            INSTALL="yes"
            ;;
        -p|--package)
            PACKAGE="yes"   # Create a DEB package
            ;;
        -j|--jobs)
            shift;
            JOBS=$1
            ;;
        --no-strip)
            NO_STRIP="yes"  # Don't strip the binaries
            ;;
        --with*-spread)
            WITH_SPREAD="$1"
            ;;
        -m32)
            CFLAGS="$CFLAGS -m32"
            CXXFLAGS="$CXXFLAGS -m32"
            CONFIGURE="yes"
            CPU="pentium"
            TARGET="i686"
            ;;
        -m64)
            CFLAGS="$CFLAGS -m64"
            CXXFLAGS="$CXXFLAGS -m64"
            CONFIGURE="yes"
            CPU="amd64"
            TARGET="x86_64"
            ;;
        --sb|--skip-build)
            SKIP_BUILD="yes"
            ;;
        --sc|--skip-configure)
            SKIP_CONFIGURE="yes"
            ;;
        --skip-clients)
            SKIP_CLIENTS="yes"
            ;;
        --scons)
            SCONS="yes"
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

if [ "$PACKAGE" == "yes" ]
then
# check whether sudo accepts -E to preserve environment
    echo "testing sudo"
    if sudo -E $(which epm) --version >/dev/null 2>&1
    then
        echo "sudo accepts -E"
        SUDO_ENV="sudo -E"
        SUDO="sudo"
    else
        echo "sudo does not accept param -E"
        if [ $(id -ur) != 0 ]
        then
            echo "error, must build as root"
            exit 1
        else
            SUDO_ENV=""
            SUDO=""
            echo "I'm root, can continue"
        fi
    fi

# If packaging with epm, make sure that mysql user exists in build system to
# get file ownerships right.
    echo "Checking for mysql user and group for epm:"
    getent passwd mysql >/dev/null
    if [ $? != 0 ]
    then
        echo "Error: user 'mysql' does not exist"
        exit 1
    else
        echo "User 'mysql' ok"
    fi
    getent group mysql >/dev/null
    if [ $? != 0 ]
    then
        echo "Error: group 'mysql' doest not exist"
        exit 1
    else
        echo "Group 'mysql' ok"
    fi
fi

if [ "$OPT"     == "yes" ]; then CONFIGURE="yes"; fi
if [ "$DEBUG"   == "yes" ]; then CONFIGURE="yes"; fi
if [ "$INSTALL" == "yes" ]; then TAR="yes"; fi
if [ "$SKIP_BUILD" == "yes" ]; then CONFIGURE="no"; fi

which dpkg >/dev/null 2>&1 && DEBIAN=1 || DEBIAN=0

# export command options for Galera build
export BOOTSTRAP CONFIGURE SCRATCH OPT DEBUG WITH_SPREAD CFLAGS CXXFLAGS \
       PACKAGE CPU TARGET SKIP_BUILD RELEASE DEBIAN SCONS JOBS DEBUG_LEVEL

set -eu

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
if [ "$TAR" == "yes" ] || [ "$BIN_DIST" == "yes" ]
then
    cd $GALERA_SRC
    GALERA_REV=$(bzr revno)
    export GALERA_VER=${RELEASE:-$GALERA_REV}
    scripts/build.sh # options are passed via environment variables
fi

######################################
##                                  ##
##           Build MySQL            ##
##                                  ##
######################################
# Obtain MySQL version and revision of Galera patch
cd $MYSQL_SRC
WSREP_REV=$(bzr revno)
# this does not work on an unconfigured source MYSQL_VER=$(grep '#define VERSION' $MYSQL_SRC/include/config.h | sed s/\"//g | cut -d ' ' -f 3 | cut -d '-' -f 1-2)
MYSQL_VER=`grep PACKAGE_VERSION include/my_config.h | awk '{gsub(/\"/,""); print $3; }'`

if [ "$PACKAGE" == "yes" ] || [ "$BIN_DIST" == "yes" ]
then
    # fetch and patch pristine sources
    cd /tmp
    mysql_tag=mysql-$MYSQL_VER
    if [ "$SKIP_BUILD" == "no" ] || [ ! -d $mysql_tag ]
    then
        mysql_orig_tar_gz=$mysql_tag.tar.gz
        url2=http://mysql.dataphone.se/Downloads/MySQL-5.1
        url1=http://downloads.mysql.com/archives/mysql-5.1
        if [ ! -r $mysql_orig_tar_gz ]
        then
            echo "Downloading $mysql_orig_tar_gz... currently works only for 5.1.x"
            wget -N $url1/$mysql_orig_tar_gz || wget -N $url2/$mysql_orig_tar_gz
        fi
        echo "Getting wsrep patch..."
        patch_file=$(${BUILD_ROOT}/get_patch.sh $mysql_tag $MYSQL_SRC)
        echo "Patching source..."
        rm -rf $mysql_tag # need clean sources for a patch
        tar -xzf $mysql_orig_tar_gz
        cd $mysql_tag/
        patch -p1 -f < $patch_file >/dev/null || :
        chmod a+x ./BUILD/*wsrep
        CONFIGURE="yes"
    else
        cd $mysql_tag/
    fi
    MYSQL_SRC=$(pwd -P)
    if [ "$CONFIGURE" == "yes" ]
    then
        echo "Regenerating config files"
        time ./BUILD/autorun.sh
    fi
fi

echo  "Building mysqld"

export WSREP_REV
export GALERA_REV
export MAKE="make -j$JOBS"

if [ "$SKIP_BUILD" == "no" ]
then
    if [ "$CONFIGURE" == "yes" ] && [ "$SKIP_CONFIGURE" == "no" ]
    then
        rm -f config.status
        if [ "$DEBUG" == "yes" ]
        then
            DEBUG_OPT="-debug"
        else
            DEBUG_OPT=""
        fi

        # This will be put to --prefix by SETUP.sh.
        export MYSQL_BUILD_PREFIX="/usr"

        if [ "$PACKAGE" == "yes" || "$BIN_DIST" == "yes" ]
        then
            # There is no other way to pass these options to SETUP.sh but
            # via env. variable
            export wsrep_configs="--exec-prefix=/usr \
                                  --libexecdir=/usr/sbin \
                                  --localstatedir=/var/lib/mysql \
                                  --with-extra-charsets=all"
        fi

        [ $DEBIAN -ne 0 ] && \
        export MYSQL_SOCKET_PATH="/var/run/mysqld/mysqld.sock" || \
        export MYSQL_SOCKET_PATH="/var/lib/mysql/mysql.sock"

        BUILD/compile-${CPU}${DEBUG_OPT}-wsrep > /dev/null
    else  # just recompile and relink with old configuration
        #set -x
        make > /dev/null
        #set +x
    fi
fi # SKIP_BUILD

# gzip manpages
# this should be rather fast, so we can repeat it every time
if [ "$PACKAGE" == "yes" ]
then
    cd $MYSQL_SRC/man && for i in *.1 *.8; do gzip -c $i > $i.gz; done || :
fi

######################################
##                                  ##
##      Making of demo tarball      ##
##                                  ##
######################################

if [ $TAR == "yes" ]; then
echo "Creating demo distribution"
# Create build directory structure
DIST_DIR=$BUILD_ROOT/dist
MYSQL_DIST_DIR=$DIST_DIR/mysql
MYSQL_DIST_CNF=$MYSQL_DIST_DIR/etc/my.cnf
GALERA_DIST_DIR=$DIST_DIR/galera
cd $BUILD_ROOT
rm -rf $DIST_DIR
# Install required MySQL files in the DIST_DIR
MYSQL_BINS=$MYSQL_DIST_DIR/bin
MYSQL_LIBS=$MYSQL_DIST_DIR/lib/mysql
MYSQL_PLUGINS=$MYSQL_DIST_DIR/lib/mysql/plugin
MYSQL_CHARSETS=$MYSQL_DIST_DIR/share/mysql/charsets
install -m 644 -D $MYSQL_SRC/sql/share/english/errmsg.sys $MYSQL_DIST_DIR/share/mysql/english/errmsg.sys
install -m 755 -D $MYSQL_SRC/sql/mysqld $MYSQL_DIST_DIR/libexec/mysqld
if [ "$SKIP_CLIENTS" == "no" ]
then
# Hack alert: install libmysqlclient.so as libmysqlclient.so.16 as client binaries
# seem to be linked against explicit version. Figure out better way to deal with
# this.
install -m 755 -D $MYSQL_SRC/libmysql/.libs/libmysqlclient.so $MYSQL_LIBS/libmysqlclient.so.16
fi
if test -f $MYSQL_SRC/storage/innodb_plugin/.libs/ha_innodb_plugin.so
then
install -m 755 -D $MYSQL_SRC/storage/innodb_plugin/.libs/ha_innodb_plugin.so \
                  $MYSQL_PLUGINS/ha_innodb_plugin.so
fi
install -m 755 -d $MYSQL_BINS
if [ "$SKIP_CLIENTS" == "no" ]
then
    if [ -x $MYSQL_SRC/client/.libs/mysql ]    # MySQL
    then
        MYSQL_CLIENTS=$MYSQL_SRC/client/.libs
    elif [ -x $MYSQL_SRC/client/mysql ]        # MariaDB
    then
        MYSQL_CLIENTS=$MYSQL_SRC/client
    else
        echo "Can't find MySQL clients. Aborting."
        exit 1
    fi
install -m 755 -s -t $MYSQL_BINS  $MYSQL_CLIENTS/mysql
install -m 755 -s -t $MYSQL_BINS  $MYSQL_CLIENTS/mysqldump
install -m 755 -s -t $MYSQL_BINS  $MYSQL_CLIENTS/mysqladmin
fi
install -m 755 -t $MYSQL_BINS     $MYSQL_SRC/scripts/wsrep_sst_mysqldump
install -m 755 -t $MYSQL_BINS     $MYSQL_SRC/scripts/wsrep_sst_rsync
install -m 755 -d $MYSQL_CHARSETS
install -m 644 -t $MYSQL_CHARSETS $MYSQL_SRC/sql/share/charsets/*.xml
install -m 644 -t $MYSQL_CHARSETS $MYSQL_SRC/sql/share/charsets/README
install -m 644 -D my.cnf $MYSQL_DIST_CNF
cat $MYSQL_SRC/support-files/wsrep.cnf >> $MYSQL_DIST_CNF
tar -xzf mysql_var.tgz -C $MYSQL_DIST_DIR
install -m 644 LICENSE.mysql $MYSQL_DIST_DIR

# Copy required Galera libraries
GALERA_LIBS=$GALERA_DIST_DIR/lib
install -m 644 -D LICENSE.galera $GALERA_DIST_DIR/LICENSE.galera
install -m 755 -d $GALERA_LIBS

if [ "$SCONS" == "yes" ]
then
    SCONS_VD=$GALERA_SRC
    cp -P $SCONS_VD/libgalera_smm.so* $GALERA_LIBS
else
    echo "Autotools compilation not supported any more."
    exit 1
fi

install -m 644 LICENSE       $DIST_DIR
install -m 755 mysql-galera  $DIST_DIR
install -m 644 README        $DIST_DIR
install -m 644 QUICK_START   $DIST_DIR

# Strip binaries if not instructed otherwise
if test "$NO_STRIP" != "yes"
then
    strip $GALERA_LIBS/lib*.so
    strip $MYSQL_DIST_DIR/libexec/mysqld
fi

fi # if [ $TAR == "yes" ]

if [ "$BIN_DIST" == "yes" ]
then
. bin_dist.sh
fi

if [ "$TAR" == "yes" ] || [ "$BIN_DIST" == "yes" ]
then

if [ "$RELEASE" != "" ]
then
    GALERA_RELEASE="galera-$RELEASE-$(uname -m)"
else
    GALERA_RELEASE="$WSREP_REV,$GALERA_REV"
fi

RELEASE_NAME=$(echo mysql-$MYSQL_VER-$GALERA_RELEASE | sed s/\:/_/g)
rm -rf $RELEASE_NAME
mv $DIST_DIR $RELEASE_NAME

# Hack to avoid 'file changed as we read it'-error 
sync
sleep 1

# Pack the release
tar -czf $RELEASE_NAME.tgz $RELEASE_NAME

fi # if [ $TAR == "yes"  || "$BIN_DIST" == "yes" ]

if [ "$TAR" == "yes" ] && [ "$INSTALL" == "yes" ]
then
    cmd="$GALERA_SRC/tests/scripts/command.sh"
    $cmd stop
    $cmd install $RELEASE_NAME.tgz
fi

get_arch()
{
    if file $MYSQL_SRC/sql/mysqld.o | grep "80386" >/dev/null 2>&1
    then
        echo "i386"
    else
        echo "amd64"
    fi
}

build_packages()
{
    pushd $GALERA_SRC/scripts/mysql

    local ARCH=$(get_arch)
    local WHOAMI=$(whoami)

    if [ $DEBIAN -eq 0 ] && [ "$ARCH" == "amd64" ]
    then
        ARCH="x86_64"
        export x86_64=$ARCH # for epm
    fi

    local STRIP_OPT=""
    [ "$NO_STRIP" == "yes" ] && STRIP_OPT="-g"

    export MYSQL_VER MYSQL_SRC GALERA_SRC RELEASE_NAME
    export WSREP_VER=${RELEASE:-"$WSREP_REV"}

    echo $MYSQL_SRC $MYSQL_VER $ARCH
    rm -rf $ARCH

    set +e
    if [ $DEBIAN -ne 0 ]
    then #build DEB
        local deb_basename="mysql-server-wsrep"
        ln -sf mysql-wsrep.list $deb_basename.list
        $SUDO_ENV $(which epm) -n -m "$ARCH" -a "$ARCH" -f "deb" \
             --output-dir $ARCH $STRIP_OPT $deb_basename
    else # build RPM
        local rpm_basename="MySQL-server-wsrep"
        ln -sf mysql-wsrep.list $rpm_basename.list
        ($SUDO_ENV $(which epm) -vv -n -m "$ARCH" -a "$ARCH" -f "rpm" \
              --output-dir $ARCH --keep-files -k $STRIP_OPT $rpm_basename || \
         $SUDO_ENV $(which rpmbuild) -bb --target "$ARCH" \
              --buildroot="$ARCH/buildroot" "$ARCH/$rpm_basename.spec" )
    fi
    local RET=$?

    $SUDO /bin/chown -R $WHOAMI.users $ARCH
    set -e

    if [ $RET -eq 0 ] && [ $DEBIAN -eq 0 ]
    then # RPM cleanup (some rpm versions put the package in RPMS)
        test -d $ARCH/RPMS/$ARCH && \
        mv $ARCH/RPMS/$ARCH/*.rpm $ARCH/ 1>/dev/null 2>&1 || :

        rm -rf $ARCH/RPMS $ARCH/buildroot $ARCH/rpms # $ARCH/mysql-wsrep.spec
    fi

    return $RET
}

if [ "$PACKAGE" == "yes" ]
then
    build_packages
fi
#
