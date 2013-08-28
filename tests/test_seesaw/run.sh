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
KILL_RATE=${KILL_RATE:-"3"}
SST_RATE=${SST_RATE:-"2"}

#restart # cluster restart should be triggered by user

# Start load
SQLGEN=${SQLGEN:-"$DIST_BASE/bin/sqlgen"}
LD_PRELOAD=$GLB_PRELOAD \
DYLD_INSERT_LIBRARIES=$GLB_PRELOAD \
DYLD_FORCE_FLAT_NAMESPACE=1 \
$SQLGEN --user $DBMS_TEST_USER --pswd $DBMS_TEST_PSWD --host $DBMS_HOST \
        --port $DBMS_PORT --users $DBMS_CLIENTS --duration 999999999 \
        --stat-interval 99999999 --sess-min 999999 --sess-max 999999 \
        --rollbacks 0.1 \
        >/dev/null 2>$BASE_RUN/seesaw.err &
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

# kills a node and restarts it after a while
cycle()
{
    local -r node=$1
    local -r node_id=${NODE_ID[$node]}

    local pause_var=10
    local var_kill=$(( $RANDOM % $KILL_RATE ))

    if [ $var_kill -eq 0 ]
    then
        echo "Killing node $node_id..."
        kill_node $node
        pause_var=$((($RANDOM % 8) + 1))
    else
        echo "Stopping node $node_id..."
        stop_node $node
    fi

    pause 0 $pause_var

    if test $pause_var -gt 5
    then
        consistency_check $sqlgen_pid
    else
        echo "skipped consistency check due to fast recycle"
    fi

    echo "Restarting node $node_id..."

    local skip_recovery_opt=""
    if [ $var_kill -eq 0 ] && [ $(($RANDOM % $SST_RATE)) -eq 0 ]
    then
        echo "Enforcing SST"
        skip_recovery_opt="--skip-recovery --start-position '00000000-0000-0000-0000-000000000000:-2'"
    fi

    if test $pause_var -gt 3
    then
        restart_node "-g $(gcs_address $node) $skip_recovery_opt" $node
    else
        stop_node $node || : # just in case the process is still lingering
        restart_node "-g $(gcs_address $node) $skip_recovery_opt" $node
    fi
}

node=0
node_num=$(( $NODE_MAX + 1 ))
try=0

echo "### Looping over $node_num nodes ###"

while [ $try -ne $TRIES ]
do
    ! let try++ # ! - to protect from -e

    echo -e "\nTry ${try}/${TRIES}\n"

    cycle $node

    pause
    consistency_check $sqlgen_pid

    node=$(( ( node + 1 ) % node_num ))
done

exit
