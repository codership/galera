=================================
Testing the Cluster
=================================
.. _`Testing Galera Cluster`:


-------------------------------------------
Replication Testing
-------------------------------------------

To test that Galera Cluster is working as expected, complete the following steps:

1. On the database client, verify that all nodes have connected to each other::

	SHOW STATUS LIKE 'wsrep_%';

	 +---------------------------+------------+
	 | Variable_name             | Value      |
	 +---------------------------+------------+
	 ...
	 | wsrep_local_state_comment | Synced (6) |
	 | wsrep_cluster_size        | 3          |
	 | wsrep_ready               | ON         |
	 +---------------------------+------------+

  - ``wsrep_local_state_comment``: The value ``Synced`` indicates that the node is connected to the cluster and operational.

  - ``wsrep_cluster_size``: The value indicates the nodes in the cluster.

  - ``wsrep_ready``: The value ``ON`` indicates that this node is connected to the cluster and able to handle transactions.

2. On the database client of node1, create a table and insert data::

	CREATE DATABASE galeratest;
	USE galeratest;
	CREATE TABLE test 
		(id INT PRIMARY KEY AUTO_INCREMENT,
		msg TEST);
	INSERT INTO test (msg)
		VALUES ("Hello my dear cluster.");
	INSERT INTO test (msg)
		VALUES ("Hello, again, cluster dear.");

3. On the database client of node2, check that the data was replicated correctly::

	USE galeratest;
	SELECT * FROM test;

	 +----+-----------------------------+
	 | id | msg                         |
	 +----+-----------------------------+
	 |  1 | Hello my dear cluster.      |
	 |  2 | Hello, again, cluster dear. |
	 +----+-----------------------------+

The results given in the ``SELECT`` query indicates that data you entered in node1 has replicated into node2.


-------------------------------------------
Split-brain Testing
-------------------------------------------

To test Galera Cluster for split-brain situations on a two node cluster, complete the following steps:

1. Disconnect the network connection between the two cluster nodes.  

   The quorum is lost and the nodes do not serve requests.

2. Reconnect the network connection.

   The quorum remains lost, and the nodes do not serve requests.

3. On one of the database clients, reset the quorum::

	SET GLOBAL wsrep_provider_options='pc.bootstrap=1';

The quorum is reset and the cluster recovered.

