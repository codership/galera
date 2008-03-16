#!/bin/bash

# Fail if any command fails
set -e



initial_stage="scratch"
gainroot=""

usage()
{
    echo -e "Usage: build.sh [OPTIONS] \n" \
	"Options: \n"                  \
        "    --stage <initial stage> \n"

}

while test $# -gt 0 
do
    case $1 in 
	--stage)
	    initial_stage=$2
	    shift
	    ;;
	--gainroot)
	    gainroot=$2
	    shift
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
build_base=`pwd`

# Define branches to be used
galerautils_branch=$build_base/galerautils/trunk
galeracomm_branch=$build_base/galeracomm/trunk
gcs_branch=$build_base/gcs/trunk
wsdb_branch=$build_base/wsdb/trunk
mysql_branch=$build_base/../5.1/trunk

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


# Cd to build directory and fetch sources
cd $build_base/build

# Function to build single project
build()
{
    echo "Building: $1 ($@)"
    cd $1
    shift
    ./bootstrap.sh
    ./configure $@
    make 
    $gainroot make install
    cd ..
}

building="false"
# Build projects

if test $initial_stage = "scratch"
then
    rm -rf
    building="true"
fi

if test $initial_stage = "galerautils" || $building = "true"
then
    rm -rf galerautils
    svn export --force $galerautils_branch galerautils
    build galerautils $conf_flags
    building="true"
fi

if test $initial_stage = "galeracomm" || $building = "true"
then
    rm -rf galeracomm
    svn export --force $galeracomm_branch galeracomm
    build galeracomm $conf_flags $galera_flags
    building="true"
fi

if test $initial_stage = "gcs" || $building = "true"
then
    rm -rf gcs
    svn export --force $gcs_branch gcs
    build gcs $conf_flags $galera_flags
    building="true"
fi

if test $initial_stage = "wsdb" || $building = "true"
then
    rm -rf wsdb
    svn export --force $wsdb_branch wsdb
    build wsdb $conf_flags $galera_flags
    building="true"
fi

if test $initial_stage = "mysql" || $building = "true"
then
    rm -rf mysql
    svn export --force $mysql_branch mysql
    cd mysql
    ./BUILD/compile-pentium-galera
    $gainroot make install
    cd ..
    building="true"
fi

if test $building != "true"
then
    echo "Warn: Nothing was built!"
fi
