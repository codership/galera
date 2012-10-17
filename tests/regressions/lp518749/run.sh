#!/bin/bash -eu


declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

echo "regression test for lp:518749"

echo "restarting cluster"
$SCRIPTS/command.sh restart

mysql --user=$DBMS_ROOT_USER --password=$DBMS_ROOT_PSWD --host=${NODE_INCOMING_HOST[0]} --port=${NODE_INCOMING_PORT[0]} < mysqlfs.sql;

check;
ret=$?;
if test $ret != 0
then
    echo "checksum failed";
    exit 1;
fi

# Cleanup
mysql --user=$DBMS_ROOT_USER --password=$DBMS_ROOT_PSWD --host=${NODE_INCOMING_HOST[0]} --port=${NODE_INCOMING_PORT[0]} -e "DROP DATABASE mysqlfs";

$SCRIPTS/command.sh stop