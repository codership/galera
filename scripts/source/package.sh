#!/bin/bash -eu

# $Id: build.sh 1323 2009-11-22 23:48:22Z alex $

usage()
{
    echo -e "Usage: build.sh [script options] [configure options] \n" \
    "Script options:\n" \
    "    -h|--help                       this help message\n"\
    "    -r|--release <RELEASE NUMBER>   release number to put in the tarball\n"\
    "                                    name: galera-source-XXX.tgz"
}

RELEASE=""
CONFIGURE="no"

LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-""}
CPPFLAGS=${CPPFLAGS:-""}
LDFLAGS=${LDFLAGS:-""}

while test $# -gt 0 
do
    case $1 in 
	-r|--release)
	    RELEASE=$2
	    shift
	    shift
	    ;;
	-h|--help)
	    usage
	    exit 1
	    ;;
	*)
	    # what's left is the arguments for configure
	    break
	    ;;
    esac
done

# Build process base directory
BUILD_BASE=$(cd $(dirname $0)/../../; pwd -P)

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
    local module=$1
    shift
    local build_dir=$BUILD_BASE/$module
    echo "Building: $build_dir ($@)"
    pushd $build_dir

    export LD_LIBRARY_PATH
    export CPPFLAGS
    export LDFLAGS

    if [ ! -x "configure" ]; then ./bootstrap.sh; fi
    if [ ! -s "Makefile"  ]; then ./configure $@; fi

    local src_base_name="lib${module}-"
    rm -rf "${src_base_name}"*.tar.gz

    make dist || return 1

    build_flags $build_dir || return 1

    local ret=$(ls "${src_base_name}"*.tar.gz)

    popd
    echo $build_dir/$ret
}

build_sources()
{
    local module
    local srcs=""

    for module in "galerautils" "gcomm" "gcs" "wsdb" "galera"
    do
        src=$(build $module $@ | tail -n 1) || return 1
        srcs="$srcs $src"
    done

    if [ -z "$RELEASE" ]
    then
        pushd "$BUILD_BASE"
        RELEASE="r$(svnversion | sed s/\:/,/g)"
        popd
    fi

    local dist_dir="galera-source-$RELEASE"
    rm -rf $dist_dir
    mkdir -p $dist_dir

    for src in $srcs
    do
        tar -C $dist_dir -xzf $src
    done

    cp "README" "COPYING" "build.sh" $dist_dir/
    tar -czf $dist_dir.tgz $dist_dir

    # return absolute path for scripts
    echo $PWD/$dist_dir.tgz
}

pushd $(dirname $0)

build_sources $@
