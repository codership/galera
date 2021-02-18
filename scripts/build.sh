#!/usr/bin/env bash

set -eu

# $Id$

get_cores()
{
    case $OS in
        "Linux")
            echo "$(grep -c ^processor /proc/cpuinfo)" ;;
        "SunOS")
            echo "$(psrinfo | wc -l | tr -d ' ')" ;;
        "Darwin" | "FreeBSD")
            echo "$(sysctl -n hw.ncpu)" ;;
        *)
            echo "CPU information not available: unsupported OS: '$OS'" >/dev/stderr
            echo 1
            ;;
    esac
}

usage()
{
    cat << EOF
Usage: build.sh [OPTIONS]
Options:
    --stage <initial stage>
    --last-stage <last stage>
    -s|--scratch    build everything from scratch
    -c|--configure  reconfigure the build system (implies -s)
    -b|--bootstap   rebuild the build system (implies -c)
    -o|--opt        configure build with debug disabled (implies -c)
    -d|--debug      configure build with debug enabled (implies -c)
    --dl            set debug level for Scons build (1, implies -c)
    -r|--release    release number
    -m32/-m64       build 32/64-bit binaries for x86
    -p|--package    build RPM packages at the end (DEB not supported).
    --with-spread   configure build with spread backend (implies -c to gcs)
    --source        build source packages
    --sb            skip actual build, use the existing binaries
    --cmake         build using CMake build system (yes)
    --co            CMake options
    --scons         build using Scons build system (no)
    --so            Sconscript option
    -j|--jobs       how many parallel jobs to use for Scons (1)
    "\nSet DISABLE_GCOMM/DISABLE_VSBES to 'yes' to disable respective modules"
EOF
}

OS=$(uname)
# disable building vsbes by default
DISABLE_VSBES=${DISABLE_VSBES:-"yes"}
DISABLE_GCOMM=${DISABLE_GCOMM:-"no"}
PACKAGE=${PACKAGE:-"no"}
SKIP_BUILD=${SKIP_BUILD:-"no"}
RELEASE=${RELEASE:-""}
SOURCE=${SOURCE:-"no"}
DEBUG=${DEBUG:-"no"}
DEBUG_LEVEL=${DEBUG_LEVEL:-"0"}
CMAKE=${CMAKE:-"yes"}
CMAKE_OPTS=${CMAKE_OPTS:-""}
SCONS=${SCONS:-"no"}
SCONS_OPTS=${SCONS_OPTS:-""}
export JOBS=${JOBS:-"$(get_cores)"}
SCRATCH=${SCRATCH:-"no"}
OPT="yes"
NO_STRIP=${NO_STRIP:-"no"}
WITH_SPREAD="no"
RUN_TESTS=${RUN_TESTS:-1}
if [ "$OS" == "FreeBSD" ]; then
  chown=/usr/sbin/chown
  true=/usr/bin/true
  epm=/usr/local/bin/epm
else
  chown=/bin/chown
  true=/bin/true
  epm=/usr/bin/epm
fi

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

if [ "$OS" == "FreeBSD" ]; then
    CC=${CC:-"clang"}
    CXX=${CXX:-"clang++"}
else
    CC=${CC:-"gcc"}
    CXX=${CXX:-"g++"}
fi

if ccache -V > /dev/null 2>&1
then
    echo "$CC"  | grep "ccache" > /dev/null || CC="ccache $CC"
    echo "$CXX" | grep "ccache" > /dev/null || CXX="ccache $CXX"
fi
export CC CXX LD_LIBRARY_PATH

CFLAGS=${CFLAGS:-"-O2"}
CXXFLAGS=${CXXFLAGS:-"$CFLAGS"}
CPPFLAGS=${CPPFLAGS:-}

initial_stage="galerautils"
last_stage="galera"
gainroot=""
TARGET=${TARGET:-""} # default target

