#!/bin/bash 
##
#
# lp:1013978
# https://bugs.launchpad.net/codership-mysql/+bug/1013978
#
# BUG BACKGRPOUND:
#
# Foreign key constraint on non-unique index in parent table can cause
# a crash in slave configured for parallel applying.
# Current wsrep patch does not populate any key information for non-unique
# keys and dependencies for parent row existence are not respected in PA 
# control. 
#
# There are five scenarios how foreign key problems can surface:
# A. FK has ON UPDATE CASCADE option
#    1. Parent table has two rows with same fk key value 
#    2. One transaction issues update on one parent row, which triggers
#       corresponding update on depending child table row(s)
#    3. Another transaction deletes the other parent table row.
#       This is now safe because child references have been changed to
#       point to the first parent row
#    => These WSs don't have mutual dependency and in slave side it may
#       happen that DELETE will be processed first, and it will fail for 
#       FK violation
#
# B. shared reference to non-UK column in parent row with inserts
#    1.  one transaction does insert on parent table
#    2.  another transaction does insert on child table referencing the
#        inserted row
#    => These WSs don't have mutual dependency and in slave side it may
#       happen that child INSERT will be processed first, and it will fail for
#       FK violation
#
# C. shared reference to non-UK column in parent row with deletes
#    1.  one transaction deletes a row from child table
#    2.  another transaction deletes corresponding row in parent table
#    => These WSs don't have mutual dependency and in slave side it may
#       happen that parent delete will be processed first, and it will fail for
#       FK violation
#
# D. delete with FK on PK
#    *  same as phase C, but with tables where FK constraint is on
#       primary key column
#
# E. shared reference to non-UK column in parent row with insert + delete
#    1. one transaction first inserts a row in parent table
#    2. second transaction inserts referencing row in child table
#    3. third transaction deletes the row from child table
#    4. fourths transaction deletes the row from parent table
#    => either insert or delete transactions can get applied in wrong order
#       in slave due to missing dependency
#
# If bug is present, slave appliers can easily conflict and cause crash.
#
# TEST SETUP:
#   - Two nodes are used in master slave mode. 
#   - Slave is configured with 4 applier threads
#   - parent and child tables are creted and populated
#   - Test phases A, B and C will be run subsequently
#
# SUCCESS CRITERIA
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
echo "##             regression test for lp:1013978"
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

ROUNDS=1000
SUCCESS=0

create_FK_parent_PK()
{
    $MYSQL --port=$port_0 -e '
        CREATE TABLE  test.lp1013978p
        (
             i int NOT NULL,
             j int DEFAULT NULL,
             PRIMARY KEY (i)
        ) ENGINE=InnoDB
    '
}

create_FK_child_PK()
{
    $MYSQL --port=$port_0 -e '
        CREATE TABLE  test.lp1013978c
        (
             i int NOT NULL,
             f int DEFAULT NULL,
             PRIMARY KEY (i),
             FOREIGN KEY (i) REFERENCES test.lp1013978p (i)
        ) ENGINE=InnoDB
    '
}

create_FK_parent_NON_UNIQ()
{
    $MYSQL --port=$port_0 -e '
        CREATE TABLE  test.lp1013978p
        (
             i int NOT NULL,
             j int DEFAULT NULL,
             PRIMARY KEY (i),
             KEY j (j)
        ) ENGINE=InnoDB
    '
}

create_FK_child_NON_UNIQ()
{
    $MYSQL --port=$port_0 -e '
        CREATE TABLE  test.lp1013978c
        (
             i int NOT NULL,
             f int DEFAULT NULL,
             PRIMARY KEY (i),
             KEY fk (f),
             FOREIGN KEY (f) REFERENCES test.lp1013978p (j)
        ) ENGINE=InnoDB
    '
}

create_FK_child_NON_UNIQ_ON_UPDATE_CASCADE()
{
    $MYSQL --port=$port_0 -e '
        CREATE TABLE  test.lp1013978c
        (
             i int NOT NULL,
             f int DEFAULT NULL,
             PRIMARY KEY (i),
             KEY fk (f),
             FOREIGN KEY (f) REFERENCES test.lp1013978p (j) ON UPDATE CASCADE
        ) ENGINE=InnoDB
    '
}

