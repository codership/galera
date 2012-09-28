#!/bin/bash -eu

#
# The purpose of this test is to test correctness of hard causal semantics
# as described in trac #688.
#
# Test outline:
# * Start cluster
# * Increase suspect timeout for second node in order to generate situation
#   where second node will still be forming new group after partitioning
#   while other nodes are already operating in new group
# * Run causal test (shell script test doesnt't seem to be enough to
#   generate causal inconsistency in cluster running in ram and communicating
#   through loopback interface)
# * Isolate second node by setting gmcast.isolate=true
#
# Test script will run three scenarios:
# 1) Verify that causal test generates causal violations with
#    --disable-causal-reads and no causal violations occur
#    without --disable-causal-reads
# 2) Verify that isolating second node from group generates causal violations
#    if evs.hard_causal=false
# 3) Run test outlined above several times in order to ensure that
#    causal violations won't happen if evs.hard_causal=true
#

CAUSAL="$(dirname $0)/causal"

if ! test -x $CAUSAL
then
    echo "causal test executable required"
    exit 1
fi

declare -r DIST_BASE=$(cd $(dirname $0)/..; pwd -P)
TEST_BASE=${TEST_BASE:-"$DIST_BASE"}

. $TEST_BASE/conf/main.conf

declare -r SCRIPTS="$DIST_BASE/scripts"
. $SCRIPTS/jobs.sh
. $SCRIPTS/action.sh
. $SCRIPTS/kill.sh
. $SCRIPTS/misc.sh
. $SCRIPTS/signal.sh
SCHEMA="test"
TABLE="hard_causal"
USER="test"
PSWD="testpass"

echo "restart cluster"
$SCRIPTS/command.sh restart

WRITE_HOST="${NODE_INCOMING_HOST[0]}:${NODE_INCOMING_PORT[0]}"
READ_HOST="${NODE_INCOMING_HOST[1]}:${NODE_INCOMING_PORT[1]}"
READ_MYSQL_CTRL="mysql -u$USER -p$PSWD -h${NODE_INCOMING_HOST[1]} -P${NODE_INCOMING_PORT[1]}"


function causal
{
    $CAUSAL --compact true --write-host $WRITE_HOST --read-host $READ_HOST --db $SCHEMA --readers 8 $@ 2>$BASE_RUN/hard_causal.err
}

function wait_prim_synced
{
    cnt=100
    while test $cnt -gt 0
    do
        status=`$READ_MYSQL_CTRL -ss -e "show status like 'wsrep_cluster_status'" | awk '{ print $2; }'`
        state=`$READ_MYSQL_CTRL -ss -e "show status like 'wsrep_local_state'" | awk '{ print $2; }'`
        if test "$status" = "Primary" && test "$state" = "4"
        then
            return
        fi
        sleep 1
        cnt=$(($cnt - 1))
    done
    if test $cnt = 0
    then
        echo "failed to reach prim synced in 100 seconds"
        exit 1
    fi
}


#
# Stage 1
#


echo "stage 1: check that causal violations can be produced"
str=`causal --disable-causal-reads --duration 10`
echo "causal: $str"

if test `echo $str | awk '{print $4; }'` == 0
then
    echo "causal test failed to produce attempted causal violations"
    echo "output: $str"
    exit 1
fi

echo "stage 1: check that causal violations are not produced if not attempted"
str=`causal --duration 10`
echo "causal: $str"

if test `echo $str | awk '{ print $4 }'` != 0
then
    echo "causal violations"
    echo "causal: $str"
    exit 1
fi

#
# Stage 2
#

echo "stage 2: check that causal violations are generated in view change if evs.causal_keepalive_period is high enough"

$READ_MYSQL_CTRL -e "set global wsrep_provider_options='evs.causal_keepalive_period=PT100S'"

round=10
violations=0
while test $round -gt 0 && test $violations == 0
do
    $READ_MYSQL_CTRL -e "set global wsrep_provider_options='evs.suspect_timeout=PT9S; evs.keepalive_period=PT100S'"

    echo "round $round"
    f=`mktemp`
    ( causal --duration 15 > $f ) &
    pid=$!
    echo "started causal with pid $pid"

    sleep 1

    echo "isolating second node"
    $READ_MYSQL_CTRL -e "set global wsrep_provider_options='gmcast.isolate=true'"

    echo "waiting for causal test to finish"
    wait $pid

    str=`cat $f`
    rm $f

    echo "causal: $str"
    violations=`echo $str | awk '{ print $4; }'`
    $READ_MYSQL_CTRL -e "set global wsrep_provider_options='gmcast.isolate=false'"

    wait_prim_synced
    round=$(($round - 1))
done

#
if test $violations == 0
then
    echo "stage 2 failed to generate violations"
    exit 1
fi

#
# Stage 3
#

echo "stage 3: check that causal violations don't happen if evs.causal_keepalive_period is short enough"

$READ_MYSQL_CTRL -e "set global wsrep_provider_options='evs.causal_keepalive_period=PT0.5S'"

round=1000
violations=0

echo "running $round rounds, reader is isolated"
while test $round -gt 0 && test $violations == 0
do
    $READ_MYSQL_CTRL -e "set global wsrep_provider_options='evs.suspect_timeout=PT9S; evs.keepalive_period=PT100S'"
    echo "round: $round"

    f=`mktemp`
    ( causal --duration 15 > $f ) &
    pid=$!
    echo "started causal with pid $pid"

    sleep 1

    if test $(($round % 2)) == 0
    then
        echo "isolating second node"
        $READ_MYSQL_CTRL -e "set global wsrep_provider_options='gmcast.isolate=true'"
        sleep 9
        $READ_MYSQL_CTRL -e "set global wsrep_provider_options='gmcast.isolate=false'"
    else
        echo "signalling node 1 to stop"
        signal_node STOP 1
        sleep 9
        signal_node CONT 1
    fi

    echo "waiting for causal test to finish"
    wait $pid

    str=`cat $f`
    rm $f

    echo "output: $str"
    violations=`echo $str | awk '{ print $4; }'`

    wait_prim_synced
    round=$(($round - 1))
done

if test $violations != 0
then
    echo "stage 3 generated violations"
    exit 1
fi