#!/bin/bash
#
# lp:970714
# https://bugs.launchpad.net/codership-mysql/+bug/970714
#
# BACKGROUND:
# For locking session support we can say that:
# 1. they cannot be supported in multi-master case
# 2. they muist be supported in master-slave case (for tansparency)
#
# Detecting MS and MM use cases is difficult from within a node, so we 
# must detect locking sessions conflicts between appliers and
# local locking sessions and abort the locking sessions.
# 
# TEST
#
# This test starts two nodes and runs two client sessions against each node:
# applier: this load is simple autoinc insert load which will cause conflicts
#          in the other node's applying
# loader: this load runs a locking session with a big number of inserts within
#       
# The idea is that in the second node, loader will run a locking session and 
# applier load from the first node will conflict with the locking session.
# mysqld should be able to abort loader transaction and  break the locking 
# session
#
# TEST OUTCOME
#
# If bug is present, second node will either crash or hang due to unresolved
# conflicts, applier will be blocked
#
# PARAMETERS
#
# How many rounds to run load
ROUNDS=${ROUNDS:-100}
#


declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

applier()
{
    for (( i=1; i<$ROUNDS; i++ )); do
	mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD                           \
            --host ${NODE_INCOMING_HOST[0]} --port ${NODE_INCOMING_PORT[0]} \
            -e "INSERT INTO test.lp(who) VALUES ('applier')"
	[ $? != 0 ] && echo "applier  aborted: $? at $cnt"
    done
}

loader()
{
    for (( i=1; i<$ROUNDS; i++ )); do
	mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD                           \
            --host ${NODE_INCOMING_HOST[1]} --port ${NODE_INCOMING_PORT[1]} \
            < ./lp.sql
	[ $? != 0 ] && echo "load aborted: $? at $cnt"
    done
}

run_test()
{
    echo "starting loader"
    loader&
    loader_pid=$!

    echo "starting applier"
    applier&
    applier_pid=$!

    echo "waiting for loader ($loader_pid) and applier ($applier_pid)"
    wait

    trap "kill $applier_pid" EXIT
    trap "kill $loader_pid" EXIT


    echo "checking consistency"
    check
}


echo "##################################################################"
echo "##             regression test for lp:970714"
echo "##################################################################"

echo "starting two nodes"
stop
start_node "-d -g gcomm://$(extra_params 0)" 0
start_node "-d -g $(gcs_address 1)" 1

echo "creating database"
mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD                           \
    --host ${NODE_INCOMING_HOST[0]} --port ${NODE_INCOMING_PORT[0]} \
    -e "DROP TABLE IF EXISTS test.lp"
mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD                           \
    --host ${NODE_INCOMING_HOST[0]} --port ${NODE_INCOMING_PORT[0]} \
    -e "CREATE TABLE test.lp(pk INT PRIMARY KEY AUTO_INCREMENT, who VARCHAR(10))"
[ $? != "0" ] && echo "table create failed" && exit 1

echo "##"
echo "##             wsrep_convert_LOCK_to_trx=1"
echo "##"
echo "setting wsrep_convert_LOCK_to_trx=1"
mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD                           \
    --host ${NODE_INCOMING_HOST[0]} --port ${NODE_INCOMING_PORT[0]} \
    -e "SET GLOBAL wsrep_convert_LOCK_to_trx=1"
[ $? != "0" ] && echo "SET failed" && exit 1

mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD                           \
    --host ${NODE_INCOMING_HOST[1]} --port ${NODE_INCOMING_PORT[1]} \
    -e "SET GLOBAL wsrep_convert_LOCK_to_trx=1"

[ $? != "0" ] && echo "SET failed" && exit 1


run_test

echo "##"
echo "##             wsrep_convert_LOCK_to_trx=0"
echo "##"
echo "setting wsrep_convert_LOCK_to_trx=0"
mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD                           \
    --host ${NODE_INCOMING_HOST[0]} --port ${NODE_INCOMING_PORT[0]} \
    -e "SET GLOBAL wsrep_convert_LOCK_to_trx=0"
[ $? != "0" ] && echo "SET failed" && exit 1

mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD                           \
    --host ${NODE_INCOMING_HOST[1]} --port ${NODE_INCOMING_PORT[1]} \
    -e "SET GLOBAL wsrep_convert_LOCK_to_trx=0"

[ $? != "0" ] && echo "SET failed" && exit 1


run_test


echo "stopping cluster"
stop
