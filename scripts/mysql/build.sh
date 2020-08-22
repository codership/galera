#!/bin/bash -eux

if test -z "$MYSQL_SRC"
then
    echo "MYSQL_SRC variable pointing at MySQL/wsrep sources is not set. Can't continue."
    exit -1
fi

use_mysql_5.1_sources()
{
    MYSQL_MAJOR="5.1"
    export MYSQL_MAJOR # for DEB build
    export MYSQL_MAJOR_VER="5"
    export MYSQL_MINOR_VER="1"
    MYSQL_VER=`grep AC_INIT $MYSQL_SRC/configure.in | awk -F '[' '{ print $3 }' | awk -F ']' '{ print $1 }'`
}

use_mariadb_5.1_sources()
{
    use_mysql_5.1_sources
}

use_mysql_5.5_sources()
{
    export MYSQL_MAJOR_VER=`grep MYSQL_VERSION_MAJOR $MYSQL_SRC/VERSION | cut -d = -f 2`
    export MYSQL_MINOR_VER=`grep MYSQL_VERSION_MINOR $MYSQL_SRC/VERSION | cut -d = -f 2`
    export MYSQL_PATCH_VER=`grep MYSQL_VERSION_PATCH $MYSQL_SRC/VERSION | cut -d = -f 2`
    MYSQL_MAJOR=$MYSQL_MAJOR_VER.$MYSQL_MINOR_VER
    export MYSQL_MAJOR # for DEB build
    MYSQL_VER=$MYSQL_MAJOR.$MYSQL_PATCH_VER
}

if test -f "$MYSQL_SRC/configure.in"
then
    use_mysql_5.1_sources
elif test -f "$MYSQL_SRC/VERSION"
then
    use_mysql_5.5_sources
else
    echo "Unknown MySQL version in MYSQL_SRC path. Versions 5.1 and 5.5 are supported. Can't continue."
    exit -1
fi

# Initializing variables to defaults
uname -p | grep -q 'i[36]86' && CPU=pentium || CPU=amd64 # this works for x86 Solaris too
BOOTSTRAP=no
DEBUG=no
DEBUG_LEVEL=0
GALERA_DEBUG=no
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
JOBS=1
GCOMM_IMPL=${GCOMM_IMPL:-"galeracomm"}
TARGET=""
MYSQLD_BINARY="mysqld"
SYNC_BEFORE_PACK=${SYNC_BEFORE_PACK:-""}

OS=$(uname)
case "$OS" in
    "Linux")
        JOBS=$(grep -c ^processor /proc/cpuinfo) ;;
    "SunOS")
        JOBS=$(psrinfo | wc -l | tr -d ' ') ;;
    "Darwin" | "FreeBSD")
        JOBS="$(sysctl -n hw.ncpu)" ;;
    *)
        echo "CPU information not available: unsupported OS: '$OS'";;
esac

if [ "$OS" == "FreeBSD" ]; then
    CC=${CC:-"gcc48"}
    CXX=${CXX:-"g++48"}
    LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-"/usr/local/lib/$(basename $CC)"}
else
    CC=${CC:-"gcc"}
    CXX=${CXX:-"g++"}
fi

if ! which "$CC" ; then echo "Can't execute $CC" ; exit 1; fi
if ! which "$CXX"; then echo "Can't execute $CXX"; exit 1; fi

CFLAGS=${CFLAGS:-""}
CXXFLAGS=${CXXFLAGS:-""}

export CC CXX LD_LIBRARY_PATH

EXTRA_SYSROOT=${EXTRA_SYSROOT:-""}
if [ "$OS" == "Darwin" ]; then
  if which -s port && test -x /opt/local/bin/port; then
    EXTRA_SYSROOT=/opt/local
  elif which -s brew && test -x /usr/local/bin/brew; then
    EXTRA_SYSROOT=/usr/local
  elif which -s fink && test -x /sw/bin/fink; then
    EXTRA_SYSROOT=/sw
  fi
elif [ "$OS" == "FreeBSD" ]; then
  EXTRA_SYSROOT=/usr/local
