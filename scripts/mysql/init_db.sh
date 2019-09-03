#!/bin/sh -eux

base_dir="$1"
data_dir="$base_dir/var"
root_pswd=$2
test_pswd=$3

mkdir -p "$data_dir"
if ls -1qA "$data_dir" | grep -q .
then
    echo "Data dir \"$data_dir\" not empty, skipping DB initialization"
    touch "$data_dir"/mysqld.err
    exit 0
fi
chmod go-rx "$data_dir"

init_sql=$(mktemp -p "$base_dir")
chmod go-r "$init_sql"
echo "ALTER USER 'root'@'localhost' IDENTIFIED BY '$root_pswd';" >> "$init_sql"
echo "UPDATE mysql.user SET Host = '%' WHERE User = 'root';" >> "$init_sql"
echo "CREATE USER IF NOT EXISTS 'test'@'%' IDENTIFIED BY '$test_pswd';" >> "$init_sql"
echo "GRANT ALL ON test.* TO 'test'@'%';" >> "$init_sql"

"$base_dir/sbin/mysqld" \
    --no-defaults --initialize-insecure --core-file --wsrep_provider=none \
    --innodb_log_file_size=4M --innodb_buffer_pool_size=64M \
    --innodb_flush_method=nosync \
    --loose-skip_ndbcluster --loose-skip_mysqlx \
    --loose-auto_generate_certs=OFF \
    --loose-sha256_password_auto_generate_rsa_keys=OFF \
    --loose-caching_sha2_password_auto_generate_rsa_keys=OFF \
    --secure_file_priv="$data_dir" --tmpdir="/tmp" --datadir="$data_dir" \
    --init_file="$init_sql" \
    --log_error="$data_dir/mysqld.err" && > "$data_dir"/mysqld.err \
    || (cat "$data_dir/mysqld.err"; exit 1)

# remove innodb log files to obey the size that will be given on startup
rm -rf "$data_dir"/ib_logfile* $init_sql
