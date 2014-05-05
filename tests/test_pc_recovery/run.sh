#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh

TRIES=${1:-"-1"} # -1 stands for indefinite loop

# Start load
SQLGEN=${SQLGEN:-"$DIST_BASE/bin/sqlgen"}
LD_PRELOAD=$GLB_PRELOAD \
DYLD_INSERT_LIBRARIES=$GLB_PRELOAD \
DYLD_FORCE_FLAT_NAMESPACE=1 \
$SQLGEN --user $DBMS_TEST_USER --pswd $DBMS_TEST_PSWD --host $DBMS_HOST \
        --port $DBMS_PORT --users $DBMS_CLIENTS --duration 999999999 \
        --stat-interval 99999999 --sess-min 999999 --sess-max 999999 \
        --rollbacks 0.1 \
        >/dev/null 2>$BASE_RUN/pc_recovery.err &
declare -r sqlgen_pid=$!
disown # forget about the job, disable waiting for it

term=0
terminate()
{
    echo "Terminating"
    term=1
}

trap terminate SIGINT SIGTERM SIGHUP SIGPIPE

trap "kill $sqlgen_pid" EXIT

pause 5 5
consistency_check $sqlgen_pid

GCOMM_EXTRA_PARAMS="pc.recovery=1"
# kill all and restart them
cycle()
{
    kill_all

    SECONDS=0 # for wait_jobs
    for node in $NODE_LIST
    do
        echo "Restarting ${NODE_ID[$node]}"
        stop_node $node
        # starts with pc.recovery=1
        start_node "-g $(gcs_address $node)" $node &
    done
    wait_jobs
}

try=0

while [ $try -ne $TRIES ]
do
    ! let try++ # ! - to protect from -e

    echo -e "\nTry ${try}/${TRIES}\n"

    cycle

    pause 3 3
    consistency_check $sqlgen_pid
done

exit
