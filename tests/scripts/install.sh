#
# Routines to install distribution on nodes
#

untar_cmd()
{
    local node=${@:$#}
    local dir="${NODE_TEST_DIR[$node]}"
    echo -n "mkdir -p \"$dir\" && tar --strip 1 -C \"$dir\" -xzf -"
}

_install_local()
{
    local dist=$1
    local node=$2
#    echo -e "\nInstall local ${NODE_ID[$node]}\n"

    cat $dist | eval "$(untar_cmd $node)"
}

_install_ssh()
{
    local dist=$1
    local node=$2
#    echo -e "\nInstall SSH ${NODE_ID[$node]}\n"

    cat $dist | ssh "${NODE_LOCATION[$node]}" "$(untar_cmd $node)"
}

_install_node()
{
    local dist=$1
    local node=${@:$#} # last parameter

    if [ "${NODE_LOCATION[$node]}" == "local" ]
    then
        install_local $dist $node 2>$BASE_RUN/${NODE_ID[$node]}.err
    else
        install_ssh $dist $node 2>$BASE_RUN/${NODE_ID[$node]}.err
    fi
    echo "Job on ${NODE_ID[$node]} complete in $SECONDS seconds"
}

install()
{
    local dist=$1

    start_jobs untar_cmd $dist

    wait_jobs
}