create_DB_PK()
{
    create_FK_parent_PK
    create_FK_child_PK
}

create_DB_NON_UNIQ()
{
    create_FK_parent_NON_UNIQ
    create_FK_child_NON_UNIQ
}

create_DB_NON_UNIQ_ON_UPDATE_CASCADE()
{
    create_FK_parent_NON_UNIQ
    create_FK_child_NON_UNIQ_ON_UPDATE_CASCADE
}

#
# Test A procedures
#
A_cleanup()
{ 
    $MYSQL --port=$port_0 -e "
        DROP TABLE IF EXISTS test.lp1013978c;
    "
    $MYSQL --port=$port_0 -e "
        DROP TABLE IF EXISTS test.lp1013978p;
    " 
}
A_createdb()
{ 
    A_cleanup
    create_DB_NON_UNIQ_ON_UPDATE_CASCADE

    $MYSQL --port=$port_0 -e "
        INSERT INTO test.lp1013978p VALUES (1,1);
        INSERT INTO test.lp1013978c VALUES (100,1);
    " 
}

A_inserter()
{
    local port=$1
    for (( i=1; i<=$ROUNDS; i++ )); do
	$MYSQL --port=$port -e "
          INSERT INTO test.lp1013978p values (2, $i); 
          UPDATE test.lp1013978p SET j=$(($i + 1)) where i=1; 
          DELETE FROM test.lp1013978p WHERE i=2;
        " 2>&1
    done
}

#
# Test phase B procedures
#
B_cleanup()
{ 
    A_cleanup
}

B_createdb()
{ 
    B_cleanup
    create_DB_NON_UNIQ_ON_UPDATE_CASCADE
}

B_inserter()
{
    local port=$1
    for (( i=1; i<=$ROUNDS; i++ )); do
	$MYSQL --port=$port -e "
          INSERT INTO test.lp1013978p values ($i, $i); 
          INSERT INTO test.lp1013978c values ($i, $i); 
        " 2>&1
    done
}

#
# Test phase C procedures
#
C_cleanup()
{
    A_cleanup
}

C_createdb()
{ 
    #
    # we can re-use tables from test phase B
    #
    C_cleanup

    create_DB_NON_UNIQ_ON_UPDATE_CASCADE
    B_inserter  $port_0 
}

C_deleter()
{
    local port=$1
    for (( i=1; i<=$ROUNDS; i++ )); do
	$MYSQL --port=$port -e "
          DELETE FROM test.lp1013978c WHERE i=$i; 
          DELETE FROM test.lp1013978p WHERE i=$i; 
        " 2>&1
    done
}

#
# Test phase D procedures, FK by PK
#
D_cleanup()
{
    A_cleanup
}

D_createdb()
{ 
    D_cleanup

    create_DB_PK

    # populate by phase B inserts
    B_inserter  $port_0 
}

D_deleter()
{
    C_deleter $port_0
}

#
# Test phase E procedures
#
E_cleanup()
{ 
    A_cleanup
}

E_createdb()
{ 
    E_cleanup
    create_DB_NON_UNIQ_ON_UPDATE_CASCADE
}

E_inserter()
{
    local port=$1
    for (( i=1; i<=$ROUNDS; i++ )); do
	$MYSQL --port=$port -e "
          INSERT INTO test.lp1013978p values ($i, $i); 
          INSERT INTO test.lp1013978c values ($i, $i); 
          DELETE FROM test.lp1013978c WHERE i=$i;
          DELETE FROM test.lp1013978p WHERE i=$i;
        " 2>&1
    done
}


run_test()
{
    phase=$1
    create=$2
    process=$3
    cleanup=$4

    echo "##################################################################"
    echo "##             test phase $phase"
    echo "##################################################################"
    echo
    echo "Creating database..."
    eval $create

    echo "Starting test process..."
    eval $process $port_0 &
    pid=$!

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

    eval $cleanup

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


run_test A A_createdb A_inserter A_cleanup
run_test B B_createdb B_inserter B_cleanup
run_test C C_createdb C_deleter  C_cleanup
run_test D D_createdb D_deleter  D_cleanup
run_test E E_createdb E_inserter E_cleanup

echo
echo "Done!"
echo

stop_node 0
stop_node 1

exit $SUCCESS

