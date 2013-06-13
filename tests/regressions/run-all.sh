#!/bin/bash

tests="lp1100496
       lp1179361
       lp1184034
       lp1089490
       lp1073220
       lp1003929
       lp1013978
       lp1026181
       lp1055961
       lp994579
       lp970714
       lp967134
       lp966950
       lp963734
       lp959512
       lp930221
       lp928150
       lp922757
       lp909155
       lp861212
       lp847353
       lp518749
       t285
       t281"

# tests ignored
#
# lp900816 - does not work out of the box
# lp917416 - hangs
#

for ii in $tests
do
    echo "running test $ii"
    ( cd $ii && ./run.sh )
    if test $? != 0
    then
        echo "test $ii failed"
        exit 1
    fi
done
