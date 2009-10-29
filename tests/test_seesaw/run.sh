#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh

TRIES=${1:-"-1"} # -1 stands for indefinite loop

gcs_address()
{
    local node=$1
    
    case "$GCS_TYPE" in
    "gcomm")
	local peer=$(( $node - 1 )) # select previous node as connection peer

        if [ $peer -lt 0 ]; then peer=$NODE_MAX; fi # rollover
	
        echo "gcomm://${NODE_GCS_HOST[$peer]}:${NODE_GCS_PORT[$peer]}"
	;;
    "vsbes")
        echo "vsbes://$VSBES_ADDRESS"
	;;
    *)
        return 1
	;;
    esac
}

# restart nodes in group mode
echo "### Initial restart ###"
SECONDS=0
for node in $NODE_LIST
do
    echo "Starting ${NODE_ID[$node]}"
    if [ $node -eq 0 ]
    then
        # must make sure 1st node completely operational
	case "$GCS_TYPE" in
	"gcomm") restart_node "-g gcomm://" 0 ;;
	"vsbes") restart_node "-g vsbes://$VSBES_ADDRESS" 0 ;;
	esac
    else
	restart_node "-g $(gcs_address $node)" $node &
    fi
done

wait_jobs

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
    local ret

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

    local var_kill=3
    if test $(( $RANDOM % var_kill )) = 0
    then
        echo "Killing node $node_id..."
        kill_node $node
    else
        echo "Stopping node $node_id..."
        stop_node $node
    fi

    pause

    consistency_check

    echo "Restarting node $node_id..."
    restart_node "-g $(gcs_address $node)" $node
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
