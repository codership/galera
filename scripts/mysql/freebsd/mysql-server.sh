#!/bin/sh
#
# $FreeBSD: tags/RELEASE_9_1_0/databases/mysql55-server/files/mysql-server.in 302141 2012-08-05 23:19:36Z dougb $
#

# PROVIDE: mysql
# REQUIRE: LOGIN
# KEYWORD: shutdown

#
# Add the following line to /etc/rc.conf to enable mysql:
# mysql_enable (bool):	Set to "NO" by default.
#			Set it to "YES" to enable MySQL.
# mysql_limits (bool):	Set to "NO" by default.
#			Set it to yes to run `limits -e -U mysql`
#			just before mysql starts.
# mysql_dbdir (str):	Default to "/var/db/mysql"
#			Base database directory.
# mysql_pidfile (str):	Custum PID file path and name.
#			Default to "${mysql_dbdir}/${hostname}.pid".
# mysql_args (str):	Custom additional arguments to be passed
#			to mysqld_safe (default empty).
#

. /etc/rc.subr

name="mysql"
rcvar=mysql_enable

load_rc_config $name

: ${mysql_enable="NO"}
: ${mysql_limits="NO"}
: ${mysql_dbdir="/var/db/mysql"}

mysql_user="mysql"
mysql_limits_args="-e -U ${mysql_user}"
pidfile=${mysql_pidfile:-"${mysql_dbdir}/`/bin/hostname`.pid"}
command="/usr/sbin/daemon"
command_args="-c -f /usr/local/bin/mysqld_safe --defaults-extra-file=${mysql_dbdir}/my.cnf --user=${mysql_user} --datadir=${mysql_dbdir} --pid-file=${pidfile} ${mysql_args}"
procname="/usr/local/libexec/mysqld"
start_precmd="${name}_prestart"
start_postcmd="${name}_poststart"
mysql_install_db="/usr/local/bin/mysql_install_db"
mysql_install_db_args="--basedir=/usr/local --datadir=${mysql_dbdir} --force"
service_startup_timeout=900
startup_sleep=1
sst_progress_file=${mysql_dbdir}/sst_in_progress
extra_commands="bootstrap"
bootstrap_cmd="mysql_bootstrap"
for gcc in gcc44 gcc48
do
    gcc_dir=/usr/local/lib/${gcc}
    [ -d "${gcc_dir}" ] && export LD_LIBRARY_PATH="${gcc_dir}:$LD_LIBRARY_PATH"
done

mysql_bootstrap()
{
	# Bootstrap the cluster, start the first node that initiate the cluster
	check_startmsgs && echo "Bootstrapping cluster"
	shift
	$0 start $rc_extra_args --wsrep-new-cluster
}

mysql_create_auth_tables()
{
	eval $mysql_install_db $mysql_install_db_args >/dev/null 2>/dev/null
        [ $? -eq 0 ] && chown -R ${mysql_user}:${mysql_user} ${mysql_dbdir}
}

mysql_prestart()
{
	if [ ! -d "${mysql_dbdir}/mysql/." ]; then
		mysql_create_auth_tables || return 1
	fi
	if checkyesno mysql_limits; then
		eval `/usr/bin/limits ${mysql_limits_args}` 2>/dev/null
	else
		return 0
	fi
}

mysql_poststart()
{
	local timeout=${service_startup_timeout}
	while [ ! -f "${pidfile}" -a ${timeout} -gt 0 ]; do
		if test -e $sst_progress_file && [ $startup_sleep -ne 100 ]; then
			check_startmsgs && echo "SST in progress, setting sleep higher"
			startup_sleep=100
		fi
		timeout=$(( timeout - 1 ))
		sleep $startup_sleep
	done
	return 0
}

run_rc_command "$1"
