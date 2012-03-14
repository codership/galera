#!/bin/bash -eu

help()
{
    echo "Usage: $0 <command> [args]"
    echo "Cluster commands: install, remove, start, stop, check"
    echo "Node    commands: start_node, stop_node, restart_node, check_node,"
    echo "                  kill_node"
    echo "Command help: $0 <command> help"
}

if [ $# -eq 0 ]; then help >&2; exit 1; fi

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
declare -r DIST_SCRIPTS="$DIST_BASE/scripts"

# later create config.sh to read config from command line options

declare -r TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. "$TEST_BASE/conf/main.conf"

. $DIST_SCRIPTS/jobs.sh
. $DIST_SCRIPTS/install.sh
. $DIST_SCRIPTS/remove.sh
. $DIST_SCRIPTS/action.sh
. $DIST_SCRIPTS/kill.sh
. $DIST_SCRIPTS/signal.sh

command=$1
shift

$command "$@"
