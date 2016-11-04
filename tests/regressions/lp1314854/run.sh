#!/bin/bash 
##
#
# lp:1314854
# https://bugs.launchpad.net/codership-mysql/+bug/1314854
#
# BUG BACKGRPOUND:
#
# Keys for varchar columns using multi-byte caharacter codes were truncated 
# in the write set, and this caused excessive certification failures.
#
# If bug is present, slave will crash for FK violation
#
declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

echo "##################################################################"
echo "##             regression test for lp:1314854"
echo "##################################################################"
echo "stopping cluster..."
stop
echo
echo "starting node0, node1..."
start_node "-d -g gcomm://$(extra_params 0)" 0
start_node "-d -g $(gcs_address 1) --slave_threads 4" 1

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=$DBMS_HOST test "

declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

ROUNDS=10000
SUCCESS=0

create_FK_parent_PK()
{
    $MYSQL --port=$port_0 -e '
        CREATE TABLE  test.lp1314854p
        (
             k varchar(10) NOT NULL,
             j int NOT NULL,
             PRIMARY KEY (k,j)
        ) ENGINE=InnoDB character set = utf8
    '
}

create_FK_child_PK()
{
    $MYSQL --port=$port_0 -e '
        CREATE TABLE  test.lp1314854c
        (
             i int NOT NULL,
             k varchar(10),
             j int DEFAULT NULL,
             PRIMARY KEY (i),
             FOREIGN KEY (k,j) REFERENCES test.lp1314854p (k,j)
        ) ENGINE=InnoDB character set = utf8
    '
}


create_utf8_table()
{
    $MYSQL --port=$port_0 -e '
        CREATE TABLE  test.lp1314854
        (
             k varchar(30) NOT NULL,
             f int DEFAULT NULL,
             PRIMARY KEY (k),
             KEY fk (f)
        ) ENGINE=InnoDB character set = utf8
    '
}



#
# Test A procedures
#
A_cleanup()
{ 
    $MYSQL --port=$port_0 -e "
        DROP TABLE IF EXISTS test.lp1314854c;
    "
    $MYSQL --port=$port_0 -e "
        DROP TABLE IF EXISTS test.lp1314854p;
    " 
}
A_createdb()
{ 
    create_FK_parent_PK
    create_FK_child_PK
}

A_init()
{
    A_cleanup
    A_createdb
}

A_inserter()
{
    local port=$1
    $MYSQL --port=$port -e "
          INSERT INTO test.lp1314854p values ('A«B', 1); 
          INSERT INTO test.lp1314854c values (1, 'A«B', 1);
        " 2>&1
}

A_test()
{
    echo "A test good"
}

#
# Test phase B procedures
#
B_cleanup()
{ 
    $MYSQL --port=$port_0 -e "
        DROP TABLE IF EXISTS test.lp1314854;
    "
}

B_createdb()
{ 
    create_utf8_table
}

B_init()
{
    B_cleanup
    B_createdb
}


B_inserter()
{
    local port=$1
    local prefix=$2

    for (( i=1; i<=$ROUNDS; i++ )); do
	$MYSQL --port=$port -e "
          INSERT INTO test.lp1314854 values ('${prefix}$i', $i)
        " 2>&1
    done
}

B_test()
{
    local port=$1
    rows=`$MYSQL --port=$port -e "
          SELECT COUNT(*) FROM  test.lp1314854; 
        "`
    if [ "$rows" != "$(( $ROUNDS * 2 ))" ]; then
        echo "B failure";
	exit 1;
    fi
    echo "B rows = $rows"
}


wait_for_load()
{
    local pid=$1

    echo "Waiting load to end ($pid)"
    wait
    $MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
    echo
    $MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'
    [ "$?" != "0" ] && echo "failed!" && exit 1

    #$SCRIPTS/command.sh check
    echo "consistency checking..."
    check0=$(check_node 0)
    check1=$(check_node 1)

    cs0=$(echo $check0 | cut -d" " -f 11)
    cs1=$(echo $check1 | cut -d" " -f 11)

    echo "node 0: $cs0"
    echo "node 1: $cs1"

    if test "$cs0" != "$cs1"
    then
	echo "Consistency check failed"
	echo "$check0 $cs0"
	echo "$check1 $cs1"
	exit 1
    fi
    if test $? != 0
    then
	echo "Consistency check failed"
	exit 1
    fi
}

run_test()
{
    echo "##################################################################"
    echo "##             test phase A - FK constraint"
    echo "##################################################################"
    echo
    echo "Creating database..."
    A_init

    echo "Starting test process..."
    A_inserter $port_0 &
    pid=$!

    wait_for_load $pid

    A_cleanup

    echo
    echo "looks good so far..."

    echo
    echo "##################################################################"
    echo "##             test phase B - concurrent varchar key inserts"
    echo "##################################################################"
    echo
    echo "Creating database..."
    B_init

    echo "Starting test process..."
    B_inserter $port_0 "AA" &
    pid1=$!

    B_inserter $port_1 "BB" &
    pid2=$!

    wait_for_load $pid1

    B_test $port_0
    B_test $port_1

    B_cleanup

    echo
    echo "looks good so far..."
    echo
}
#########################################################
#
# Test begins here
#
#########################################################

threads=$($MYSQL --port=$port_1 -e "SHOW VARIABLES LIKE 'wsrep_slave_threads'")

echo "applier check: $threads"
[ "$threads" = "wsrep_slave_threads	4" ] && echo "enough slaves"


run_test 

echo
echo "Done!"
echo

stop_node 1
stop_node 0

exit $SUCCESS

