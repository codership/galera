#!/bin/bash -eu

# $Id$

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
    --scons         build using Scons build system (yes)
    -j|--jobs       how many parallel jobs to use for Scons (1)
    "\nSet DISABLE_GCOMM/DISABLE_VSBES to 'yes' to disable respective modules"
EOF
}

# disable building vsbes by default
DISABLE_VSBES=${DISABLE_VSBES:-"yes"}
DISABLE_GCOMM=${DISABLE_GCOMM:-"no"}
PACKAGE=${PACKAGE:-"no"}
SKIP_BUILD=${SKIP_BUILD:-"no"}
RELEASE=${RELEASE:-""}
SOURCE=${SOURCE:-"no"}
DEBUG=${DEBUG:-"no"}
DEBUG_LEVEL=${DEBUG_LEVEL:-"1"}
SCONS=${SCONS:-"yes"}
JOBS=${JOBS:-"$(cat /proc/cpuinfo | grep -c ^processor)"}
SCRATCH=${SCRATCH:-"no"}
OPT="yes"
NO_STRIP=${NO_STRIP:-"no"}
WITH_SPREAD="no"

which dpkg >/dev/null 2>&1 && DEBIAN=${DEBIAN:-1} || DEBIAN=${DEBIAN:-0}

if ccache -V > /dev/null 2>&1
then
    CC=${CC:-"gcc"}
    CXX=${CXX:-"g++"}
    echo "$CC"  | grep "ccache" > /dev/null || CC="ccache $CC"
    echo "$CXX" | grep "ccache" > /dev/null || CXX="ccache $CXX"
    export CC CXX
fi

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
	--scons)
	    SCONS="yes"
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
    echo "testing sudo"
    if sudo -E /bin/true >/dev/null 2>&1
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
set -x

# Build process base directory
build_base=$(cd $(dirname $0)/..; pwd -P)

get_arch()
{
    if file $build_base/gcs/src/gcs.o | grep "80386" >/dev/null 2>&1
    then
        echo "i386"
    else
        echo "amd64"
    fi
}

build_packages()
{
    pushd $build_base/scripts/packages

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

    rm -rf $ARCH

    set +e
    if [ $DEBIAN -ne 0 ]
    then # build DEB
        $SUDO /usr/bin/epm -n -m "$ARCH" -a "$ARCH" -f "deb" \
             --output-dir $ARCH $STRIP_OPT galera # && \
        $SUDO /bin/chown -R $WHOAMI.users $ARCH
    else # build RPM
        ./rpm.sh $GALERA_VER
    fi
    local RET=$?

    set -e

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
    GALERA_REV=$(bzr revno)
#fi
popd

if [ -z "$RELEASE" ]
then
    RELEASE=$GALERA_REV
fi

if [ "$SCONS" == "yes" ] # Build using Scons
then
    # Scons variant dir, defaults to GALERA_SRC
    export SCONS_VD=$build_base
    scons_args="-C $build_base revno=$GALERA_REV"

    if [ -n "$TARGET" ]
    then
        scons_args="$scons_args arch=$TARGET"
    fi

    if [ "$DEBUG" == "yes" ]
    then
        scons_args="$scons_args debug=$DEBUG_LEVEL"
    fi

    if [ "$SCRATCH" == "yes" ]
    then
        scons -Q -c --conf=force $scons_args
    fi

    if [ "$SKIP_BUILD" != "yes" ]
    then
        scons $scons_args -j $JOBS
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


