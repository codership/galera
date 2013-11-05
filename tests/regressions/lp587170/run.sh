#!/bin/bash -e
#
# lp:587170
#
# Verify that ALTER TABLE ... AUTO_INCREMENT produces consistent results.
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

echo "restarting cluster"
../../scripts/command.sh restart


MYSQL="mysql --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD --host=${NODE_INCOMING_HOST[0]} --port=${NODE_INCOMING_PORT[0]} -Dtest"

$MYSQL -e "DROP TABLE IF EXISTS lp587170;
           CREATE TABLE lp587170 (i INT);
           INSERT INTO lp587170 VALUES (1),(2),(3),(4),(5);
           ALTER TABLE lp587170 ADD id INT NOT NULL AUTO_INCREMENT PRIMARY KEY FIRST;"

../../scripts/command.sh check


$MYSQL -e "DROP TABLE lp587170"
