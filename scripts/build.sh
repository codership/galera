#!/bin/bash -e

# $Id$

usage()
{
    echo -e "Usage: build.sh [OPTIONS] \n" \
    "Options:                      \n" \
    "    --stage <initial stage>   \n" \
    "    --last-stage <last stage> \n" \
    "    -s|--scratch    build everything from scratch\n"\
    "    -c|--configure  reconfigure the build system (implies -s)\n"\
    "    -b|--bootstap   rebuild the build system (implies -c)\n"\
    "    -o|--opt        configure build with debug disabled (implies -c)\n" \
    "    -d|--debug      configure build with debug enabled (implies -c)\n" \
    "    -r|--release    release number\n"\
    "    -m32/-m64       build 32/64-bit binaries for x86\n" \
    "    -p|--package    build RPM and DEB packages at the end.\n" \
    "    --with-spread   configure build with spread backend (implies -c to gcs)\n" \
    "    --source        build source packages\n"\
    "    --sb            skip actual build, use the existing binaries\n"\
    "\nSet DISABLE_GCOMM/DISABLE_VSBES to 'yes' to disable respective modules"
}

# disable building vsbes by default
DISABLE_VSBES=${DISABLE_VSBES:-"yes"}
PACKAGE=${PACKAGE:-"no"}
SKIP_BUILD=${SKIP_BUILD:-"no"}
RELEASE=${RELEASE:-""}
SOURCE=${SOURCE:-"no"}

which dpkg >/dev/null 2>&1 && DEBIAN=${DEBIAN:-1} || DEBIAN=${DEBIAN:-0}

if ccache -V > /dev/null 2>&1
then
    CC=${CC:-"gcc"}
    CXX=${CXX:-"g++"}
    echo "$CC"  | grep "ccache" > /dev/null || CC="ccache $CC"
    echo "$CXX" | grep "ccache" > /dev/null || CXX="ccache $CXX"
    export CC CXX
fi

initial_stage="galerautils"
last_stage="galera"
gainroot=""

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
	    NO_STRIP="no"
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
	    ;;
	-m64)
	    CFLAGS="$CFLAGS -m64"
	    CXXFLAGS="$CXXFLAGS -m64"
	    SCRATCH="yes"
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
   conf_flags="$conf_flags --disable-debug --disable-dbug";
fi
if [ "$DEBUG" == "yes" ]; then CONFIGURE="yes"; fi
if [ -n "$WITH_SPREAD" ]; then CONFIGURE="yes"; fi

if [ "$CONFIGURE" == "yes" ]; then SCRATCH="yes"; fi

# Be quite verbose
set -x

# Build process base directory
build_base=$(cd $(dirname $0); cd ..; pwd -P)

# Define branches to be used
galerautils_src=$build_base/galerautils
galeracomm_src=$build_base/galeracomm
gcomm_src=$build_base/gcomm
gcs_src=$build_base/gcs
wsdb_src=$build_base/wsdb
galera_src=$build_base/galera

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

    if   [ ! -x "configure" ]; then BOOTSTRAP=yes;
    elif [ ! -s "Makefile"  ]; then CONFIGURE=yes;
    fi

    if [ "$BOOTSTRAP" == "yes" ]; then ./bootstrap.sh; CONFIGURE=yes ; fi
    if [ "$CONFIGURE" == "yes" ]; then rm -rf config.status; ./configure $@; SCRATCH=yes ; fi
    if [ "$SCRATCH"   == "yes" ]; then make clean ; fi
    make || return 1
#    $gainroot make install
    popd
}

# Updates build flags for the next stage
build_flags()
{
    local build_dir=$1
    LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$build_dir/src/.libs
    CPPFLAGS="$CPPFLAGS -I$build_dir/src "
    LDFLAGS="$LDFLAGS -L$build_dir/src/.libs"
}

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

    [ "$DISABLE_GCOMM" != "yes" ] && export GCOMM="yes"
    [ "$DISABLE_VSBES" != "yes" ] && export VSBES="yes"

    export BUILD_BASE=$build_base
    export GALERA_VER=$RELEASE
    echo "GCOMM=$GCOMM VSBES=$VSBES ARCH=$ARCH"

    if [ $DEBIAN -eq 0 ] && [ "$ARCH" == "amd64" ]
    then
        ARCH="x86_64"
        export x86_64=$ARCH # for epm
    fi

    local STRIP_OPT=""
    [ "$NO_STRIP" == "yes" ] && STRIP_OPT="-g"

    rm -rf $ARCH

    set +e
    if [ $DEBIAN -ne 0 ]
    then # build DEB
        sudo -E /usr/bin/epm -n -m "$ARCH" -a "$ARCH" -f "deb" \
             --output-dir $ARCH $STRIP_OPT galera # && \
