=====================
 Creating a Cluster
=====================
.. _`Creating a Cluster`:

To create and bootstrap a new cluster, you must set up the group
communication structure for the cluster nodes. Proceed as follows:

1. Power up the servers that will join the cluster. Do not
   start the *mysqld* servers yet.
2. Define an empty cluster address URL for the first *mysqld*
   server. Specify this :abbr:`URL (Uniform Resource Locator)`
   either in the *my.cnf* configuration
   file or by issuing the command below:

   ``$ mysqld --wsrep_cluster_address=gcomm://``
   
   This command implies to the starting *mysqld* server that
   there is no existing cluster to connect to, and the server
   will create a new history :abbr:`UUID (Universally Unique Identifier)`.
   
   .. warning:: Only use an empty *gcomm* address when you want to
                create a new cluster. Never use it when you want to reconnect
                to an existing one.

3. Start the first *mysqld* server with an empty cluster
   address URL.
4. To add the second node to the cluster, see
   chapter `Adding Nodes to a Cluster`_ below.

-----------------------------
 Adding Nodes to a Cluster
-----------------------------
.. _`Adding Nodes to a Cluster`:
.. index::
   pair: Weighted Quorum; Setting weight on a node

To add a new node to an existing cluster, proceed as follows:

1. Power up the server that will join the cluster. Do not
   start the *mysqld* server yet.
2. Define a :abbr:`URL (Uniform Resource Locator)` address to
   one of the existing cluster nodes for the new node by issuing
   the command below:

   ``$ mysqld --wsrep_cluster_address=gcomm://192.168.0.1``

   .. note:: You can also use :abbr:`DNS (Domain Name System)` addresses.

   This command implies to the starting *mysqld* server that
   there an existing cluster to connect to.
3. (Optional) If the node will be part of a weighted quorum, set the
   initial node weight to zero. In this way, it can be guaranteed
   that if the joining node fails before it gets synchronized,
   it does not have effect in the quorum computation that follows. 
4. Start the *mysqld* server.
5. The new node connects to the defined cluster member. It will
   automatically retrieve the cluster map and reconnect to the
   rest of the nodes, if any.

As soon as all cluster members agree on the membership, state
exchange will be initiated. In state exchange, the new node is
informed of the cluster state. If the node state differs from
the cluster state (which is normally the case), the new node
requests for a state snapshot from the cluster and installs
it. After this, the new node is ready for use.
