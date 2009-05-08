#!/bin/bash

SYSBENCH_ROOT=$(cd $(dirname $0)/..; pwd -P)
SYSBENCH=$SYSBENCH_ROOT/sysbench/sysbench

run() {
    local CMD=$@
    local rcode
    echo "RUNNING: $CMD" >> $OUTPUT
    $CMD > /tmp/run_test.out ; rcode=$? ; cat /tmp/run_test.out | tee -a $OUTPUT
    return $rcode
}

log() {
    echo "$@" | tee -a $OUTPUT
}

load_db() {
    local host=$1
    local port=$2
    log "creating tables"
    run $SYSBENCH --test=oltp --db-driver=mysql \
                  --mysql-user=$user --mysql-password=$password \
                  --mysql-host=$host --mysql-port=$port \
                  --mysql-db=test \
                  cleanup
                  
    run $SYSBENCH --test=oltp --db-driver=mysql \
                  --mysql-user=$user --mysql-password=$password \
                  --mysql-host=$host --mysql-port=$port  \
                  --mysql-table-engine=innodb --mysql-db=test \
                  --oltp-table-size=$rows --oltp-auto-inc=off \
                  prepare

}

run_load() {
    local hosts="$1"
    shift
    local port=$1
    shift
    local users=$1
    shift

    log "running against $hosts $port with $users concurrent connections"

    run $SYSBENCH --test=oltp --db-driver=mysql \
                  --mysql-user=$user --mysql-password=$password \
                  --mysql-host=$hosts --mysql-port=$port  \
                  --mysql-table-engine=innodb --mysql-db=test \
                  --oltp-table-size=$rows --oltp-test-mode=$mode \
                  --oltp-read-only=$readonly --mysql-ignore-duplicates=on \
                  --num-threads=$users --max-time=$duration \
                  --max-requests=$requests \
                  run
}

#set -e
#test_dir=$1
#cd $test_dir
load_balancer=multihost
requests=10000
source sysbench.conf

RUN_NUMBER=-1
RUN_FILE="./results/run_number"
if test -f ${RUN_FILE}; then
  read RUN_NUMBER < ${RUN_FILE}
fi
if [ $RUN_NUMBER -eq -1 ]; then
        RUN_NUMBER=0
fi

OUTPUT=./results/$RUN_NUMBER/log

mkdir ./results/$RUN_NUMBER

# Update the run number for the next test.
RUN_NUMBER=`expr $RUN_NUMBER + 1`
echo $RUN_NUMBER > ${RUN_FILE}

echo "sysbench for: $test_dir at `date`" | tee $OUTPUT

NODE=${1:-"$primary_node"}
PORT=${2:-"3306"}
USERS=${3:-"$users"}

load_db  $NODE $PORT
echo ""
run_load $NODE $PORT $USERS

exit
#