while test $# -gt 0
do
    case $1 in
        --stage)
            initial_stage=$2
            shift
            ;;
        --last-stage)
            last_stage=$2
            shift
            ;;
        --gainroot)
            gainroot=$2
            shift
            ;;
        -b|--bootstrap)
            BOOTSTRAP="yes" # Bootstrap the build system
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
            NO_STRIP="yes"
            ;;
        -r|--release)
            RELEASE="$2"
            shift
            ;;
        -m32)
            CFLAGS="$CFLAGS -m32"
            CXXFLAGS="$CXXFLAGS -m32"
            SCRATCH="yes"
            TARGET="i686"
            ;;
        -m64)
            CFLAGS="$CFLAGS -m64"
            CXXFLAGS="$CXXFLAGS -m64"
            SCRATCH="yes"
            TARGET="x86_64"
            ;;
        -p|--package)
            PACKAGE="yes"   # build binary packages
            ;;
        --with*-spread)
            WITH_SPREAD="$1"
            ;;
        --help)
            usage
            exit 0
            ;;
        --source)
            SOURCE="yes"
            ;;
        --sb)
            SKIP_BUILD="yes"
            ;;
        --cmake)
            CMAKE="yes"
            SCONS="no"
            ;;
        --co)
            CMAKE_OPTS="$CMAKE_OPTS $2"
            shift
            ;;
        --scons)
            SCONS="yes"
            CMAKE="no"
            ;;
        --so)
            SCONS_OPTS="$SCONS_OPTS $2"
            shift
            ;;
        -j|--jobs)
            JOBS=$2
            shift
            ;;
        --dl)
            DEBUG_LEVEL=$2
            shift
            ;;
        *)
            if test ! -z "$1"; then
               echo "Unrecognized option: $1"
            fi
            usage
            exit 1
            ;;
    esac
    shift
done

# check whether sudo accepts -E to preserve environment
if [ "$PACKAGE" == "yes" ]
then
    if which dpkg >/dev/null 2>&1
    then
        echo "Error: Package build not supported on Debian, use dpkg-buildpackage"
        exit 1
    fi
    echo "testing sudo"
    if sudo -E $true >/dev/null 2>&1
    then
        echo "sudo accepts -E"
        SUDO="sudo -E"
    else
        echo "sudo does not accept param -E"
        if [ $(id -ur) != 0 ]
        then
            echo "error, must build as root"
            exit 1
        else
            echo "I'm root, can continue"
            SUDO=""
        fi
    fi
fi

if [ "$OPT"   == "yes" ]; then CONFIGURE="yes";
   conf_flags="--disable-debug --disable-dbug";
fi
if [ "$DEBUG" == "yes" ]; then CONFIGURE="yes"; fi
if [ -n "$WITH_SPREAD" ]; then CONFIGURE="yes"; fi

if [ "$CONFIGURE" == "yes" ] && [ "$SCONS" != "yes" ]; then SCRATCH="yes"; fi

# Be quite verbose
#set -x

# Build process base directory
build_base=${GALERA_SRC:-$(cd $(dirname $0)/..; pwd -P)}

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
        if file $build_base/gcs/src/gcs.o | grep "i386" >/dev/null 2>&1
        then
            echo "i386"
        else
            echo "amd64"
        fi
    else
        if file $build_base/gcs/src/gcs.o | grep "80386" >/dev/null 2>&1
        then
            echo "i386"
        else
            echo "amd64"
        fi
    fi
}

