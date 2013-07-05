=====================
 Starting a Cluster
=====================
.. _`Starting a Cluster`:

This chapter describes how to start up a cluster with three nodes.
We use host names *host1*, *host2* and *host3* in the command
examples.

------------------
 Before You Start
------------------
.. _`Before You Start`:

Before you start, ensure that you have:

- Three database hosts with the *Galera Cluster* installed.
- No firewalls between the hosts.
- *Selinux* or *apparmor* disabled or running in permissive mode.
- Defined the ``wsrep_provider`` parameter value.

.. seealso:: Chapter :ref:`Using Galera Cluster with SElinux <Using Galera Cluster with SElinux>`

------------------------------------------------------
 Creating a MySQL Client Connection Between the Nodes
------------------------------------------------------
 .. _`Creating a MySQL Client Connection Between the Nodes`:
 
After the MySQL and *Galera Cluster*
installations, the MySQL servers are
running as three independent databases. They are not connected
to each other as a cluster. Furthermore, the Galera Replication
Plugin is not loaded in the configuration.

To connect the MySQL servers to each other as a cluster, you
must create MySQL client connections between the nodes. In
this way, the nodes can carry out state snapshot transfers
with each other.

In this example, you use the default state snapshot transfer
method, that is, *mysqldump*. Grant root privileges in the
MySQL user database on all nodes to allow this, as follows:

::

    mysql
    GRANT ALL ON *.* TO 'root'@'host1';
    GRANT ALL ON *.* TO 'root'@'host2';
    GRANT ALL ON *.* TO 'root'@'host3';
    exit

------------------------------------
 Starting the First Cluster Node
------------------------------------

.. index::
   pair: Parameters; wsrep-cluster-address

To create and bootstrap the first cluster node, you must set up
the group communication structure for the cluster nodes. Proceed
as follows:

1. Power up the servers that will join the cluster.
2. Start the first *mysqld* server with an empty cluster
   address URL::
 
     /etc/init.d/mysql start --wsrep-cluster-address="gcomm://"
   
   .. warning:: Only use an empty *gcomm* address when you want to
                create a new cluster. Never use it when you want to reconnect
                to an existing one.

3. Check that the startup succeeded::
   
     mysql -e "SHOW VARIABLES LIKE 'wsrep_cluster_address'"

     +-----------------------+----------+
     | Variable_name         | Value    |
     +-----------------------+----------+
     | wsrep_cluster_address | gcomm:// |
     +-----------------------+----------+
   
4. Immediately after startup, open the *my.cnf* configuration file
   in a text editor and change the value of ``wsrep_cluster_address``
   to point to the other two nodes::
   
     wsrep_cluster_address="host2,host3"
   
   .. note:: Do not restart MySQL at this point.
   
   .. note:: You can also use :abbr:`IP (Internet protocol)` addresses.
   
5. To add the second and third node to the cluster, see
   chapter `Adding Nodes to a Cluster`_ below.

-----------------------------
 Adding Nodes to a Cluster
-----------------------------
.. _`Adding Nodes to a Cluster`:

.. index::
   pair: Weighted Quorum; Setting weight on a node
.. index::
   pair: Parameters; wsrep_provider
.. index::
   pair: Parameters; wsrep-cluster-address
.. index::
   single: my.cnf

To add a new node to an existing cluster, proceed as follows:

1. Power up the server that will join the cluster. Do not
   start the *mysqld* server yet.
2. Define the wsrep provider and the host names for the other
   *mysqld* servers in the cluster. Specify these parameters
   in the *my.cnf* configuration file as follows::

      wsrep_provider="/usr/lib/libgalera_smm.so"
      wsrep_cluster_address="host1,host3"

   .. note:: You can also use :abbr:`IP (Internet protocol)` addresses.

   This command implies to the starting *mysqld* server that
   there an existing cluster to connect to.
3. (Optional) If the node will be part of a weighted quorum, set the
   initial node weight to zero. In this way, it can be guaranteed
   that if the joining node fails before it gets synchronized,
   it does not have effect in the quorum computation that follows. 
   See chapter :ref:`Weighted Quorum <Weighted Quorum>`.
4. Start the *mysqld* server:

   ``/etc/init.d/mysql start``

5. The new node connects to the defined cluster members. It will
   automatically retrieve the cluster map and reconnect to the
   rest of the nodes.

Carry out the procedure above the *node3*. The only difference is
that you must define host *host1* and *host2* for it in step 2
as follows::

    wsrep_provider="/usr/lib/libgalera_smm.so"
    *wsrep_cluster_address="host1,host2"*
   

As soon as all cluster members agree on the membership, state
exchange will be initiated. In state exchange, the new node is
informed of the cluster state. If the node state differs from
the cluster state (which is normally the case), the new node
requests for a state snapshot from the cluster and installs
it. After this, the new node is ready for use.

--------------------------------
 Testing that the Cluster Works
--------------------------------
.. _`Testing that the Cluster Works`:

.. index::
   pair: Parameters; wsrep_local_state_comment

.. index::
   pair: Parameters; wsrep_cluster_size

.. index::
   pair: Parameters; wsrep_ready

You can test that *Galera Cluster* works as follows:

1. Connect to MySQL on any node:

::

   mysql

