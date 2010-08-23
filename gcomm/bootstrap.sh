#!/bin/sh

# This script bootraps the build process for the freshly checked
# working copy

LOG=$0.log

run_prog()
{
    echo -n "Running $1... "
    $* 1>$LOG 2>&1 && echo "Ok" && rm -f $LOG || \
    (echo "Failed. See $LOG"; return 1)
}
set -e
# Make aclocal to search for m4 macros in /usr/local
if test -d /usr/local/share/aclocal
then
    ACLOCAL_OPTS=" -I /usr/local/share/aclocal "
fi

if test -x "$(which autoreconf)"
then
    export ACLOCAL="aclocal $ACLOCAL_OPTS"
    run_prog autoreconf -fisv
else
    run_prog libtoolize && \
    run_prog aclocal $ACLOCAL_OPTS && \
    run_prog autoheader configure.ac && \
    run_prog automake -af && \
    run_prog autoconf
fi

#

