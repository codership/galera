#!/bin/sh -eu

OS=$(uname)
[ $OS = "Linux" ] && JOBS=$(grep -c ^processor /proc/cpuinfo) || JOBS=1

TEST_ROOT=$(cd $(dirname $0); pwd -P)
GALERA_SRC=$TEST_ROOT/../../

LOGFILE=cppcheck.log

# --xml output provides us with error ID in case we need to add suppression
cppcheck --quiet -j $JOBS --force --inline-suppr --xml --inconclusive \
         $GALERA_SRC 2>$LOGFILE

RCODE=$(grep -c '^<error' cppcheck.log) || :

[ $RCODE -gt 255 ] && RCODE=255
[ $RCODE -eq 0   ] && rm cppcheck.log

# return the number of errors capped by 255
exit $RCODE
