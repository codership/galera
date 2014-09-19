==============================
 Cluster Deployment Variants
==============================
.. _`deployment-variants`:

An instance of Galera Cluster consists of a series of nodes, preferably three or more.  Each node is an instance of MySQL, MariaDB or Percona XtraDB that you convert to Galera Cluster, allowing you to use that node as a cluster base.

Galera Cluster provides synchronous multi-master replication, meaning that you can think of the cluster as a single database server that listens through many interfaces.  To give you with an idea of what Galera Cluster is capable of, consider a typical *n*-tier application and the various benefits that would come from deploying it with Galera Cluster.



-------------------
 No Clustering
-------------------
.. _`no-clustering`:

In the typical *n*-tier application cluster without database clustering, there is no concern for database replication or synchronization.

Internet traffic filters down to your application servers, all of which read and write from the same :abbr:`DBMS (Database Management System)` server.  Given that the upper tiers usually remain stateless, you can start up as many instances as you need to meet the demand from the internet as each instance in turn stores its data in the data tier.


.. figure:: images/galerausecases0.png

   *No Clustering*


This solution is simple and easy to manage, but suffers a particular weakness in the data tier's lack of redundancy.  

For example, should for any reason the :abbr:`DBMS (Database Management System)` server become unavailable, your application also becomes unavailable.  This is the same whether the server crashes or if you need to take it down for maintenance. 

Similarly, this deployment also introduces performance concerns.  While you can start as many instances as you need to meet the demands on your web and application servers, they can only put so much load on the :abbr:`DBMS (Database Management System)` server before the load begins to slow down the experience for end users.


----------------------------
 Whole Stack Clustering
----------------------------
.. _`whole-stack-cluster`:

In the typical *n*-tier application cluster you can avoid the performance bottleneck by building a whole stack cluster.  

Internet traffic filters down to the application server, which stores data on its own dedicated :abbr:`DBMS (Database Management System)` server.  Galera Cluster then replicates the data through to the cluster, ensuring that it remains synchronous.



.. figure:: images/galerausecases1.png

   *Whole Stack Cluster*

This solution is simple and easy to manage, especially if you can install the whole stack of each node on one physical machine.  The direct connection from the application tier to the data tier ensures low latency.

There are, however, certain disadvantages to whole stack clustering:

- **Lack of Redundancy within the Stack** When the database server fails the whole stack fails.  This is because the application server uses a dedicated database server, if the database server fails there's no alternative for the application server, so the whole stack goes down.

- **Inefficient Resource Usage** A dedicated :abbr:`DBMS (Database Management System)` server for each application server is overuse.  This is poor resource consolidation.  For instance, one server with a 7 GB buffer pool is much faster than two servers with 4 GB buffer pools.

- **Increased Unproductive Overhead**  Each server reproduces the work of the other servers in the cluster.

- **Increased Rollback Rate** Given that each application server writes to a dedicated database server, cluster-wide conflicts are more likely, which can increases the likelihood of corrective rollbacks.

- **Inflexibility** There is no way for you to limit the number of master nodes or to perform intelligent load balancing.

Despite the disadvantages, however, this setup can prove very usable for several applications.  It depends on your needs.


-----------------------
Data Tier Clustering
-----------------------
.. _`data-tier-cluster`:

To compensate for the shortcomings in whole stack clusters, you can cluster the data tier separate from your web and application servers. 

Here, the :abbr:`DBMS (Database Management System)` servers form a cluster distinct from your *n*-tier application cluster.  The application servers treat the database cluster as a single virtual server, making their calls through load balancers to the data tier.

.. figure:: images/galerausecases2.png

   *Data Tier Clustering*

In a data tier cluster, the failure of one node does not effect the rest of the cluster.  Furthermore, resources are consolidated better and the setup is flexible.  That is, you can assign nodes different roles using intelligent load balancing.

There are, however, certain disadvantages to consider in data tier clustering:

- **Complex Structure**  Load balancers are involved and you must back them up in case of failures.  This typical means that you have two more servers than you would otherwise, as well as a failover solution between them.

- **Complex Management**  You need to configure and reconfigure the load balancers whenever a :abbr:`DBMS (Database Management System)` server is added to or removed from the cluster.

- **Indirect Connections** The load balancers between the application cluster and the data tier cluster increase the latency for each query.  As such, this can easily become a performance bottleneck.  You need powerful load balancing servers to avoid this.

- **Scalability** The scheme does not scale well over several datacenters.  Attempts to do so may remove any benefits you gain from resource consolidation, given that each datacenter must include at least two :abbr:`DBMS (Database Management System)` servers.


^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
Data Tier Clustering with Distributed Load Balancing
^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
.. _`data-tier-load-balancers`:

One solution to the limitations of data tier clustering is to deploy them with distributed load balancing.  This scheme roughly follows the standard data tier cluster, but includes a dedicated load balancer installed on each application server.

.. figure:: images/galerausecases3.png

   *Data Tier Cluster with Distributed Load Balancing*

In this deployment, the load balancer is no longer a single point of failure.  Furthermore, the load balancer scales with the application cluster and thus is unlikely to become a bottleneck.  Additionally, it keeps down the client-server communications latency.

Data tier clustering with distributed load balancing has the following disadvantage:

- **Complex Management** Each application server you deploy to meet the needs of your *n*-tier application cluster means another load balancer that you need to set up, manage and reconfigure whenever you change or otherwise update the database cluster configuring.


--------------------------------
Aggregated Stack Clustering
--------------------------------
.. _`aggregated-stack-cluster`:

In addition to these deployment schemes, you also have the option of a hybrid setup that integrates whole stack and data tier clustering by aggregating several application stacks around single :abbr:`DBMS (Database Management System)` servers.

.. figure:: images/galerausecases4.png

   *Aggregated Stack Clustering*

This scheme improves on the resource utilization of the whole stack cluster while maintaining it's relative simplicity and direct :abbr:`DBMS (Database Management System)` connection benefits.  It is also how a data tier cluster with distributed load balancing with look if you were to use only one  :abbr:`DBMS (Database Management System)` server per datacenter.

The aggregated stack cluster is a good setup for sites that are not very big, but still are hosted at more than one datacenter.
