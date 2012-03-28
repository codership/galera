#!/bin/bash
#
# DESCRIPTION:
#
# Regression test to reproduce lp:966950
# See https://bugs.launchpad.net/codership-mysql/+bug/966950
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

TABLE_PREFIX="lp966950_"
PARENT_TABLE="${TABLE_PREFIX}parent"
CHILD_TABLE="${TABLE_PREFIX}child"

$MYSQL -e "DROP TABLE IF EXISTS $CHILD_TABLE, $PARENT_TABLE;
           CREATE TABLE $PARENT_TABLE
           (
               a INT PRIMARY KEY,
               b INT,
               INDEX idx_b(b)
           ) ENGINE=InnoDB;
           CREATE TABLE $CHILD_TABLE
           (
               c INT,
               d INT,
               INDEX idx_d(d),
               FOREIGN KEY (d) REFERENCES $PARENT_TABLE(b)
                   ON DELETE CASCADE
           ) ENGINE=InnoDB;
           INSERT INTO $PARENT_TABLE VALUES (1, 1);
           INSERT INTO $CHILD_TABLE VALUES (101, 1);"

$SCRIPTS/command.sh check

if test $? != 0
then
    echo "Consistency check failed"
    exit 1
fi
