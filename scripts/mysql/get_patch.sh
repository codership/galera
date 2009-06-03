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

if [ $# -lt 1 ]
then
    usage
    exit -1
fi

#set -x
set -e

# Source paths are either absolute or relative to script, get absolute
THIS_DIR=$(pwd -P)

cd $MYSQL_SRC
WSREP_REV=$(bzr revno)
MYSQL_LP_REV=$(bzr tags | grep -m1 "$1" | awk '{ print $2 }')
WSREP_PATCH="$THIS_DIR/$MYSQL_LP_REV-$WSREP_REV.diff"
# normally we expect bzr diff return 1 (changes available)
bzr diff -p1 -v --diff-options " --exclude=.bzrignore " -r$MYSQL_LP_REV > \
    "$WSREP_PATCH" || if [ $? -gt 1 ]; then exit -1; fi

echo $WSREP_PATCH
