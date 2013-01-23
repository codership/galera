#!/bin/bash 
##
#
# lp:1100496
# https://bugs.launchpad.net/codership-mysql/+bug/1100496
#
# BUG BACKGROUND:
#
# Foreign keys can cause slave crash if following conditions are met:
# 1. foreign key column in child table and referenced column in parent
#    table have mixed types of CHAR and VARCHAR
# 2. slave uses parallel applying 
# 3. work load contains concurrent direct deletes on parent table and deletes 
#    on child table 
# Crash happens because, wsrep appends shared parent key (through FK processing)
# and exclusive key (due to direct parent row delete) in different formats. 
# => certification does not detect the conflict and lets write sets to apply
# in parallel.
#
# TEST SETUP:
#   - Two nodes are used in master slave mode. 
#   - Slave is configured with 4 applier threads
#   - parent and child tables are created and populated
#   - test load runs one connection, which issues a large detete for child
#     table and one row delete for parent table. We try to make the applying
#     of child table delete to last so long that parent table delete gets to
#     apply in parallel.
#
# SUCCESS CRITERIA
#
# If bug is present, slave will crash for not being able to delete a row from
# child table
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
echo "##             regression test for lp:1100496"
echo "##################################################################"
echo "stopping cluster"
../../scripts/command.sh stop
echo
echo "starting node0, node1..."
../../scripts/command.sh start "-d  --slave_threads 4"

declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=$DBMS_HOST --port=$port_0 test "

populate()
{
    local rows=$1

    $MYSQL -e "INSERT INTO test.lp1100496_parent(pk) VALUES('Parent');"

    for ((i=1; i<$rows; i++)); do
	$MYSQL -e "$I1K" 2>&1
    done
}

run_deletes()
{
    $MYSQL -e "DELETE FROM test.lp1100496_child; DELETE FROM test.lp1100496_parent;"

}


check_cluster()
{
    local original_size=$1
    size=`$MYSQL -e "SHOW STATUS LIKE 'wsrep_cluster_size';" | cut -f 2`
    if [ "$size" != "$original_size" ]; then
	echo "cluster broken; $size $original_size"
	exit 1
    fi

}

cleandb()
{ 
    $MYSQL --port=$port_0 -e "reset master;"
    $MYSQL --port=$port_1 -e "reset master;"

    $MYSQL -e "
        DROP TABLE IF EXISTS test.lp1100496_child;"

    $MYSQL -e '
        DROP TABLE IF EXISTS test.lp1100496_parent;'
}

createdb()
{ 
    local charset=$1
    cleandb

    $MYSQL --port=$port_0 -e "
        CREATE TABLE test.lp1100496_parent
        (
           pk VARCHAR(30) PRIMARY KEY
        ) CHARACTER SET = $charset ENGINE=innodb"

    $MYSQL --port=$port_0 -e "
        CREATE TABLE test.lp1100496_child
        (
            i INT PRIMARY KEY AUTO_INCREMENT, 
            fk CHAR(20), 
            CONSTRAINT  FOREIGN KEY (fk) REFERENCES test.lp1100496_parent(pk)
        ) CHARACTER SET = $charset, ENGINE=innodb"
}

run_test()
{
    local charset=$1

    echo
    echo "##    Testing character set: $charset"
    echo

    createdb $charset

    for i in 10 20 30 40 50 60; do
	echo
	echo "populating database, ${i}K rows..."
	populate $i

	echo "testing..."
	run_deletes
	
	check_cluster $INITIAL_SIZE
    done

    echo
    echo "##    Consistency checks"
    echo

    $MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
    echo
    $MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'
    [ "$?" != "0" ] && echo "failed!" && exit 1


    wait_sync $NODE_LIST
    $SCRIPTS/command.sh check
}

#########################################################
#
# Test begins here
#
#########################################################

threads=$($MYSQL --port=$port_1 -e "SHOW VARIABLES LIKE 'wsrep_slave_threads'")

echo "applier check: $threads"
[ "$threads" = "wsrep_slave_threads	4" ] || { echo "NOT ENOUGH SLAVES"; exit 1; }

INITIAL_SIZE=`$MYSQL -e "SHOW STATUS LIKE 'wsrep_cluster_size';" | cut -f 2`
echo "Initial cluster size: $INITIAL_SIZE"

I1K="INSERT INTO test.lp1100496_child(fk) VALUES ('Parent')"
for ((i=1; i<1000; i++)); do
    I1K="$I1K,('Parent')"
done

run_test latin1
run_test utf8


echo
echo "Done!"
echo

../../scripts/command.sh stop

exit 0

