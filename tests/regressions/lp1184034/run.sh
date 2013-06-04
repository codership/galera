#!/bin/bash -ue
#
# https://bugs.launchpad.net/galera/+bug/1184034
#
# BUG BACKGROUND
#
# Running
#
#  nmap -sT -p <port> <ip>
#
# causes uncaught exception in gcomm asio code
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
echo "##             regression test for lp:1184034"
echo "##################################################################"
echo "Restarting cluster"
../../scripts/command.sh restart


for ii in `seq 1 100`
do
    nmap -sT -p ${NODE_GCS_PORT[0]} ${NODE_GCS_HOST[0]}
done

check_node 0