fi

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
    --gd|--galera-debug only galera debug build (optimized mysqld)
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
            CONFIGURE="yes"
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
            CONFIGURE="yes" # Reconfigure without debug
            ;;
        -d|--debug)
            DEBUG="yes"     # Reconfigure with debug
            CONFIGURE="yes"
            NO_STRIP="yes"  # Don't strip the binaries
            ;;
        --dl|--debug-level)
            shift;
            DEBUG_LEVEL=$1
            ;;
        --gd|--galera-debug)
            GALERA_DEBUG="yes"
            ;;
        -r|--release)
            RELEASE="$2"    # Compile without debug
            CONFIGURE="yes"
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
            CONFIGURE="yes" # don't forget to reconfigure with --prefix=/usr
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

if [ "$PACKAGE" == "yes" -a "$OS" == "Linux" ]
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

if [ "$INSTALL" == "yes" ]; then TAR="yes"; fi
if [ "$SKIP_BUILD" == "yes" ]; then CONFIGURE="no"; fi

which dpkg >/dev/null 2>&1 && DEBIAN=1 || DEBIAN=0

# export command options for Galera build
export BOOTSTRAP CONFIGURE SCRATCH DEBUG WITH_SPREAD CFLAGS CXXFLAGS \
       PACKAGE CPU TARGET SKIP_BUILD RELEASE DEBIAN SCONS JOBS DEBUG_LEVEL

set -eu

# Absolute path of this script folder
BUILD_ROOT=$(cd $(dirname $0); pwd -P)
GALERA_SRC=${GALERA_SRC:-$BUILD_ROOT/../../}
# Source paths are either absolute or relative to script, get absolute
MYSQL_SRC=$(cd $MYSQL_SRC; pwd -P; cd $BUILD_ROOT)
GALERA_SRC=$(cd $GALERA_SRC; pwd -P; cd $BUILD_ROOT)

if [ "$MYSQL_MAJOR" = "5.1" ]
then
    MYSQL_BUILD_DIR="$MYSQL_SRC"
else
    [ "$DEBUG" == "yes" ] \
    && MYSQL_BUILD_DIR="$MYSQL_SRC/build_debug" \
    || MYSQL_BUILD_DIR="$MYSQL_SRC/build_release"
fi


######################################
##                                  ##
##          Build Galera            ##
##                                  ##
######################################
# Also obtain SVN revision information
if [ "$TAR" == "yes" -o "$BIN_DIST" == "yes" ]
then
    cd $GALERA_SRC
    debug_opt=""
    if [ $GALERA_DEBUG == "yes" ]
    then
        debug_opt="-d"
    fi
    scripts/build.sh $debug_opt # options are passed via environment variables
    # sadly we can't easily pass GALERA_REV from Galera build script
    GALERA_REV=${GALERA_REV:-"XXXX"}
fi

