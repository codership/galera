#!/bin/bash -eu

if [ $# -ne 2 ]
then
    echo "Usage: $0 <old>.tgz <new>.tgz" >&2
    exit 1
else
    OLD=$1
    NEW=$2
    echo "Upgrading $OLD to $NEW"
fi

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/remove.sh
. $SCRIPTS/install.sh
. $SCRIPTS/misc.sh

# Remove previous install
kill_all
remove

# Install old version
install $OLD
start

# Start load
SQLGEN=${SQLGEN:-"$DIST_BASE/bin/sqlgen"}
LD_PRELOAD=$GLB_PRELOAD \
DYLD_INSERT_LIBRARIES=$GLB_PRELOAD \
DYLD_FORCE_FLAT_NAMESPACE=1 \
$SQLGEN --user $DBMS_TEST_USER --pswd $DBMS_TEST_PSWD --host $DBMS_HOST \
        --port $DBMS_PORT --users $DBMS_CLIENTS --duration 999999999 \
        --stat-interval 99999999 --sess-min 999999 --sess-max 999999 \
        --rollbacks 0.1 --ac-frac 20 --long-keys \
        >/dev/null 2>$BASE_RUN/seesaw.err &
declare -r sqlgen_pid=$!
disown # forget about the job, disable waiting for it

term=0
terminate()
{
    echo "Terminating"
    term=1
}

trap terminate SIGINT SIGTERM SIGHUP SIGPIPE

trap "kill $sqlgen_pid" EXIT

pause 5 1
consistency_check $sqlgen_pid

# Stops a node, installs new version and restarts
upgrade()
{
    local -r node=$1
    local -r node_id=${NODE_ID[$node]}

    echo "Upgrading node $node_id..."

    stop_node $node
    install_node $NEW $node

    echo "Restarting node $node_id..."

#    if test $pause_var -gt 3
#    then
        start_node "-g $(gcs_address $node)" $node
#    else
#        stop_node $node || : # just in case the process is still lingering
#        start_node "-g $(gcs_address $node)" $node
#    fi
}

for node in $NODE_LIST
do
    upgrade $node
    pause
done

consistency_check $sqlgen_pid

exit

