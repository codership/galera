HowTo: Start your first Galera Cluster
======================================

What you need
-------------

You need 3 hosts running a recent Linux distribution. 

.. note:: With the instructions given here, various Galera features will default
          to using the first network interface. For this reason, the below
          instructions are not sufficient for running Galera under Vagrant
          Virtualbox instances. See :doc:`/configuration_and_monitoring/configuring_galera_for_vagrant`.

In the below, we will use the hostnames: *host1*, *host2*, *host3*.

Make sure there is no firewall between the nodes and that `selinux` or `apparmor`
are disabled or running in permissive mode.

Installing
----------

This example uses Ubuntu Precise and apt-get. For other Linux 
distributions steps are similar, for details see: :doc:`/installing`.

Easiest way to install Galera is with a package manager like apt or yum. As of
this writing Codership doesn't provide apt and yum repositories, so to keep
things simple we will install Percona XtraDB Cluster 
`from Percona's repositories <http://www.percona.com/doc/percona-server/5.5/installation.html#using-percona-software-repositories?id=repositories:start>`_.

Do the following on *all hosts*:

::
   sudo su
   gpg --keyserver  hkp://keys.gnupg.net --recv-keys 1C4CBDCDCD2EFD2A
   gpg -a --export CD2EFD2A | apt-key add -
   echo deb http://repo.percona.com/apt *precise* main > /etc/apt/sources.list.d/percona.list
   echo deb-src http://repo.percona.com/apt *precise* main >> /etc/apt/sources.list.d/percona.list
   apt-get update
   apt-get install -y percona-xtradb-cluster-server-5.5 percona-xtradb-cluster-client-5.5 percona-xtradb-cluster-galera-2.x
   /etc/init.d/mysql start

.. note:: Change the 2 occurences of "*precise*" with the version of your
          own Ubuntu or Debian installation. (e.g. "quantal" or "squeeze"...)

.. note:: When asked for a MySQL root password, in this example we have pressed
          enter to leave it empty.

You can check that the installation was successful with a command like this:

::

   mysql -e "SHOW VARIABLES LIKE 'version_%'"

Which will output:

::

   +-------------------------+------------------------------------------------+
   | Variable_name           | Value                                          |
   +-------------------------+------------------------------------------------+
   | version_comment         | Percona XtraDB Cluster (GPL), wsrep_23.7.r3821 |
   | version_compile_machine | i686                                           |
   | version_compile_os      | Linux                                          |
   +-------------------------+------------------------------------------------+

Starting a Galera cluster
-------------------------

You have now successfully installed and started Percona XtraDB Cluster on your 
3 hosts. But the MySQL servers are not yet connected to each other as a cluster,
and the Galera library is not even loaded in the configuration. The MySQL 
servers are simply running as 3 independent databases.

We will use the default *State Snapshot Transfer* method, which is mysqldump.
For this it is necessary to create a mysql client connection between the nodes.
We need to grant root privileges in the MySQL user database to allow that:

On *all nodes* do the following:

   mysql
   GRANT ALL ON *.* TO 'root'@'host1';
   GRANT ALL ON *.* TO 'root'@'host2';
   GRANT ALL ON *.* TO 'root'@'host3';
   exit


*Starting the first node is special.* Do the following *on host1* only:

::

   cp /usr/share/mysql/wsrep.cnf /etc/mysql/my.cnf

Edit /etc/mysql/my.cnf with your favorite editor (mine is nano):

::

   nano /etc/mysql/my.cnf

Find the following options and set these values for them:

::

   wsrep_provider="/usr/lib/libgalera_smm.so"
   wsrep_cluster_address="gcomm://"

The "gcomm:://" cluster address tells MySQL that it is the first node in the
cluster. It will start operating immediately without trying to connect to any
other nodes. You should only use this when starting the first cluster node
for the first time!

Save the changes, and start MySQL:

::

   /etc/init.d/mysql restart

Check that startup succeeded and changes are applied:

::

   mysql -e "SHOW VARIABLES LIKE 'wsrep_cluster_address'"

   +-----------------------+----------+
   | Variable_name         | Value    |
   +-----------------------+----------+
   | wsrep_cluster_address | gcomm:// |
   +-----------------------+----------+

Immediately after startup, open my.cnf in your editor again, and change the 
value of `wsrep_cluster_address` so that it points to the other two nodes:

::

   wsrep_cluster_address="host2,host3"

.. note:: Do not restart MySQL at this point.

.. warning:: You should never leave `wsrep_cluster_address="gcomm://"` in your
             my.cnf configuration file. Always change it so that it lists the
             other nodes in your cluster. For more information, see :ref:`option_wsrep_cluster_address`.

Do the following *on host2*:

::

   cp /usr/share/mysql/wsrep.cnf /etc/mysql/my.cnf

Edit /etc/mysql/my.cnf. Find the following options and set these values for 
them:

::

   wsrep_provider="/usr/lib/libgalera_smm.so"
   *wsrep_cluster_address="host1,host3"*

Restart MySQL:

   /etc/init.d/mysql restart

Do the following *on host3*:

::

   cp /usr/share/mysql/wsrep.cnf /etc/mysql/my.cnf

Edit /etc/mysql/my.cnf. Find the following options and set these values for 
them:

::

   wsrep_provider="/usr/lib/libgalera_smm.so"
   *wsrep_cluster_address="host1,host2"*

Restart MySQL:

   /etc/init.d/mysql restart

Testing that the cluster works
------------------------------

You should now have a Galera cluster up and running! 

On *any node*, connect to MySQL:

::

   mysql

You can verify that all nodes have connected by checking the following status 
variables:

::

   show status like 'wsrep_%';

   +----------------------------+--------------------------------------+
   | Variable_name              | Value                                |
   +----------------------------+--------------------------------------+
   ...
   | wsrep_local_state_comment  | Synced (6)                           |
   | wsrep_cluster_size         | 3                                    |
   | wsrep_ready                | ON                                   |
   +----------------------------+--------------------------------------+

In the above we see that this node is *Synced* ie it is connected to the 
cluster and operational. There are a total of *3* nodes in the cluster.
The `wsrep_ready` status variable also tells us that this node is connected
to the cluster and able to handle transactions.

We can now create a test table and insert some data.

On *host1* open a MySQL prompt and do:

::

   CREATE DATABASE galeratest;
   use galeratest
   CREATE TABLE t (id INT PRIMARY KEY auto_increment, msg TEXT);
   INSERT INTO t (msg) VALUES ("Hello my dear cluster");
   INSERT INTO t (msg) VALUES ("Hello again");

On *host2*, see if the data was replicated correctly:

::

   use galeratest
   SELECT * FROM t;

   +----+-----------------------+
   | id | msg                   |
   +----+-----------------------+
   |  3 | Hello my dear cluster |
   |  6 | Hello again           |
   +----+-----------------------+

It worked!

(Notice that the auto_increment id's are not 1 and 2. This is a Galera feature,
it is ok. See :ref:`option_wsrep_auto_increment_control`)

