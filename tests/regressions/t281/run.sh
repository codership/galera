#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/signal.sh
. $SCRIPTS/misc.sh

function get_status
{
    cmd=$(echo "show status like '$2'")
    echo $(mysql -s -s -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD -h${NODE_INCOMING_HOST[$1]} -P${NODE_INCOMING_PORT[$1]} -e "$cmd" | awk '{print $2;}');
}

echo "regression test for #281"
echo "restarting cluster"

$SCRIPTS/command.sh restart


mysql -s -s -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD \
    -h${NODE_INCOMING_HOST[1]} -P${NODE_INCOMING_PORT[1]} test \
    -e "DROP TABLE IF EXISTS t281";

signal_node STOP 1
pause 20 1

signal_node CONT 1

pause 10 1

status=$(get_status 1 "wsrep_cluster_status");
ready=$(get_status 1 "wsrep_ready");

echo $status $ready

if test $status=="Primary" && test $ready=="ON"
then

    mysql -s -s -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD \
        -h${NODE_INCOMING_HOST[1]} -P${NODE_INCOMING_PORT[1]} test \
        -e "CREATE TABLE t281 (a int)";
    if test $? != 0; then
        echo "Failed to execute command";
        exit 1;
    fi
else
    echo "Wrong state";
    exit 1;
fi

mysql -s -s -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD \
    -h${NODE_INCOMING_HOST[1]} -P${NODE_INCOMING_PORT[1]} test \
    -e "DROP TABLE IF EXISTS t281";

$SCRIPTS/command.sh stop

exit 0;
