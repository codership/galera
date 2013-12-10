# Helper to get status variable value

cluster_status()
{
    local node=$1
    case "$DBMS" in
        "MYSQL")
            local res=$(mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD \
                -h${NODE_INCOMING_HOST[$node]} -P${NODE_INCOMING_PORT[$node]} \
                --skip-column-names -ss \
                -e "SET wsrep_on=0;
                    SHOW STATUS WHERE Variable_name LIKE 'wsrep_cluster_status'
                    OR Variable_name LIKE 'wsrep_cluster_size'" 2>/dev/null)
            echo -n $res | awk '{ print $4 ":" $2; }'
            ;;
        "PGSQL"|*)
            return -1
    esac
}

mysql_query()
{
    local node=$1
    local query=$2
    mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD \
          -h${NODE_INCOMING_HOST[$node]} -P${NODE_INCOMING_PORT[$node]} \
          --skip-column-names -ss -e "$query" 2>/dev/null
}

wait_node_state()
{
    local node=$1
    local state=$2

    while true
    do
        local res="-1"

        case "$DBMS" in
        "MYSQL")
            res=$(mysql_query $node "SHOW STATUS LIKE 'wsrep_local_state'" \
                  | awk '{ print $2 }')
            ;;
        "PGSQL"|*)
            return -1
        esac

        if [ "$res" = "$state" ]; then break; fi
        sleep 1
    done
}

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

dump_cmd()
{
    action_cmd "dump" "$@"
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

dump()
{
    action "dump_cmd" "$@"
}

check()
{
    wait_sync $NODE_LIST || true


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

# Query each node with causal reads on to make sure that slave
# queue has been fully processed.
# Arguments: list of nodes
wait_sync()
{
    local node
    for node in "$@"
    do
        mysql_query "$node" "set wsrep_causal_reads=1; select 0;" 1>/dev/null
    done
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

dump_node()
{
    node_job "dump_cmd" "$@"
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

extra_params()
{
    local node=$1
    local extra_params
    [ -z "$GCOMM_EXTRA_PARAMS" ] && extra_params="?" || extra_params="?${GCOMM_EXTRA_PARAMS}&"
#    echo "${extra_params}gmcast.listen_addr=tcp://${NODE_GCS_HOST[$node]}:${NODE_GCS_PORT[$node]}"
    echo "${extra_params}gmcast.listen_addr=tcp://0.0.0.0:${NODE_GCS_PORT[$node]}"
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

        echo "'gcomm://${NODE_GCS_HOST[$peer]}:${NODE_GCS_PORT[$peer]}$(extra_params $node)'"
        ;;
    "vsbes")
        echo "'vsbes://$VSBES_ADDRESS'"
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
#            "gcomm") $cmd "-g 'gcomm://:${NODE_GCS_PORT[$node]}$(extra_params $node)'" "$@" 0 ;;
            "gcomm") $cmd "-g $(gcs_address $node) --mysql-opt --wsrep-new-cluster" "$@" 0 ;;
            "vsbes") $cmd "-g 'vsbes://$VSBES_ADDRESS'" "$@" 0 ;;
            esac
        else
            $cmd "-g $(gcs_address $node)" "$@" $node &
        fi
    done
    wait_jobs
}


# start/restart nodes in group mode.
bootstrap()
{
    SECONDS=0 # for wait_jobs

    local cnt=0
    for node in $NODE_LIST
    do
        echo "Starting ${NODE_ID[$node]}"
        start_node "-g $(gcs_address $node)" "$@" $node &
        cnt=$(($cnt + 1))
    done

    # TODO: Poll until all have reached non-prim
    for node in 0 # only one node is sufficient
    do
        while true
        do
            st=$(cluster_status $node)
            if test "x$st" = "xnon-Primary:$cnt"
            then
                break;
            fi
            sleep 1
        done
    done

    # TODO: Figure out how to do this in DBMS indepent way
    case "$DBMS" in
        "MYSQL")
            mysql -u$DBMS_ROOT_USER -p$DBMS_ROOT_PSWD \
                -h${NODE_INCOMING_HOST[0]} \
                -P${NODE_INCOMING_PORT[0]} \
                -e "SET GLOBAL wsrep_provider_options='pc.bootstrap=1'"
            ;;
        "PGSQL"|*)
            return -1
            ;;
    esac

    # Jobs will finish when nodes reach primary
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

