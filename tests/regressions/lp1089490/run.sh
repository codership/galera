#!/bin/bash 
##
#
# lp:1089490
# https://bugs.launchpad.net/codership-mysql/+bug/1089490
#
# BUG BACKGRPOUND:
#
# Foreign keys can cause slave crash if following conditions are met:
# 1. foreign key constraint has been defined with CASCADE ON DELETE option
# 2. foreign key constraint has mixed NULL options in referencing columns
#  (i.e. client column defined with NOT NULL option and parent column 
#   with NULL option)
# 3. slave is configured with multiple slave threads (wsrep_slave_threads > 1)
# 4. work load has conflicting DELETE operations for parent and child table
#
# TEST SETUP:
#   - Two nodes are used in master slave mode. 
#   - Slave is configured with 4 applier threads
#   - parent and child tables are created and populated
#   - test load runs two separate connections, where other session deletes
#     rows from parent table and other sessions delete rows from child table, 
#     one by one
# Due to the cascade on delete, option, the delete from parent table will issue
# delete for the child table, and this cascaded delete may conflict with the 
# direct delete on child table
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
echo "##             regression test for lp:1089490"
echo "##################################################################"
echo "stopping cluster"
../../scripts/command.sh stop
echo
echo "starting node0, node1..."
../../scripts/command.sh start "-d  --slave_threads 4"

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=$DBMS_HOST test "

declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

inserter()
{
    local port=$1
    for i in {1..1000}; do
	$MYSQL --port=$port -e "
          INSERT INTO test.lp1089490_parent(pk, j) VALUES($i, $i); 
          INSERT INTO test.lp1089490_child(i,fk) VALUES ($i, $i); 
        " 2>&1
    done
}

delete_parent()
{
    local port=$1
    for i in {1..1000}; do
	$MYSQL --port=$port -e "
          DELETE FROM test.lp1089490_parent WHERE pk=$i; 
        " 2>&1
    done
}

delete_child()
{
    local port=$1
    for i in {1..1000}; do
	$MYSQL --port=$port -e "
          DELETE FROM  test.lp1089490_child WHERE i=$i; 
        " 2>&1
    done
}

createdb()
{ 
    $MYSQL --port=$port_0 -e "reset master;"
    $MYSQL --port=$port_1 -e "reset master;"

    $MYSQL --port=$port_0 -e "
        DROP TABLE IF EXISTS test.lp1089490_child;"

    $MYSQL --port=$port_0 -e '
        DROP TABLE IF EXISTS test.lp1089490_parent;'

    $MYSQL --port=$port_0 -e '
        CREATE TABLE test.lp1089490_parent
        (
           pk INT PRIMARY KEY AUTO_INCREMENT,
           j int
        )'

    $MYSQL --port=$port_0 -e "
        CREATE TABLE test.lp1089490_child
        (
            i INT PRIMARY KEY AUTO_INCREMENT, 
            fk int, 
            CONSTRAINT  FOREIGN KEY (fk) REFERENCES test.lp1089490_parent(pk) 
            ON DELETE CASCADE
        )"
}

#########################################################
#
# Test begins here
#
#########################################################

threads=$($MYSQL --port=$port_1 -e "SHOW VARIABLES LIKE 'wsrep_slave_threads'")

echo "applier check: $threads"
[ "$threads" = "wsrep_slave_threads	4" ] || { echo "NOT ENOUGH SLAVES"; exit 1; }

echo "Creating database..."
createdb

for i in {1..200}; do
    echo
    echo "### round $i ###"

    echo "populating database..."
    inserter $port_0

    echo "starting delete for parent table"
    delete_parent $port_0 &
    declare parent_pid=$!

    echo "starting delete for child table"
    delete_child $port_0 &
    declare child_pid=$!

    echo "waiting load to end ($parent_pid $child_pid)"
    wait
done

$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
echo
$MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'
[ "$?" != "0" ] && echo "failed!" && exit 1

$SCRIPTS/command.sh check

echo
echo "Done!"
echo

../../scripts/command.sh stop

exit 0

