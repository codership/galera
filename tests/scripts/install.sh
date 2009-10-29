#
# Routines to install distribution on nodes
#

untar_cmd()
{
    local node=${@:$#}
    local dir="${NODE_TEST_DIR[$node]}"
    echo -n "mkdir -p \"$dir\" && tar --strip 1 -C \"$dir\" -xzf -"
}

install()
{
    local dist=$1

    start_jobs untar_cmd $dist

    wait_jobs
}

