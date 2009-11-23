#!/bin/bash -eu

# $Id: build.sh 1323 2009-11-22 23:48:22Z alex $

usage()
{
    echo -e "Usage: build.sh [configure options] \n"
    "Options:                      \n" \
    "    --stage <initial stage>   \n"
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

set -x

while test $# -gt 0 
do
    case $1 in 
	--install)
	    INSTALL="yes"
	    shift
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
        build_flags $(pwd -P)
    fi

    popd
}

#build_packages()
#{
#    local ARCH=$(uname -m)
#    local ARCH_DEB
#    local ARCH_RPM
#    if [ "$ARCH" == "i686" ]
#    then
#        ARCH_DEB=i386
#        ARCH_RPM=i386
#    else
#        ARCH_DEB=amd64
#        ARCH_RPM=x86_64
#    fi
#
#    if [ "$DISABLE_GCOMM" != "yes" ]; then export GCOMM=yes; fi
#    if [ "$DISABLE_VSBES" != "yes" ]; then export VSBES=yes; fi
#
#    export BUILD_BASE=$build_base
#    echo GCOMM=$GCOMM VSBES=$VSBES ARCH_DEB=$ARCH_DEB ARCH_RPM=$ARCH_RPM
#    pushd $build_base/scripts/packages                       && \
#    rm -rf $ARCH_DEB $ARCH_RPM                               && \
#    epm -n -m "$ARCH_DEB" -a "$ARCH_DEB" -f "deb" galera     && \
#    epm -n -m "$ARCH_DEB" -a "$ARCH_DEB" -f "deb" galera-dev && \
#    epm -n -m "$ARCH_RPM" -a "$ARCH_RPM" -f "rpm" galera     && \
#    epm -n -m "$ARCH_RPM" -a "$ARCH_RPM" -f "rpm" galera-dev || \
#    return 1
#}

# Most modules are standard, so we can use a single function
#build_module()
#{
#    local module="$1"
#    shift
#    local build_dir="$build_base/$module"
#    if test "$initial_stage" == "$module" || "$building" = "true"
#    then
#        build $build_dir $conf_flags $@ && building="true" || return 1
#    fi
#
#    build_flags $build_dir || return 1
#}

#build_source()
#{
#    local module="$1"
#    shift
#    local build_dir="$build_base/$module"
#    pushd $build_dir
#
#    if [ ! -x "configure" ]; then ./bootstrap.sh; fi
#    if [ ! -s "Makefile"  ]; then ./configure;    fi
#
#    local src_base_name="lib${module}-"
#    rm -rf "${src_base_name}"*.tar.gz
#    make dist || (echo $?; echo "make dist failed"; echo)
#    local ret=$(ls "${src_base_name}"*.tar.gz)
#    popd
#
#    echo $build_dir/$ret
#}

#build_sources()
#{
#    local module
#    local srcs=""
#
#    for module in "galerautils" "gcomm" "gcs" "wsdb" "galera"
#    do
#        src=$(build_source $module | tail -n 1)
#        srcs="$srcs $src"
#    done
#
#    if [ -z "$RELEASE" ]
#    then
#        pushd "$build_base"
#        RELEASE="r$(svnversion | sed s/\:/,/g)"
#        popd
#    fi
#
#    local ret="galera-$RELEASE.tar"
#    tar --transform 's/.*\///' -cf $ret $srcs "README_BUILD" "COPYING"
#
#    # return absolute path for scripts
#    echo $PWD/$ret
#}

#building="false"

# The whole purpose of ccache is to be able to safely make clean and not rebuild
#if test $initial_stage = "scratch"
#then
## Commented out, not sure where this does its tricks (teemu)
##    rm -rf
#    if test $have_ccache = "true"
#    then
#        ccache -C
#    fi
#    building="true"
#fi

#echo "CC: $CC"
#echo "CPPFLAGS: $CPPFLAGS"

build "libgalerautils*" $@
build "libgcomm*"       $@
build "libgcs*"         $@
build "libwsdb*"        $@
build "libgalera*"      $@
