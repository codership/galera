#!/bin/bash -ue

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

echo "##################################################################"
echo "##             regression test for lp:900816"
echo "##################################################################"
echo "restarting cluster"
$SCRIPTS/command.sh restart

PORT=${NODE_INCOMING_PORT[0]}
HOST=${NODE_INCOMING_HOST[0]}
USER=$DBMS_TEST_USER
PSWD=$DBMS_TEST_PSWD
DB=test
TABLE=nopk
TRIES=1000

MYSQL="mysql -u$USER -p$PSWD -h$HOST -P$PORT $DB"

CREATE="
DROP TABLE IF EXISTS $DB.$TABLE;
CREATE TABLE $DB.$TABLE (i INT, j INT);"

INSERT="INSERT INTO $DB.$TABLE VALUES (1, 0),(2,0);"

UPDATE="UPDATE $DB.$TABLE SET j=j+1;"

DELETE="DELETE FROM $DB.$TABLE;"

echo $CREATE | $MYSQL

for i in $(seq 1 $TRIES)
do
    echo $INSERT | $MYSQL
    echo $UPDATE | $MYSQL >> update.log 2>&1 &
    echo $DELETE | $MYSQL
done

echo "$i tries passed"

$SCRIPTS/command.sh check
$SCRIPTS/command.sh stop