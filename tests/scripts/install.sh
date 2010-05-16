#
# Routines to install distribution on nodes
#

untar_cmd()
{
    local node=${@:$#}
    local dir="${NODE_TEST_DIR[$node]}"
    echo -n "mkdir -p \"$dir\" && tar --strip 1 -C \"$dir\" -xzf - && exit 0"
}

copy_config()
{
    local -r node=$1
    local cnf
    local cnf_dir

    case $DBMS in
    MYSQL)
        cnf="${NODE_MY_CNF[$node]}"
        cnf_dir="${NODE_TEST_DIR[$node]}/mysql/etc"
        ;;
    PGSQL|*)
        echo "Unsupported DBMS: '$DBMS'" >&2
        return 1
        ;;
    esac

    if [ -n "$cnf" ]
    then
        local -r location="${NODE_LOCATION[$node]}"

        if [ "$location" == "local" ]
        then
            cp "$cnf" "$cnf_dir"
        else
            local cnf_file="$(basename $cnf)"
            cat "$cnf" | ssh $location "cat > $cnf_dir/$cnf_file"
        fi
    fi
}

install()
{
    local dist=$1

    start_jobs untar_cmd $dist

    wait_jobs
}
