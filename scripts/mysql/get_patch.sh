#!/bin/bash

# This script produces WSREP patch against given official mysql version

if test -z "$MYSQL_SRC"
then
    echo "MYSQL_SRC variable pointing at MySQL/wsrep sources. Can't continue."
    exit -1
fi

usage()
{
    echo -e "Usage: $0 <mysql version or bzr tag/revision>" 
}

set -x
set -e

# Source paths are either absolute or relative to script, get absolute
THIS_DIR=$(pwd -P)
MYSQL_SRC=$(cd $MYSQL_SRC; pwd -P; cd $BUILD_ROOT)

pushd $MYSQL_SRC

WSREP_REV=$(bzr revno)
MYSQL_LP_REV=$(bzr tags | grep -m1 "$1" | awk '{ print $2 }')
WSREP_PATCH="$THIS_DIR/$MYSQL_LP_REV-$WSREP_REV.diff"
bzr diff -r$MYSQL_LP_REV -p1 --diff-options " --exclude=.bzrignore " > \
    "$WSREP_PATCH"
echo $WSREP_PATCH
