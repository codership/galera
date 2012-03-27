#!/bin/bash
#
# PARAMETERS:
#
# MYSQL_DIR  - mysql build directory
# FIRST_TEST - first test to run from test suite(s)
# SOCKET     - unix socket for the master node
#
# WARNING:
#
# * This script drops test database
# * Root password is reset to empty password to make most of the
#   mtr tests work
#
# LIMITATIONS:
#
# Some of the tests don't work if unix socket is not specified.
# Therefore master node must always reside on the machine where
# tests are run from.
#
# Cluster databases are not reset between test runs. If any of the
# tests fails, databases may need to be recoverd manually.
#
# TODO:
#
# BUGS:
#

debug_log=/tmp/galera_test_mtr.log

BASE_DIR=$(cd $(dirname $0); pwd -P)

# Parameters
if test "x$MYSQL_DIR" == "x"
then
    echo "MYSQL_DIR is not set"
    exit 1
fi
FIRST_TEST=${FIRST_TEST:-""}
SOCKET=${SOCKET:-"/run/shm/galera/local1/mysql/var/mysqld.sock"}

# Helpers to manage the cluster
declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf
declare -r SCRIPTS="$DIST_BASE/scripts"

# MySQL management
MYSQL="mysql -u${DBMS_ROOT_USER} -p${DBMS_ROOT_PSWD}
             -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]}"


# Build list of tests to be run.
TESTS=""
if test $# -ge 1
then
    TESTS="$@"
else
    for suite in $(ls $(dirname $0)/*.suite)
    do
        TESTS="$TESTS $(grep -v '#' $suite | grep -v -E '^\ ')"
    done
fi

if test "x$FIRST_TEST" != "x"
then
    TESTS=$(echo -n $TESTS | sed "s/.*$FIRST_TEST\ /$FIRST_TEST\ /")
else
    TESTS=$(echo -n $TESTS)
fi

echo "-- Running tests: $TESTS"

cd $MYSQL_DIR/mysql-test

for test in $TESTS
do
    $SCRIPTS/command.sh restart >> $debug_log
    echo "-- Running test $test"
    $MYSQL -e "DROP DATABASE IF EXISTS test;
               CREATE DATABASE test;
               SET GLOBAL auto_increment_increment=1;
               SET GLOBAL wsrep_drupal_282555_workaround=0;
               UPDATE mysql.user SET Password='' where User='root';
               FLUSH PRIVILEGES;"
    ./mysql-test-run \
        --extern socket=$SOCKET \
        --extern host=${NODE_INCOMING_HOST[0]} \
        --extern port=${NODE_INCOMING_PORT[0]} \
        --extern user=root \
        $test
    result=$?
    # attempt to reset password back to original
    mysql -h${NODE_INCOMING_HOST[0]} -P${NODE_INCOMING_PORT[0]} -uroot \
        -e "SET wsrep_on=0;
            UPDATE mysql.user set Password=PASSWORD('"${DBMS_ROOT_PSWD}"')
                   where User='root';
            FLUSH PRIVILEGES;"

    if test $result != 0
    then
        echo "Test $test failed"
        exit 1
    fi

    # Iterate over nodes and check that cluster is up and running
    node_cnt=0
    for node in $NODE_LIST
    do
        node_cnt=$(($node_cnt + 1))
    done

    for node in $NODE_LIST
    do
        cs=$($SCRIPTS/command.sh cluster_status $node)
        if test $cs != "Primary:$node_cnt"
        then
            echo "Invalid cluster status for $node: $cs"
            echo "Test failed"
            exit 1
        fi
    done


    $SCRIPTS/command.sh check
    if test $? != 0
    then
        echo "Consistency check failed"
        exit 1
    fi
done

