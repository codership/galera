#!/bin/bash 
##
#
# lp:lp1055961
# https://bugs.launchpad.net/codership-mysql/+bug/lp1055961
#
# BUG BACKGRPOUND:
#
# autocommit query retrying was effective only for statements which were
# really BF aborted. Autocommit statements, which tried to commit but
# failed in certification, were never retried, they were immune to
# wsrep_retry_autocommit setting.
#
#
# TEST SETUP:
#   - two nodes to be started, wsrep_retry_autocommit set to 100
#   - sqlgen is run against one node in the cluster
#   - sending autocommit updates on one sqlgen table to the other node
#     and checking that deadlock error should never come as result
#
# SUCCESS CRITERIA
#
# If bug is present, deadlock error will be observed
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
echo "##             regression test for lp1055961:"
echo "##################################################################"
echo "stopping cluster..."
stop
echo
#echo "starting nodes..."
#start

echo "starting node0, node1..."
start_node "-d -g gcomm://$(extra_params 0)" 0
start_node "-d -g $(gcs_address 1)" 1

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=$DBMS_HOST test "

declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

$MYSQL --port=$port_0 -e "
          SET GLOBAL wsrep_retry_autocommit=100 
" 2>&1

ROUNDS=10
SUCCESS=0

sqlgen_load()
{
    local port=$1

    echo "running sqlgen..."
    $sqlgen --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD \
        --host=${NODE_INCOMING_HOST[0]} --port=$port \
        --create=1 --tables=1 --rows=20 --users=1 --selects=0 --inserts=0 \
        --updates=100 --ac-frac=100 --duration=600 > /dev/null
}

deadlock_test()
{
    local port=$1
    echo "running AC updates for $port..."
    for (( i=1; i<=$ROUNDS; i++ )); do
    $MYSQL --port=$port -e "
          UPDATE test.comm00 SET z=99 WHERE p=0; 
        " 2>&1
        ret=$?
        if [ "$ret" != "0" ]; then
            echo "DEADLOCK ERROR";
            SUCCESS=1;
        fi
    done
    echo "... Deadlock test over"
}

non_effective_update()
{
    local port=$1
    echo "running AC updates for $port..."
    for (( i=1; i<=$ROUNDS; i++ )); do
        $MYSQL --port=$port -e "
          UPDATE test.comm00 SET z=z WHERE p=0; 
        " 2>&1
        ret=$?
        if [ "$ret" != "0" ]; then
            echo "DEADLOCK ERROR FOR NON-EFFECTIVE UPDATE";
            exit_code=1;
        fi
    done
    echo "... Non effective update test over"
}

run_test()
{
    echo "##################################################################"
    echo "##             test phase deadlock"
    echo "##################################################################"
    echo
    echo "Starting sqlgen..."

    sqlgen_load $port_1 &

    echo "sqlgen $sqlgen_pid"
    sleep 3

    deadlock_test $port_0

    echo "##################################################################"
    echo "##             test phase non effective updates"
    echo "##################################################################"
    echo
    non_effective_update $port_0

    echo
    echo "stopping sqlgen load ($sqlgen_pid)"
    kill $(pidof sqlgen)

    echo "Waiting remaining load to end..."
    wait $(pidof sqlgen)

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

run_test 

echo
echo "Done!"
echo

stop_node 0
stop_node 1

exit $SUCCESS

