#!/bin/bash
#
# lp:930221
# https://bugs.launchpad.net/codership-mysql/+bug/930221
#
# TEST
#
# This test starts two nodes, runs sqlgen against the first and
# FTWRL against the second. Finally consistency is checked.
#
# TEST OUTCOME
#
# If bug is present, test will hang.
#
# PARAMETERS
#
# How many rounds to run FTWRL
ROUNDS=${ROUNDS:-10000}
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
echo "##             regression test for lp:930221"
echo "##################################################################"

echo "starting two nodes"
stop
start_node "-d -g gcomm://$(extra_params 0)" 0
start_node "-d -g $(gcs_address 1)" 1

echo "starting sqlgen load for $DURATION" seconds
SQLGEN=${SQLGEN:-"$DIST_BASE/bin/sqlgen"}

$SQLGEN --user $DBMS_TEST_USER --pswd $DBMS_TEST_PSWD      \
    --host ${NODE_INCOMING_HOST[0]}                        \
    --port ${NODE_INCOMING_PORT[0]}                        \
    --users $DBMS_CLIENTS --duration 99999999              \
    --stat-interval 30 --sess-min 999999 --sess-max 999999 \
    --rows 1000 --ac-frac 100 &
sqlgen_pid=$!

trap "kill $sqlgen_pid" EXIT

echo "waiting 10 seconds for sqlgen to start)"
sleep 10

echo "starting FTWRL load"

cnt=$ROUNDS
while test $cnt != 0
do
    mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD                           \
        --host ${NODE_INCOMING_HOST[1]} --port ${NODE_INCOMING_PORT[1]} \
        -e "FLUSH TABLES WITH READ LOCK"
    cnt=$(($cnt - 1))
done

kill $sqlgen_pid || true

echo "checking consistency"
check

echo "stopping cluster"
stop
