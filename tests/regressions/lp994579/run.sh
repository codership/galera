#!/bin/bash 
##
#
# lp:994579
# https://bugs.launchpad.net/codership-mysql/+bug/994579
#
# BUG BACKGRPOUND:
#
# Foreign keys are not yet safe with parallel applying. This is due to
# a bug in populating of shared keys.
# If bug is present, slave appliers can easily conflict and cause crash.
#
# TEST SETUP:
#   - Two nodes are used in master slave mode. 
#   - Slave is configured with 4 applier threads
#   - parent and child tables are created
#   - test load runs inserts for parent and child table, each parent 
#     insert is followed by corresponding child insert
#
#
# SUCCESS CRITERIA
#
# If bug is present, slave will crash for FK constraint failure. This is 
# because slave applied inserts in wrong order - child row was added before
# corresponding parent row. 
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
echo "##             regression test for lp:994579"
echo "##################################################################"
echo "stopping cluster"
../../scripts/command.sh stop
#echo "stopping node0, node1..."
#../../scripts/command.sh stop_node 0
#../../scripts/command.sh stop_node 1
echo
echo "starting node0, node1..."
../../scripts/command.sh start_node "-d -g gcomm://$(extra_params 0)" 0
../../scripts/command.sh start_node "-d -g $(gcs_address 1) --slave_threads 4" 1

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=$DBMS_HOST test "

declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

inserter_int_trx()
{
    local port=$1
    for i in {1..10000}; do
	$MYSQL --port=$port -e "
        BEGIN; 
          INSERT INTO test.lp994579_parent(pk, j) VALUES($i, $i); 
          INSERT INTO test.lp994579_child(i,fk) VALUES ($i, $i); 
        COMMIT;" 2>&1
    done
}

inserter_int_ac()
{
    local port=$1
    for i in {1..10000}; do
	$MYSQL --port=$port -e "
          INSERT INTO test.lp994579_parent(pk, j) VALUES($i, $i); 
          INSERT INTO test.lp994579_child(i,fk) VALUES ($i, $i); 
        " 2>&1
    done
}

createdb_int()
{ 
    $MYSQL --port=$port_0 -e "
        DROP TABLE IF EXISTS test.lp994579_child;"

    $MYSQL --port=$port_0 -e '
        DROP TABLE IF EXISTS test.lp994579_parent;'

    $MYSQL --port=$port_0 -e '
        CREATE TABLE test.lp994579_parent
        (
           pk INT PRIMARY KEY AUTO_INCREMENT,
           j int
        )'

    $MYSQL --port=$port_0 -e "
        CREATE TABLE test.lp994579_child
        (
            i INT PRIMARY KEY AUTO_INCREMENT, 
            fk int, 
            CONSTRAINT  FOREIGN KEY (fk) REFERENCES test.lp994579_parent(pk)
        )"
}

createdb_varchar()
{ 
    local charset=$1

    $MYSQL --port=$port_0 -e "
        DROP TABLE IF EXISTS test.lp994579_child;"

    $MYSQL --port=$port_0 -e '
        DROP TABLE IF EXISTS test.lp994579_parent;'

    $MYSQL --port=$port_0 -e "
        CREATE TABLE test.lp994579_parent
        (
           pk VARCHAR(20) PRIMARY KEY,
           j int
        ) CHARACTER SET '$charset' "

    $MYSQL --port=$port_0 -e "
        CREATE TABLE test.lp994579_child
        (
            i INT PRIMARY KEY AUTO_INCREMENT, 
            fk VARCHAR(20), 
            CONSTRAINT  FOREIGN KEY (fk) REFERENCES test.lp994579_parent(pk)
        ) CHARACTER SET '$charset'"
}

inserter_varchar_ac()
{
    local port=$1
    for i in {1..10000}; do
	$MYSQL --port=$port -e "
          INSERT INTO test.lp994579_parent(pk, j) VALUES('key-$i', $i); 
          INSERT INTO test.lp994579_child(i,fk) VALUES ($i, 'key-$i'); 
        " 2>&1
    done
}

#########################################################
#
# Test begins here
#
#########################################################

threads=$($MYSQL --port=$port_1 -e "SHOW VARIABLES LIKE 'wsrep_slave_threads'")

echo "applier check: $threads"
[ "$threads" = "wsrep_slave_threads	4" ] && echo "enough slaves"

echo "Creating database..."
createdb_int

echo
echo 
echo "#############################################"
echo "### Phase 1, FK by int column, autocommit ###"
echo

echo "starting inserter for int keys, autocommit"
inserter_int_ac $port_0 &
declare inserter1_pid=$!

echo "waiting phase 1 load to end ($inserter1_pid)"
wait
$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
echo
$MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'
[ "$?" != "0" ] && echo "failed!" && exit 1

createdb_int

echo
echo 
echo "###############################################"
echo "### Phase 2, FK by int column, transactions ###"
echo

echo "starting inserter for int keys, transactions"
inserter_int_trx $port_0 &
declare inserter2_pid=$!

echo "waiting phase 2 load to end ($inserter2_pid)"
wait
$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
echo
$MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'
[ "$?" != "0" ] && echo "failed!" && exit 1

echo 
echo 
echo "#############################################"
echo "### Phase 3, FK by varchar column, latin1 ###"
echo

createdb_varchar latin1

echo "starting inserter for VARCHAR keys, autocommit"
inserter_varchar_ac $port_0 &
declare inserter3_pid=$!

echo "waiting phase 3 load to end ($inserter3_pid)"
wait
$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
echo
$MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'
[ "$?" != "0" ] && echo "failed!" && exit 1

echo 
echo 
echo "###########################################"
echo "### Phase 4, FK by varchar column, utf8 ###"
echo

createdb_varchar utf8

echo "starting inserter for VARCHAR keys, autocommit"
inserter_varchar_ac $port_0 &
declare inserter4_pid=$!

echo "waiting phase 4 load to end ($inserter4_pid)"
wait
$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
[ "$?" != "0" ] && echo "failed!" && exit 1
echo
$MYSQL --port=$port_1 -e 'SHOW PROCESSLIST'
[ "$?" != "0" ] && echo "failed!" && exit 1


$SCRIPTS/command.sh check

if test $? != 0
then
    echo "Consistency check failed"
    exit 1
fi

echo
echo "Done!"
echo

../../scripts/command.sh stop_node 0
../../scripts/command.sh stop_node 1

exit 0

