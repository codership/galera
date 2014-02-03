#!/bin/sh -eu

BASE_DIR=$(cd $(dirname $0); pwd -P)

REPORT_DIR="$TEST_REPORT_DIR/sqlgen"
if ! test -d $REPORT_DIR
then
    mkdir $REPORT_DIR
    if test $? != 0
    then
        echo "Failed to create report directory"
        exit 1
    fi
fi

SQLGEN_LOG=$REPORT_DIR/sqlgen.log
echo "Running sqlgen test, args: $@" >> $SQLGEN_LOG

. $TEST_BASE_DIR/tap/tap-functions

plan_tests 1


if test $CLUSTER_N_NODES -lt 1
then
    skip "ok" "No nodes available, skipping test"
elif ! test -x $BASE_DIR/sqlgen
then
    skip "ok" "sqlgen binary not found, skipping test"
else
    echo "Starting load" >> $SQLGEN_LOG
    args=""
    for ii in $CLUSTER_NODES
    do
        args="$args --host $ii"
    done
    args="$args --port $MYSQL_PORT $@"
    LD_PRELOAD=$GLB_PRELOAD \
    DYLD_INSERT_LIBRARIES=$GLB_PRELOAD \
    DYLD_FORCE_FLAT_NAMESPACE=1 \
    $BASE_DIR/sqlgen $args >> $SQLGEN_LOG 2>&1
    ok $? "sqlgen"
fi

