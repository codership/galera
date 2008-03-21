#!/bin/bash

# Fail if any command fails
set -e

have_ccache="false"
#if test -n "`which ccache`"
#then
#    have_ccache="true"
#    if test -n "`which distcc`"
#    then
#	export CCACHE_PREFIX="distcc"
#	export MAKE="make -j6"
#	export DISTCC_HOSTS="192.168.2.1 192.168.2.2 192.168.2.3"
#    fi
#    export CC="ccache gcc"
#    export CXX="ccache g++"
#
#fi

initial_stage="galerautils"
last_stage="wsdb"
gainroot=""

usage()
{
    echo -e "Usage: build.sh [OPTIONS] \n" \
	"Options:                      \n" \
        "    --stage <initial stage>   \n" \
	"    --last-stage <last stage> \n" \
	"    -s|--scratch    build everything from scratch\n"\
	"    -c|--configure  reconfigure the build system (implies -s)\n"\
	"    -b|--bootstap   rebuild the build system (implies -c)\n"\
	"    -r|--release    configure build with debug disabled (implies -c)\n"
	"    -d|--debug      configure build with debug disabled (implies -c)\n"

}

set -x

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
	    DEBUG=yes   # Compile without debug
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


# Be quite verbose
set -x

# Build process base directory
#build_base=`pwd`
build_base=$(cd $(dirname $0); cd ..; pwd -P)

# Define branches to be used
galerautils_branch=$build_base/galerautils
galeracomm_branch=$build_base/galeracomm
gcs_branch=$build_base/gcs
wsdb_branch=$build_base/wsdb
mysql_branch=$build_base/../../5.1/trunk

# Create build directory if it does not exist
if ! test -d $build_base/build ; then 
    mkdir $build_base/build
fi


# Install destination setup
prefix=${GALERA_BUILD_PREFIX:-"opt/galera"}
destdir=${GALERA_BUILD_DEST:-"$build_base/build/dest"}

export GALERA_DEST="$destdir/$prefix"


#
# export DESTDIR=$destdir
# export LIBDIR="$destdir/lib"

# Export this for configure scripts
# For example wsdb conf calls AC_FUNC_MALLOC which tries
# to link against -lgcs, which obviously is not found from
# standard locations. 

export LD_LIBRARY_PATH="$GALERA_DEST/lib"

# Export build prefix also for mysql
export MYSQL_BUILD_PREFIX=$GALERA_DEST

# Flags for configure scripts 
conf_flags="--prefix=$GALERA_DEST"
galera_flags="--with-galera=$GALERA_DEST"

if [ "$RELEASE" == "yes" ]
then
    conf_flags="$conf_flags --disable-debug"
    CONFIGURE="yes"
fi
if [ "$DEBUG" == "yes" ]
then
    CONFIGURE="yes"
fi


# Function to build single project
build()
{
    local build_dir=$1
    shift
    echo "Building: $build_dir ($@)"
    pushd $build_dir
    export LD_LIBRARY_PATH
    export CPPFLAGS
    export LDFLAGS
    if [ "$BOOTSTRAP" == "yes" ]; then ./bootstrap.sh; CONFIGURE=yes ; fi
    if [ "$CONFIGURE" == "yes" ]; then ./configure $@; SCRATCH=yes ; fi
    if [ "$SCRATCH"   == "yes" ]; then make clean ; fi
    make
    LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$build_dir/src/.libs
    CPPFLAGS="$CPPFLAGS -I$build_dir/src "
    LDFLAGS="$LDFLAGS -L$build_dir/src/.libs"
#    $gainroot make install
    popd
}

building="false"
# Build projects

if test $initial_stage = "scratch"
then
    rm -rf
    if test $have_ccache = "true"
    then
	ccache -C
    fi
    building="true"
fi

if test $initial_stage = "galerautils" || $building = "true"
then
    build $galerautils_branch $conf_flags
    building="true"
fi

if test $initial_stage = "galeracomm" || $building = "true"
then
    build $galeracomm_branch $conf_flags $galera_flags
    CPPFLAGS="$CPPFLAGS -I$galeracomm_branch/vs/include" # non-standard location
    CPPFLAGS="$CPPFLAGS -I$galeracomm_branch/common/include" # non-standard location
    LDFLAGS="$LDFLAGS -L$galeracomm_branch/common/src/.libs"
    LDFLAGS="$LDFLAGS -L$galeracomm_branch/transport/src/.libs"
    LDFLAGS="$LDFLAGS -L$galeracomm_branch/vs/src/.libs"
    building="true"
fi

if test $initial_stage = "gcs" || $building = "true"
then
    build $gcs_branch $conf_flags $galera_flags
    building="true"
fi

if test $initial_stage = "wsdb" || $building = "true"
then
    build $wsdb_branch $conf_flags $galera_flags
    CPPFLAGS="$CPPFLAGS -I$wsdb_branch/include" # non-standard location
    building="true"
fi

if test $last_stage != "mysql"
then
    exit 0
fi

if test $initial_stage = "mysql" || $building = "true"
then
    export CPPFLAGS
    export LD_LIBRARY_PATH
    pushd $mysql_branch
    if [ "$DEBUG" == "yes" ]; then
        ./BUILD/compile-pentium-debug-galera
    else
        ./BUILD/compile-pentium-galera
    fi
    $gainroot make install
    popd
    building="true"
fi

if test $building != "true"
then
    echo "Warn: Nothing was built!"
fi
