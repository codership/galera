#!/bin/bash
##
#
# lp:1208493
# https://bugs.launchpad.net/codership-mysql/+bug/1208493
#
# BUG BACKGRPOUND:
#
# When wsrep_provider or wsrep_cluster_address is changed, wsrep will
# close all client connections to mysqld. 
# During the client connection closing phase, wsrep used to hold a
# lock on global variable access. Now, some of the closing connections
# may be accessing global variables, and refuse to die if cannot grab the 
# variable protecting lock. 
# The fix for this issue, will release global variable lock for the duration
# of conneciton closing, and all client connections should be able to close
# gracefully
#
# If bug is present, mysqld will hang after wsrep_provider or 
# wsrep_cluster_address change
#
# TEST SETUP:
#   - Two nodes are started
#   - Client test load is started just to constantly change a global variable
#   - One node is dropped and joined by changing wsrep_provider
#     and wsrep_cluster_address
#
# SUCCESS CRITERIA
#
# If bug is present, the test will hang
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
echo "##             regression test for lp:1208493"
echo "##################################################################"
echo "stopping cluster"
$SCRIPTS/command.sh stop
echo
echo "starting node0, node1..."
../../scripts/command.sh start_node "-d -g gcomm://$(extra_params 0)" 0
../../scripts/command.sh start_node "-d -g $(gcs_address 1) --slave_threads 4" 1


declare -r port_0=$(( DBMS_PORT ))
declare -r port_1=$(( DBMS_PORT + 1))

declare -r node_0="-h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]}"
declare -r node_1="-h${NODE_INCOMING_HOST[1]} -P${NODE_INCOMING_PORT[1]}"

SET_ROUNDS=20000
DROP_ROUNDS=30

MYSQL="mysql --batch --silent $node_0 --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD  -Dtest "

read_param()
{
    var=$1
    local varname="$2"

    value=`$MYSQL -e"SHOW VARIABLES LIKE '$varname'"  | awk '{ print $2 }'`
    eval "$1=$value"

#    return $value
}

set_vars()
{ 
    trap "echo trap" SIGHUP
    for (( i=1; i<$SET_ROUNDS; i++ ))
    do
	$MYSQL -e "set global thread_cache_size=11; set global thread_cache_size=10";
	[ "$?" != "0" ] && echo "SET query failed"
    done
    echo "Setters done"
}

dropper()
{ 

    for (( i=1; i<$DROP_ROUNDS; i++ ))
    do
	$MYSQL -e "set global wsrep_provider=none;";
	$MYSQL -w -e "set global wsrep_provider='$provider'";
	$MYSQL -e "set global wsrep_cluster_address='$address'";

    done
}


#########################################################
#
# Test begins here
#
#########################################################

#[ "$threads" = "wsrep_slave_threads	4" ] && echo "enough slaves"


read_param provider wsrep_provider
echo "wsrep_provider = $provider"

read_param address wsrep_cluster_address
echo "wsrep_cluster_address = $address"

echo "starting variable setters..."
set_vars&
declare setter_pid=$!

echo "dropping and joining node..."
dropper

echo "Dropper Done"
echo "Waiting load to end ($setter_pid)"
wait

$MYSQL -e 'SHOW PROCESSLIST'
[ "$?" != "0" ] && echo "failed!" && exit 1

check

echo
echo "Done!"
echo

../../scripts/command.sh stop_node 0
../../scripts/command.sh stop_node 1

exit 0

