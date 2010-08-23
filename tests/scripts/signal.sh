#
# Sends signal to process
#

signal_cmd()
{
    local sig=$1
    local node=$2
    local dir="${NODE_TEST_DIR[$node]}"

    echo $sig $node
    case $DBMS in
    "MYSQL")
        echo -n "kill -$sig \$(cat $dir/mysql/var/mysqld.pid)"
	;;
    "PGSQL"|*)
        echo "Not supported" >&2
        return 1
	;;
    esac
}

signal_node()
{
    node_job signal_cmd "$@"
}
