#!/bin/bash -eux

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
echo "RENAME USER root@'localhost' TO root@'%';" >> "$init_sql"
echo "CREATE USER IF NOT EXISTS 'test'@'%' IDENTIFIED BY '$test_pswd';" >> "$init_sql"
echo "GRANT ALL ON test.* TO 'test'@'%';" >> "$init_sql"
echo "CREATE SCHEMA IF NOT EXISTS test;" >> "$init_sql"

GENERAL_OPTS="--core-file --wsrep_provider=none --skip-networking"
GENERAL_OPTS+=" --loose-sha256_password_auto_generate_rsa_keys=OFF"
GENERAL_OPTS+=" --loose-caching_sha2_password_auto_generate_rsa_keys=OFF"
GENERAL_OPTS+=" --loose-auto_generate_certs=OFF"
GENERAL_OPTS+=" --secure_file_priv=$data_dir --tmpdir=/tmp --datadir=$data_dir"
GENERAL_OPTS+=" --log_error=$data_dir/mysqld_install.err"


MYSQLD="$base_dir/bin/mariadb-install-db"
MYSQLD_OPTS="--bind-address=0.0.0.0"
if [ ! -x "$MYSQLD" ]
then
    MYSQLD="$base_dir/sbin/mysqld"
    MYSQLD_OPTS+=" --initialize-insecure"
    MYSQLD_OPTS+=" --innodb_flush_method=nosync"
    MYSQLD_OPTS+=" --loose-skip_ndbcluster --loose-skip_mysqlx"
    MYSQLD_OPTS+=" --init_file=$init_sql"
fi
if [ ! -x "$MYSQLD" ]
then
    echo "Could not find mysqld binary" > /dev/stderr
    exit 1
fi

LD_LIBRARY_PATH=$base_dir/lib/mysql/private:${LD_LIBRARY_PATH:-} \
"$MYSQLD" --no-defaults \
    $MYSQLD_OPTS \
    $GENERAL_OPTS \
    && > "$data_dir"/mysqld.err \
    || (cat "$data_dir/mysqld_install.err"; exit 1)

if [ "$base_dir/bin/mariadb-install-db" = "$MYSQLD" ]
then
    # mariadb-install-db does not respect --init_file option, so
    # need to do this separately
    echo "\nMariaDB additional config:\n" >> "$data_dir/mysqld_install.err"

    echo "DROP USER IF EXISTS $(whoami)@'localhost';" >> "$init_sql"
    echo "DROP USER IF EXISTS ''@'localhost';" >> "$init_sql"
    echo "DROP USER IF EXISTS ''@'$(hostname)';" >> "$init_sql"
    echo "SHUTDOWN;" >> "$init_sql"

    MYSQLD="$base_dir/sbin/mariadbd"
    "$MYSQLD" --no-defaults \
        $MYSQLD_OPTS \
        $GENERAL_OPTS \
        --socket="$data_dir"/mysqld.sock \
        --init_file="$init_sql" \
        && > "$data_dir"/mysqld.err \
        || (cat "$data_dir/mysqld_install.err"; exit 1)
fi

rm -rf $init_sql
