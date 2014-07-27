=====================================
Starting the Cluster
=====================================
.. _`Starting a Cluster`:

When your servers all have databases running and Galera Cluster installed, you are ready to start the cluster.

The databases are not connected to each other as a cluster

Before you begin, ensure that you have:

- At least three database hosts with Galera Cluster installed.

- No firewalls between the hosts.

- SELinux and AppArmor disabled or running in permissive mode.

- Value given on each host for the ``wsrep_provider`` parameter.


-------------------------------------
Creating Client Connections between the Nodes
-------------------------------------
.. _`Creating Client Connections between Nodes`:

Connecting the database servers to each other as a cluster requires that you create client connections between the nodes.  This allows the nodes to carry out state snapshot transfers with each other.

To connect the clients, on each node run the following queries:

.. code-block:: mysql

	GRANT ALL ON *.* TO 'root'@'node1-address';
	GRANT ALL ON *.* TO 'root'@'node2-address';
	GRANT ALL ON *.* TO 'root'@'node3-address';

This grants the root user of each node root privileges on the other nodes in the cluster.


-------------------------------------
Starting the First Cluster Node
-------------------------------------
.. _`Starting First Cluster Node`:

In order to create and bootstrap the first cluster node, you must set up the group communication structure for the entire cluster.

To create and bootstrap the first cluster node, complete the following steps:

1. Start the database server with an empty wsrep cluster address value:

   .. code-block:: console

	$ mysqld --wsrep-new-cluster

  .. warning:: Only use ``--wsrep-new-cluster`` when you want to create a new cluster.  Never use it when you want to reconnect to an existing one.

2. To check that the startup was successful, run the following query in the client:

   .. code-block:: mysql

	SHOW VARIABLES LIKE 'wsrep_cluster_address';

	 +-----------------------+----------+
	 | Variable_name         | Value    |
	 +-----------------------+----------+
	 | wsrep_cluster_address | gcomm:// |
	 +-----------------------+----------+

3. If the output is correct, use a text editor to open your configuration file, (either ``my.cnf`` or ``my.ini`` depending on your build), and add the addresses for the other nodes in the cluster:

   .. code-block:: ini

	wsrep_cluster_address="node2-address, node3-address"

   .. note:: You can use either :abbr:`IP (Internet Protocol)` or :abbr:`DNS (Domain Name System)` addresses.

The first node in your cluster is now live.

	.. note:: Do not restart ``mysqld`` at this point.


-------------------------------------
Adding Additional Nodes to the Cluster
-------------------------------------
.. _`Add Nodes to Cluster`:

Once the first node is live, you can begin adding additional nodes to the cluster.  

To add a new node to an existing cluster, complete the following steps:

1. Before you start ``mysqld``, use a text editor to modify the configuration file (either ``my.cnf`` or ``my.ini``, depending on your build), to enter the addresses for the other nodes in the cluster:

   .. code-block:: ini

	wsrep_cluster_address="node1-address, node3-address"

  .. note:: You can use either :abbr:`IP (Internet Protocol)` or :abbr:`DNS (Domain Name System)` addresses.

2. Start ``mysqld``:

   .. code-block:: console

	$ mysql start

The new node connects to the cluster members as defined by the ``wsrep_cluster_address`` parameter.  It will now automatically retrieve the cluster map and reconnect to the rest of the nodes.

Repeat this process for each node in the cluster.

When all nodes in the cluster agree on the membership state, the they will initiate exchange.  In state exchange, the new node checks cluster state.  If the node state differs from the cluster state, (which is normally the case), the new node requests a state snapshot from the cluster and installs it.  After this, the new node is ready for use.


-------------------------------------
Understanding Cluster Addresses
-------------------------------------
.. _`Understand Cluster Address`:

For each node in the cluster, you must enter an address in the ``wsrep_cluster_address`` parameter of your configuration file.

The syntax for cluster addresses is explained below:

.. code-block:: ini

	<backend schema>://<cluster address>[?option1=value1[&option2=value2]]

- ``<backend schema>``: Indicates the Galera Cluster schema.

  - ``dummy``: This schema is a pass-through backend for testing and profiling purposes.  It does not connect to anywhere.  The node ignores any values given to it.

  - ``gcomm``: This schema is the group communication backend for use in production.  It takes an address and has several settings that you can enable through the option list or through the configuration file, using the ``wsrep_provider_options`` parameter.

- ``<cluster address>``: The address for each node in the cluster.

  - An address of any current member, if you want to connect to an existing cluster, or

  - A comma-separated list of possible cluster members, assuming that the list members can belong to no more than one :term:`Primary Component`.  Or,

  - An empty string, if you want this node to the first in a new cluster, (that is, there are no pre-existing node that you want it to connect to).

- ``options``: The option list sets backend parameters, such as the listen address and timeout values.

  .. note:: The option list is not durable and must be resubmitted on every connection to the cluster.  To make the options durable, set them in the configuration file using the ``wsrep_provider_options`` parameter.

  The parameters set in the URL take precedence over parameters set elsewhere, (for example, the configuration file).  Parameters that you can set through the options list are:

  - ``evs.*``

  - ``pc.*``

  - ``gmcast.*``

  You can follow the option list with a list of ``key=value`` queries according to the URL standard.

  .. note:: If the listen address and port are not set in the parameter list, ``gcomm`` will listen on all interfaces.  The listen port will be taken from the cluster address.  If it is not specified in the cluster address, the default port is ``4567``.

