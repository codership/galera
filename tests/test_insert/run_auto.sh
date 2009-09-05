#!/bin/sh

set -e
set -u

TEST_BASE=$(cd $(dirname $0)/..; pwd -P)
. $TEST_BASE/conf/main.conf

TRX_LEN=10

# Auto gernerate does not seem to work with mysqlslap 5.1.37
#	  --number-int-cols=5 --number-char-cols=3 \
#	  --auto-generate-sql-add-autoincrement \
#	  --auto-generate-sql-guid-primary \
#	  --auto-generate-sql-secondary-indexes=1 \
#	  --auto-generate-sql-unique-write-number=10000 \
#	  --auto-generate-sql-write-number=$TRX_LEN \
#	  --auto-generate-sql-load-type=write \
mysqlslap --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD \
          --host=$DBMS_HOST --port=$DBMS_PORT \
          --delimiter=';' --create-schema=$DBMS_TEST_SCHEMA \
	  --create="create_auto.sql" --query="insert_auto.sql" \
          --commit=$TRX_LEN --detach=$(($TRX_LEN * 4)) \
	  --concurrency=30 --number-of-queries=10000 \
	  --verbose --csv

#
