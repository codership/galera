#!/bin/bash -eu
#
# https://bugs.launchpad.net/galera/+bug/1284803
#
# BUG BACKGROUND:
# ==============
#
# Big writesets in IST stream caused replicator to launch background
# threads for write set checksumming. One responsibility of this thread
# is also to assign certain write set data structures. In IST codepath
# there was no synchronization point for this thread before write set
# was applied so sometimes uninitialized write sets caused skip in
# applying and the assertion mentioned in bug report.
#
test -n "${DEBUG:-}" && set -x

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

echo "##################################################################"
echo "##             regression test for lp:1284803"
echo "##################################################################"


MYSQL="mysql --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=${NODE_INCOMING_HOST[0]} --port=${NODE_INCOMING_PORT[0]} -Dtest"

MYSQL_SLAVE="mysql --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=${NODE_INCOMING_HOST[1]} --port=${NODE_INCOMING_PORT[1]} -Dtest"


function ins
{
    while true
    do
        for ii in {1..100}
        do
     # Insert long string of chars to generate big write set
            $MYSQL -e "INSERT INTO lp1284803 VALUES ($ii, REPEAT('a', 1115693))" || :
        done
        $MYSQL -e "TRUNCATE TABLE lp1284803"
    done
}

# Restart nodes to bring nodes into sync
echo
echo "restarting cluster"
echo

restart
stop_node 2

node_list="0 1"

echo
echo "initializing table"
echo

# Create test table to store long string of chars
$MYSQL -e "DROP TABLE IF EXISTS  lp1284803;
           CREATE TABLE lp1284803 (a INT PRIMARY KEY, b MEDIUMTEXT)"


ins &
inspid=$!

trap "{ kill $inspid || : ; }" EXIT

for ii in {1..10}
do
    echo "-- Stopping node"

    # Stop node 1
    stop_node 1

    SLEEPTIME=1

    echo "-- Pausing for $SLEEPTIME secs"
    sleep $SLEEPTIME

    # Pause load for IST
    kill -STOP $inspid

    echo "-- Starting node"

    # Start node 1 which will attempt IST
    start_node "-g $(gcs_address 1)" 1

    echo "-- Waiting sync"
    # Stop load and wait nodes to sync

    wait_sync $node_list
    kill -CONT $inspid

done

kill $inspid || :


$MYSQL -e "DROP TABLE IF EXISTS  lp1284803"

# Success
exit 0