#!/bin/bash -e
##
#
# lp:1003929
# https://bugs.launchpad.net/codership-mysql/+bug/1003929
#
# BUG BACKGRPOUND:
#
# Table which ahs no primary key but has one or more unique keys
# defined may cause a crash in slave configured for parallel applying.
# This is due to the probölem that for such tables we generate only
# full row hash as key value - the individual unique keys are not appended
# in write set.
#
# If bug is present, slave appliers can easily conflict and cause crash.
#
# TEST SETUP:
#   - Two nodes are used in master slave mode. 
#   - Slave is configured with 4 applier threads
#   - a table with no PK but one UK is created
#   - test load runs inserts/delete load for the table so that 
#     inserted rows are different but unoque key values match
#
# SUCCESS CRITERIA
#
# If bug is present, slave will crash for DUPKEY error, PA control
# allow two subsequent inserts to happen in parallel, althoug they 
# try to insert same unique key
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
echo "##             regression test for lp:1003929"
echo "##################################################################"
echo "stopping cluster"
$SCRIPTS/command.sh stop
echo
echo "starting node0, node1..."
../../scripts/command.sh start_node "-d -g gcomm://$(extra_params 0)" 0
../../scripts/command.sh start_node "-d -g $(gcs_address 1) --slave_threads 4" 1

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD  -Dtest "

declare -r port_0=$(( DBMS_PORT ))
declare -r port_1=$(( DBMS_PORT + 1))

declare -r node_0="-h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]}"
declare -r node_1="-h${NODE_INCOMING_HOST[1]} -P${NODE_INCOMING_PORT[1]}"

inserter()
{
    local node="$@"
    for i in {1..10000}; do
	$MYSQL $node -e "
          DELETE FROM test.lp1003929; 
          INSERT INTO test.lp1003929 VALUES('a',1,1);
          DELETE FROM test.lp1003929; 
          INSERT INTO test.lp1003929 VALUES('a',1,2);
        " 2>&1
    done
}

createdb()
{ 
    $MYSQL $node_0 -e "
        DROP TABLE IF EXISTS test.lp1003929;"

    $MYSQL $node_0 -e '
        CREATE TABLE test.lp1003929
        (
           a varchar(20),
           i INT,
           j INT,
           UNIQUE KEY(a,i)
        )'
}

#########################################################
#
# Test begins here
#
#########################################################

threads=$($MYSQL $node_1 -e "SHOW VARIABLES LIKE 'wsrep_slave_threads'")

echo "applier check: $threads"
[ "$threads" = "wsrep_slave_threads	4" ] && echo "enough slaves"

echo "Creating database..."
createdb

echo "Starting inserter..."
inserter $node_0 &
declare inserter_pid=$!

echo "Waiting load to end ($inserter_pid)"
wait
$MYSQL $node_0 -e 'SHOW PROCESSLIST'
echo
$MYSQL $node_1 -e 'SHOW PROCESSLIST'
[ "$?" != "0" ] && echo "failed!" && exit 1

$SCRIPTS/command.sh check

if test $? != 0
then
    echo "Consistency check failed"
    exit 1
fi

echo
echo "Done!"
echo

../../scripts/command.sh stop_node 0
../../scripts/command.sh stop_node 1

exit 0

