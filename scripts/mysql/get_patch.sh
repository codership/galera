#!/bin/bash -x

# This script produces WSREP patch against given official mysql version

if test -z "$MYSQL_SRC"
then
    echo "No MYSQL_SRC variable pointing at MySQL/wsrep sources. Can't continue."
    exit -1
fi

usage()
{
    echo -e "Usage: $0 <mysql version tag (e.g. mysql-5.1.42)" 
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
WSREP_PATCH_SPEC=$1-$WSREP_REV

# Check existing file
# This is done to not to depend on LP operation, however it looks like
# any changes uncommitted locally might go unnoticed as revno stays the same
#WSREP_PATCH_FILE=$(ls $THIS_DIR/${WSREP_PATCH_SPEC}_*_.diff 2>/dev/null || : )
#if [ -r "$WSREP_PATCH_FILE" ]
#then
#    WSREP_PATCH_MD5SAVE=$(basename $WSREP_PATCH_FILE | awk -F _ '{ print $2 }' )
#    WSREP_PATCH_MD5TEST=$(md5sum $WSREP_PATCH_FILE | awk '{ print $1 }')
#    if [ $WSREP_PATCH_MD5SAVE == $WSREP_PATCH_MD5TEST ]
#    then
#        echo $WSREP_PATCH_FILE
#        exit 0
#    fi
#fi

# Existing file either not found or corrupted, try to create a new one

rm -f $WSREP_PATCH_FILE

MYSQL_BRANCH="lp:mysql-server/5.1"
#MYSQL_LP_REV=$( bzr tags -d $MYSQL_BRANCH | grep -m1 "$1" | awk '{ print $2 }' )
#if [ -z "$MYSQL_LP_REV" ]
#then
#    echo "No such tag/revision: $1"
#    exit -1
#fi

WSREP_PATCH_TMP="$THIS_DIR/$WSREP_PATCH_SPEC.diff"
# normally we expect bzr diff return 1 (changes available)
bzr diff -p1 -v --diff-options " --exclude=.bzrignore " \
    -r tag:$1..branch:$MYSQL_SRC \
    > "$WSREP_PATCH_TMP" || if [ $? -gt 1 ]; then exit -1; fi
WSREP_PATCH_MD5SUM=$(md5sum $WSREP_PATCH_TMP | awk '{ print $1 }')
WSREP_PATCH_FILE=$THIS_DIR/${WSREP_PATCH_SPEC}_${WSREP_PATCH_MD5SUM}_.diff
mv $WSREP_PATCH_TMP $WSREP_PATCH_FILE

echo $WSREP_PATCH_FILE
