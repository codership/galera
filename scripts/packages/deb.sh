#!/bin/bash -eu
#
# Script to build Galera library debian package
#
# Source of information:
# - https://wiki.debian.org/IntroDebianPackaging
# - https://wiki.debian.org/HowToPackageForDebian
#

SCRIPT_ROOT=$(cd $(dirname $0); pwd -P)
JOBS=${JOBS:-3}

function build_deb
{
    # Create upstream tar.gz package
    local version="$1"
    local debian_version="$(lsb_release -sc)"

    galera_dir="galera-$version"
    source_tar="galera-$version.tar.gz"
    orig_tar="galera_$version.orig.tar.gz"

    pushd "$SCRIPT_ROOT"

    test -d "$galera_dir" && rm -rf "$galera_dir"
    test -f "$source_tar" && rm -r "$source_tar"
    test -f "$orig_tar" && rm -r "$orig_tar"

    # Create source tarball
    (cd ../../ && git checkout-index -a -f --prefix="$SCRIPT_ROOT/$galera_dir/")
    tar zcf "$galera_dir.tar.gz" "$galera_dir"

    # Copy to orig.tar.gz
    cp "$source_tar" "$orig_tar"

    tar zxf "$orig_tar"
    cp -r debian "$galera_dir"
    cd "$galera_dir"

    dch -m -D "$debian_version" --force-distribution -v "$version-$debian_version" "Version upgrade"
    DEB_BUILD_OPTIONS="parallel=$JOBS" dpkg-buildpackage -us -uc

    test -d "$galera_dir" && rm -rf "$galera_dir"
    test -f "$source_tar" && rm -r "$source_tar"
    test -f "$orig_tar" && rm -r "$orig_tar"

    popd
    cp "$SCRIPT_ROOT"/galera_"$version"*.deb ./
    cp "$SCRIPT_ROOT"/galera-dbg_"$version"*.deb ./

}

function usage
{
    cat - <<EOF
Usage:
build-deb.sh <version>
EOF
}

function main
{
    set -x
    test $# -eq 1 || (usage && exit 1)

    local version="$1"
    build_deb "$version"
}

main $@
