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
    node_job kill_cmd "$@"
}

