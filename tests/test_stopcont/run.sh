#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/signal.sh
. $SCRIPTS/misc.sh

TRIES=${1:-"-1"} # -1 stands for indefinite loop
MIN_PAUSE=${2:-"3"}
PAUSE_RND=${3:-"20"}

#restart # cluster restart should be triggered by user

# Start load
SQLGEN=${SQLGEN:-"$DIST_BASE/bin/sqlgen"}
LD_PRELOAD=$GLB_PRELOAD \
DYLD_INSERT_LIBRARIES=$GLB_PRELOAD \
DYLD_FORCE_FLAT_NAMESPACE=1 \
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

pause 10 10
consistency_check $sqlgen_pid
# kills a node and restarts it after a while
cycle()
{
    local -r node=$1
    local -r node_id=${NODE_ID[$node]}

    echo "Signaling node $node_id with STOP... "
    signal_node STOP $node

    pause $MIN_PAUSE $PAUSE_RND

    echo "Signaling node $node_id with CONT..."
    signal_node CONT $node

    sleep 10 # this pause needed to get node to notice that it lost the PC
             # if it did...

    wait_node_state $node 4 # 4 - SYNCED
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

    consistency_check $sqlgen_pid
    pause 2

    node=$(( ( node + 1 ) % node_num ))
done

exit