######################################
##                                  ##
##           Build MySQL            ##
##                                  ##
######################################
# Obtain MySQL version and revision number
cd $MYSQL_SRC
WSREP_REV=$(git log --pretty=oneline | wc -l) || \
WSREP_REV=$(bzr revno --tree -q)              || \
WSREP_REV="XXXX"
WSREP_REV=${WSREP_REV//[[:space:]]/}
# this does not work on an unconfigured source MYSQL_VER=$(grep '#define VERSION' $MYSQL_SRC/include/config.h | sed s/\"//g | cut -d ' ' -f 3 | cut -d '-' -f 1-2)

if [ "$PACKAGE" == "yes" ] || [ "$BIN_DIST" == "yes" ]
then
    # fetch and patch pristine sources
    cd ${TMPDIR:-/tmp}
    mysql_tag=mysql-$MYSQL_VER
    if [ "$SKIP_BUILD" == "no" ] || [ ! -d $mysql_tag ]
    then
        mysql_orig_tar_gz=$mysql_tag.tar.gz
        url2=http://ftp.sunet.se/pub/unix/databases/relational/mysql/Downloads/MySQL-$MYSQL_MAJOR
        url1=ftp://sunsite.informatik.rwth-aachen.de/pub/mirror/www.mysql.com/Downloads/MySQL-$MYSQL_MAJOR
        if [ ! -r $mysql_orig_tar_gz ]
        then
            echo "Downloading $mysql_orig_tar_gz..."
            wget -N $url1/$mysql_orig_tar_gz || wget -N $url2/$mysql_orig_tar_gz
        fi
        echo "Getting wsrep patch..."
        patch_file=$(${BUILD_ROOT}/get_patch.sh $mysql_tag $MYSQL_SRC)
        echo "Patching source..."
        rm -rf $mysql_tag # need clean sources for a patch
        tar -xzf $mysql_orig_tar_gz
        cd $mysql_tag/
        patch -p1 -f < $patch_file >/dev/null || :
        # chmod a+x ./BUILD/*wsrep
        CONFIGURE="yes"
    else
        cd $mysql_tag/
    fi
    MYSQL_SRC=$(pwd -P)
    MYSQL_BUILD_DIR=$MYSQL_SRC
    if [ "$CONFIGURE" == "yes" ]
    then
        echo "Regenerating config files"
        time ./BUILD/autorun.sh
    fi
fi

echo  "Building mysqld"

export WSREP_REV
export MAKE="make -j$JOBS"

if [ "$SKIP_BUILD" == "no" ]
then
    if [ "$CONFIGURE" == "yes" ] && [ "$SKIP_CONFIGURE" == "no" ]
    then
        rm -f config.status

        BUILD_OPT=""
        if [ "$OS" == "FreeBSD" ]; then
            # don't use INSTALL_LAYOUT=STANDALONE(default), it assumes prefix=.
            CMAKE_LAYOUT_OPTIONS=(
                -DCMAKE_INSTALL_PREFIX="/usr/local" \
                -DINSTALL_LAYOUT=RPM \
                -DMYSQL_UNIX_ADDR="/tmp/mysql.sock" \
                -DINSTALL_BINDIR="bin" \
                -DINSTALL_DOCDIR="share/doc/mysql" \
                -DINSTALL_DOCREADMEDIR="share/doc/mysql" \
                -DINSTALL_INCLUDEDIR="include/mysql" \
                -DINSTALL_INFODIR="info" \
                -DINSTALL_LIBDIR="lib/mysql" \
                -DINSTALL_MANDIR="man" \
                -DINSTALL_MYSQLDATADIR="/var/db/mysql" \
                -DINSTALL_MYSQLSHAREDIR="share/mysql" \
                -DINSTALL_MYSQLTESTDIR="share/mysql/tests" \
                -DINSTALL_PLUGINDIR="lib/mysql/plugin" \
                -DINSTALL_SBINDIR="libexec" \
                -DINSTALL_SCRIPTDIR="bin" \
                -DINSTALL_SHAREDIR="share" \
                -DINSTALL_SQLBENCHDIR="share/mysql" \
                -DINSTALL_SUPPORTFILESDIR="share/mysql" \
                -DWITH_UNIT_TESTS=0 \
                -DWITH_LIBEDIT=0 \
                -DWITH_LIBWRAP=1 \
            )
        else
            [ $DEBIAN -ne 0 ] && \
                MYSQL_SOCKET_PATH="/var/run/mysqld/mysqld.sock" || \
                MYSQL_SOCKET_PATH="/var/lib/mysql/mysql.sock"

            CMAKE_LAYOUT_OPTIONS=( \
                -DINSTALL_LAYOUT=RPM \
                -DCMAKE_INSTALL_PREFIX="/usr" \
                -DINSTALL_SBINDIR="/usr/sbin" \
                -DMYSQL_DATADIR="/var/lib/mysql" \
                -DMYSQL_UNIX_ADDR=$MYSQL_SOCKET_PATH \
                -DCMAKE_OSX_ARCHITECTURES=$(uname -m) \
            )
        fi

        [ -n "$EXTRA_SYSROOT" ] && \
            CMAKE_LAYOUT_OPTIONS+=( \
                -DCMAKE_PREFIX_PATH="$EXTRA_SYSROOT" \
            )

        if [ $MYSQL_MAJOR = "5.1" ]
        then
            # This will be put to --prefix by SETUP.sh.
            export MYSQL_BUILD_PREFIX="/usr"
            export wsrep_configs="--libexecdir=/usr/sbin \
                                  --localstatedir=/var/lib/mysql/ \
                                  --with-unix-socket-path=$MYSQL_SOCKET_PATH \
                                  --with-extra-charsets=all \
                                  --with-ssl"

            [ "$DEBUG" = "yes" ] && BUILD_OPT="-debug"
            BUILD/compile-${CPU}${BUILD_OPT}-wsrep > /dev/null
        else # CMake build
            [ "$DEBUG" = "yes" ] \
            && BUILD_OPT="-DCMAKE_BUILD_TYPE=Debug -DDEBUG_EXTNAME=OFF" \
            || BUILD_OPT="-DCMAKE_BUILD_TYPE=RelWithDebInfo" # like in RPM spec

            MYSQL_MM_VER="$MYSQL_MAJOR_VER$MYSQL_MINOR_VER"

            [ "$MYSQL_MM_VER" -ge "56" ] \
            && MEMCACHED_OPT="-DWITH_LIBEVENT=bundled -DWITH_INNODB_MEMCACHED=ON" \
            || MEMCACHED_OPT=""

            if [ "$MYSQL_MM_VER" -ge "57" ]
            then
                BOOST_OPT="-DWITH_BOOST=boost_$MYSQL_MM_VER"
                [ "yes" = "$BOOTSTRAP" ] && \
                    BOOST_OPT="$BOOST_OPT -DDOWNLOAD_BOOST=1"
                SSL_OPT="-DWITH_SSL=yes"
            else
                BOOST_OPT=""
                SSL_OPT="-DWITH_SSL=yes"
            fi

            if [ "$MYSQL_BUILD_DIR" != "$MYSQL_SRC" ]
            then
               [ "$BOOTSTRAP" = "yes" ] && rm -rf $MYSQL_BUILD_DIR
               [ -d "$MYSQL_BUILD_DIR" ] || mkdir -p $MYSQL_BUILD_DIR
            fi

            pushd $MYSQL_BUILD_DIR

            # cmake wants either absolute path or a link from build directory
            # Incidentally link trick also allows us to use ccache
            # (at least it distinguishes between gcc/clang)
            ln -sf $(which ccache || which $CC)  $(basename $CC)
            ln -sf $(which ccache || which $CXX) $(basename $CXX)

            cmake \
                  -DCMAKE_C_COMPILER=$(pwd)/$(basename $CC) \
                  -DCMAKE_CXX_COMPILER=$(pwd)/$(basename $CXX) \
                  -DCMAKE_C_FLAGS="$CFLAGS" \
                  -DCMAKE_CXX_FLAGS="$CXXFLAGS" \
                  -DBUILD_CONFIG=mysql_release \
                  "${CMAKE_LAYOUT_OPTIONS[@]}" \
                  $BUILD_OPT \
                  -DWITH_WSREP=1 \
                  -DWITH_EXTRA_CHARSETS=all \
                  $SSL_OPT \
                  -DWITH_ZLIB=system \
                  -DMYSQL_MAINTAINER_MODE=0 \
                  $MEMCACHED_OPT \
                  $BOOST_OPT \
                  $MYSQL_SRC \
            && make -j $JOBS -S && popd || exit 1
        fi
    else  # just recompile and relink with old configuration
        if [ $MYSQL_MAJOR = "5.1" ]
        then
            make -j $JOBS -S > /dev/null
        else
            pushd $MYSQL_BUILD_DIR
            CC=$(pwd)/$(basename $CC) CXX=$(pwd)/$(basename $CXX) \
            make -j $JOBS -S > /dev/null
            popd
        fi
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

install_mysql_5.1_demo()
{

    MYSQL_LIBS=$MYSQL_DIST_DIR/lib/mysql
    MYSQL_PLUGINS=$MYSQL_DIST_DIR/lib/mysql/plugin
    MYSQL_CHARSETS=$MYSQL_DIST_DIR/share/mysql/charsets
    # BSD-based OSes does not have -D option on 'install'
    install -m 755 -d $MYSQL_DIST_DIR/share/mysql/english
    install -m 644 $MYSQL_SRC/sql/share/english/errmsg.sys $MYSQL_DIST_DIR/share/mysql/english/errmsg.sys
    install -m 755 -d $MYSQL_DIST_DIR/sbin
    install -m 755 $MYSQL_SRC/sql/mysqld $MYSQL_DIST_DIR/sbin/mysqld
    if [ "$SKIP_CLIENTS" == "no" ]
    then
        # Hack alert:
        #  install libmysqlclient.so as libmysqlclient.so.16 as client binaries
        #  seem to be linked against explicit version. Figure out better way to 
        #  deal with this.
        install -m 755 -d $MYSQL_LIBS
        install -m 755 $MYSQL_SRC/libmysql/.libs/libmysqlclient.so $MYSQL_LIBS/libmysqlclient.so.16
    fi
    if test -f $MYSQL_SRC/storage/innodb_plugin/.libs/ha_innodb_plugin.so
    then
        install -m 755 -d $MYSQL_PLUGINS
        install -m 755 $MYSQL_SRC/storage/innodb_plugin/.libs/ha_innodb_plugin.so \
                $MYSQL_PLUGINS/ha_innodb_plugin.so
    fi
    install -m 755 -d $MYSQL_BINS
    install -m 644 $MYSQL_SRC/sql/share/english/errmsg.sys $MYSQL_DIST_DIR/share/mysql/english/errmsg.sys
    install -m 755 -d $MYSQL_DIST_DIR/sbin
    install -m 755 $MYSQL_SRC/sql/mysqld $MYSQL_DIST_DIR/sbin/mysqld
    if [ "$SKIP_CLIENTS" == "no" ]
    then
        # Hack alert:
        #  install libmysqlclient.so as libmysqlclient.so.16 as client binaries
        #  seem to be linked against explicit version. Figure out better way to 
        #  deal with this.
        install -m 755 -d $MYSQL_LIBS
        install -m 755 $MYSQL_SRC/libmysql/.libs/libmysqlclient.so $MYSQL_LIBS/libmysqlclient.so.16
    fi
    if test -f $MYSQL_SRC/storage/innodb_plugin/.libs/ha_innodb_plugin.so
    then
        install -m 755 -d $MYSQL_PLUGINS
        install -m 755 $MYSQL_SRC/storage/innodb_plugin/.libs/ha_innodb_plugin.so \
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

    install -m 755 -t $MYSQL_BINS     $MYSQL_SRC/scripts/wsrep_sst_common
    install -m 755 -t $MYSQL_BINS     $MYSQL_SRC/scripts/wsrep_sst_mysqldump
    install -m 755 -t $MYSQL_BINS     $MYSQL_SRC/scripts/wsrep_sst_rsync
    install -m 755 -d $MYSQL_CHARSETS
    install -m 644 -t $MYSQL_CHARSETS $MYSQL_SRC/sql/share/charsets/*.xml
    install -m 644 -t $MYSQL_CHARSETS $MYSQL_SRC/sql/share/charsets/README
}

install_mysql_5.5_dist()
{
    export DESTDIR=$BUILD_ROOT/dist/mysql
    mkdir -p $DESTDIR
    pushd $MYSQL_BUILD_DIR
    make install
    popd
    unset DESTDIR
}

install_mysql_5.5_demo()
{
    export DESTDIR=$BUILD_ROOT/dist/mysql
    mkdir -p $DESTDIR
    pushd $MYSQL_BUILD_DIR
    cmake -DCMAKE_INSTALL_COMPONENT=Server -P cmake_install.cmake
    cmake -DCMAKE_INSTALL_COMPONENT=Client -P cmake_install.cmake
    cmake -DCMAKE_INSTALL_COMPONENT=SharedLibraries -P cmake_install.cmake
    cmake -DCMAKE_INSTALL_COMPONENT=ManPages -P cmake_install.cmake
    [ "$DEBUG" == "yes" ] && cmake -DCMAKE_INSTALL_COMPONENT=Debuginfo -P cmake_install.cmake
    popd
    unset DESTDIR
    pushd $MYSQL_DIST_DIR
    [ -d usr/local ] && ( mv usr/local/* ./ && rmdir usr/local ) # FreeBSD
    [ -d libexec -a ! -d sbin ] && mv libexec sbin # FreeBSD
    mv usr/* ./ && rmdir usr
    [ -d lib64 -a ! -d lib ] && mv lib64 lib
    popd
}

if [ $TAR == "yes" ]; then
    echo "Creating demo distribution"
    # Create build directory structure
    DIST_DIR=$BUILD_ROOT/dist
    MYSQL_DIST_DIR=$DIST_DIR/mysql
    MYSQL_DIST_CNF=$MYSQL_DIST_DIR/etc/my.cnf
    GALERA_DIST_DIR=$DIST_DIR/galera
    MYSQL_BINS=$MYSQL_DIST_DIR/bin

    cd $BUILD_ROOT
    rm -rf $DIST_DIR

    # Install required MySQL files in the DIST_DIR
    if [ $MYSQL_MAJOR == "5.1" ]; then
        install_mysql_5.1_demo
        install -m 755 -d $(dirname $MYSQL_DIST_CNF)
        install -m 644 my-5.1.cnf $MYSQL_DIST_CNF
    else
        install_mysql_5.5_demo > /dev/null
        install -m 755 -d $(dirname $MYSQL_DIST_CNF)
        install -m 644 my-5.5.cnf $MYSQL_DIST_CNF
    fi

    cat $MYSQL_BUILD_DIR/support-files/wsrep.cnf | \
        sed 's/root:$/root:rootpass/' >> $MYSQL_DIST_CNF
    pushd $MYSQL_BINS; ln -s wsrep_sst_rsync wsrep_sst_rsync_wan; popd
    tar -xzf mysql_var_$MYSQL_MAJOR.tgz -C $MYSQL_DIST_DIR
    install -m 644 LICENSE.mysql $MYSQL_DIST_DIR

    # Copy required Galera libraries
    GALERA_BINS=$GALERA_DIST_DIR/bin
    GALERA_LIBS=$GALERA_DIST_DIR/lib
    install -m 755 -d $GALERA_DIST_DIR
    install -m 644 ../../LICENSE $GALERA_DIST_DIR/LICENSE.galera
    install -m 755 -d $GALERA_BINS
    install -m 755 -d $GALERA_LIBS

    if [ "$SCONS" == "yes" ]
    then
        SCONS_VD=$GALERA_SRC
        cp -P $SCONS_VD/garb/garbd       $GALERA_BINS
        cp -P $SCONS_VD/libgalera_smm.so $GALERA_LIBS
        if [ "$OS" == "Darwin" -a "$DEBUG" == "yes" ]; then
            cp -P -R $SCONS_VD/garb/garbd.dSYM       $GALERA_BINS
            cp -P -R $SCONS_VD/libgalera_smm.so.dSYM $GALERA_LIBS
        fi
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
        for d in $GALERA_BINS $GALERA_LIBS \
                 $MYSQL_DIST_DIR/bin $MYSQL_DIST_DIR/lib $MYSQL_DIST_DIR/sbin
        do
            for f in $d/*
            do
                file $f | grep 'not stripped' >/dev/null && strip $f || :
            done
        done
    fi

fi # if [ $TAR == "yes" ]

if [ "$BIN_DIST" == "yes" ]; then
    . bin_dist.sh
fi

if [ "$TAR" == "yes" ] || [ "$BIN_DIST" == "yes" ]; then

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
    if test -n "$SYNC_BEFORE_PACK"
    then
        echo "syncing disks"
        sync
        sleep 1
    fi
    # Pack the release
    tar -czf $RELEASE_NAME.tgz $RELEASE_NAME && rm -rf $RELEASE_NAME

fi # if [ $TAR == "yes"  || "$BIN_DIST" == "yes" ]

if [ "$TAR" == "yes" ] && [ "$INSTALL" == "yes" ]; then
    cmd="$GALERA_SRC/tests/scripts/command.sh"
    $cmd stop
    $cmd install $RELEASE_NAME.tgz
fi

get_arch()
{
    if ! [ -z "$TARGET" ]
    then
        if [ "$TARGET" == "i686" ]
        then
            echo "i386"
        else
            echo "amd64"
        fi
    elif [ "$OS" == "Darwin" ]; then
        if file $MYSQL_SRC/sql/mysqld | grep "i386" >/dev/null 2>&1
        then
            echo "i386"
        else
            echo "amd64"
        fi
    else
        if file $MYSQL_SRC/sql/mysqld | grep "80386" >/dev/null 2>&1
        then
            echo "i386"
        else
            echo "amd64"
        fi
    fi
}

build_linux_packages()
{
    pushd $GALERA_SRC/scripts/mysql

    local ARCH=$(get_arch)
    local WHOAMI=$(whoami)

    if [ $DEBIAN -eq 0 ] && [ "$ARCH" == "amd64" ]; then
        ARCH="x86_64"
        export x86_64=$ARCH # for epm
    fi

    local STRIP_OPT=""
    [ "$NO_STRIP" == "yes" ] && STRIP_OPT="-g"

    export MYSQL_VER MYSQL_SRC GALERA_SRC RELEASE_NAME MYSQLD_BINARY
    export WSREP_VER=${RELEASE:-"$WSREP_REV"}

    echo $MYSQL_SRC $MYSQL_VER $ARCH
    rm -rf $ARCH

    set +e
    if [ $DEBIAN -ne 0 ]; then #build DEB
        local deb_basename="mysql-server-wsrep"
        pushd debian
        $SUDO_ENV $(which epm) -n -m "$ARCH" -a "$ARCH" -f "deb" \
             --output-dir $ARCH $STRIP_OPT $deb_basename
        RET=$?
        $SUDO /bin/chown -R $WHOAMI.users $ARCH
    else # build RPM
        echo "RPMs are now built by a separate script."
        return 1
    fi

    popd
    return $RET
}

build_freebsd_packages()
{
    echo "Creating FreeBSD packages"
    # Create build directory structure
    DIST_DIR=$BUILD_ROOT/dist/mysql
    MYSQL_DIST_DIR=$DIST_DIR/usr/local
    MYSQL_DIST_CNF=$MYSQL_DIST_DIR/share/mysql/my_wsrep.cnf
    MYSQL_BINS=$MYSQL_DIST_DIR/bin
    MYSQL_CLIENT_LICENSE_DIR=$MYSQL_DIST_DIR/share/licenses/mysql-client-${MYSQL_VER}_wsrep_${RELEASE}
    MYSQL_SERVER_LICENSE_DIR=$MYSQL_DIST_DIR/share/licenses/mysql-server-${MYSQL_VER}_wsrep_${RELEASE}
    MYSQL_SERVER_DOC_DIR=$MYSQL_DIST_DIR/share/doc/mysql${MYSQL_MAJOR_VER}${MYSQL_MINOR_VER}-server_wsrep

    cd $BUILD_ROOT
    rm -rf $BUILD_ROOT/dist

    install_mysql_5.5_dist > /dev/null
    install -m 755 -d $(dirname $MYSQL_DIST_CNF)
    install -m 644 my-5.5.cnf $MYSQL_DIST_CNF

    cat $MYSQL_BUILD_DIR/support-files/wsrep.cnf | \
        sed 's/root:$/root:rootpass/' >> $MYSQL_DIST_CNF
    pushd $MYSQL_BINS; ln -s wsrep_sst_rsync wsrep_sst_rsync_wan; popd

    install -m 755 -d "$MYSQL_CLIENT_LICENSE_DIR"
    install -m 644 ../../LICENSE "$MYSQL_CLIENT_LICENSE_DIR/GPLv3"
    install -m 644 freebsd/LICENSE "$MYSQL_CLIENT_LICENSE_DIR"
    install -m 644 freebsd/catalog.mk "$MYSQL_CLIENT_LICENSE_DIR"

    install -m 755 -d "$MYSQL_SERVER_LICENSE_DIR"
    install -m 644 ../../LICENSE "$MYSQL_SERVER_LICENSE_DIR/GPLv3"
    install -m 644 freebsd/LICENSE "$MYSQL_SERVER_LICENSE_DIR"
    install -m 644 freebsd/catalog.mk "$MYSQL_SERVER_LICENSE_DIR"

    install -m 755 -d "$MYSQL_SERVER_DOC_DIR"
    install -m 644 README      "$MYSQL_SERVER_DOC_DIR"
    install -m 644 QUICK_START "$MYSQL_SERVER_DOC_DIR"

    # Strip binaries if not instructed otherwise
    if test "$NO_STRIP" != "yes"
    then
         for d in $MYSQL_DIST_DIR/bin $MYSQL_DIST_DIR/lib $MYSQL_DIST_DIR/libexec
        do
            for f in $d/*
            do
                file $f | grep 'not stripped' >/dev/null && strip $f || :
            done
        done
    fi

    pwd
    ./freebsd.sh $MYSQL_VER $RELEASE
    rm -rf $BUILD_ROOT/dist
}

if [ "$PACKAGE" == "yes" ]; then
    case "$OS" in
        Linux)
            build_linux_packages
            ;;
        FreeBSD)
            build_freebsd_packages
            mv *.tbz ../..
            ;;
        *)
            echo "packages for $OS are not supported."
            return 1
            ;;
    esac
fi
#
