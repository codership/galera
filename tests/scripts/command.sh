#!/bin/bash -eu

help()
{
    echo "Usage: $0 <command> [args]"
    echo "Cluster commands: install, remove, start, stop, check"
    echo "Node    commands: start_node, stop_node, restart_node, check_node,"
    echo "                  kill_node"
    echo "Command help: $0 <command> help"
}

if [ $# -eq 0 ]; then usage >&2; exit 1; fi

declare -r TEST_BASE=$(cd $(dirname $0)/..; pwd -P)

. "$TEST_BASE/conf/main.conf"

mkdir -p "$BASE_RUN"
mkdir -p "$BASE_OUT"

. $SCRIPTS/jobs.sh
. $SCRIPTS/install.sh
. $SCRIPTS/remove.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh

command=$1
shift

$command "$@"
