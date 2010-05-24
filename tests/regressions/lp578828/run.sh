#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

create_stmt="CREATE TABLE test.lp578828 ("\
"id INT(10) UNSIGNED NOT NULL AUTO_INCREMENT,"\
"email VARCHAR(255) NOT NULL,"\
"PRIMARY KEY (id),"\
"UNIQUE KEY email (email)"\
") ENGINE=InnoDB DEFAULT CHARSET=utf8;"

insert_stmt="INSERT INTO test.lp578828 (email) VALUES ('test@test.tld');"

MYSQL="mysql --user=$DBMS_ROOT_USER --password=$DBMS_ROOT_PSWD --host=${NODE_INCOMING_HOST[0]} --port=${NODE_INCOMING_PORT[0]}"

$MYSQL -e "DROP TABLE IF EXISTS test.lp578828"
$MYSQL -e "$create_stmt"
$MYSQL -e "$insert_stmt"
$MYSQL -e "$insert_stmt"

$MYSQL -e "DROP TABLE test.lp578828"

exit $?
