#
# Routines to remove test distribution from nodes
#

remove_cmd()
{
    local node=${@:$#}
    local dirn="${NODE_TEST_DIR[$node]}"
    echo -n 'rm -rf '"$dirn"'/*'
}

remove()
{
    start_jobs remove_cmd

    wait_jobs
}

