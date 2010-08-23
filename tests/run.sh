#!/bin/bash

set -e

# This script assumes that galera cluster is alredy installed and configured

# This is location of this script. _HOME suffix preferred to _ROOT to avoid
# confusion
THIS_HOME=$(cd $(dirname $0); pwd -P)

# Optional configuration file
if test -n "$GALERA_TEST_CONFIG"
then
. "$GALERA_TEST_CONFIG"
fi

GALERA_TESTS_HOME=${GALERA_TESTS_HOME:-$THIS_HOME}
GALERA_RESULTS_HOME=${GALERA_RESULTS_HOME:-$GALERA_TESTS_HOME/results}

# Incoming cluster address (load balancer)
export GALERA_CLUSTER_IP=${GALERA_CLUSTER_IP:-"127.0.0.1"}
export GALERA_CLUSTER_PORT=${GALERA_CLUSTER_PORT:-3306}

# List of tests to run
GALERA_TESTS=${GALERA_TESTS:-"sqlgen dbt2 dots"}

# This is needed for native load balancing and consistency checking
export GALERA_NODES_IPS=${GALERA_NODE_IPS:?"GALERA_NODE_IPS not set"}
export GALERA_NODES_PORTS=${GALERA_NODE_PORTS:?"GALERA_NODE_PORTS not set"}


# Create a results directory for this run
GALERA_DATE=$(date +%Y-%m-%d_%H:%M:%S)
mkdir -p $GALERA_RESULTS_HOME/$GALERA_DATE

declare TESTS_FAILED
TESTS_FAILED=0
for TEST in $GALERA_TESTS
do
    export GALERA_RESULT_DIR=$GALERA_RESULTS_HOME/$GALERA_DATE/$TEST
    mkdir -p $GALERA_RESULT_DIR
    echo -n "Running $TEST... "
    $GALERA_TESTS_HOME/test_$TEST/run.sh && echo "passed" \
    || { TESTS_FAILED=$[ $TESTS_FAILED + 1 ]; echo "failed"; }
done

if [ $TESTS_FAILED != "0" ]
then
    echo "Tests failed: $TESTS_FAILED"
    exit 1
fi

#
