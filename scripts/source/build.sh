#!/bin/bash -eu

# $Id: build.sh 1323 2009-11-22 23:48:22Z alex $

usage()
{
    echo -e "Usage: build.sh [script options] [configure options]\n"\
    "Script options:\n"\
    "    -h|--help       this help message\n"\
    "    -i|--install    install libraries system-wide\n"
}

INSTALL="no"
CONFIGURE="no"
LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-""}
CPPFLAGS=${CPPFLAGS:-""}
LDFLAGS=${LDFLAGS:-""}

if ccache -V > /dev/null 2>&1
then
    CC=${CC:-"gcc"}
    CXX=${CXX:-"g++"}
    echo "$CC"  | grep "ccache" > /dev/null || CC="ccache $CC"
    echo "$CXX" | grep "ccache" > /dev/null || CXX="ccache $CXX"
    export CC CXX
fi

while test $# -gt 0
do
    case $1 in
	-i|--install)
	    INSTALL="yes"
	    shift
	    ;;
	-h|--help)
	    usage
	    exit 1
	    ;;
	*)
	    # what's left is the arguments for configure
	    CONFIGURE="yes"
	    break
	    ;;
    esac
done

# Build process base directory
build_base=$(cd $(dirname $0); pwd -P)

# Updates build flags for the next stage
build_flags()
{
    local build_dir=$1
    LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$build_dir/src/.libs
    CPPFLAGS="$CPPFLAGS -I$build_dir/src "
    LDFLAGS="$LDFLAGS -L$build_dir/src/.libs"
}

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

    local SCRATCH=no

    if [ ! -s "Makefile"  ]; then CONFIGURE=yes; fi
    if [ "$CONFIGURE" == "yes" ]; then rm -rf config.status; ./configure $@; SCRATCH=yes ; fi
    if [ "$SCRATCH"   == "yes" ]; then make clean ; fi

    make || return 1

    if [ "$INSTALL" == "yes" ]
    then
        make install || return 1
    else
        build_flags $(pwd -P) || return 1
    fi

    popd
}

build "libgalerautils*" $@
build "libgcomm*"       $@
build "libgcs*"         $@
build "libwsdb*"        $@
build "libgalera*"      $@
