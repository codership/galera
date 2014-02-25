#!/bin/bash -eu
##
#
# lp:1019473
# https://bugs.launchpad.net/codership-mysql/+bug/1019473
#
# BUG BACKGROUND:
#
# wsrep generates certification keys for tables with no PK nor UK, by
# taking a MD5 digest over the whole row. This makes it possible to replicate
# and control PA for key-less tables. Unfortunately this implementation
# had a bug which caused such row key hashing to be non deterministic, if row 
# contains certain binary types (blob or text at least are vulnerable)
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
# If bug is present, slave will crash for not being able to delete a rowk
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
echo "##             regression test for lp:1019473"
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
USERS=3

echo "rounds=$ROUNDS"
echo "rows=$ROWS"
echo "users=$USERS"

insert()
{
    PORT=$1
    for r in $(seq 1 $ROUNDS); do
	for i in $(seq 1 $ROWS); do
	    $MYSQL --port=$PORT -e "
               DELETE FROM lp1019473 WHERE fid=$i AND uid=$i; 
               INSERT INTO lp1019473 VALUES ($i,$i,'C-$1')" || true
	done;
    done
}

createdb()
{
    $MYSQL -P $port_0 -e "drop table if exists lp1019473;";
    $MYSQL -P $port_0 -e "CREATE TABLE lp1019473 (
      fid int(10) unsigned DEFAULT 0,
      uid int(10) unsigned DEFAULT 0,
      value text,
      KEY uid (uid),
      KEY fid (fid)
    ) ENGINE=InnoDB DEFAULT CHARSET=utf8"
}

cleandb()
{ 
    PORT=$1
    $MYSQL --port $PORT -e "
        DROP TABLE IF EXISTS test.lp1019473;"
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

for u in $(seq 0 $USERS); do
    insert $port_0 &
    insert $port_1 &
done

wait

../../scripts/command.sh check

cleandb $port_0

echo
echo "Done!"
echo

../../scripts/command.sh stop

exit 0

