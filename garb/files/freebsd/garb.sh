#!/bin/sh
#
# garb.sh for rc.d usage (c) 2013 Codership Oy
# $Id$

# PROVIDE: garb
# REQUIRE: LOGIN
# KEYWORD: shutdown
#
# Add the following line to /etc/rc.conf to enable Galera Arbitrator Daemon (garbd):
#  garb_enable (bool):         Set to "NO" by default.
#                              Set it to "YES" to enable Galera Arbitrator Daemon.
#  garb_galera_nodes (str):    A space-separated list of node addresses (address[:port]) in the cluster
#                              (default empty).
#  garb_galera_group (str):    Galera cluster name, should be the same as on the rest of the nodes.
#                              (default empty).
# Optional:
#  garb_galera_options (str):  Optional Galera internal options string (e.g. SSL settings)
#                              see http://www.codership.com/wiki/doku.php?id=galera_parameters
#                              (default empty).
#  garb_log_file (str):        Log file for garbd (default empty). Optional, by default logs to syslog
#  garb_pid_file (str):        Custum PID file path and name.
#                              Default to "/var/run/garb.pid".
#

. /etc/rc.subr

name="garb"
rcvar=garb_enable

load_rc_config $name

# set defaults
: ${garb_enable="NO"}
: ${garb_galera_nodes=""}
: ${garb_galera_group=""}
: ${garb_galera_options=""}
: ${garb_log_file=""}
: ${garb_pid_file="/var/run/garb.pid"}
: ${garb_working_directory=""}

procname="/usr/local/bin/garbd"
command="/usr/sbin/daemon"
command_args="-c -f -u nobody -p $garb_pid_file $procname"
start_precmd="${name}_prestart"
#start_cmd="${name}_start"
start_postcmd="${name}_poststart"
stop_precmd="${name}_prestop"
#stop_cmd="${name}_stop"
#stop_postcmd="${name}_poststop"
#extra_commands="reload"
#reload_cmd="${name}_reload"
export LD_LIBRARY_PATH=/usr/local/lib/gcc44

garb_prestart()
{
	[ "$(id -ur)" != "0" ] && err 4 "root rights are required to start $name"
	[ -r "$garb_pid_file" ] && err 0 "$procname is already running with PID $(cat $garb_pid_file)"
	[ -x "$procname" ] || err 5 "$procname is not found"

	# check that node addresses are configured
	[ -z "$garb_galera_nodes" ] && err 6 "List of garb_galera_nodes is not configured"
	[ -z "$garb_galera_group" ] && err 6 "garb_galera_group name is not configured"

	GALERA_PORT=${GALERA_PORT:-4567}

	# Concatenate all nodes in the list (for backward compatibility)
	ADDRESS=
	for NODE in ${garb_galera_nodes}; do
		[ -z "$ADDRESS" ] && ADDRESS="$NODE" || ADDRESS="$ADDRESS,$NODE"
	done

	command_args="$command_args -a gcomm://$ADDRESS"
	[ -n "$garb_galera_group" ]   && command_args="$command_args -g $garb_galera_group"
	[ -n "$garb_galera_options" ] && command_args="$command_args -o $garb_galera_options"
	[ -n "$garb_log_file" ]       && command_args="$command_args -l $garb_log_file"
	[ -n "$garb_working_directory" ] && command_args="$command_args -w $garb_working_directory"
	return 0
}

garb_poststart()
{
	local timeout=15
	while [ ! -f "$garb_pid_file" -a $timeout -gt 0 ]; do
		timeout=$(( timeout - 1 ))
		sleep 1
	done
	return 0
}

garb_prestop() {
	[ "$(id -ur)" != "0" ] && err 4 "root rights are required to stop $name"
	[ -r $garb_pid_file ] || err 0 ""
	return 0
}

run_rc_command "$1"
