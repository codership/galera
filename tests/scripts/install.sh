#
# Routines to install distribution on nodes
#

untar_cmd()
{
    local node=${@:$#}
    local path="${NODE_TEST_DIR[$node]}"
    local hst=$(hostname -s)
    echo -n "mkdir -p \"$path\" && tar --strip 1 -C \"$path\" -xzf - && > \"$path/mysql/var/mysqld.err\" && exit 0"
}

copy_config()
{
    local -r node=$1
    local cnf
    local cnf_dir
    local key_src="$BASE_CONF/galera_key.pem"
    local key_dst
    local cert_src="$BASE_CONF/galera_cert.pem"
    local cert_dst

    case $DBMS in
    MYSQL)
        common_cnf="$COMMON_MY_CNF"
        cnf_src="${NODE_MY_CNF[$node]}"
        cnf_dst="${NODE_TEST_DIR[$node]}/mysql/etc/my.cnf"
        key_dst="${NODE_TEST_DIR[$node]}/mysql/var/galera_key.pem"
        cert_dst="${NODE_TEST_DIR[$node]}/mysql/var/galera_cert.pem"
        ;;
    PGSQL|*)
        echo "Unsupported DBMS: '$DBMS'" >&2
        return 1
        ;;
    esac

    if [ -n "$common_cnf" ] || [ -n "$cnf_src" ]
    then
        if [ "${NODE_LOCATION[$node]}" = "local" ]
        then
            ([ -n "$common_cnf" ] && cat "$common_cnf" && \
             [ -n "$cnf_src" ]    && cat "$cnf_src") > "$cnf_dst"

            cat "$key_src"  > "$key_dst"
            cat "$cert_src" > "$cert_dst"
        else
            local remote="${NODE_LOCATION[$node]}"
            ([ -n "$common_cnf" ] && cat "$common_cnf" && \
             [ -n "$cnf_src" ]    && cat "$cnf_src")    | \
            ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "$remote" "cat > $cnf_dst"

            cat "$key_src"  | ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "$remote" "cat > $key_dst"
            cat "$cert_src" | ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "$remote" "cat > $cert_dst"
        fi
    fi
}

copy_file_node()
{
    set -x
    local -r src_file="$1"
    local -r dst_file="$2"
    local -r node="$3"

    cat "$src_file" | ssh -o UserKnownHostsFile=/dev/null -o StrictHostKeyChecking=no "${NODE_LOCATION[$node]}" "cat - > ${NODE_TEST_DIR[$node]}/$dst_file"

}

copy_file()
{
    local -r src_file="$1"
    local -r dst_file="$2"
    start_jobs copy_file_node "$src_file" "$dst_file"
}

install_node()
{
    local dist=$1

    node_job untar_cmd "$@"
}

install()
{
    local dist=$1

    start_jobs untar_cmd $dist

    wait_jobs
}
