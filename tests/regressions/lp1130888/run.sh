#!/bin/bash 
##
#
# lp:1130888
# https://bugs.launchpad.net/codership-mysql/+bug/1130888
#
# BUG BACKGROUND:
#
# wsrep appends shared key for FK parent references, when child table 
# is beign modified. This FK parent reference requires a read in parent table,
# and essentially new shared lock on paretn table row. Now this extra lock can
# enter in deadlock, and bad news is that existing code is note prepared to see
# deadlocks at this point. This unresolved deadlock left the server in hanging 
# state. 
#
# TEST SETUP:
#   - Two nodes are used in multi-master mode
#   - child/parent tables are created with cascading delete option
#   - node1 will get transactions modifying parent table
#   - node2 will get transactions modifying child table
#
# SUCCESS CRITERIA
#
# If bug is present, one node will hang
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
echo "##             regression test for lp:1130888"
echo "##################################################################"
echo "stopping cluster"
../../scripts/command.sh stop
echo
echo "starting node0, node1..."
../../scripts/command.sh start "-d  --slave_threads 4"

declare -r port_0=${NODE_INCOMING_PORT[0]}
declare -r port_1=${NODE_INCOMING_PORT[1]}

MYSQL="mysql --batch --silent --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=$DBMS_HOST test "

ROUNDS=1000
ROWS=5

echo "rounds=$ROUNDS"
echo "rows=$ROWS"

insert()
{
k    PORT=$1
    for j in $(seq 1 $ROWS); do
	$MYSQL --port=$PORT -e "INSERT INTO TRANSACTION(ID) VALUES ('$j')";
	$MYSQL --port=$PORT -e "INSERT INTO TRANSACTIONLOG(IDTRANSACTION,IDMESSAGE) VALUES ('$j','$j')";
    done
 
}

trxlog()
{
    PORT=$1
    for i in $(seq 1 $ROUNDS); do
	for j in $(seq 1 $ROWS); do
	    $MYSQL --port=$PORT -e "DELETE FROM TRANSACTIONLOG WHERE IDTRANSACTION = '$j'; INSERT INTO TRANSACTIONLOG(IDTRANSACTION,IDMESSAGE) VALUES ('$j','$j')";
	done 
    done
}

trx()
{
    PORT=$1
    for i in $(seq 1 $ROUNDS); do
	for j in $(seq 1 $ROWS); do
	    $MYSQL --port=$PORT -e "DELETE FROM TRANSACTION WHERE ID = '$j'; INSERT INTO TRANSACTION(ID) VALUES ('$j'); INSERT INTO TRANSACTIONLOG(IDTRANSACTION,IDMESSAGE) VALUES ('$j','$j')";
	done 
    done
}

createdb()
{
    PORT=$1
    $MYSQL --port=$PORT -e "CREATE TABLE IF NOT EXISTS TRANSACTION (
     ID varchar(25) NOT NULL,
     MARCA datetime DEFAULT NULL,
     TIMEOUT int(11) DEFAULT NULL,
     DESCRIPCION varchar(255) DEFAULT NULL,
      PRIMARY KEY (ID)
    ) ENGINE=InnoDB DEFAULT CHARSET=latin1";
   
    $MYSQL --port=$PORT -e "CREATE TABLE IF NOT EXISTS TRANSACTIONLOG (
      IDTRANSACTION varchar(25) NOT NULL,
      IDMESSAGE varchar(25) NOT NULL,
      TIPO int(11) DEFAULT NULL,
      FECHA datetime DEFAULT NULL,
      PRIMARY KEY (IDTRANSACTION,IDMESSAGE),
      UNIQUE KEY IX_EBITRANSACTIONLOG (IDMESSAGE),
      CONSTRAINT FK_EBITRANSACTIONLOG_EBITRAN FOREIGN KEY (IDTRANSACTION) REFERENCES TRANSACTION (ID) ON DELETE CASCADE ON UPDATE CASCADE
    ) ENGINE=InnoDB DEFAULT CHARSET=latin1";
    $MYSQL --port=$PORT -e "TRUNCATE TRANSACTIONLOG";
    $MYSQL --port=$PORT -e "TRUNCATE TRANSACTION";
}

cleandb()
{ 
    PORT=$1
    $MYSQL --port=$PORT -e "
        DROP TABLE IF EXISTS test.TRANSACTION;        
        DROP TABLE IF EXISTS test.TRANSACTIONLOG"
}


#########################################################
#
# Test begins here
#
#########################################################

threads=$($MYSQL --port=$port_1 -e "SHOW VARIABLES LIKE 'wsrep_slave_threads'")

echo "applier check: $threads"
[ "$threads" = "wsrep_slave_threads	4" ] || { echo "NOT ENOUGH SLAVES"; exit 1; }

INITIAL_SIZE=`$MYSQL -P $port_0 -e "SHOW STATUS LIKE 'wsrep_cluster_size';" | cut -f 2`
echo "Initial cluster size: $INITIAL_SIZE"



createdb

insert $port_0
trxlog $port_0 &
trx $port_1 &

wait

../../scripts/command.sh check

cleandb $port_0

echo
echo "Done!"
echo

../../scripts/command.sh stop

exit 0



