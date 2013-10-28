#!/bin/bash -e
#
# lp:1206129
#
# Verify that LOAD DATA INFILE is executed and data is replicated
# over cluster.
#
# This test assumes that if the LOAD DATA would be processed by single
# 500k row commit, it would take several seconds process that on slaves.
# On the other hand it is assumed that if LOAD DATA splitting works as
# expected, Galera flow control limits master load rate and final 10k
# batches are committed nearly synchronously.
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

rm -f /tmp/lp1206129.dat

$MYSQL -e "DROP TABLE IF EXISTS lp1206129;
           DROP PROCEDURE IF EXISTS populate;
           CREATE TABLE lp1206129 (a INT PRIMARY KEY);
           DELIMITER ;;
           CREATE PROCEDURE populate()
           BEGIN
             DECLARE i INT DEFAULT 0;
             WHILE i < 500000 DO
               INSERT INTO lp1206129 VALUES (i);
               SET i = i + 1;
             END WHILE;
           END;;
           DELIMITER ;
           BEGIN;
           CALL populate();
           SELECT * FROM lp1206129 INTO OUTFILE '/tmp/lp1206129.dat';
           COMMIT;
           SELECT SLEEP(0);
           TRUNCATE lp1206129;
           SELECT SLEEP(0);
           LOAD DATA INFILE '/tmp/lp1206129.dat' INTO TABLE lp1206129;"

rm /tmp/lp1206129.dat

sleep 2

 ../../scripts/command.sh check

$MYSQL -e "DROP TABLE lp1206129;
           DROP PROCEDURE populate;"