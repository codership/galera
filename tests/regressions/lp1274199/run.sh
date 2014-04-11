#!/bin/bash -eu
#
# This sript starts cluster, disables flow control on node 1, locks node1 for
# a certain duration while loading node 0, and then waits for node1 to catch up
#
# Triggering the bug requires sufficinetly long SLEEP_DURATION or suffcinetly
# small cache settings in order to trigger ring buffer overflow.
#
# For 10 duration gcache.size=1M;gcache.page_size=1M would wor best
#
declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}
. $TEST_BASE/conf/main.conf

declare -r NODE0="--host ${NODE_INCOMING_HOST[0]} --port ${NODE_INCOMING_PORT[0]}"
declare -r NODE1="--host ${NODE_INCOMING_HOST[1]} --port ${NODE_INCOMING_PORT[1]}"
declare -r MYSQL1="mysql -uroot -prootpass $NODE1"

declare -r SCRIPTS="$DIST_BASE/scripts"

SLEEP_DURATION=10

$SCRIPTS/command.sh restart

$MYSQL1 -e "set global wsrep_desync=1"

for ii in `seq 1 4`
do
    $MYSQL1 -e "FLUSH TABLES WITH READ LOCK; SELECT SLEEP($SLEEP_DURATION)" &

    $TEST_BASE/bin/sqlgen $NODE0 --users 16 --duration $SLEEP_DURATION

    echo "`date` waiting sync"
    $SCRIPTS/command.sh wait_sync $NODE_LIST
    echo "`date` round $ii finished"
done

$MYSQL1 -e "set global wsrep_desync=0"
