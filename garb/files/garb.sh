#!/bin/bash
#
# Copyright (C) 2012-2013 Codership Oy <info@codership.com>
#
# init.d script for garbd
#
# chkconfig: - 99 01
# config: /etc/sysconfig/garb | /etc/default/garb

### BEGIN INIT INFO
# Provides:          garb
# Required-Start:    $network $local_fs
# Required-Stop:     $network $local_fs
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Galera Arbitrator Daemon
# Description:       Galera Arbitrator Daemon is used
#                    as part of clusters that have only two
#                    real Galera servers and need an extra
#                    node to arbitrate split brain situations.
### END INIT INFO

# Source function library.
if [ -f /etc/redhat-release ]; then
	. /etc/init.d/functions
	. /etc/sysconfig/network
	config=/etc/sysconfig/garb
else
	. /lib/lsb/init-functions
	config=/etc/default/garbd
fi

log_failure() {
	if [ -f /etc/redhat-release ]; then
		echo -n $*
		failure "$*"
		echo
	else
		log_failure_msg "$*"
	fi
}

PIDFILE=/var/run/garbd

prog=$(which garbd)

program_start() {
	local rcode
	if [ -f /etc/redhat-release ]; then
		echo -n $"Starting $prog: "
		sudo -u nobody $prog $* >/dev/null
		rcode=$?
		[ $rcode -eq 0 ] && pidof $prog > $PIDFILE \
		&& echo_success || echo_failure
		echo
	else
		log_daemon_msg "Starting $prog: "
		start-stop-daemon --start --quiet --background \
		                  --exec $prog -- $*
		rcode=$?
		# Hack: sleep a bit to give garbd some time to fork
		sleep 1
		[ $rcode -eq 0 ] && pidof $prog > $PIDFILE
		log_end_msg $rcode
	fi
	return $rcode
}

program_stop() {
	local rcode
	if [ -f /etc/redhat-release ]; then
		echo -n $"Shutting down $prog: "
		killproc -p $PIDFILE
		rcode=$?
		[ $rcode -eq 0 ] && echo_success || echo_failure
#		echo
	else
		start-stop-daemon --stop --quiet --oknodo --retry TERM/30/KILL/5 \
		                  --pidfile $PIDFILE
		rcode=$?
		log_end_msg $rcode
	fi
	[ $rcode -eq 0 ] && rm -f $PIDFILE
	return $rcode
}

program_status() {
	if [ -f /etc/redhat-release ]; then
		status $prog
	else
		status_of_proc -p $PIDFILE "$prog" garb
	fi
}

start() {
	[ "$EUID" != "0" ] && return 4
	[ "$NETWORKING" = "no" ] && return 1

	if grep -q -E '^# REMOVE' $config;then 
	    log_failure "Garbd config $config is not configured yet"
	    return 0
	fi

	if [ -r $PIDFILE ]; then
		log_failure "$prog is already running with PID $(cat ${PIDFILE})"
		return 3 # ESRCH
	fi

	[ -x $prog ] || return 5
	[ -f $config ] && . $config
	# Check that node addresses are configured
	if [ -z "$GALERA_NODES" ]; then
		log_failure "List of GALERA_NODES is not configured"
		return 6
	fi
	if [ -z "$GALERA_GROUP" ]; then
		log_failure "GALERA_GROUP name is not configured"
		return 6
	fi

	GALERA_PORT=${GALERA_PORT:-4567}

	# Find a working node
	for ADDRESS in ${GALERA_NODES} 0; do
		HOST=$(echo $ADDRESS | cut -d \: -f 1 )
		PORT=$(echo $ADDRESS | cut -d \: -f 2 )
		PORT=${PORT:-$GALERA_PORT}
		if [[ -x `which nc` ]] && nc -h 2>&1 | grep -q  -- '-z';then
                    nc -z $HOST $PORT >/dev/null && break
                elif [[ -x `which nmap` ]];then
                    nmap -Pn -p$PORT $HOST | awk "\$1 ~ /$PORT/ {print \$2}" | grep -q open && break
                else
                    log_failure "Neither netcat nor nmap are present for zero I/O scanning"
                    return 1
                fi
	done
	if [ ${ADDRESS} == "0" ]; then
		log_failure "None of the nodes in $GALERA_NODES is accessible"
		return 1
	fi

	OPTIONS="-d -a gcomm://$ADDRESS"
	[ -n "$GALERA_GROUP" ]   && OPTIONS="$OPTIONS -g $GALERA_GROUP"
	[ -n "$GALERA_OPTIONS" ] && OPTIONS="$OPTIONS -o $GALERA_OPTIONS"
	[ -n "$LOG_FILE" ]       && OPTIONS="$OPTIONS -l $LOG_FILE"

	program_start $OPTIONS
}

stop() {
	[ "$EUID" != "0" ] && return 4
	[ -r $PIDFILE ]    || return 3 # ESRCH
	program_stop
}

restart() {
	stop
	start
}

# See how we were called.
case "$1" in
  start)
	start
	;;
  stop)
	stop
	;;
  status)
	program_status
	;;
  restart|reload|force-reload)
	restart
	;;
  condrestart)
	if status $prog > /dev/null; then
	    stop
	    start
	fi
	;;
  *)
	echo $"Usage: $0 {start|stop|status|restart|reload}"
	exit 2
esac

exit 0
