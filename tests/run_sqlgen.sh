#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0); pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

check()
{
    consistency_check $sqlgen_pid
}

#trap check SIGINT
#node=1
#DBMS_HOST=${NODE_INCOMING_HOST[$node]}
#DBMS_PORT=${NODE_INCOMING_PORT[$node]}

# Start load
SQLGEN=${SQLGEN:-"$DIST_BASE/bin/sqlgen"}
LD_PRELOAD=$GLB_PRELOAD \
DYLD_INSERT_LIBRARIES=$GLB_PRELOAD \
DYLD_FORCE_FLAT_NAMESPACE=1 \
$SQLGEN --user $DBMS_TEST_USER --pswd $DBMS_TEST_PSWD --host $DBMS_HOST \
        --port $DBMS_PORT --users $DBMS_CLIENTS --duration 999999999 \
        --stat-interval 20 --sess-min 999999 --sess-max 999999 \
        --rollbacks 0.1
#        >/dev/null 2>$BASE_RUN/seesaw.err &
#declare -r sqlgen_pid=$!
#fg

