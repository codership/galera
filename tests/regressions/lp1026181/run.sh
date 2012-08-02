#!/bin/bash 
##
#
# lp:1026181
# https://bugs.launchpad.net/codership-mysql/+bug/1026181
#
# BUG BACKGRPOUND:
#
# RSU method for DDL has been observed to cause intermittent hanging of
# the client issueing the DDL. Original problem was that thread running 
# under TO isolation could not always abort victims. If there was a MDL 
# conflict with local DML processor it could happen that DDL query just kept
# on waiting for the DML to end, and DML was waiting for commit monitor. 
#
# Test scenarios:
# A. DDL on same node as DML
#
# B. DDL on applier node
#
#
# TEST SETUP:
#   - Cluster will be started
#   - sqlgen is run against one node in the cluster
#   - Connection to run the DDL is opened to one of the nodes
#
# SUCCESS CRITERIA
#
# If bug is present, DDL execution will hang
#
declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

declare sqlgen=$DIST_BASE/bin/sqlgen

echo "##################################################################"
echo "##             regression test for lp1026181:"
echo "##################################################################"
echo "stopping cluster..."
stop
echo
#echo "starting nodes..."
#start

echo "starting node0, node1..."
start_node "-d -g gcomm://$(extra_params 0)" 0
start_node "-d -g $(gcs_address 1) --slave_threads 4" 1

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=$DBMS_HOST test "

declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

$MYSQL --port=$port_0 -e "
          SET GLOBAL wsrep_osu_method=TOI 
" 2>&1
$MYSQL --port=$port_1 -e "
          SET GLOBAL wsrep_osu_method=TOI 
" 2>&1

ROUNDS=50
SUCCESS=0

dml()
{
    local port=$1
    local users=$2

    echo "running sqlgen..."
    $sqlgen --user=root --password=rootpass --host=0 --port=$port --create=1 --tables=4 --rows=20000 --users=$users --duration=600 > /dev/null
}

alter()
{
    local port=$1
    echo "running DDL for $port..."
    for (( i=1; i<=$ROUNDS; i++ )); do
	echo "DDL round: $i"
	$MYSQL --port=$port -e "
          CREATE INDEX keyx ON test.comm00(x) 
        " 2>&1
	$MYSQL --port=$port -e "
          DROP INDEX keyx ON test.comm00 
        " 2>&1
    done
    echo "... DDL over"
}

run_test()
{
    phase=$1
    dml_port=$2
    ddl_port=$3
    users=$4

    echo "##################################################################"
    echo "##             test phase $phase"
    echo "##################################################################"
    echo
    echo "Starting sqlgen..."

    dml $dml_port $users &
    dml_pid=$!

    sleep 3

    $MYSQL --port=$ddl_port -e "
          SET GLOBAL wsrep_osu_method=RSU
        " 2>&1

    alter $ddl_port &
    alter_pid=$!

    echo "Waiting alter load to end (alter: $alter_pid, dml: $dml_pid)"
    wait $alter_pid
    
    echo "stopping dml load ($dml_pid)"
    kill $dml_pid

    echo "Waiting remaining load to end..."
    wait

    $MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
    echo
    $MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'
    [ "$?" != "0" ] && echo "failed!" && exit 1

    #$SCRIPTS/command.sh check
    echo "consistency checking..."
    check0=$(check_node 0)
    check1=$(check_node 1)

    cs0=$(echo $check0 | cut -d" " -f 11)
    cs1=$(echo $check1 | cut -d" " -f 11)

    echo "node 0: $cs0"
    echo "node 1: $cs1"

    if test "$cs0" != "$cs1"
    then
	echo "Consistency check failed"
	echo "$check0 $cs0"
	echo "$check1 $cs1"
	exit 1
    fi
    if test $? != 0
    then
	echo "Consistency check failed"
	exit 1
    fi

    eval $cleanup

    echo
    echo "looks good so far..."
    echo
}
#########################################################
#
# Test begins here
#
#########################################################

run_test A $port_0 $port_1 1
run_test B $port_0 $port_0 1

run_test C $port_0 $port_1 4
run_test D $port_0 $port_0 4


echo
echo "Done!"
echo

stop_node 0
stop_node 1

exit $SUCCESS

