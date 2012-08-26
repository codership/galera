#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

#declare -r SCRIPTS="$DIST_BASE/scripts"
#. $SCRIPTS/jobs.sh
#. $SCRIPTS/action.sh
#. $SCRIPTS/kill.sh
#. $SCRIPTS/misc.sh

SCHEMA="test"
TABLE="causal"
USER="test"
PSWD="testpass"

for i in 0 1 2
do
    NODE[$i]="-h${NODE_INCOMING_HOST[$i]} -P${NODE_INCOMING_PORT[$i]}"
done

MYSQL="mysql -u$USER -p$PSWD"

DROP_TABLE="DROP TABLE IF EXISTS $SCHEMA.$TABLE;"

echo $DROP_TABLE | $MYSQL ${NODE[2]}

CREATE_TABLE=\
"CREATE TABLE $SCHEMA.$TABLE (c1 INT AUTO_INCREMENT PRIMARY KEY, c2 INT)"

echo $CREATE_TABLE | $MYSQL ${NODE[2]}
sleep 1

echo "INSERT INTO $SCHEMA.$TABLE VALUES (1, 0)" | $MYSQL ${NODE[1]}

failure=0
for (( i=1; i<=10000; i++ ))
do
    echo "UPDATE $SCHEMA.$TABLE SET c2 = $i WHERE c1 = 1;" \
        | $MYSQL ${NODE[1]}
    echo "SELECT c2 FROM $SCHEMA.$TABLE WHERE c1 = 1;" \
        | $MYSQL ${NODE[0]} | grep ^$i >/dev/null || failure=$(( $failure + 1 ))
done

#[ $failure -ne 0 ] &&
echo "Causal failures: $failure"

exit $failure
#
