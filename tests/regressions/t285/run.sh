#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/signal.sh
. $SCRIPTS/misc.sh

echo "regression test for #285"
echo "restarting cluster"

$SCRIPTS/command.sh restart

mysql -s -s -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD \
    -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]} test \
    -e "DROP TABLE IF EXISTS t285";

mysql -s -s -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD \
    -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]} test \
    -e "CREATE TABLE t285 (a INT PRIMARY KEY)";

stmt=$(
    echo "BEGIN;";
    for i in `seq 1 $((1 << 16))`
    do
        echo "INSERT INTO t285 VALUES ($i);";
    done
    echo "COMMIT;";
)

echo $stmt | mysql -s -s -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD \
    -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]} test


if test $? != 0
then
    echo "query failed";
    exit 1;
else
    mysql -s -s -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD \
        -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]} test \
        -e "DROP TABLE IF EXISTS t285";
    if test $? != 0
    then
        echo "cleanup failed, check server status";
        exit 1;
    fi
fi

$SCRIPTS/command.sh check

if test $? != 0
then
    echo "checksum failed"
    exit 1
fi

$SCRIPTS/command.sh stop

exit 0;