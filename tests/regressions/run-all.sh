#!/bin/bash

tests="lp1100496
       lp1179361
       lp1184034
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

skipped_tests="lp900816 
               lp917416"

# old way was to run just ~50% of available tests

# tests ignored
#
# lp900816 - does not work out of the box
# lp917416 - hangs
#

#for ii in $tests
#do
#    echo "running test $ii"
#    ( cd $ii && ./run.sh )
#    if test $? != 0
#    then
#        echo "test $ii failed"
#        exit 1
#    fi
#done

# new way to run all tests in directory, 
# but skipping those which were declared to be skipped

for ii in `ls -d ./lp*`
do
    skip=0
    for skipped in { "$skipped_tests" }
    do
	if [ "$ii" == "$skipped" ]
	then
	    echo "skipping test: $ii";
	    skip=1;
	fi
    done
    if [ "$skip" == "0" ]
    then
	echo "running test $ii"
	( cd $ii && ./run.sh )
	if test $? != 0
	then
            echo "test $ii failed"
            exit 1
	fi
    fi
done
