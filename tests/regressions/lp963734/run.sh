#!/bin/bash

#
# Regression test for lp:963734
# https://bugs.launchpad.net/codership-mysql/+bug/963734
#
# TEST DESCRIPTION:
#
# This test runs create table, table rename, insert into in single
# session. If alter table rename is missing table keys for both table names,
# slave will eventually crash on insert because replicator allows parallel
# applying of insert when alter is still running.
#
# Table renaming is tested with both ALTER TABLE ... RENAME and
# RENAME TABLE ... TO.
#

set -e

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"

TABLE_BASENAME="lp963734"

ALTER_STMTS="DROP TABLE IF EXISTS ${TABLE_BASENAME}_a, ${TABLE_BASENAME}_b;
       CREATE TABLE ${TABLE_BASENAME}_a (a int);
       ALTER TABLE ${TABLE_BASENAME}_a RENAME ${TABLE_BASENAME}_b;
       INSERT INTO ${TABLE_BASENAME}_b VALUES (1);
       DROP TABLE ${TABLE_BASENAME}_b;"

RENAME_STMTS="DROP TABLE IF EXISTS ${TABLE_BASENAME}_a, ${TABLE_BASENAME}_b;
       CREATE TABLE ${TABLE_BASENAME}_a (a int);
       RENAME TABLE ${TABLE_BASENAME}_a TO ${TABLE_BASENAME}_b;
       INSERT INTO ${TABLE_BASENAME}_b VALUES (1);
       DROP TABLE ${TABLE_BASENAME}_b;"


ROUNDS=${ROUNDS:-1000}

cnt=$ROUNDS

$SCRIPTS/command.sh restart

while test $cnt != 0
do
    mysql -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD \
        -h${NODE_INCOMING_HOST[0]}       \
        -P${NODE_INCOMING_PORT[0]}       \
        -Dtest \
        -e "$ALTER_STMTS"
    cnt=$(($cnt - 1))
done

while test $cnt != 0
do
    mysql -u$DBMS_TEST_USER -p$DBMS_TEST_PSWD \
        -h${NODE_INCOMING_HOST[0]}       \
        -P${NODE_INCOMING_PORT[0]}       \
        -Dtest \
        -e "$RENAME_STMTS"
    cnt=$(($cnt - 1))
done


$SCRIPTS/command.sh check
ret=$?
if test $ret != 0
then
    echo "Test failed"
    exit 1
fi

node_cnt=0
for node in $NODE_LIST
do
    node_cnt=$(($node_cnt + 1))
done

for node in $NODE_LIST
do
    cs=$($SCRIPTS/command.sh cluster_status $node)
    if test $cs != "Primary:$node_cnt"
    then
        echo "invalid cluster status for $node: $cs"
        echo "test failed"
        exit 1
    fi
done


$SCRIPTS/command.sh stop
