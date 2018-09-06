#!/bin/bash -eu

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/signal.sh
. $SCRIPTS/misc.sh

TRIES=${1:-"-1"} # -1 stands for indefinite loop

stop
try=0

while [ $try -ne $TRIES ]
do
    ! let try++ # ! - to protect from -e

    echo -e "\nTry ${try}/${TRIES}\n"

    start
    pause 5
    consistency_check
    stop

done
