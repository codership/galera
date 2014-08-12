=================
 Getting Started
=================
.. _`getting-started`:


The :term:`Galera Replication Plugin` is a synchronous multi-master replication plugin for MySQL, MariaDB and Percona XtraDB.  It features:

- Unconstrained parallel application, (also known as parallel replication)

- Multicast replication

- Automatic node provisioning

The primary focus is data consistency.  The transactions are either applied on every node or not all.  So, the databases stay synchronized, provided that they were properly configured and synchronized at the beginning.

The Galera Replication Plugin differs from the regular MySQL Replication by addressing several issues, including multi-master write conflicts, replication lag and slaves being out of sync with the master.

--------------------------------------
How Galera Cluster Works
--------------------------------------
.. `how-galera-works`:

In a typical instance of a Galera Cluster, applications can write to any node in the cluster and transaction commits, (RBR events), are then applied to all the servers, through certification-based replication.

.. figure:: images/galerausecases1.png


Certification-based replication is an alternative approach to synchronous database replication, using group communication and transaction ordering techniques.


----------------------------
System Requirements
----------------------------
.. _`system-requirements`:

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
.. _`installation`:

Galera Cluster runs in Linux on MySQL, MariaDB and Percona XtraDB through the Galera Replicator Plugin.  You can install the plugin into any of these database servers through your package manager or by building it from source.

^^^^^^^^^^^^^^^^^^^^^^^^^
Galera Cluster for MySQL
^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`install-mysql`:

.. toctree::
   :maxdepth: 1

   installmysqldeb
   installmysqlrpm
   installmysqlsrc

^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Galera Cluster for MariaDB
^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`install-mariadb`:

.. toctree::
   :maxdepth: 1

   installmariadbdeb
   installmariadbrpm
   installmariadbsrc

^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Galera Cluster for Percona XtraDB
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`install-xtradb`:

.. toctree::
   :maxdepth: 1

   installxtradbdeb
   installxtradbrpm
   installxtradbsrc

------------------------------
Configuration
------------------------------
.. _`galera-configuration`:

Before you can bring Galera Cluster online, each node in the cluster requires some configuration, to grant other nodes access and to enable write-set replication on the database server.

.. toctree::
   :maxdepth: 2

   sysconfiguration
   dbconfiguration


------------------------------
Cluster Management
------------------------------
.. _`cluster-management`:

When you finish installation and configuration on your server, you're ready to launch the first node and bring the cluster online.  Once all the nodes are started, you can test that they're working and restart if necessary.

.. toctree::
   :maxdepth: 2

   startingcluster
   testingcluster
   restartingcluster
