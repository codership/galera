#
# Ungracefully kills the process
#

kill_cmd()
{
    local node=${@:$#}
    local dir="${NODE_TEST_DIR[$node]}"

    case $DBMS in
    "MYSQL")
        echo -n "kill -9 \$(cat $dir/mysql/var/mysqld.pid)"
        ;;
    "PGSQL"|*)
        echo "Not supported" >&2
        return 1
        ;;
    esac
}

kill_node()
{
    local node=${@:$#}
    local dir="${NODE_TEST_DIR[$node]}"
    local pid=$(cat $dir/mysql/var/mysqld.pid)
    node_job kill_cmd "$@"
    # wait process to disappear.
    while find_mysqld_pid $pid
    do
        sleep 0.1
    done
}

kill_all()
{
    start_jobs kill_cmd
    wait_jobs
}

