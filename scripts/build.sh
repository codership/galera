#!/usr/bin/env bash

set -eux

# $Id$

# Galera library version
VERSION="26.4.22"

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
    -p|--package    build RPM and DEB packages at the end.
    --with-spread   configure build with spread backend (implies -c to gcs)
    --source        build source packages
    --sb            skip actual build, use the existing binaries
    --cmake         build using CMake build system (yes)
    --co            CMake options
    --scons         build using Scons build system (no)
    --so            Sconscript option
    -j|--jobs       how many parallel jobs to use for Scons (1)
    -a|--archive    Generate source archive (tar.gz)
    "\nSet DISABLE_GCOMM/DISABLE_VSBES to 'yes' to disable respective modules"
EOF
}

# Scons executable on RHEL-8 is scons-3
SCONSBIN=scons
[[ -n "$(which scons-3)" ]] && SCONSBIN=scons-3

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
ARCHIVE=${ARCHIVE:-"no"}

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
export CK_TIMEOUT_MULTIPLIER=5

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

which dpkg-buildpackage >/dev/null 2>&1 && DEBIAN=${DEBIAN:-1} || DEBIAN=${DEBIAN:-0}

if [ "$OS" == "FreeBSD" ]; then
    CC=${CC:-"clang"}
    CXX=${CXX:-"clang++"}
else
    CC=${CC:-"gcc"}
    CXX=${CXX:-"g++"}
fi

# MariaDB Centos5 & SLES-SP1 buildbot images require LD_LIBRARY_PATH to
# /usr/local/lib
if [ -r /etc/redhat-release ]; then
  if grep 'CentOS release 5' /etc/redhat-release > /dev/null 2>&1; then
    LD_LIBRARY_PATH=/usr/local/lib64/:/usr/local/lib/:${LD_LIBRARY_PATH:-}
  fi
  DEBIAN=0
fi
if [ -r /etc/issue ]; then
  if grep 'SUSE Linux Enterprise Server 11 SP1' /etc/issue > /dev/null 2>&1; then
    LD_LIBRARY_PATH=/usr/local/lib64/:/usr/local/lib/:${LD_LIBRARY_PATH:-}
    DEBIAN=0
  fi
fi

export CC CXX LD_LIBRARY_PATH

CFLAGS=${CFLAGS:-}
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
        -a|--archive)
            ARCHIVE="yes"   # Generate source archive
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

if [ "$OPT"   == "yes" ]; then CONFIGURE="yes";
   conf_flags="--disable-debug --disable-dbug";
fi
if [ "$DEBUG" == "yes" ]; then CONFIGURE="yes"; fi
if [ -n "$WITH_SPREAD" ]; then CONFIGURE="yes"; fi

if [ "$CONFIGURE" == "yes" ] && [ "$SCONS" != "yes" ]; then SCRATCH="yes"; fi

#A workaround to fix MDEV-8249 on Centos 5.
if [ -r /etc/redhat-release ]; then
  if grep 'CentOS release 5' /etc/redhat-release > /dev/null 2>&1; then
    DEBUG="yes"
  fi
fi

# Be quite verbose
#set -x

# Build process base directory
build_base=${GALERA_SRC:-$(cd $(dirname $0)/..; pwd -P)}

# Use $VERSION if one is not specified via --release
RELEASE=${RELEASE:-$VERSION}

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

    local ARCH=$(get_arch)
    local WHOAMI=$(whoami)

    local RET=0

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

    rm -rf $ARCH

    set +e
    if [ $DEBIAN -ne 0 ]; then # build DEB
        source /etc/os-release
        # sid doesn't have VERSION_ID set
        version=${VERSION_ID:-sid}
        # remove . from version e.g. 22.04
        debian_version=${ID:0:3}${version/.}

        dch -m -D "$debian_version" --force-distribution -v "$GALERA_VER-$debian_version" "Version upgrade"
        # -d : Do not check build dependencies and conflicts.
        DEB_BUILD_OPTIONS="version=$GALERA_VER revno=$GALERA_REV parallel=$JOBS nostrip" dpkg-buildpackage -us -uc -b -d
        RET=$?
    else
        if [ "$OS" == "FreeBSD" ]; then
            if test "$NO_STRIP" != "yes"; then
                strip $build_base/{garb/garbd,libgalera_smm.so}
            fi
            $PKG_DIR/freebsd.sh $GALERA_VER
        else # build RPM
            $PKG_DIR/rpm.sh $GALERA_VER
        fi
        RET=$?
    fi

    set -e

    return $RET
}

gen_source_archive()
{
  SOURCE_DIR="galera-$VERSION"
  rm -rf "$SOURCE_DIR.tar.gz" "$SOURCE_DIR"
  git checkout-index -a --prefix="$SOURCE_DIR/"
  tar czf "$SOURCE_DIR.tar.gz" "$SOURCE_DIR"
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

GALERA_VER=$RELEASE

pushd "$build_base"
GALERA_REV=$(git log --pretty=format:'%h' -n1.) || \
GALERA_REV="XXXXX"
# trim spaces (sed is not working on Solaris, so using bash built-in)
GALERA_REV=${GALERA_REV//[[:space:]]/}
popd

if [ -z "$RELEASE" ]
then
    source GALERA_VERSION
    RELEASE="$GALERA_VERSION_WSREP_API.$GALERA_VERSION_MAJOR.$GALERA_VERSION_MINOR"
fi
if [ "$PACKAGE" == "yes" -a $DEBIAN -ne 0 ]
then
    echo "Debian package build"
elif [ "$CMAKE" == "yes" ] # Build using CMake
then
    cmake_args="$CMAKE_OPTS -DGALERA_REVISION=$GALERA_REV"
    cmake_args="$cmake_args -DCMAKE_C_FLAGS="\'"$CFLAGS"\'" -DCMAKE_CXX_FLAGS="\'"$CXXFLAGS"\'
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
        eval cmake $cmake_args $build_base
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
    scons_args="-C $build_base version=$GALERA_VER revno=$GALERA_REV tests=$RUN_TESTS strict_build_flags=1"

    [ -n "$TARGET"        ] && scons_args="$scons_args arch=$TARGET"
    [ -n "$RELEASE"       ] && scons_args="$scons_args version=$RELEASE"
    [ "$DEBUG" == "yes"   ] && scons_args="$scons_args debug=$DEBUG_LEVEL"
    [ -n "$EXTRA_SYSROOT" ] && scons_args="$scons_args extra_sysroot=$EXTRA_SYSROOT"

    if [ "$SCRATCH" == "yes" ]
    then
        ${SCONSBIN} -Q -c --conf=force $scons_args $SCONS_OPTS
    fi

    if [ "$SKIP_BUILD" != "yes" ]
    then
        ${SCONSBIN} $scons_args -j $JOBS $SCONS_OPTS
    fi

elif test "$SKIP_BUILD" == "no"; then # Build using autotools
    echo "Error: autotools not supported anymore! Nothing was built."
    exit 1
fi # SKIP_BUILD / SCONS

if test "$ARCHIVE" == "yes"
then
    echo "Generating source archive ..."
    gen_source_archive
fi

if test "$PACKAGE" == "yes"
then
    build_packages
fi

if test "$SOURCE" == "yes"
then
    build_sources
fi