#       sudo -E /usr/bin/epm -n -m "$ARCH" -a "$ARCH" -f "deb" \
#            --output-dir $ARCH $STRIP_OPT galera-dev
    else # build RPM
        (sudo -E /usr/bin/epm -vv -n -m "$ARCH" -a "$ARCH" -f "rpm" \
              --output-dir $ARCH --keep-files -k $STRIP_OPT galera || \
         /usr/bin/rpmbuild -bb --target "$ARCH" "$ARCH/galera.spec" \
              --buildroot="$ARCH/buildroot" ) # && \
#        /usr/bin/epm -n -m "$ARCH" -a "$ARCH" -f "rpm" \
#             --output-dir $ARCH $STRIP_OPT galera-dev
    fi
    local RET=$?

    sudo /bin/chown -R $WHOAMI.users $ARCH
    set -e

    if [ $RET -eq 0 ] && [ $DEBIAN -eq 0 ]
    then
        mv $ARCH/RPMS/$ARCH/*.rpm $ARCH/ && \
        rm -rf $ARCH/RPMS $ARCH/buildroot $ARCH/rpms # $ARCH/galera.spec
    fi

    return $RET
}

# Most modules are standard, so we can use a single function
build_module()
{
    local module="$1"
    shift
    local build_dir="$build_base/$module"
    if test "$initial_stage" == "$module" || "$building" = "true"
    then
        build $build_dir $conf_flags $@ && building="true" || return 1
    fi

    build_flags $build_dir || return 1
}

fix_buildroot()
{
    rm -rf ~/rpmbuild/BUILDROOT
    mkdir -p ~/rpmbuild
    ln -s $(pwd)/$1/buildroot ~/rpmbuild/BUILDROOT
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

    for module in "galerautils" "gcomm" "gcs" "wsdb" "galera"
    do
        src=$(build_source $module | tail -n 1)
        srcs="$srcs $src"
    done

    local ret="galera-source-r$RELEASE.tar"
    tar --transform 's/.*\///' -cf $ret $srcs \
    "source/README_BUILD" "source/COPYING" "source/build.sh"

    # return absolute path for scripts
    echo $PWD/$ret
}

building="false"

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

echo "CC: $CC"
echo "CPPFLAGS: $CPPFLAGS"

if [ -z "$RELEASE" ]
then
    pushd "$build_base"
    RELEASE="$(svnversion | sed s/\:/,/g)"
    popd
fi

if test "$SKIP_BUILD" == "no"; then

build_module "galerautils"

if test "$DISABLE_GCOMM" != "yes"
then 
    build_module "gcomm"
else
    gcs_conf_flags="$gcs_conf_flags --disable-gcomm"
fi

if test "$DISABLE_VSBES" != "yes"
then 
    if test $initial_stage = "galeracomm" || $building = "true"
    then
        build $galeracomm_src $conf_flags
        building="true"
    fi

    # Galera comm is not particularly easy to handle
    CPPFLAGS="$CPPFLAGS -I$galeracomm_src/vs/include" # non-standard location
    CPPFLAGS="$CPPFLAGS -I$galeracomm_src/common/include" # non-standard location
    LDFLAGS="$LDFLAGS -L$galeracomm_src/common/src/.libs"
    LDFLAGS="$LDFLAGS -L$galeracomm_src/transport/src/.libs"
    LDFLAGS="$LDFLAGS -L$galeracomm_src/vs/src/.libs"
    LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$galeracomm_src/common/src/.libs"
    LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$galeracomm_src/transport/src/.libs"
    LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$galeracomm_src/vs/src/.libs"
else
    gcs_conf_flags="$gcs_conf_flags --disable-vs"
fi

build_module "gcs" $gcs_conf_flags
build_module "wsdb"
build_module "galera"

fi # SKIP_BUILD

if test "$PACKAGE" == "yes"
then
    build_packages
fi

if test "$SOURCE" == "yes"
then
    build_sources
fi

if test $building != "true"
then
    echo "Warn: Nothing was built!"
fi
