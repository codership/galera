#!/bin/bash

#
# DESCRIPTION:
#
# Test for lp:967134
# https://bugs.launchpad.net/codership-mysql/+bug/967134
#
# This test script may not be able to crash nodes or generate inconsistency.
# However, some error messages may be generated in error log if the
# fix is not effective.
#

set -e

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"


$SCRIPTS/command.sh restart

MYSQL="mysql -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD
             -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]}
             -Dtest"

TABLE_PREFIX=""
PARENT_TABLE="${TABLE_PREFIX}parent"
CHILD_TABLE="${TABLE_PREFIX}child"


rounds=10
while test $rounds != 0
do
    PARENT_TABLE_CREATE="CREATE TABLE $PARENT_TABLE (a INT PRIMARY KEY"
    echo "-- generating $((100 - $rounds)) columns for parent"
    for ii in `seq 0 $((100 - $rounds))`
    do
        PARENT_TABLE_CREATE="$PARENT_TABLE_CREATE, a$ii INT DEFAULT $ii"
    done
    PARENT_TABLE_CREATE="$PARENT_TABLE_CREATE) ENGINE=InnoDB"

#    echo $PARENT_TABLE_CREATE


    $MYSQL -e "DROP TABLE IF EXISTS $CHILD_TABLE, $PARENT_TABLE;
           $PARENT_TABLE_CREATE;
           CREATE TABLE $CHILD_TABLE
           (
               c INT,
               d INT,
               INDEX idx_c(c),
               FOREIGN KEY (c) REFERENCES $PARENT_TABLE(a)
                   ON DELETE CASCADE
           ) ENGINE=InnoDB;"

    ROWS=${ROWS:-1000}
    ii=0

    echo "-- Filling parent table"
    while test $ii != $ROWS
    do
        $MYSQL -e "INSERT INTO $PARENT_TABLE (a) VALUES  ($ii)"
        ii=$(($ii + 1))
    done

    echo "-- Filling child table"
    ii=0
    while test $ii != $ROWS
    do
        $MYSQL -e "INSERT INTO $CHILD_TABLE VALUES (1, 1)"
        ii=$(($ii + 1))
    done
    rounds=$(($rounds - 1))
done

$SCRIPTS/command.sh check