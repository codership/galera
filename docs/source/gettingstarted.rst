=================
 Getting Started
=================
.. _`Getting Started with Galera Cluster`:


The :term:`Galera Replication Plugin` is a synchronous multi-master replication plugin for MySQL, MariaDB and Percona XtraDB.  It features:

- Unconstrained parallel application, (also known as parallel replication)

- Multicast replication

- Automatic node provisioning

The primary focus is data consistency.  The transactions are either applied on every node or not all.  So, the databases stay synchronized, provided that they were properly configured and synchronized at the beginning.

The Galera Replication Plugin differs from the regular MySQL Replication by addressing several issues, including multi-master write conflicts, replication lag and slaves being out of sync with the master.

--------------------------------------
How Galera Cluster Works
--------------------------------------
.. `How Galera Works`:

In a typical instance of a Galera Cluster, applications can write to any node in the cluster and transaction commits, (RBR events), are then applied to all the servers, through certification-based replication.

.. figure:: images/galerausecases1.png


Certification-based replication is an alternative approach to synchronous database replication, using group communication and transaction ordering techniques.


----------------------------
System Requirements
----------------------------
.. _`System Requirements`:

Galera Cluster requires:

- Server hardware for a minimum of three nodes
- 100 Mbps or better network connectivity
- Linux
- Database server for MySQL, MariaDB or Percona XtraDB
- wsrep API
- Galera Replication Plugin

.. note:: For security and performance reasons, it's recommended that you run Galera Cluster on its own subnet.


------------------------------
Installation
------------------------------
.. _`Installation`:

Galera Cluster runs in Linux on MySQL, MariaDB and Percona XtraDB through the Galera Replicator Plugin.  You can install the plugin into any of these database servers through your package manager or by building it from source.

**Galera Cluster for MySQL**

- :doc:`installmysqldeb`
- :doc:`installmysqlrpm`
- :doc:`installmysqlsrc`

**Galerea Cluster for MariaDB**

- :doc:`installmariadbdeb`
- :doc:`installmariadbrpm`
- :doc:`installmariadbsrc`


**Galera Cluster for Percona XtraDB**

- :doc:`installxtradbdeb`
- :doc:`installxtradbrpm`
- :doc:`installxtradbsrc`

------------------------------
Configuration
------------------------------
.. _`Galera Configuration`:

Before you can bring Galera Cluster online, each node in the cluster requires some configuration, to grant other nodes access and to enable write-set replication on the database server.

- :doc:`sysconfiguration`
- :doc:`dbconfiguration`


------------------------------
Cluster Management
------------------------------
.. _`Cluster Management`:

When you finish installation and configuration on your server, you're ready to launch the first node and bring the cluster online.  Once all the nodes are started, you can test that they're working and restart if necessary.

- :doc:`startingcluster`
- :doc:`testingcluster`
- :doc:`restartingcluster`


.. toctree::
	:numbered:
	:hidden: 
	
	galerainstallation
	sysconfiguration
	dbconfiguration
	startingcluster
	testingcluster
	restartingcluster