#!/bin/bash -eux

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}
. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"

NODE0="--host ${NODE_INCOMING_HOST[0]} --port ${NODE_INCOMING_PORT[0]}"
NODE1="--host ${NODE_INCOMING_HOST[1]} --port ${NODE_INCOMING_PORT[1]}"
MYSQL="mysql -utest -ptestpass -hgw -Dtest"
MYSQL0="$MYSQL $NODE0"
MYSQL1="$MYSQL $NODE1"

$MYSQL0 -e "drop table if exists uniq;"
$MYSQL0 -e "create table uniq (u varchar(10), unique key unique_key(u));"

for i in $(seq 1 10)
do
    $MYSQL0 -e "insert into uniq (u) values ('const');" &
    $MYSQL1 -e "insert into uniq (u) values ('const');" &
    wait
done

$SCRIPTS/command.sh check | wc -l

