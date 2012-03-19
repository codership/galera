#!/bin/bash

##
# lp:959512
# https://bugs.launchpad.net/codership-mysql/+bug/959512
#
# Description:
#
# Running the statements described below caused slave nodes to crash.
# The reson was that multi statement transaction produced empty write
# set, which caused IO cache not to be reset at transaction cleanup.
# Following INSERT INTO foo failed then to replicate because IO cache
# remained in read mode, following slave crashes on final UPDATE foo
# because row to be updated could not be found.
#
##

set -e

declare -r DIST_BASE=$(cd $(dirname $0)/../..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"
# . $SCRIPTS/jobs.sh
# . $SCRIPTS/action.sh
# . $SCRIPTS/kill.sh
# . $SCRIPTS/misc.sh

# restart the cluster
$SCRIPTS/command.sh restart


STMTS="DROP TABLE IF EXISTS variable;
 DROP TABLE IF EXISTS foo;
 CREATE TABLE variable (
   name varchar(128) NOT NULL DEFAULT '' COMMENT 'The name of the variable.',
   value longblob NOT NULL COMMENT 'The value of the variable.',
    PRIMARY KEY (name)
 ) ENGINE=InnoDB DEFAULT CHARSET=utf8
   COMMENT='Named variable/value pairs created by Drupal core or any...';
 CREATE TABLE foo (a int);
 INSERT INTO variable (name, value) VALUES ('menu_expanded', 'a:0:{}');
 START TRANSACTION;
 SELECT 1 AS expression FROM variable variable
   WHERE ( (name = 'menu_expanded') )  FOR UPDATE;
 UPDATE variable SET value='a:0:{}' WHERE ( (name = 'menu_expanded') );
 COMMIT;
 INSERT INTO foo VALUES (1);
 UPDATE foo SET a = 2 WHERE a = 1;"

MYSQL="mysql --batch --silent -uroot -prootpass -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]}"
$MYSQL -Dtest -e "$STMTS"

$SCRIPTS/command.sh check

if test $? != 0
then
    echo "test failed"
    exit 1
fi

$SCRIPTS/command.sh stop
