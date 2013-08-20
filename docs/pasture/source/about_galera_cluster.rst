About Galera Cluster for MySQL
==============================

*Galera Cluster for MySQL* is a synchronous, multi-master cluster for the
MySQL database. You can *read and write* to any node, and you can lose any one
node without interruption in operations, and without the need to handle 
complicated failover procedures.

.. image:: /images/galera_multi_master.png

.. _galera_overview:

Galera Overview
---------------

*Galera Replication* is a general purpose replication library for any 
transactional system. It can be used to create a synchronous multi-master
replication solution, such as to achieve high availability and scale-out.

The first and most popular Galera based solution is *Galera Cluster for MySQL*.
On a high level, this is standard MySQL, and standard InnoDB storage engine, 
using the Galera replication library as a replication plugin. To be precise,
we do extend the MySQL replication plugin API to provide all the information
and hooks needed for truly multi-master, synchronous replication. We call this
API the *Write Set REPlication API*. [#fn_wsrepapi]_ To learn more about the 
internals of Galera, WSREP API and how they interact with MySQL, you should
read :doc:`understanding/internals`.

To use Galera Cluster for MySQL, you need to download a version of MySQL that
includes the *WSREP patches*. Galera enabled versions are available for all
major MySQL forks: Oracle MySQL, Percona Server and MariaDB. The chapter
:doc:`installing` contains links to download each of these.

Galera implements so called *certification based* replication. This concept
was introduced in, and Galera was inspired by, the academic research of Fernando
Pedone. The basic idea is that the transaction to be replicated - the write
set - not only contains the database rows to replicate, it also includes 
information about all the locks that were held by the database (ie. InnoDB)
during the transaction. Each node then *certifies* the replicated write set
against other write sets in the applier queue, and if there are no conflicting
locks, we know that the write set can be applied. At this point the transaction
is considered committed, after which each node continues to apply it to the
InnoDB tablespace. 

This approach is also called *virtually synchronous replication* since it is 
logically synchronous, but actual writing (and committing) to the InnoDB
tablespace happens independently (and thus, strictly speaking, asynchronously)
on each node.

.. _galera_benefits:

Galera benefits
---------------

Galera is a significant step in terms of improving MySQL high availability.
The MySQL ecosystem has offered users various ways to achieve high availability,
but typically all alternatives have provided only some of the following
features, but not all, thus making the choice of a high availability solution
an exercise in tradeoffs.

Galera provides all of the following features:

* *True multi-master*. Read and write to any node at any time.
* *Synchronous replication*. No data is lost at node crash or failover.
* *Tightly coupled*. All nodes hold the same state. No diverged data between nodes allowed.
* *Multi-threaded slave*, for any workload. No slave lag or performance overhead.
* *No master-slave failover* operations or use of VIP necessary.
* *Hot standby*. No downtime during failover (since there is no failover).
* *Automatic node provisioning*. No need to manually back up the database and 
  copy it to the new node.
* *Supports InnoDB* storage engine. Compatible with MySQL installed base.

The result is a high availability solution that is both robust in terms of
data integrity and high performance with instant failovers. The following 
diagram illustrates how different common database clustering solutions compare 
in these dimensions:

.. image:: /images/ha_alternatives.png

The other main benefit of Galera is good support for typical cloud usage.
Automatic node provisioning makes elastic scale-out and scale-in operations
painless. Galera has proven to perform very well in the cloud, such as when
using multiple small node instances, across multiple data centers (e.g. AWS
zones) or even over Wider Area Network.


.. _about_codership:

About Codership Oy
------------------

Galera was created by *Codership Oy*, a company based in Helsinki, Finland.

Codership was founded and development on Galera was started in 2007 by the three
founders Seppo Jaakola, Teemu Ollakka and Alex Yurchenko. Development was funded
by doing consulting work, and later by startup grants and loans from Finnish
government agency Tekes. Before starting work on Galera, the founders had 
already worked on 3 previous clustering solutions for MySQL, the last of which, 
Tungsten, is also in widespread use today. 

Galera 1.0 was eventually released in October 2011. At this point there were 
already several customers running Galera Cluster for MySQL in production on
the beta versions 0.7 and 0.8.

The word "Codership" is based on the word *"coder"* / *"coders"*. Think of it as
similar to the word "friendship".




.. rubric:: Footnotes

.. [#fn_wsrepapi] To use the Galera replication library in some other database 
                  or transactional application than MySQL, you would similarly 
                  implement this WSREP API.