#!/bin/bash


BASE_DIR=$(cd $(dirname $0); pwd -P)


. $BASE_DIR/../tap/tap-functions

REPORT_DIR="$TEST_REPORT_DIR/dummy"
if ! test -d $REPORT_DIR
then
    mkdir $REPORT_DIR
fi

DUMMY_LOG=$REPORT_DIR/dummy.log

echo "args: " $@ >> $DUMMY_LOG
echo "pwd: " `pwd` >> $DUMMY_LOG
echo "output: " $REPORT_DIR >> $DUMMY_LOG
echo "nodes :" $CLUSTER_NODES >> $DUMMY_LOG
echo "n_nodes: " $CLUSTER_N_NODES >> $DUMMY_LOG

plan_tests 7

ok 0 "first"
okx  test 1 -eq 1 
is 1 1 "third"
isnt 1 2 "fourth"

skip ok "maybe not ok but skip" 

TODO="not implemented yet"
ok 0 "test not implemented yet"
unset TODO

diag "some message"

ok 0 "final"