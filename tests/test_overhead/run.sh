#!/bin/bash -eu

# This test measures master replication overhead by running a number of
# autocommit queries with wsrep_on set to 0 and 1.
#
# NOTES:
# - The load was deliberately chosen to produce maximum replication overhead.
# - SQL commands are first dumped into a text file in order to minimize client
#   overhead when benchmarking.

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh

stop
start_node -g gcomm:// --mysql-opt --wsrep-new-cluster 0

declare -r SQL="$(dirname $0)/tmp.sql"
declare -r TABLE="overhead"
declare -r TABLE_DEFINITION="(pk INT PRIMARY KEY, u VARCHAR(64))"
declare -r MAX_ROWS=50000

insert_load()
{
    local WSREP_ON=$1
    echo "USE $DBMS_TEST_SCHEMA; SET GLOBAL wsrep_on=$WSREP_ON; "
#    echo "BEGIN; "
    for i in $(seq 1 $MAX_ROWS)
    do
        echo "INSERT INTO $TABLE VALUES ($i, uuid()); "
    done
#    echo "COMMIT;"
}

update_load()
{
    local WSREP_ON=$1
    echo "USE $DBMS_TEST_SCHEMA; SET GLOBAL wsrep_on=$WSREP_ON; "
#    echo "BEGIN; "
    for i in $(seq 1 $MAX_ROWS)
    do
        local PK=$(( $RANDOM * $RANDOM % $MAX_ROWS))
        echo "UPDATE $TABLE SET u = uuid() WHERE pk = $PK; "
    done
#    echo "COMMIT;"
}

MYSQL="mysql -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD"
MYSQL="$MYSQL -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]} -B"

warm_up()
{
    echo -n "Warming up: "
    $MYSQL -e "DROP TABLE IF EXISTS $DBMS_TEST_SCHEMA.$TABLE"
    $MYSQL -e "CREATE TABLE $DBMS_TEST_SCHEMA.$TABLE $TABLE_DEFINITION"
    insert_load 1 > $SQL # generate sql commands
    /usr/bin/time -o /dev/stdout -f '%e' $MYSQL < $SQL
}

warm_up

declare -a INSERTS
declare -a UPDATES

for wsrep in 0 1
do
    $MYSQL -e "DROP TABLE IF EXISTS $DBMS_TEST_SCHEMA.$TABLE"
    $MYSQL -e "CREATE TABLE $DBMS_TEST_SCHEMA.$TABLE $TABLE_DEFINITION"
    echo -n "wsrep_on = $wsrep :  "
    insert_load $wsrep > $SQL # generate sql commands
    TIMING=$(/usr/bin/time -o /dev/stdout -f '%e' $MYSQL < $SQL)
    echo -n "${TIMING} / "
    INSERTS[$wsrep]=$(echo -n ${TIMING} | sed s/\\.//)
    update_load $wsrep > $SQL # generate sql commands
    TIMING=$(/usr/bin/time -o /dev/stdout -f '%e' $MYSQL < $SQL)
    echo "${TIMING}"
    UPDATES[$wsrep]=$(echo -n ${TIMING} | sed s/\\.//)
done

INSERT_OVERHEAD=$(( ${INSERTS[1]} * 100 / ${INSERTS[0]} - 100))
UPDATE_OVERHEAD=$(( ${UPDATES[1]} * 100 / ${UPDATES[0]} - 100))
echo "Overhead: $INSERT_OVERHEAD% / $UPDATE_OVERHEAD%"

