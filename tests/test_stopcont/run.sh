#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/signal.sh

TRIES=${1:-"-1"} # -1 stands for indefinite loop

#restart # cluster restart should be triggered by user

# Start load
SQLGEN=${SQLGEN:-"$DIST_BASE/bin/sqlgen"}
$SQLGEN --user $DBMS_TEST_USER --pswd $DBMS_TEST_PSWD --host $DBMS_HOST \
        --port $DBMS_PORT --users $DBMS_CLIENTS --duration 999999999 \
        --stat-interval 99999999 >/dev/null 2>$BASE_RUN/seesaw.err &
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

# Returns variable amount of seconds to sleep
pause()
{
    local min_sleep=1
    local var_sleep=10
    local p=$(( $RANDOM % var_sleep + min_sleep ))

    echo "Sleeping for $p sec."
    sleep $p
}

# Pauses load to perform consistency check
consistency_check()
{
    local ret=0

    kill -STOP $sqlgen_pid
    sleep 1
    check || check || ret=$?
    kill -CONT $sqlgen_pid # will receive SIGHUP in the case of script exit
    return $ret
}

pause
consistency_check
# kills a node and restarts it after a while
cycle()
{
    local -r node=$1
    local -r node_id=${NODE_ID[$node]}

    echo "Signaling node $node_id with STOP... "
    signal_node STOP $node

    pause

    echo "Signaling node $node_id with CONT..."
    signal_node CONT $node
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
    consistency_check

    node=$(( ( node + 1 ) % node_num ))
done

exit
