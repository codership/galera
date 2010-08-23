#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh

# big - for IO bound workload, small - for CPU bound workload
SIZE=${SIZE:-"small"}
TRX_LEN=${TRX_LEN:-"1"}

#restart # cluster restart should be triggered by user

# Auto gernerate does not seem to work with mysqlslap 5.1.37
#	  --number-int-cols=5 --number-char-cols=3 \
#	  --auto-generate-sql-add-autoincrement \
#	  --auto-generate-sql-guid-primary \
#	  --auto-generate-sql-secondary-indexes=1 \
#	  --auto-generate-sql-unique-write-number=10000 \
#	  --auto-generate-sql-write-number=$TRX_LEN \
#	  --auto-generate-sql-load-type=write \
#         --detach=$(($TRX_LEN * 4)) # detach is broken, m'kay?
mysqlslap --user=$DBMS_TEST_USER --password=$DBMS_TEST_PSWD \
          --host=$DBMS_HOST --port=$DBMS_PORT \
          --delimiter=';' --create-schema=$DBMS_TEST_SCHEMA \
	  --create="create_$SIZE.sql" --query="insert_$SIZE.sql" \
          --commit=$TRX_LEN \
	  --concurrency=$DBMS_CLIENTS --number-of-queries=1000000 \
	  --verbose $@

#check || check || exit $? there is no point to check consistency after mysqlslap -
# it deletes the table
#exit 0
#
