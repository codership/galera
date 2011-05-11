#
# Routines to install distribution on nodes
#

untar_cmd()
{
    local node=${@:$#}
    local path="${NODE_TEST_DIR[$node]}"
    local hst=$(hostname)
    echo -n "mkdir -p \"$path\" && tar --strip 1 -C \"$path\" -xzf - && > \"$path/mysql/var/$hst.err\" && exit 0"
}

copy_config()
{
    local -r node=$1
    local cnf
    local cnf_dir

    case $DBMS in
    MYSQL)
        common_cnf="$COMMON_MY_CNF"
        cnf_src="${NODE_MY_CNF[$node]}"
        cnf_dst="${NODE_TEST_DIR[$node]}/mysql/etc/my.cnf"
        ;;
    PGSQL|*)
        echo "Unsupported DBMS: '$DBMS'" >&2
        return 1
        ;;
    esac

    if [ -n "$common_cnf" ] || [ -n "$cnf_src" ]
    then
        if [ "${NODE_LOCATION[$node]}" == "local" ]
        then
            ([ -n "$common_cnf" ] && cat "$common_cnf" && \
             [ -n "$cnf_src" ]    && cat "$cnf_src") > "$cnf_dst"
        else
            ([ -n "$common_cnf" ] && cat "$common_cnf" && \
             [ -n "$cnf_src" ]    && cat "$cnf_src")    | \
            ssh ${NODE_LOCATION[$node]} "cat > $cnf_dst"
        fi
    fi
}

install()
{
    local dist=$1

    start_jobs untar_cmd $dist

    wait_jobs
}
