#!/bin/bash

# lp:847353
# https://bugs.launchpad.net/codership-mysql/+bug/847353
#
# BUG BACKGRPOUND:
#
# FTWRL needs to pause replicator from committing. FTWRL will take global read
# lock and this can cause a deadlock between applying trasnactions and FTWRL
# execution. When FTWRL has called wsrep->pause(), all earlier applying tasks 
# must be able to complete.
#
# TEST SETUP:
#
# This test starts two nodes to play with. 
# It uses sqlgen to run load against both nodes and one process running
# FTWRL agains one node only.
#
# TEST PROCESSES:
#
# Test creates two sqlgen processes
# sqlgen:    regular sqlgen load. First sqlgen process will create
#            the tables, latyter one just sends DML load in.
# FTWRL:     FTWRL keeps on sending FLUSTE TABLES WITH READ LOCK
#            statements agains other cluster node
#
# SUCCESS CRITERIA
#
# If bug is present, either one node may hang
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
echo "##             regression test for lp:847353"
echo "##################################################################"
echo "stopping cluster"
../../scripts/command.sh stop
echo
echo "starting node0, node1..."
../../scripts/command.sh start_node "-d -g gcomm://$(extra_params 0)" 0
../../scripts/command.sh start_node "-d -g $(gcs_address 1)" 1

# Start load
SQLGEN=${SQLGEN:-"$DIST_BASE/bin/sqlgen"}

LD_PRELOAD=$GLB_PRELOAD \
DYLD_INSERT_LIBRARIES=$GLB_PRELOAD \
DYLD_FORCE_FLAT_NAMESPACE=1 \
$SQLGEN --user $DBMS_TEST_USER --pswd $DBMS_TEST_PSWD --host ${NODE_INCOMING_HOST[0]} \
        --port ${NODE_INCOMING_PORT[0]} --users 1 --duration 300 \
        --stat-interval 99999999 --sess-min 999999 --sess-max 999999 \
        --rollbacks 0.1 \
        >/dev/null 2>$BASE_RUN/lp.err &
declare -r sqlgen1_pid=$!

sleep 0.2

# this connects strictly to one node so no libglb preloading
$SQLGEN --user $DBMS_TEST_USER --pswd $DBMS_TEST_PSWD --host ${NODE_INCOMING_HOST[1]} \
        --port ${NODE_INCOMING_PORT[1]} --users 1 --duration 300 \
        --stat-interval 99999999 --sess-min 999999 --sess-max 999999 \
        --rollbacks 0.1 --create 0 \
        >/dev/null 2>$BASE_RUN/lp.err &
declare -r sqlgen2_pid=$!

for i in {1..150}; do
  mysql --show-warnings --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=${NODE_INCOMING_HOST[1]} --port=${NODE_INCOMING_PORT[1]} test -e 'FLUSH TABLES WITH READ LOCK';

  sleep 0.5;
done

echo "waiting sqlgens ($sqlgen1_pid $sqlgen2_pid) to complete"
wait

echo "Done!"
../../scripts/command.sh stop_node 0
../../scripts/command.sh stop_node 1

exit

