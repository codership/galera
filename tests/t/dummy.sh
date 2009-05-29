#!/bin/bash


BASE_DIR=$(cd $(dirname $0); pwd -P)


. $BASE_DIR/../scripts/tap-functions

REPORT_DIR="$TEST_REPORT_DIR/dummy"
mkdir $REPORT_DIR

echo "args: " $@ 1>&2
echo "pwd: " `pwd`
echo "output: " $REPORT_DIR

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