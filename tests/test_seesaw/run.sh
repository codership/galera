#!/bin/bash -eu

declare -r TEST_BASE=$(cd $(dirname $0)/..; pwd -P)

. $TEST_BASE/conf/main.conf

. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh

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
        echo "vsbes://$VSBES_ADDR"
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
        restart_node "-g gcomm://" 0
    else
	restart_node "-g $(gcs_address $node)" $node &
    fi
done

wait_jobs

# Returns variable amount of seconds to sleep
pause()
{
    local min_sleep=10
    local var_sleep=16
    local p=$(( $RANDOM % var_sleep + min_sleep ))
    
    echo "Sleeping for $p sec."
    sleep $p
}

# kills a node and restarts it after a while
cycle()
{
    local node=$1
    local node_id=${NODE_ID[$node]}

    echo "Killing node $node_id..."
    kill_node $node
    
    pause

    echo "Restarting node $node_id..."
    restart_node "-g $(gcs_address $node)" $node
}


node=0
node_num=$(( $NODE_MAX + 1 ))

echo "### Looping over $node_num nodes ###"

while [ 1 ]
do
    pause
    
    node=$(( ( node + 1 ) % node_num ))
    
    cycle $node
done
