#!/bin/bash 


##
#
# lp:917416
# https://bugs.launchpad.net/codership-mysql/+bug/917416
#
# BUG BACKGROUND:
#
# BF processing can kill local transactins doing only reads. One situation 
# where this happens very probably is when DDL is run under TO isolation
# and some local transaction tries to read from mysql database.
# Native MySQL uses thr locks to serialize such access and reads are not 
# aborted, so we have a transaparency issue here. And there are use cases
# especially related to SP's and functions where proc table is consulted 
# for available routines  SELECT ROUTINE_TYPE FROM INFORMATION_SCHEMA.ROUTINES 
# WHERE SPECIFIC_NAME = '<routine name>' etc...
#
# TEST SETUP:
#
# This test starts two nodes to play with
# 
#
# TEST PROCESSES:
#
# Test creates one DDL process and one mysql schema reader process:
# reader:    keeps on reading mysql.user table
#            the two inserters target separate cluster nodes
#            This way the dropper will conflict both with local state 
#            and slave inserts
# DDL:       DDL process creates and drops one user to cause locking
#            needs for mysql.user table 
#
# SUCCESS CRITERIA
#
# If bug is present, the select process will encounter many deadlocks
# due to BF aborting
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
echo "##             regression test for lp:917416"
echo "##################################################################"
echo "stopping cluster"
../../scripts/command.sh stop
echo
echo "starting node0, node1..."
../../scripts/command.sh start_node "-d -g gcomm://$(extra_params 0)" 0
../../scripts/command.sh start_node "-d -g $(gcs_address 1)" 1

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=$DBMS_HOST test "

declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

declare -r ROUNDS=10000
declare -r ERROR_LIMIT=10
declare errors=0
declare success=0

#######################
#       Processes
########################
send_sql()
{
    local port=$1
    local type=$2
    local rounds=$3
    local sleep=$4
    local SQL=$5

    for (( i=1; i<$rounds; i++ )); do
	$MYSQL --port=$port -e "$SQL" 2>&1 > /tmp/lp917416.out 
	ret=$?
	[ $ret -ne 0 ]                      && \
	    echo "SELECT failed ($ret $i) " && \
	    cat /tmp/lp917416.out           && \
	    success=1                       && \
            errors=$(( $errors + 1 ))

       [ $errors -gt $ERROR_LIMIT ]  && echo "$type STOPPED" && exit 1
       sleep $sleep
    done
}

reader_m()
{
    local port=$1
    send_sql $port "SELECT-M" $ROUNDS 0 \
	'select user,host from mysql.user limit 1'
    echo "mysql reader complete `date`"
}

ddl_m()
{
    send_sql $port_0 "DDL-M" $ROUNDS 0 \
	'CREATE USER seppo; DROP USER seppo; FLUSH PRIVILEGES'
    echo "mysql DDL complete `date`"
}

#
# InnODB reader and DDL
#
reader_i()
{
    local port=$1
    send_sql $port "SELECT-I" $ROUNDS 0 'select i from test.t917416 limit 1'
    echo "innodb reader complete `date`"
}

ddl_i()
{
    send_sql $port_0 "DDL-I" $(( $ROUNDS / 20 )) 0.1 \
     'ALTER TABLE t917416 ADD COLUMN (j int); ALTER TABLE t917416 DROP COLUMN j'
    echo "innodb DDL complete `date`"
}

#######################
#       Main, init
########################

echo "Creating database..."
$MYSQL --port=$port_0 -e 'DROP USER seppo; FLUSH PRIVILEGES' 2>&1 > /dev/null
$MYSQL --port=$port_0  -e 'DROP TABLE IF EXISTS test.t917416'
$MYSQL --port=$port_0  -e 'CREATE TABLE test.t917416(i int) engine=innodb'

#######################
#       Phase 1
########################
echo
echo "### Phase 1, SELECT & DDL => node0 ###"
echo

echo "starting reader for port $port_0"
reader_m $port_0 &
declare reader_pid=$!

echo "starting ddl"
ddl_m &
declare ddl_pid=$!

echo "waiting phase 1 load to end (PIDs: $ddl_pid $reader_pid)"
wait
echo 
echo "Processlist now:"
$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
echo

#######################
#       Phase 2 
########################
echo
echo "### Phase 2, InnoDB SELECT & ALTER => node0 ###"
echo

errors=0

echo "starting reader for port $port_0"
reader_i $port_0 &
reader_pid=$!

echo "starting ddl"
ddl_i &
ddl_pid=$!

echo "waiting phase 2 load to end (PIDs: $ddl_pid $reader_pid)"
wait

echo 
echo "Processlist now:"
$MYSQL --port=$port_0 -e 'SHOW PROCESSLIST'
echo
#######################
#       Cleanup
########################
$MYSQL --port=$port_0 -e 'DROP TABLE t917416'	;

../../scripts/command.sh stop_node 0
../../scripts/command.sh stop_node 1

exit $success