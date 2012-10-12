#!/bin/bash -ue
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

echo "starting node0, node1..."
start_node "-d -g gcomm://$(extra_params 0)" 0
start_node "-d -g $(gcs_address 1) --slave_threads 4" 1

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD test "

declare -r host_0=${NODE_INCOMING_HOST[0]}
declare -r host_1=${NODE_INCOMING_HOST[1]}
declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

MYSQL_0="$MYSQL --host=$host_0 --port=$port_0 -e"
MYSQL_1="$MYSQL --host=$host_1 --port=$port_1 -e"

$MYSQL_0 "SET GLOBAL wsrep_osu_method=TOI" 2>&1
$MYSQL_1 "SET GLOBAL wsrep_osu_method=TOI" 2>&1

echo -n "Populating test database... "
$sqlgen --user=root --password=rootpass --host=$host_0 --port=$port_0 \
        --create=1 --tables=4 --rows=20000 --users=1 --duration=0 > /dev/null
echo "done"

ROUNDS=50
SUCCESS=0

dml()
{
    local host=$1
    local port=$2
    local users=$3

    $sqlgen --user=root --password=rootpass --host=$host --port=$port \
            --create=0 --users=$users --duration=600 > /dev/null 2>&1 &
    echo $!
}

alter()
{
    local host=$1
    local port=$2
    local mysql_="$MYSQL --host=$host --port=$port -e"
    echo "running DDL for $host:$port..."
    for (( i=1; i<=$ROUNDS; i++ )); do
        echo "DDL round: $i"
        $mysql_ 'CREATE INDEX keyx ON test.comm00(x)' 2>&1 || :
        sleep 0.1 # this reduces the number of desync collisions
        $mysql_ 'DROP INDEX keyx ON test.comm00' 2>&1 || :
        sleep 0.1
    done
    echo "... DDL over"
}

consistency_check()
{
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

    echo
    echo "looks good so far..."
    echo
}

run_test()
{
    local phase=$1
    local dml_host=${NODE_INCOMING_HOST[$2]}
    local ddl_host=${NODE_INCOMING_HOST[$3]}
    local dml_port=${NODE_INCOMING_PORT[$2]}
    local ddl_port=${NODE_INCOMING_PORT[$3]}
    local users=$4

    echo "##################################################################"
    echo "##             test phase $phase"
    echo "##################################################################"
    echo
    echo "Starting sqlgen..."
    dml_pid=$(dml $dml_host $dml_port $users)

    sleep 3

    $MYSQL --host=$ddl_host --port=$ddl_port -e "
          SET GLOBAL wsrep_osu_method=RSU
        " 2>&1

    alter $ddl_host $ddl_port &
    alter_pid=$!

    echo "Waiting alter load to end (alter: $alter_pid, dml: $dml_pid)"
    wait $alter_pid

    echo "stopping dml load ($dml_pid)"
    kill $dml_pid

    echo "Waiting remaining load to end..."
    wait

    $MYSQL_0 'SHOW PROCESSLIST' && \
    echo && \
    $MYSQL_1 'SHOW PROCESSLIST'

    [ "$?" != "0" ] && echo "failed!" && exit 1 || :

#    eval $cleanup # what is this supposed to mean?
}

#########################################################
#
# Test begins here
#
#########################################################

run_test A 0 1 1
run_test B 0 0 1

consistency_check

run_test C 0 1 4
run_test D 0 0 4

consistency_check

echo
echo "Done!"
echo

# Automatic cleanup at the end of the test - bad parctice: who knows what
# you may want to check afterwards?
#stop_node 0
#stop_node 1

exit $SUCCESS

