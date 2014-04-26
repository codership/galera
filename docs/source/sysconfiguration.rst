==========================================
Configuring the Server
==========================================
.. _`Server Configuration`

After you install Galera Cluster, the next step is to configure your server to function as a node in the cluster.

---------------------------------------
Setting up the Configuration Files
---------------------------------------
.. _`Configuration File`:

After installing Galera Cluster, open the configuration file, (``my.cnf`` or ``my.ini``, depending on your system) and make the following changes:

- Ensure that ``mysqld`` is not bound to 127.0.0.1.  If the configuration variable appears in your configuration file, comment it out.::

	# bind-address=127.0.0.1

- Ensure that the configuration file includes the ``conf.d/`` directory.  For example::

	!includedir /etc/mysql/conf.d/

After you save the configuration file, open the ``wsrep.cnf`` file in the ``conf.d/`` directory and make the following changes:

- Set the login user and password for state snapshot transfers::

	wsrep_sst_auth=wsrep_sst-user:password

  This defines the authentication information required for the state snapshot transfers of new nodes joining the cluster.

After you save these changes, you can start your database server. 

------------------------------
Configuring Database Privileges
------------------------------
.. _`Database Privileges`:

Once your database server is running, you can log into the client and configure the user privileges for the node, to remove empty users and to create the write-set replication user for state snapshot transfers.

In the case of empty users, they create problems for database authentication matching rules.  To remove them, run the following queries as root::

	SET wsrep_on=OFF;
	DELETE FROM mysql.user WHERE user='';

In order for the nodes in the cluster to perform state snapshot transfers, each node requires the configuration of a write-set replication user.  You can create a wsrep user by running the following queries as root::

	SET wsrep_on=OFF;
	GRANT ALL ON *.* TO 'wsrep_sst-user'@'%' IDENTIFIED BY 'password';

For the user and password, use the same values as you assigned to the ``wsrep_sst_auth`` configuration variable above.


--------------------------
Configuring the Firewall
--------------------------
.. _`Firewall Config`:

When you bring your server into the cluster, other nodes must have access to the database server through its client listening socket and through the wsrep provider socket.

For example, on CentOS these are ports 3306, 4567 and 4568::

	$ iptables --insert RH-Firewall-1-INPUT 1 --proto tcp  \
		--source <my IP>/24 --destination <my IP>/32 \
		--dport 3306 -j ACCEPT
	$ iptables --insert RH-Firewall-1-INPUT 1 --proto tcp  \
		--source <my IP>/24 --destination <my IP>/32 \
		--dport 4567 -j ACCEPT
	$ iptables --insert RH-Firewall-1-INPUT 1 --proto tcp  \
		--source <my IP>/24 --destination <my IP>/32 \
		--dport 4568 -j ACCEPT

Additionally, if there is a NAT firewall between the nodes, you must configure it to allow for direct connections between the nodes, (for example, through port forwarding).


----------------------------
Disabling SELinux
----------------------------
.. _`Disable SELinux`:

If you have SELinux enabled, it may block ``mysqld`` from carrying out required operations.  You must disable SELinux or configure it to allow ``mysqld`` to run external programs and open listen sockets at unprivileged ports, (that is, things that an unprivileged user can do).

To disable SELinux, complete the following steps:

1. Change the mode SELinux is running in to permissive by running the following command as root::

	$ setenforce 0

2. Using a text editor, open the config file as ``/etc/selinux/config`` and change the default mode to permissive::

	SELINUX=permissive

For more information, see the SELinux Documentation.


----------------------------
Disabling AppArmor
----------------------------
.. _`Disable AppArmor`:

By default, Ubuntu servers include AppArmor, which may prevent ``mysqld`` from openning additional ports or running scripts.  You must disable AppArmor or configure it to allow ``mysqld`` to run external programs and open listen sockets at unprivileged ports.

To disable AppArmor, run the following commands::

	$ cd /etc/apparmor.d/disable/
	$ sudo ln -s /etc/apparmor.d/usr.sbin.mysqld
	$ sudo service apparmor restart

For more information, see the AppArmor Documentation.