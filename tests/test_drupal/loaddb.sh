#!/bin/sh

TEST_HOME=$(cd $(dirname $0); pwd -P)
MYSQL_HOST=${MYSQL_HOST:-"127.0.0.1"}
MYSQL_PORT=${MYSQL_PORT:-"3306"}
MYSQL_USER=${MYSQL_USER:-"root"}
MYSQL_PSWD=${MYSQL_PSWD:-"rootpass"}

MYSQL_CMD="mysql -u$MYSQL_USER -p$MYSQL_PSWD -h$MYSQL_HOST -P$MYSQL_PORT "

set -e

#Prepare drupaldb database
$MYSQL_CMD -e \
"GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, INDEX, ALTER, CREATE
TEMPORARY TABLES, LOCK TABLES
ON drupaldb.*
TO 'drupal'@'%'  IDENTIFIED BY 'password'"

zcat $TEST_HOME/drupaldb.sql.gz | $MYSQL_CMD


# Prepare drupal6 database
$MYSQL_CMD -e \
"GRANT SELECT, INSERT, UPDATE, DELETE, CREATE, DROP, INDEX, ALTER, CREATE
TEMPORARY TABLES, LOCK TABLES
ON drupal6.*
TO 'drupal'@'%'  IDENTIFIED BY 'password'"

zcat $TEST_HOME/drupal6.sql.gz | $MYSQL_CMD

$MYSQL_CMD -e "FLUSH PRIVILEGES"

