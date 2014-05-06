#!/bin/bash -eu
##!/bin/bash -eux

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}
. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"

NODE0="--host ${NODE_INCOMING_HOST[0]} --port ${NODE_INCOMING_PORT[0]}"
NODE1="--host ${NODE_INCOMING_HOST[1]} --port ${NODE_INCOMING_PORT[1]}"
MYSQL="mysql -utest -ptestpass  -Dtest"
MYSQL0="$MYSQL $NODE0"
MYSQL1="$MYSQL $NODE1"

$SCRIPTS/command.sh restart

test_1()
{
    $MYSQL0 -e "drop table if exists uniq;"
    $MYSQL0 -e "create table uniq (u varchar(10), unique key unique_key(u));"

    for i in $(seq 1 10)
    do
	$MYSQL0 -e "insert into uniq (u) values ('const');" &
	$MYSQL1 -e "insert into uniq (u) values ('const');" &
	wait
    done
}

test_2_load()
{
    local node="${@:1}"

    echo "load for $node"; 

    for i in {1..10000}; do 
	$MYSQL $node -e"insert into uniq(u) values('keys'); 
                          delete from uniq;"; 
    done
}

test_2()
{
    $MYSQL0 -e "drop table if exists uniq;"
    $MYSQL0 -e "create table uniq (
                  u varchar(10),
                  i int auto_increment,
                  key(i),
                  unique key unique_key(u));"
    #test_2_load  ${NODE_INCOMING_PORT[0]} &
    #test_2_load  ${NODE_INCOMING_PORT[1]}
    test_2_load  "$NODE0" &
    test_2_load  "$NODE1" &

    echo "test loads started"

    wait
}

test_1
test_2

$SCRIPTS/command.sh wait_sync 0 1

$SCRIPTS/command.sh check | wc -l

$SCRIPTS/command.sh stop
