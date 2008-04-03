#!/bin/sh
#
# This script checks the output of the gcs_test program
# to verify that all actions that were sent were received
# intact
#
# $Id$

SEND_LOG="gcs_test_send.log"
RECV_LOG="gcs_test_recv.log"

echo "Sent     action count: $(wc -l $SEND_LOG)"
echo "Received action count: $(wc -l $RECV_LOG)"

SEND_MD5=$(cat "$SEND_LOG" | awk '{ print $4 " " $5 }'| sort -n -k 2 | tee sort_send | md5sum)
echo "send_log md5: $SEND_MD5"
RECV_MD5=$(cat "$RECV_LOG" | awk '{ print $4 " " $5 }'| sort -n -k 2 | tee sort_recv | md5sum)
echo "recv_log md5: $RECV_MD5"

#
