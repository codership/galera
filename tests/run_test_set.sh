#!/bin/sh
#
# This is a wrapper script for ./tap/run_test_set.pl
# 


BASE_DIR=$(cd $(dirname $0); pwd -P)
cd $BASE_DIR
res=$?
if test $res != 0
then
    echo "Failed to change directory to $BASE_DIR"
    exit 1
fi

export TEST_BASE_DIR=$BASE_DIR

. $BASE_DIR/conf/cluster.conf

$BASE_DIR/tap/run_test_set.pl $@

res=$?

if test $res != 0
then
    echo "Failed to run test set, exit code: $res"
    exit 1
fi

exit 0

