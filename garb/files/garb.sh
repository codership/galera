#!/bin/bash
#
# Copyright (C) 2012-2015 Codership Oy <info@codership.com>
#
# init.d script for garbd
#
# chkconfig: - 99 01
# config: /etc/sysconfig/garb | /etc/default/garb
#
### BEGIN INIT INFO
# Provides:          garb
# Required-Start:    $remote_fs $syslog
# Required-Stop:     $remote_fs $syslog
# Should-Start:      $network $named $time
# Should-Stop:       $network $named $time
# Default-Start:     2 3 4 5
# Default-Stop:      0 1 6
# Short-Description: Galera Arbitrator Daemon
# Description:       The Galera Arbitrator is used as part of clusters
#                    that have only two real Galera servers and need an
#                    extra node to arbitrate split brain situations.
### END INIT INFO

# On Debian Jessie, avoid redirecting calls to this script to 'systemctl start'

_SYSTEMCTL_SKIP_REDIRECT=true

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

if grep -q -E '^# REMOVE' $config; then
	log_failure "Garbd config $config is not configured yet"
	exit 0
fi

PIDFILE=/var/run/garbd

prog="/usr/bin/garbd"

program_start() {
	local rcode
	local gpid
	if [ -f /etc/redhat-release ]; then
                if [ -r $PIDFILE ];then
                    gpid=$(cat $PIDFILE)
                    echo -n $"Stale pid file found at $PIDFILE"
                    if [[ -n ${gpid:-} ]] && kill -0 $gpid;then
                        echo -n $"Garbd already running wiht PID $gpid"
                        exit 17
                    else
                        echo -n $"Removing stale pid file $PIDFILE"
                        rm -f $PIDFILE
                    fi
                fi
		echo -n $"Starting $prog: "
		runuser nobody -s /bin/sh -c "$prog $*" >/dev/null
		rcode=$?
		sleep 2
		[ $rcode -eq 0 ] && pidof $prog > $PIDFILE \
		&& echo_success || echo_failure
		echo
	else

                if [ -r $PIDFILE ];then
                    gpid=$(cat $PIDFILE)
                    log_daemon_msg "Stale pid file found at $PIDFILE"
                    if [[ -n ${gpid:-} ]] && kill -0 $gpid;then
                        log_daemon_msg "Garbd already running wiht PID $gpid"
                        exit 17
                    else
                        log_daemon_msg "Removing stale pid file $PIDFILE"
                        rm -f $PIDFILE
                    fi
                fi
                if [ -r $PIDFILE ];then
                    log_daemon_msg "Stale pid file with $(cat $PIDFILE)"
                fi
		log_daemon_msg "Starting $prog: "
		start-stop-daemon --start --quiet -c nobody --background \
		                  --exec $prog -- "$@"
		rcode=$?
		# Hack: sleep a bit to give garbd some time to fork
		sleep 1
		if [ $rcode -eq 0 ]; then
			pidof $prog > $PIDFILE || rcode=$?
		fi
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

	if [ -r $PIDFILE ]; then
		local PID=$(cat ${PIDFILE})
		if ps -p $PID >/dev/null 2>&1; then
			log_failure "$prog is already running with PID $PID"
			return 3 # ESRCH
		else
			rm -f $PIDFILE
		fi
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

	OPTIONS="-d -a gcomm://${GALERA_NODES// /,}"
	# substitute space with comma for backward compatibility

	[ -n "$GALERA_GROUP" ]   && OPTIONS="$OPTIONS -g '$GALERA_GROUP'"
	[ -n "$GALERA_OPTIONS" ] && OPTIONS="$OPTIONS -o '$GALERA_OPTIONS'"
	[ -n "$LOG_FILE" ]       && OPTIONS="$OPTIONS -l '$LOG_FILE'"

	eval program_start $OPTIONS
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
	exit
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

