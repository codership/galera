#!/bin/bash -e
#
# lp:1073220
# https://bugs.launchpad.net/codership-mysql/+bug/1073220
#
# TEST SETUP
#
# This test starts two nodes and runs mixed DML/DDL load described in
#
#   https://bugs.launchpad.net/codership-mysql/+bug/861212/comments/2
#
# for some time.
#
# PARAMETERS
#
# Number of test rounds
ROUNDS=${ROUNDS:-"100"}
# Duration of single iteration
DURATION=${DURATION:-"3"}
#
#


declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

echo "##################################################################"
echo "##             regression test for lp:1073220"
echo "##################################################################"

echo "restarting cluster to clean state"
restart


echo "starting load for $DURATION" seconds
SQLGEN=${SQLGEN:-"$DIST_BASE/bin/sqlgen"}

round=0
while test $round -lt $ROUNDS
do
    LD_PRELOAD=$GLB_PRELOAD \
    DYLD_INSERT_LIBRARIES=$GLB_PRELOAD \
    DYLD_FORCE_FLAT_NAMESPACE=1 \
    $SQLGEN --user $DBMS_TEST_USER --pswd $DBMS_TEST_PSWD --host $DBMS_HOST \
            --port $DBMS_PORT --users $DBMS_CLIENTS --duration $DURATION \
            --stat-interval 30 --rows 1000 --ac-frac 10 --rollbacks 0.1 \
            --alters 1

    wait_sync $NODE_LIST
    echo "checking consistency"
    check || (sleep 5 && check)
    round=$(($round + 1))
done

echo "stopping cluster"
stop
