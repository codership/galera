#!/bin/bash -eu

# This test measures master replication overhead by running a number of
# autocommit queries with wsrep_on set to 0 and 1.
#
# NOTES:
# - The load was deliberately chosen to produce maximum replication overhead.
# - SQL commands are first dumped into a text file in order to minimize client
#   overhead when benchmarking.

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh

declare -r TABLE_NAME="memory"
declare -r TABLE="$DBMS_TEST_SCHEMA.$TABLE_NAME"
declare -r TABLE_DEFINITION="(c1 INT AUTO_INCREMENT PRIMARY KEY, c2 CHAR(255))"

MYSQL="mysql -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD"
MYSQL="$MYSQL -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]} -B"

prepare()
{
    stop
    start_node -g gcomm:// --mysql-opt --wsrep-new-cluster 0

    echo -n "Preparing... "
    $MYSQL -e "DROP TABLE IF EXISTS $TABLE;
               CREATE TABLE $TABLE $TABLE_DEFINITION;
               INSERT INTO  $TABLE(c2) VALUES('abc');"
    # make sure the latest writeset protocol is used
    $MYSQL -e "SET GLOBAL wsrep_provider_options='repl.proto_max=10'"
    echo "done"
}

# load() will progressively generate larger and larger writesets in geometric
# progression, by inserting table onto itself. At some point it will have to
# spill to disk and trigger the bug.
load()
{
    echo -e "Rows\tSeconds\tRSS"
    for rows in $(seq 0 20)
    do
        echo -en "$(( 1 << $rows ))\t"
        begin=$SECONDS
        $MYSQL -e "INSERT INTO $TABLE(c2) SELECT c2 FROM $TABLE;"
        seconds=$(( $SECONDS - $begin ))
        echo -en "$seconds\t"; ps --no-headers -C mysqld -o rss || ps --no-headers -C mysqld-debug -o rss
    done
}

prepare
load 1
