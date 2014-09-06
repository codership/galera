=====================
 Using Galera Cluster
=====================
.. _`admin-guide`:

Once you become familiar with the basics of how Galera Cluster works, consider how it can work for you.

Bear in mind that there are certain key differences between how a standalone instance of the MySQL server works and the Galera Cluster wsrep database server.  This is especially important if you plan to install Galera Cluster over an existing MySQL server, preserving its data for replication.

.. toctree::
   :maxdepth: 2

   limitations
   myisamtoinnodb

.. seealso:: For more information on the installation and basic management of Galera Cluster, see the :doc:`Getting Started Guide <gettingstarted>`.

----------------------------------------
Working with the Cluster
----------------------------------------
.. _`working-cluster`:

How do you recover failed nodes or a Primary Component?  How to secure communications between the cluster nodes?  How do you back up cluster data?  With your cluster up and running, you can begin to manage its particular operations, monitor for and recover from issues, and maintain security.

.. toctree::
   :maxdepth: 2

   nodeprovisioning
   pcrecovery
   quorumreset
   monitoringthecluster
   schemaupgrades
   ssl
   firewallsettings
   upgrading
   arbitrator
   backingupthecluster

--------------------------------------
Improving Cluster Performance
--------------------------------------
.. _`improve-performance`:

Now that you're comfortable with how to use Galera Cluster, you can begin to consider how to use it more efficiently and effectively.  This can include variant deployment schemes and techniques to improve performance, or simply tips in configuring the cluster to better suit your needs.


.. toctree::
   :maxdepth: 2

   deploymentvariants
   performance
   configurationtips
