#
# Routines pertaining to parallel execution
#
# xxxx_job() functions assume node index as last parameter
#
# Logging must happen on different layers.
# e.g. logging of stderr must happen on the same level as recording of return
# code. While stdout log should record only the output of command.

#_local_job()
#{
#    local cmd="$1"
#    eval "$($@)"
#    eval $cmd
#}

#_ssh_job()
#{
#    local node=${@:$#} # last argument
#    local cmd="$($@)"
#    local cmd="$1"
#
#    ssh -ax ${NODE_LOCATION[$node]} "$cmd"
#}

_date()
{
    echo -n $(date +'%y%m%d %T.%N' | cut -c 1-19)
}

virtual_job()
{
    local node=${@:$#} # last argument
    local out="$BASE_OUT/${1}_${NODE_ID[$node]}.out"
    local cmd="$($@)"

    if [ "${NODE_LOCATION[$node]}" = "local" ]
    then
#        local_job "$cmd" 1>"$out"
        eval "$cmd" 1>"$out"
    else
#        ssh_job "$cmd" 1>"$out"
        ssh -ax ${NODE_LOCATION[$node]} "$cmd" 1>"$out"
    fi

}

# This function tries to add a bit of polymorphism by treating untar cmd
# specially. The speciality comes from that it not only excutes command,
# but also transfers data
#
# Usage: node_job cmd opt1 opt2 ... optN node
#
node_job()
{
    local cmd=$1
    shift
    local node=${@:$#} # last argument
    local node_id="${NODE_ID[$node]}"
    local prefix="$BASE_RUN/${cmd}_${node_id}"
    local rcode=0

    local start=$SECONDS

    case $cmd in
    "untar_cmd")
        local dist="$1"
        shift
        cat "$dist" | virtual_job "$cmd" "$@" 2>"$prefix.err" && \
        copy_config $node 2>"$prefix.err" || rcode=$?
        ;;
    *)
        virtual_job "$cmd" "$@" 2>"$prefix.err" || rcode=$?
        ;;
    esac

    echo $rcode > "$prefix.ret"

    echo -n "$(_date) Job '$cmd' on '$node_id'"

    if [ $rcode -eq 0 ]
    then
        echo " complete in $(($SECONDS - $start)) seconds, "
    else
        echo " failed with code: $rcode, "
        echo "FAILED COMMAND: $($cmd $@)"
        echo "REASON: $(cat "$prefix.err")"
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

        node_job "$@" $node &
        echo $! > "$prefix.pid"
        echo "$(_date) Job '$1' on '$node_id' started"
    done

    echo "All jobs started"
}

wait_jobs()
{
    local err=0
    local node

    for node in $NODE_LIST
    do
        wait %% 2>$BASE_RUN/wait.err || err=$?;

        # 127 - no more jobs
        if [ $err -eq 127 ]; then err=0; break; fi
        if [ $err -gt 128 ]; then err=0; fi # ignore signals
    done

    echo "$(_date) All jobs complete in $SECONDS seconds"

    return $err
}
