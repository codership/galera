#
# Routines pertaining to parallel execution
#
# xxxx_job() functions assume node index as last parameter
#
# Logging must happen on different layers.
# e.g. logging of stderr must happen on the same level as recording of return
# code. While stdout log should record only the output of command.

local_job()
{
    eval "$($@)"
}

ssh_job()
{
    local node=${@:$#} # last argument
    local cmd="$($@)"

    ssh "${NODE_LOCATION[$node]}" "$cmd"
}

virtual_job()
{
    local node=${@:$#} # last argument
    local output=$BASE_OUT/$1_${NODE_ID[$node]}.out

    if [ "${NODE_LOCATION[$node]}" == "local" ]
    then
        local_job "$@" 1>"$output"
    else
        ssh_job "$@" 1>"$output"
    fi

}

# This function tries to add a bit of polymorphism by treating untar cmd
# specially. The speciality comes from that it not only excutes command,
# but also transfers data
node_job()
{
    local cmd=$1
    local node=${@:$#} # last argument
    local node_id="${NODE_ID[$node]}"
    local rcode=0

    local start=$SECONDS

    case $cmd in
    "untar_cmd")
        shift
	local dist="$1"
	shift
	cat "$dist" | virtual_job "$cmd" "$@" || rcode=$?
        ;;
    *)
        virtual_job "$@" || rcode=$?
	;;
    esac

    echo $rcode > "$BASE_RUN/${cmd}_$node_id.ret"

    echo -n "Job '$cmd' on '$node_id'"
    
    if [ $rcode -eq 0 ]
    then
        echo " complete in $(($SECONDS - $start)) seconds"
    else
        echo " failed with code: $rcode"
        echo -n "REASON: "
        cat "$BASE_RUN/${cmd}_$node_id.err"
    fi

    return $rcode
}

start_jobs()
{
    SECONDS=0

    local node

    for node in $NODE_LIST
    do
        local node_id="${NODE_ID[$node]}"
        local prefix="$BASE_RUN/${1}_$node_id"
	
	node_job "$@" $node 2>"$prefix.err" &
	echo $! > "$prefix.pid"
	echo "Job '$1' on '$node_id' started"
    done

    echo "All jobs started"
}

wait_jobs()
{
    local err=0
    
    for node in $NODE_LIST
    do
        wait %% 2>$BASE_RUN/wait.err || err=$?;
	if [ $err -ne 0 ]; then break; fi
    done

    echo "All jobs complete in $SECONDS seconds"

    # 127 - no such job. Job has completed before we got around to wait for it
    if [ $err -eq 127 ]; then err=0; fi

    return $err
}
