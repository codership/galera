#!/bin/bash -eux

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
    echo 
    echo "Test 1 starting..."; 
    echo
    $MYSQL0 -e "drop table if exists uniq;"
    $MYSQL0 -e "create table uniq (u varchar(10), unique key unique_key(u));"

    for i in $(seq 1 10)
    do
	$MYSQL0 -e "insert into uniq (u) values ('const');" &
	$MYSQL1 -e "insert into uniq (u) values ('const');" &
	wait
    done
    echo
    echo "Test 1 done"
    echo "****************************************************"
}

test_2_load()
{
    local node="${@:1}"

    echo "load 2 for $node starting..."; 

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
    test_2_load  "$NODE0" &
    test_2_load  "$NODE1" &

    echo "test 2 loads started"

    wait
    echo
    echo "Test 2 done"
    echo "****************************************************"
}

test_3_load()
{
    echo "test load 3"; 
    for i in $(seq 1 1000)
    do
        #echo "round: $i"
	$MYSQL0 -e "insert into uniq (u) values ('const');" &
	$MYSQL1 -e "insert into uniq (u) values ('const');" &
	wait
	val0=$($MYSQL0 -N -s  -e "select count(*) from uniq;")
	val1=$($MYSQL1 -N -s  -e "select count(*) from uniq;")

        #sleep 1

	[ "$val0" != "1" ] && echo "0 $val0 $val1" && exit
	[ "$val1" != "1" ] && echo "1 $val0 $val1" && exit

	echo "truncing"
	$MYSQL0 -e "truncate uniq;"
    done
}

test_3()
{
    echo 
    echo "Test 3 starting..."; 
    echo
    $MYSQL0 -e "drop table if exists uniq;"
    $MYSQL0 -e "create table uniq (u varchar(10), unique key unique_key(u));"

    test_3_load
    echo
    echo "Test 3 done"
    echo "****************************************************"
}

test_1
test_2
test_3

$SCRIPTS/command.sh wait_sync 0 1

test $($MYSQL0 -ss -e "select count(*) from uniq") == 1 || \
    (echo "duplicate uniq key" && exit 1)
test $($MYSQL1 -ss -e "select count(*) from uniq") == 1 || \
    (echo "duplicate uniq key" && exit 1)

$SCRIPTS/command.sh check | wc -l

$SCRIPTS/command.sh stop
