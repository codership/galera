#
# Routines to start|stop|check cluster nodes
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

# By convention node index is the last in the arguments list.
# So we prepend command to the argument list otherwise it'll go after node
# index here.
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

stop()
{
    action "stop_cmd" "$@"
}

check()
{
    cmd="check_cmd"
    ! action "$cmd" "$@" # ! - to ignore possible connection error

    local -r prefix="$BASE_OUT/$cmd"
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

        if [ -n "$chk" ]  # skip 0-length checksum: the node was down
        then
            echo "$chk" | sed s/-/${node_id}/
            if [ -z "$prev" ]
            then
                prev="$chk"
            else
                if [ "$prev" != "$chk" ]
                then
                    fail="yes"
                fi
            fi
        fi
    done

    if [ -z "$fail" ] && [ -n "$prev" ]; then return 0; fi

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
    local cmd="check_cmd"
    node_job "$cmd" "$@"

    local node_id="${NODE_ID[$1]}"
    cat "${BASE_OUT}/${cmd}_${node_id}.out" | sed s/-/${node_id}/
    return $(cat $BASE_RUN/check_cmd_$node_id.ret)
}

# return GCS address at which node N should connect to group
gcs_address()
{
    local node=$1

    case "$GCS_TYPE" in
    "gcomm")
        local peer=$(( $node - 1 )) # select previous node as connection peer
#        local peer=0 # use the first node as a connection handle

        if [ $peer -lt 0 ]; then peer=$NODE_MAX; fi # rollover

        echo "gcomm://${NODE_GCS_HOST[$peer]}:${NODE_GCS_PORT[$peer]}${GCOMM_EXTRA_PARAMS}"
        ;;
    "vsbes")
        echo "vsbes://$VSBES_ADDRESS"
        ;;
    *)
        return 1
        ;;
    esac
}

# start/restart nodes in group mode.
_cluster_up()
{
    local -r cmd=$1
    shift

    SECONDS=0 # for wait_jobs

    for node in $NODE_LIST
    do
        echo "Starting ${NODE_ID[$node]}"
        if [ $node -eq 0 ]
        then
            # must make sure 1st node completely operational
            case "$GCS_TYPE" in
            "gcomm") $cmd "-g gcomm://:${NODE_GCS_PORT[$node]}${GCOMM_EXTRA_PARAMS}" "$@" 0 ;;
            "vsbes") $cmd "-g vsbes://$VSBES_ADDRESS" "$@" 0 ;;
            esac
        else
            $cmd "-g $(gcs_address $node)" "$@" $node &
        fi
    done
    wait_jobs
}

start()
{
    _cluster_up start_node "$@"
}

restart()
{
    stop
    _cluster_up start_node "$@"
}

