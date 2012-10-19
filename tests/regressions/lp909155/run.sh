#!/bin/bash 

##
#
# lp:909155
# https://bugs.launchpad.net/codership-mysql/+bug/909155
#
# BUG BACKGRPOUND:
#
# under certain situations, DROP TABLE processing may be postponed to happen
# in background thread. This can be fatal for galera processing, as DROP 
# needs to be processed under total order isolation and has strict requirements
# for when the processing begins and ends.
#
# TEST SETUP:
#
# This test starts two nodes to play with. 
# It creates one table with AC primary key: test.t
#
# TEST PROCESSES:
#
# Test creates one db dropper process and two inserter process:
# inserter:  keeps on insertting rows in the test table
#            the two inserters target separate cluster nodes
#            This way the dropper will conflict both with local state 
#            and slave inserts
# dropper:   dropper process runs drop table; create table sequences
#            for the test table. When table is dropped, local inserts 
#            keep on failing until the table is re-created again.
#            There is small sleep after CREATE
#
# SUCCESS CRITERIA
#
# If bug is present, either one node can crash. When DROP processing happens in 
# background, the drop can happen in unpredictable time, and slave applier 
# may fail to apply an insert => this will crash the slave
# It is a good idea to check mysql logs as well. Backgrounded DROP will cause
# the following CREATE TABLE to fail. galera apply processor tolerates failures
# for DDL, and this type of error does not cause crash. However, there is 
# slave error in the log. This latter symptom is much more probable than
# actual crash.
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
echo "##             regression test for lp:909155"
echo "##################################################################"

echo "stopping cluster"
../../scripts/command.sh stop
echo
echo "starting node0, node1..."
../../scripts/command.sh start_node "-d -g gcomm://$(extra_params 0)" 0
../../scripts/command.sh start_node "-d -g $(gcs_address 1)" 1

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=$DBMS_HOST test "

#declare -r port_0=$(( DBMS_PORT ))
#declare -r port_1=$(( DBMS_PORT + 1))
declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

inserter()
{
    local port=$1
    for i in {1..10000}; do
	$MYSQL --port=$port -e "INSERT INTO test.t(j) VALUES($i)" 2>&1 | grep -v "ERROR 1146";
    done
}

createdb()
{ 
    $MYSQL --port=$port_0 -e 'DROP TABLE IF EXISTS test.t; CREATE TABLE test.t(i INT PRIMARY KEY AUTO_INCREMENT, j int)'
}

dropper()
{
    for i in {1..200}; do
	createdb;
        sleep 0.2;
    done
}

echo "Creating database..."
createdb

echo
echo "### Phase 1, DML & DDL => node0 ###"
echo

echo "starting inserter for port $port_0"
inserter $port_0 &
declare inserter1_pid=$!

echo "starting dropper"
dropper &
declare dropper_pid=$!

echo "waiting phase 1 load to end ($dropper_pid $inserter1_pid)"
wait
$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
echo
$MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'

echo
echo "### Phase 2, DML => node1  DDL => node0 ###"
echo

echo "starting inserter for port $port_1"
inserter $port_1 &
declare inserter2_pid=$!

echo "starting dropper"
dropper &
dropper_pid=$!

echo "waiting phase 2 load to end ($dropper_pid $inserter2_pid)"
wait
$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
echo
$MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'

echo
echo "### Phase 3, DML => node0, node1  DDL => node0 ###"
echo

echo "starting inserter for port $port_0"
inserter $port_0 &
inserter1_pid=$!

echo "starting inserter for port $port_1"
inserter $port_1 &
inserter2_pid=$!

echo "starting dropper"
dropper &
dropper_pid=$!

echo "waiting phase 3 load to end ($dropper_pid $inserter1_pid $inserter2_pid)"
wait

$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
echo
$MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'


echo
echo "Done!"
echo

../../scripts/command.sh stop_node 0
../../scripts/command.sh stop_node 1

exit

