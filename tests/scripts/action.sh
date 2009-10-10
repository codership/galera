#
# Routines to remove test distribution from nodes
#

action_cmd()
{
    local cmd=$1
    local node=${@:$#}
    local nargs=$(( $# - 2 ))  # minus cmd and node
    local args="${@:2:$nargs}" # arguments range from 2 to n-1
    local dir="${NODE_TEST_DIR[$node]}"

    case "$DBMS" in
    "MYSQL")
        echo -n "MYSQL_PORT=${NODE_INCOMING_PORT[$node]} "\
                "\"$dir/mysql-galera\" $args $cmd"
	;;
    "PGSQL"|*)
        return -1
	;;
    esac
}

start_cmd()
{
    action_cmd "start" "$@"
}

stop_cmd()
{
    action_cmd "stop" "$@"
}

restart_cmd()
{
    action_cmd "restart" "$@"
}

check_cmd()
{
    action_cmd "check" "$@"
}

action()
{
    start_jobs "$@"
    wait_jobs
}

start()
{
    action "start_cmd" "$@"
}

stop()
{
    action "stop_cmd" "$@"
}

restart()
{
    action "restart_cmd" "$@"
}

check()
{
    cmd="check_cmd"
    action "$cmd" "$@"
    
    local prefix="$BASE_OUT/$cmd"
    local node
    local prev=""
    local fail=""

    for node in $NODE_LIST
    do
        local node_id="${NODE_ID[$node]}"
        local out="${prefix}_${node_id}.out"
        
        chk=$(cat "$out") # no need to check if file exists:
                          # should be created even if command fails
#        echo "$node_id: ${chk%% -}"
        echo "$chk" | sed s/-/${node_id}/

        if [ -n "$chk" ]  # skip 0-length checksum: the node was down
        then
            if [ -z "$prev" ]
            then
                prev="$chk"
            else
                if [ "$prev" != "$chk" ]
                then
                    fail="yes"
                    break
                fi
            fi
        fi
    done
    
    if [ -z "$fail" ]; then return 0; fi

    echo "Checksum failed."
#    for node in $NODE_LIST
#    do
#        local node_id="${NODE_ID[$node]}"
#        echo -n "$node_id: "
#        cat "${prefix}_$node_id.out"
#    done

    return 1
}

start_node()
{
    node_job "start_cmd" "$@"
}

stop_node()
{
    node_job "stop_cmd" "$@"
}

restart_node()
{
    node_job "restart_cmd" "$@"
}

# unlike bulk check this one returns error when the node could not be checked
check_node()
{
    node_job "check_cmd" "$@"
    
    local node_id="${NODE_ID[$node]}"
    echo "$chk" | sed s/-/${node_id}/
    return $(cat $BASE_RUN/check_cmd_$node_id.ret)
}