build_packages()
{
    local PKG_DIR=$build_base/scripts/packages
    pushd $PKG_DIR

    local ARCH=$(get_arch)
    local WHOAMI=$(whoami)

    export BUILD_BASE=$build_base
    export GALERA_VER=$RELEASE

    if [ $DEBIAN -eq 0 ] && [ "$ARCH" == "amd64" ]
    then
        ARCH="x86_64"
        export x86_64=$ARCH # for epm
    fi
    export $ARCH

    local STRIP_OPT=""
    [ "$NO_STRIP" == "yes" ] && STRIP_OPT="-g"

    $SUDO rm -rf $ARCH

    set +e
    if [ "$OS" == "FreeBSD" ]; then
        if test "$NO_STRIP" != "yes"; then
            strip $build_base/{garb/garbd,libgalera_smm.so}
        fi
        ./freebsd.sh $GALERA_VER
    else # build RPM
        ./rpm.sh $GALERA_VER
    fi
    local RET=$?

    set -e

    popd
    if [ "$OS" == "FreeBSD" ]; then
        mv -f $PKG_DIR/*.tbz ./
    else
        mv -f $PKG_DIR/*.rpm ./
    fi
    return $RET
}

build_source()
{
    local module="$1"
    shift
    local build_dir="$build_base/$module"
    pushd $build_dir

    if [ ! -x "configure" ]; then ./bootstrap.sh; fi
    if [ ! -s "Makefile"  ]; then ./configure;    fi

    local src_base_name="lib${module}-"
    rm -rf "${src_base_name}"*.tar.gz
    make dist || (echo $?; echo "make dist failed"; echo)
    local ret=$(ls "${src_base_name}"*.tar.gz)
    popd

    echo $build_dir/$ret
}

build_sources()
{
    local module
    local srcs=""

    for module in "galerautils" "gcomm" "gcs" "galera"
    do
        src=$(build_source $module | tail -n 1)
        srcs="$srcs $src"
    done

    local ret="galera-source-r$RELEASE.tar"
    tar --transform 's/.*\///' -cf $ret $srcs \
    "source/README" "source/COPYING" "source/build.sh"

    # return absolute path for scripts
    echo $PWD/$ret
}

pushd "$build_base"
#GALERA_REV="$(svnversion | sed s/\:/,/g)"
#if [ "$GALERA_REV" == "exported" ]
#then
    GALERA_REV=$(git log --pretty=oneline | wc -l) || \
    GALERA_REV=$(bzr revno --tree -q)              || \
    GALERA_REV=$(svn info >&/dev/null && svnversion | sed s/\:/,/g) || \
    GALERA_REV="XXXX"
    # trim spaces (sed is not working on Solaris, so using bash built-in)
    GALERA_REV=${GALERA_REV//[[:space:]]/}
#fi

if [ -z "$RELEASE" ]
then
    source GALERA_VERSION
    RELEASE="$GALERA_VERSION_WSREP_API.$GALERA_VERSION_MAJOR.$GALERA_VERSION_MINOR"
fi

if [ "$CMAKE" == "yes" ] # Build using CMake
then
    cmake_args="$CMAKE_OPTS -DGALERA_REVISION=$GALERA_REV"
    [ -n "$TARGET"        ] && \
        echo "WARN: TARGET=$TARGET ignored by CMake build"
    [ -n "$RELEASE"       ] && \
        echo "WARN: RELEASE=$RELEASE ignored by CMake build"
    [ "$DEBUG" == "yes"   ] && cmake_args="$cmake_args -DCMAKE_BUILD_TYPE=Debug"
    [ -n "$EXTRA_SYSROOT" ] && \
        echo "EXTRA_SYSROOT=$EXTRA_SYSROOT ignored by CMake build"

    if [ "$SCRATCH" == "yes" ]
    then
        (cd $build_base && make clean) || :
        rm -f $build_base/CMakeCache.txt
        cmake $cmake_args $build_base
    fi

    if [ "$SKIP_BUILD" != "yes" ]
    then
        make -j $JOBS VERBOSE=1
    fi

    if [ $RUN_TESTS ]
    then
        make test ARGS=-V
    fi
elif [ "$SCONS" == "yes" ] # Build using Scons
then
    # Scons variant dir, defaults to GALERA_SRC
    export SCONS_VD=$build_base
    scons_args="-C $build_base revno=$GALERA_REV tests=$RUN_TESTS strict_build_flags=1"

    [ -n "$TARGET"        ] && scons_args="$scons_args arch=$TARGET"
    [ -n "$RELEASE"       ] && scons_args="$scons_args version=$RELEASE"
    [ "$DEBUG" == "yes"   ] && scons_args="$scons_args debug=$DEBUG_LEVEL"
    [ -n "$EXTRA_SYSROOT" ] && scons_args="$scons_args extra_sysroot=$EXTRA_SYSROOT"

    if [ "$SCRATCH" == "yes" ]
    then
        scons -Q -c --conf=force $scons_args $SCONS_OPTS
    fi

    if [ "$SKIP_BUILD" != "yes" ]
    then
        scons $scons_args -j $JOBS $SCONS_OPTS
    fi

elif test "$SKIP_BUILD" == "no"; then # Build using autotools
    echo "Error: autotools not supported anymore! Nothing was built."
    exit 1
fi # SKIP_BUILD / SCONS

if test "$PACKAGE" == "yes"
then
    build_packages
fi

if test "$SOURCE" == "yes"
then
    build_sources
fi

popd # $build_base
