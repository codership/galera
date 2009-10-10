#
# Routines to remove test distribution from nodes
#

remove_cmd()
{
    local node=${@:$#}
    local dir="${NODE_TEST_DIR[$node]}"
    echo -n "rm -rf \"$dir\""
}

_remove_local()
{
    local node=$1

    eval "$(remove_cmd $node)"
}

_remove_ssh()
{
    local node=$1

    ssh "${NODE_LOCATION[$node]}" "$(remove_cmd $node)"
}

_remove_node()
{
    local node=${@:$#} # last parameter

    if [ "${NODE_LOCATION[$node]}" == "local" ]
    then
        remove_local $node 2>$BASE_RUN/${NODE_ID[$node]}.err
    else
        remove_ssh $node 2>$BASE_RUN/${NODE_ID[$node]}.err
    fi
    echo "Job on ${NODE_ID[$node]} complete in $SECONDS seconds"
}

remove()
{
    start_jobs remove_cmd

    wait_jobs
}

