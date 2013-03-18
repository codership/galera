#!/bin/bash

SYSBENCH_ROOT=$(cd $(dirname $0)/..; pwd -P)
SYSBENCH=$SYSBENCH_ROOT/sysbench/sysbench

run() {
    local CMD=$@
    local rcode
    echo "RUNNING: $CMD" >> $OUTPUT
    LD_PRELOAD=$PRELOAD $CMD > /tmp/run_test.out
    rcode=$?
    cat /tmp/run_test.out | tee -a $OUTPUT
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
    local port=$1
    shift
    local users=$1
    shift
    local host
    local hosts="$1"
    shift

    for host in  "$@" ; do
        hosts="$hosts,$host"
    done

    log "running against $hosts $port with $users"

    PRELOAD=$GLB_PRELOAD \
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

test_galera() {
    local gcs=$1
    local level=$2
    log "**********************************************************************"
    log "**      Testing galera cluster with gcs: $gcs level: $level         **"
    log "**********************************************************************"
    run ~/galera stop
    run ~/galera --host $primary_node start

    mysql -uroot -prootpass -h$primary_node -P3306 -e"show variables like 'wsrep%'" 
>> $OUTPUT

    cluster=""
    gra_users=0
    hosts=""

    for node in $nodes ; do
        cluster="$cluster $node"
        gra_users=`expr $gra_users + $users`
        hosts="$hosts --host $node "
        log "setting cluster to $cluster, using: $gra_users concurrent users"

        run ~/galera stop
        run ~/galera --ws_level $level $hosts start 

        load_db $primary_node 3306
        sleep 5

       # run load against pen or use sqlgen multihost for LB
        if [ $load_balancer = "pen" ] ;  then
            run "killall pen"
            echo "pen -r 3307 $cluster" >> $OUTPUT
            pen -r 3307 $cluster

            run_load 3307 $gra_users "127.0.0.1"

        else
            run_load 3306 $gra_users $cluster
        fi

        # consistency check
        sleep 20
        echo "consistency check now"
        run ~/galera $hosts check test
        if [ $? != 0 ] ; then
            sleep 60
            run ~/galera $hosts check test
            if [ $? != 0 ] ; then
                exit 1
            fi
        fi
    done
}

set -x
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

#log  "stopping servers"
#run ~/mysql-plain stop
#run ~/galera stop
#
# testing against plain mysql
#
#log "starting mysql plain"
#run ~/mysql-plain start

load_db $primary_node 3306
run_load 3306 $users $primary_node

#log "stopping plain mysql"
#run ~/mysql-plain stop

#
# testing against galera cluster
#

#killall spread
#log "starting spread"
#nohup spread -c /etc/spread/spread.conf -n localhost&
#log "spread started: $?"

#test_galera 'spread' 'SQL'
#test_galera 'spread' 'RBR'

#log "stopping spread"
#killall spread
#killall vsbes
#log "starting vsbes"
#~/vsbes_start
#test_galera vsbes 'SQL'
#test_galera vsbes 'RBR'


#log "stopping vsbes"
#killall vsbes
#log "exiting at `date`"

#cd ..
exit