2. Verify that all nodes have connected to each other by checking
   the following status variables:

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

   In the example above:
   
   - The ``wsrep_local_state_comment`` value *Synced* indicates that
     the node is connected to the cluster and operational.
   - The ``wsrep_cluster_size`` value *3* indicates that there are
     three nodes in the cluster.
   - The ``wsrep_ready`` value *ON* indicates that this node is connected
     to the cluster and able to handle transactions.

3. Create a test table and insert data. On *host1*, open a MySQL prompt
   and issue commands:

::

   CREATE DATABASE galeratest;
   use galeratest
   CREATE TABLE t (id INT PRIMARY KEY auto_increment, msg TEXT);
   INSERT INTO t (msg) VALUES ("Hello my dear cluster");
   INSERT INTO t (msg) VALUES ("Hello again");

4. Check that the data was replicated correctly. On *host2*, open
   a MySQL prompt and issue commands:

::

   use galeratest
   SELECT * FROM t;

   +----+-----------------------+
   | id | msg                   |
   +----+-----------------------+
   |  3 | Hello my dear cluster |
   |  6 | Hello again           |
   +----+-----------------------+

5. The results above indicate that the cluster works.

--------------------
 Failure Simulation
--------------------
.. _`Failure Simulation`:

You can also test *Galera Cluster* by simulating various
failure situations on three nodes as follows:

- To simulate a crash of a single *mysqld* process, run the command
  below on one of the nodes:

      ``killall -9 mysqld``

- To simulate a network disconnection, use *iptables* or *netem*
  to block all TCP/IP traffic to a node.
- To simulate an entire server crash, run each *mysqld* in a
  virtualized guest, and abrubtly terminate the entire
  virtual instance.

If you have three or more *Galera Cluster*
nodes, the cluster should be able to survive the simulations.

---------------------
 Split-brain Testing
---------------------

.. index::
   pair: Split-brain; Prevention

You can test *Galera Cluster* for split-brain
situations on a two node cluster as follows:

- Disconnect the network connection between the cluster nodes.
  The quorum is lost, and the nodes do not serve requests.
- Re-connect the network connection between the cluster nodes.
  The quorum remains lost, and the nodes do not serve requests.
- Run the command below on one of the servers::

     mysql> SET GLOBAL wsrep_provider_options='pc.bootstrap=1';

  This command resets the quorum and the cluster is recovered. 

----------------------------------
 Galera Cluster for MySQL URL
----------------------------------
.. _`Galera Cluster for MySQL URL`:
.. index::
   single: my.cnf

The syntax for the *Galera Cluster*
URL address where the nodes connect to, is shown below::

    <backend schema>://<cluster address>[?option1=value1[&option2=value2]]

where:

- ``<backend schema>`` |---| Refers to the *Galera Cluster*
  schema. *Galera Cluster* supports two schemata:
  
    - ``dummy`` |---| This schema is a pass-through backend for
      testing and profiling purposes. The schema does not connect
      to anywhere. Any values given with it will be ignored.
    - ``gcomm`` |---| This schema is a real group communication
      backend used for production purposes. This backend takes
      an address and has a number of parameters that can be set
      throught the option list (or through ``wsrep_provider_options``
      as of version 0.8.x).

- ``<cluster address>`` |---| The cluster adress must be:

    - An address of any current cluster member, if you want to
      connect the node to an existing cluster, or
    - A comma-separated list of possible cluster members. It is
      assumed that the list members can belong to no more than
      one :term:`Primary Component`. Or
    - An empty string, if you want this node to be the first in
      a new cluster (that is, there are no pre-existing nodes to
      connect to).

  .. note:: As of version 2.2, *Galera Cluster*
            supports a comma-separated list of cluster members in the
            cluster address, such as::

                gcomm://node1,node2:port2,node3?key1=value1&key2=value2...

  .. warning:: Only use an empty ``gcomm://`` address when you want to
               create a *new* cluster. Never use it when your intention
               is to reconnect to an existing one. Furthermore, never
               leave it hard coded in any configuration files.

  .. note:: One way to avoid editing the *my.cnf* configuration file to
            to remove ``gcomm://`` is to start all cluster nodes with the
            following URL::

                gcomm://node1,node2:port2,node3?pc.wait_prim=no&...
                
            The ``pc.wait_prim=no`` option makes the node to wait for a
            primary component indefinitely. Then bootstrap the primary
            component by setting ``pc.bootstrap=1`` on any other node.

            However, you can only use the ``pc.wait_prim=no`` option with
            mysqldump SST, as the MySQL parser must be initialized before
            SST, to pass the ``pc.bootstrap=1`` option.

- ``options`` |---| The option list can be used to set backend parameters,
  such as the listen address and timeout values. In version .7.x, this was
  the only way to customize *Galera Cluster* behavior. The parameter values
  set in the option list are not durable and must be resubmitted on every
  connection to the cluster. As of version 0.8, customized parameters can
  be made durable by seting them in ``wsrep_provider_options``.
  
  The parameters specified in the URL take precedence
  over parameters specified elsewhere (for example ``wsrep_provider_options``
  in the *my.cnf* configuration file).
  
  Parameters that you can set through the option list are
  ``evs.*``, ``pc.*`` and ``gmcast.*``.
  
  The option list can be optionally followed by a list of ``key=value`` *queries*
  according to the URL standard.
  
  .. note:: If the listen address and port are not set in the parameter
            list, ``gcomm`` will listen on all interfaces. The listen
            port will be taken from the cluster address. If it is not
            specified in the cluster address, the default port 4567
            will be used.


.. |---|   unicode:: U+2014 .. EM DASH
   :trim: